#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#define FRAME_BYTES (1280u * 1024u * 2u)
#define STAGING_PHYS 0x30000000u
static void fail(const char *what) { perror(what); exit(1); }
static void bus_error(int sig, siginfo_t *info, void *context)
{
    (void)context;
    dprintf(STDERR_FILENO,"STAGE_FRAME_SIGBUS address=%p code=%d\n",info->si_addr,info->si_code);
    _exit(128+sig);
}
/* Device mappings require byte accesses: do not replace with memcpy/memcmp. */
static int stage_copy(volatile unsigned char *to,const unsigned char *from,size_t size)
{
    for(size_t i=0;i<size;i++)to[i]=from[i];
    __sync_synchronize();
    for(size_t i=0;i<size;i++)if(to[i]!=from[i]) {
        fprintf(stderr,"STAGE_FRAME_VERIFY_MISMATCH offset=%zu\n",i);return -1;
    }
    __sync_synchronize();
    return 0;
}
int main(int argc,char **argv)
{
    if(argc>2) {fprintf(stderr,"Usage: stage-frame [YUY2_FILE]\n");return 2;}
    const char *path=argc==2?argv[1]:"/tmp/sc130-frame.yuy2";
    int fd=open(path,O_RDONLY);
    if(fd<0)fail("open frame");
    struct stat st;
    if(fstat(fd,&st))fail("stat frame");
    if(!S_ISREG(st.st_mode)||st.st_size!=FRAME_BYTES) {
        fprintf(stderr,"Rejected: expected exactly %u-byte regular YUY2 file, got %jd\n",FRAME_BYTES,(intmax_t)st.st_size);
        close(fd);return 3;
    }
    unsigned char *frame=malloc(FRAME_BYTES);
    if(!frame)fail("allocate frame");
    size_t done=0;
    while(done<FRAME_BYTES) {
        ssize_t n=read(fd,frame+done,FRAME_BYTES-done);
        if(n<0&&errno==EINTR)continue;
        if(n<=0) {if(!n)errno=EIO;fail("read frame");}
        done+=(size_t)n;
    }
    /* Detect concurrent truncation/growth before touching reserved memory. */
    unsigned char extra;
    ssize_t n;
    do {n=read(fd,&extra,1);}while(n<0&&errno==EINTR);
    if(n!=0) {fprintf(stderr,"Rejected: frame changed size while reading\n");free(frame);close(fd);return 3;}
    close(fd);
    struct sigaction sa={.sa_sigaction=bus_error,.sa_flags=SA_SIGINFO};
    sigemptyset(&sa.sa_mask);
    if(sigaction(SIGBUS,&sa,NULL))fail("sigaction");
    int memfd=open("/dev/mem",O_RDWR|O_SYNC);
    if(memfd<0)fail("open reserved /dev/mem");
    volatile unsigned char *destination=mmap(NULL,FRAME_BYTES,PROT_READ|PROT_WRITE,MAP_SHARED,memfd,STAGING_PHYS);
    if(destination==MAP_FAILED)fail("mmap reserved DDR");
    int rc=stage_copy(destination,frame,FRAME_BYTES);
    if(!rc)printf("SC130_FRAME_STAGED_OK bytes=%u address=0x%08x source=%s\n",FRAME_BYTES,STAGING_PHYS,path);
    munmap((void *)destination,FRAME_BYTES);close(memfd);free(frame);
    return rc?4:0;
}
