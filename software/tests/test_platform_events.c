#define ioctl mock_ioctl
#define main platform_bridge_main
#include "../platform_bridge.c"
#undef main
#undef ioctl
#include <stdarg.h>
#include <assert.h>
static int event_read,cap_calls,output_stops;
int mock_ioctl(int fd,unsigned long req,...)
{
    va_list ap;va_start(ap,req);void *arg=va_arg(ap,void*);va_end(ap);
    if(fd==3)cap_calls++;
    if(req==VIDIOC_DQEVENT) {
        if(!event_read++){struct v4l2_event *e=arg;memset(e,0,sizeof(*e));e->type=UVC_EVENT_STREAMOFF;return 0;}
        errno=ENOENT;return -1;
    }
    if(req==VIDIOC_STREAMOFF){assert(fd==4);output_stops++;return 0;}
    if(req==VIDIOC_REQBUFS)return 0;
    if(req==VIDIOC_S_FMT)return 0;
    assert(0);return -1;
}
int main(void)
{
    struct platform p={.cap={.fd=3,.on=true,.count=8},.out={.fd=4,.on=true,.count=4}};
    broker_init(&p.broker,8);assert(broker_dequeue(&p.broker,0)==0);
    assert(broker_latest(&p.broker,0)==0);assert(broker_dp_hold(&p.broker,0)==0);
    p.dp=(struct dp_display*)(uintptr_t)1;
    uvc_session_init(&p.session,true,1);p.session.streaming=true;p.session.committed=true;
    policy_init(&p.usb,4,120);p.usb.slot[1].state=USB_IN_FLIGHT;
    assert(usb_events(&p)==0);
    assert(cap_calls==0 && output_stops==1);
    assert(p.cap.on && p.cap.count==8 && p.cap.fd==3);
    assert(p.broker.latest==0 && p.broker.dp[0] && !p.broker.queued[0]);
    assert(p.dp==(struct dp_display*)(uintptr_t)1);
    assert(!p.session.streaming && p.session.committed && p.usb.slot[1].state==FREE);
    assert(!p.out.on && p.out.count==0);
    /* The unified default/GET_DEF must match the descriptor's83333 interval. */
    uvc_session_init(&p.session,true,1);assert(p.session.fps==120);
    struct usb_ctrlrequest req={.bRequestType=0xa1,.bRequest=0x87,
        .wValue=htole16(0x100),.wIndex=htole16(1),.wLength=htole16(34)};
    struct uvc_request_data data;
    assert(uvc_handle_setup(&p.session,&req,&data)==0);
    uint32_t interval=(uint32_t)data.data[4]|((uint32_t)data.data[5]<<8)|((uint32_t)data.data[6]<<16)|((uint32_t)data.data[7]<<24);
    assert(interval==83333);
    puts("USB stop preserves capture/DP and default120 test passed");
    return 0;
}
