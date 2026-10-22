#define ioctl mock_ioctl
#define main platform_bridge_main
#include "../platform_bridge.c"
#undef main
#undef ioctl
#include <stdarg.h>
#include <assert.h>

static struct v4l2_buffer reply[4];
static unsigned replies, cursor;
static int terminal = EAGAIN;
int mock_ioctl(int fd, unsigned long req, ...)
{
    assert(fd == 4 && req == VIDIOC_DQBUF);
    va_list ap; va_start(ap,req);
    struct v4l2_buffer *b=va_arg(ap,void *); va_end(ap);
    if(cursor<replies){*b=reply[cursor++];return 0;}
    errno=terminal;return -1;
}
static struct platform setup(void)
{
    struct platform p={.out={.fd=4,.on=true,.count=4,.type=V4L2_BUF_TYPE_VIDEO_OUTPUT}};
    broker_init(&p.broker,8);
    assert(!broker_dequeue(&p.broker,2));
    assert(!broker_latest(&p.broker,2));
    assert(!broker_dp_hold(&p.broker,2));
    policy_init(&p.usb,4,120);
    p.usb.slot[0]=(struct frame_slot){USB_IN_FLIGHT,1234,100};
    p.usb.slot[1]=(struct frame_slot){USB_IN_FLIGHT,1235,200};
    cursor=0;replies=0;terminal=EAGAIN;
    return p;
}
int main(void)
{
    struct platform p=setup();
    struct capture_broker before=p.broker;
    reply[0]=(struct v4l2_buffer){.index=0,.flags=V4L2_BUF_FLAG_ERROR,.bytesused=0,.sequence=99};
    reply[1]=(struct v4l2_buffer){.index=1,.bytesused=SC130_SIZE,.sequence=100};
    replies=2;
    FILE *log=tmpfile();assert(log);
    int saved=dup(STDERR_FILENO);assert(saved>=0);
    assert(dup2(fileno(log),STDERR_FILENO)>=0);
    assert(usb_complete(&p)==0);
    fflush(stderr);assert(dup2(saved,STDERR_FILENO)>=0);close(saved);
    rewind(log);char text[4096]={0};assert(fread(text,1,sizeof(text)-1,log)>0);fclose(log);
    /* Old code silently increments an aggregate, losing both cause and source. */
    assert(strstr(text,"kind=buffer") && strstr(text,"source_seq=1234"));
    assert(p.returned==2 && p.usb_errors==1);
    assert(p.usb_error_kind[USB_ERR_BUFFER]==1);
    assert(p.usb_error_kind[USB_ERR_OWNERSHIP]==0 && p.usb_error_kind[USB_ERR_DQBUF]==0);
    assert(p.usb.slot[0].state==FREE && p.usb.slot[1].state==FREE);
    assert(!memcmp(&before,&p.broker,sizeof(before)));
    /* A duplicate completion must not free another slot or count twice. */
    cursor=0;replies=1;
    assert(usb_complete(&p)==-1 && errno==EPROTO);
    assert(p.returned==2 && p.usb_errors==2 && p.usb_error_kind[USB_ERR_OWNERSHIP]==1);
    assert(!memcmp(&before,&p.broker,sizeof(before)));
    p=setup();replies=1;reply[0].index=16;
    assert(usb_complete(&p)==-1 && errno==EPROTO);
    assert(p.returned==0 && p.usb_error_kind[USB_ERR_OWNERSHIP]==1);
    assert(p.usb.slot[0].state==USB_IN_FLIGHT);
    p=setup();terminal=EIO;
    assert(usb_complete(&p)==-1 && errno==EIO);
    assert(p.usb_errors==1 && p.usb_error_kind[USB_ERR_DQBUF]==1);
    assert(p.usb.slot[0].state==USB_IN_FLIGHT);
    p=setup();terminal=EINTR;quitting=1;
    assert(usb_complete(&p)==0 && p.usb_errors==0);quitting=0;
    p=setup();p.out.on=false;
    assert(usb_complete(&p)==0 && cursor==0 && p.usb_errors==0);
    p=setup();errno=ENODEV;
    usb_error(&p,USB_ERR_EVENT,NULL,errno);assert(errno==ENODEV);
    usb_error(&p,USB_ERR_SEND,NULL,EIO);
    assert(p.usb_errors==2 && p.usb_error_kind[USB_ERR_EVENT]==1 && p.usb_error_kind[USB_ERR_SEND]==1);
    /* Log sampling and session reset must never erase accumulated errors. */
    for(unsigned i=0;i<20;i++)usb_error(&p,USB_ERR_BUFFER,NULL,0);
    policy_reset(&p.usb);uvc_session_stop(&p.session,true);
    assert(p.usb_errors==22 && p.usb_error_kind[USB_ERR_BUFFER]==20);
    puts("USB completion retains error context and capture/DP ownership: PASS");
    return 0;
}
