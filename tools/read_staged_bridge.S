// Copy the camera probe gzip from reserved DDR via mmap, to stdout.
// arm64 /dev/mem read() rejects this reserved region; mmap is supported.
.global _start
.text
_start:
    mov x0, #-100
    adr x1, path
    mov x2, #0
    mov x3, #0
    mov x8, #56
    svc #0
    cmp x0, #0
    b.lt fail
    mov x4, x0
    mov x0, #0
    ldr x1, =1043793
    mov x2, #1
    mov x3, #1
    mov x5, #0x32000000
    mov x8, #222
    svc #0
    cmp x0, #0
    b.lt fail
    mov x1, x0
    ldr x2, =1043792
copy:
    mov x0, #1
    mov x8, #64
    svc #0
    cmp x0, #0
    b.le fail
    add x1, x1, x0
    sub x2, x2, x0
    cbnz x2, copy
    mov x0, #0
    b exit
fail:
    mov x0, #2
exit:
    mov x8, #93
    svc #0
path:
    .asciz "/dev/mem"

