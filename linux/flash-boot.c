#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <mtd/mtd-user.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#define FLASH_BYTES 0x02000000u
#define STAGING_PHYS 0x32000000u
#define BACKUP_PHYS 0x30000000u
#define CHUNK 0x10000u

static void fail(const char *what) { perror(what); exit(1); }
static void bus_error(int signo, siginfo_t *info, void *context) {
    (void)signo; (void)context;
    /* Diagnostic output is only used on fatal faults; process exits immediately. */
    dprintf(STDERR_FILENO, "FLASH_BOOT_SIGBUS address=%p code=%d\n", info->si_addr, info->si_code);
    _exit(128 + SIGBUS);
}
/* /dev/mem no-map DDR can be mapped with Device attributes. Byte-sized volatile
 * accesses avoid libc SIMD and unaligned accesses that fault on Device memory. */
static void ddr_read(unsigned char *to, const volatile unsigned char *from, size_t n) {
    for (size_t i = 0; i < n; i++) to[i] = from[i];
}
static void ddr_write(volatile unsigned char *to, const unsigned char *from, size_t n) {
    for (size_t i = 0; i < n; i++) to[i] = from[i];
}
static void exact_io(int fd, unsigned char *buf, size_t count, off_t offset, int writing) {
    size_t done = 0;
    while (done < count) {
        ssize_t n = writing ? pwrite(fd, buf + done, count - done, offset + done)
                            : pread(fd, buf + done, count - done, offset + done);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) { if (!n) errno = EIO; fail(writing ? "flash write" : "flash read"); }
        done += (size_t)n;
    }
}
int main(int argc, char **argv) {
    struct sigaction action = { .sa_sigaction = bus_error, .sa_flags = SA_SIGINFO };
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGBUS, &action, NULL)) fail("sigaction SIGBUS");
    unsigned char expected[CHUNK], actual[CHUNK];
    struct mtd_info_user info;
    int writing = argc == 3 && !strcmp(argv[1], "--write");
    int backup = argc == 2 && !strcmp(argv[1], "--backup");
    if (!(argc == 2 && !strcmp(argv[1], "--info")) && !writing && !backup) {
        fprintf(stderr, "Usage: flash-boot --info | flash-boot --backup | flash-boot --write IMAGE_LENGTH_BYTES\n"
                        "Source fixed at reserved DDR 0x32000000; destination /dev/mtd0 offset 0.\n");
        return 2;
    }
    int fd = open("/dev/mtd0", writing ? O_RDWR | O_SYNC : O_RDONLY);
    if (fd < 0) fail("open /dev/mtd0");
    if (ioctl(fd, MEMGETINFO, &info)) fail("MEMGETINFO");
    printf("MTD type=%u size=%u erasesize=%u writesize=%u flags=0x%x\n",
           info.type, info.size, info.erasesize, info.writesize, info.flags);
    if (info.type != MTD_NORFLASH || info.size != FLASH_BYTES ||
        !info.erasesize || info.erasesize > 0x100000u ||
        (info.erasesize & (info.erasesize - 1)) || FLASH_BYTES % info.erasesize) {
        fprintf(stderr, "Rejected: expected 32 MiB NOR with sane power-of-two erase geometry.\n");
        return 3;
    }
    if (backup) {
        /* MTD is open O_RDONLY. Only the reserved DDR destination is writable. */
        int backup_memfd = open("/dev/mem", O_RDWR | O_SYNC);
        if (backup_memfd < 0) fail("open backup /dev/mem");
        volatile unsigned char *destination = mmap(NULL, FLASH_BYTES, PROT_READ | PROT_WRITE,
                                         MAP_SHARED, backup_memfd, BACKUP_PHYS);
        if (destination == MAP_FAILED) fail("mmap backup DDR");
        for (uint32_t offset = 0; offset < FLASH_BYTES; offset += CHUNK) {
            exact_io(fd, actual, CHUNK, offset, 0);
            ddr_write(destination + offset, actual, CHUNK);
            __sync_synchronize();
            ddr_read(expected, destination + offset, CHUNK);
            if (memcmp(expected, actual, CHUNK)) {
                fprintf(stderr, "BACKUP DDR VERIFY MISMATCH offset=0x%08x\n", offset);
                return 7;
            }
        }
        __sync_synchronize();
        printf("ZUMINI_QSPI_BACKUP_DDR_OK bytes=%u address=0x%08x\n", FLASH_BYTES, BACKUP_PHYS);
        munmap((void *)destination, FLASH_BYTES); close(backup_memfd); close(fd);
        return 0;
    }
    if (!writing) { close(fd); return 0; }
    char *end;
    errno = 0;
    uintmax_t parsed = strtoumax(argv[2], &end, 0);
    if (errno || !*argv[2] || *end || parsed == 0 || parsed > FLASH_BYTES) {
        fprintf(stderr, "Rejected invalid image length.\n"); return 4;
    }
    uint32_t length = (uint32_t)parsed;
    uint32_t erase_end = (length + info.erasesize - 1) & ~(info.erasesize - 1);
    int memfd = open("/dev/mem", O_RDONLY | O_SYNC);
    if (memfd < 0) fail("open /dev/mem");
    volatile unsigned char *source = mmap(NULL, length, PROT_READ, MAP_SHARED, memfd, STAGING_PHYS);
    if (source == MAP_FAILED) fail("mmap staged BOOT image");
    /* Reject empty/erased staging before any destructive operation. */
    unsigned char first = source[0];
    int nonuniform = 0;
    for (uint32_t i = 1; i < length; i++) if (source[i] != first) { nonuniform = 1; break; }
    if (!nonuniform && (first == 0 || first == 0xff)) {
        fprintf(stderr, "Rejected blank staging buffer.\n"); return 5;
    }
    printf("WRITE AUTHORIZED: staged DDR=0x%08x image=%u erase=[0,%u)\n", STAGING_PHYS, length, erase_end);
    fflush(stdout);
    for (uint32_t offset = 0; offset < erase_end; offset += info.erasesize) {
        struct erase_info_user erase = { .start = offset, .length = info.erasesize };
        if (ioctl(fd, MEMERASE, &erase)) fail("MEMERASE");
    }
    for (uint32_t offset = 0; offset < length; offset += CHUNK) {
        size_t count = length - offset < CHUNK ? length - offset : CHUNK;
        ddr_read(expected, source + offset, count);
        exact_io(fd, expected, count, offset, 1);
    }
    if (fsync(fd) && errno != EINVAL) fail("fsync");
    for (uint32_t offset = 0; offset < length; offset += CHUNK) {
        size_t count = length - offset < CHUNK ? length - offset : CHUNK;
        ddr_read(expected, source + offset, count);
        exact_io(fd, actual, count, offset, 0);
        if (memcmp(expected, actual, count)) {
            fprintf(stderr, "READBACK MISMATCH at chunk offset 0x%08x\n", offset); return 6;
        }
    }
    printf("ZUMINI_QSPI_BOOT_WRITE_READBACK_OK bytes=%u erase_bytes=%u\n", length, erase_end);
    munmap((void *)source, length); close(memfd); close(fd);
    return 0;
}
