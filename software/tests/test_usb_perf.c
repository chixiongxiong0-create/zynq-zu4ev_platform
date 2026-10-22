#define ioctl mock_ioctl
#define main platform_bridge_main
#include "../platform_bridge.c"
#undef main
#undef ioctl
#include <stdarg.h>
#include <assert.h>
static unsigned queued;
int mock_ioctl(int fd,unsigned long req,...)
{
    va_list ap;va_start(ap,req);struct v4l2_buffer *b=va_arg(ap,void*);va_end(ap);
    assert(fd==4 && req==VIDIOC_QBUF && b->index==0 && b->bytesused==SC130_SIZE);
    queued++;return 0;
}
static void run(bool frozen)
{
    struct platform p={.profile=true,.static_usb=frozen,.out={.fd=4,.on=true,.count=4,.type=V4L2_BUF_TYPE_VIDEO_OUTPUT}};
    unsigned char *src=malloc(SC130_SIZE),*dst=malloc(SC130_SIZE);assert(src && dst);
    p.cap.b[2]=(struct mapped){src,SC130_SIZE};p.out.b[0]=(struct mapped){dst,SC130_SIZE};
    broker_init(&p.broker,8);assert(!broker_dequeue(&p.broker,2));assert(!broker_latest(&p.broker,2));
    assert(!broker_dp_hold(&p.broker,2));struct capture_broker before=p.broker;
    policy_init(&p.usb,4,120);p.session.streaming=true;p.meta[2].sequence=10;
    memset(src,0x22,SC130_SIZE);memset(dst,0,SC130_SIZE);queued=0;
    assert(!usb_send(&p,now_ns()));assert(queued==1 && !memcmp(src,dst,SC130_SIZE));
    assert(p.copy_wall.count==1 && p.qbuf_cost.count==1 && p.submitted_ns[0]);
    assert(!policy_usb_done(&p.usb,0));p.usb.next_ns=0;p.meta[2].sequence++;
    memset(src,0x33,SC130_SIZE);assert(!usb_send(&p,now_ns()));assert(queued==2);
    assert(dst[0]==(frozen?0x22:0x33) && dst[SC130_SIZE-1]==dst[0]);
    assert(p.copy_wall.count==(frozen?1:2));assert(p.qbuf_cost.count==2);
    assert(src[0]==0x33 && src[SC130_SIZE-1]==0x33);
    assert(!memcmp(&before,&p.broker,sizeof(before)));
    free(src);free(dst);
}
int main(void)
{
    run(false);run(true);
    puts("live/frozen copy accounting, output queue, source integrity and DP/capture ownership passed");
    return 0;
}
