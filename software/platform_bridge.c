#define _GNU_SOURCE
#include "uvc_core.h"
#include "dp_display.h"
#include "capture_broker.h"
#include "perf_stats.h"
#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>

struct mapped { void *ptr; size_t length; };
struct video { int fd; enum v4l2_buf_type type; unsigned count; bool on; struct mapped b[16]; };
static volatile sig_atomic_t quitting;
static volatile sig_atomic_t snapshot_requested;
static void snapshot_signal(int sig) { (void)sig; snapshot_requested=1; }
static void stop_signal(int sig) { (void)sig; quitting=1; }
static uint64_t now_ns(void)
{ struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec; }
static uint64_t cpu_ns(void)
{ struct timespec t;clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&t);return (uint64_t)t.tv_sec*1000000000+t.tv_nsec; }
static int xioctl(int fd,unsigned long cmd,void *arg)
{ int r; do r=ioctl(fd,cmd,arg); while(r<0 && errno==EINTR && !quitting);return r; }
static int write_text(const char *path,const char *text)
{
    int fd=open(path,O_WRONLY|O_CLOEXEC);if(fd<0)return -1;
    size_t n=strlen(text);ssize_t r=write(fd,text,n);int e=errno;close(fd);errno=e;
    return r==(ssize_t)n?0:-1;
}
static int open_video(struct video *v,const char *path,bool output)
{
    v->fd=open(path,O_RDWR|O_NONBLOCK|O_CLOEXEC);if(v->fd<0)return -1;
    struct v4l2_capability cap={0};
    if(xioctl(v->fd,VIDIOC_QUERYCAP,&cap)<0)goto fail;
    unsigned caps=cap.capabilities&V4L2_CAP_DEVICE_CAPS?cap.device_caps:cap.capabilities;
    if(!(caps&V4L2_CAP_STREAMING) || !(caps&(output?V4L2_CAP_VIDEO_OUTPUT:V4L2_CAP_VIDEO_CAPTURE))) {
        errno=ENOTSUP;goto fail;
    }
    if(output && strcmp((char *)cap.driver,"g_uvc")) {errno=ENODEV;goto fail;}
    v->type=output?V4L2_BUF_TYPE_VIDEO_OUTPUT:V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fprintf(stderr,"device=%s driver=%s bus=%s\n",path,cap.driver,cap.bus_info);return 0;
fail: { int e=errno;close(v->fd);v->fd=-1;errno=e;return -1; }
}
static int find_output(struct video *v)
{
    glob_t g={0};int ret=-1;
    if(glob("/dev/video*",0,NULL,&g)==0) for(size_t i=0;i<g.gl_pathc;i++)
        if(open_video(v,g.gl_pathv[i],true)==0){ret=0;break;}
    globfree(&g);return ret;
}
static void free_buffers(struct video *v)
{
    if(v->fd<0)return;
    if(v->on){xioctl(v->fd,VIDIOC_STREAMOFF,&v->type);v->on=false;}
    for(unsigned i=0;i<v->count;i++) if(v->b[i].ptr) {
        munmap(v->b[i].ptr,v->b[i].length);v->b[i].ptr=NULL;
    }
    struct v4l2_requestbuffers r={.type=v->type,.memory=V4L2_MEMORY_MMAP};
    xioctl(v->fd,VIDIOC_REQBUFS,&r);v->count=0;
}
static int alloc_buffers(struct video *v)
{
    struct v4l2_format f={.type=v->type};
    f.fmt.pix=(struct v4l2_pix_format){.width=SC130_WIDTH,.height=SC130_HEIGHT,
        .pixelformat=V4L2_PIX_FMT_YUYV,.field=V4L2_FIELD_NONE,.bytesperline=SC130_STRIDE,
        .sizeimage=SC130_SIZE,.colorspace=V4L2_COLORSPACE_REC709,
        .ycbcr_enc=V4L2_YCBCR_ENC_709,.quantization=V4L2_QUANTIZATION_LIM_RANGE,
        .xfer_func=V4L2_XFER_FUNC_709};
    if(xioctl(v->fd,VIDIOC_S_FMT,&f)<0)return -1;
    if(f.fmt.pix.width!=SC130_WIDTH || f.fmt.pix.height!=SC130_HEIGHT ||
       f.fmt.pix.pixelformat!=V4L2_PIX_FMT_YUYV || f.fmt.pix.sizeimage!=SC130_SIZE ||
       (f.fmt.pix.bytesperline && f.fmt.pix.bytesperline!=SC130_STRIDE)) {errno=EINVAL;return -1;}
    struct v4l2_requestbuffers r={.count=v->type==V4L2_BUF_TYPE_VIDEO_CAPTURE?8:4,.type=v->type,.memory=V4L2_MEMORY_MMAP};
    /* Cacheable capture pages; vb2 and the driver's per-master DMA mappings
     * retain cache ownership synchronization. Never request skip-sync flags. */
    if(v->type==V4L2_BUF_TYPE_VIDEO_CAPTURE)r.flags=V4L2_MEMORY_FLAG_NON_COHERENT;
    if(xioctl(v->fd,VIDIOC_REQBUFS,&r)<0)return -1;
    if(r.count<4 || r.count>16){errno=ENOMEM;return -1;}
    v->count=r.count;
    for(unsigned i=0;i<v->count;i++) {
        struct v4l2_buffer b={.index=i,.type=v->type,.memory=V4L2_MEMORY_MMAP};
        if(xioctl(v->fd,VIDIOC_QUERYBUF,&b)<0)return -1;
        if(b.length<SC130_SIZE){errno=EINVAL;return -1;}
        void *p=mmap(NULL,b.length,PROT_READ|PROT_WRITE,MAP_SHARED,v->fd,b.m.offset);
        if(p==MAP_FAILED)return -1;
        v->b[i]=(struct mapped){p,b.length};
    }
    return 0;
}
static int queue_buffer(struct video *v,unsigned index,uint64_t stamp)
{
    struct v4l2_buffer b={.index=index,.type=v->type,.memory=V4L2_MEMORY_MMAP,
        .bytesused=v->type==V4L2_BUF_TYPE_VIDEO_OUTPUT?SC130_SIZE:0,
        .field=V4L2_FIELD_NONE};
    b.timestamp.tv_sec=(long)(stamp/1000000000);b.timestamp.tv_usec=(long)(stamp%1000000000/1000);
    return xioctl(v->fd,VIDIOC_QBUF,&b);
}
static int start_capture(struct video *c)
{
    if(alloc_buffers(c)<0)return -1;
    for(unsigned i=0;i<c->count;i++)if(queue_buffer(c,i,0)<0)return -1;
    if(xioctl(c->fd,VIDIOC_STREAMON,&c->type)<0)return -1;
    c->on=true;return 0;
}

enum usb_error_kind { USB_ERR_BUFFER, USB_ERR_OWNERSHIP, USB_ERR_DQBUF,
    USB_ERR_EVENT, USB_ERR_SEND, USB_ERR_KINDS };
struct platform {
    struct video cap,out;
    struct capture_broker broker;
    struct policy usb;
    struct uvc_session session;
    struct dp_display *dp;
    bool dp_fault,latest_used,have_sequence,usb_sent_valid,usb_sequence_marker;
    uint32_t sequence,usb_sent_sequence;
    int exported[16];
    struct v4l2_buffer meta[16];
    uint64_t captured,sent,returned,invalid,dropped,usb_errors,dp_flips;
    uint64_t usb_error_kind[USB_ERR_KINDS];
    bool profile,static_usb,perf_started,static_filled[16];
    uint64_t perf_start,perf_cpu_start,perf_end,perf_cpu_end,perf_sent_base,perf_returned_base,perf_errors_base,last_submit,dequeued_ns[16],submitted_ns[16];
    struct perf_metric copy_wall,copy_cpu,source_age,dq_to_submit,qbuf_cost,usb_residence,submit_gap;
};
static void profile_report(struct platform *p)
{
    if(!p->profile)return;
    uint64_t wall=p->perf_started?p->perf_end-p->perf_start:0,cpu=p->perf_started?p->perf_cpu_end-p->perf_cpu_start:0;
    fprintf(stderr,"PERF total static_usb=%u elapsed_s=%.3f cpu_one_core_pct=%.3f copy_wall_pct=%.3f copy_cpu_pct=%.3f sent=%llu returned=%llu errors=%llu\n",
        p->static_usb,wall/1e9,wall?100.0*cpu/wall:0,wall?100.0*p->copy_wall.sum/wall:0,
        wall?100.0*p->copy_cpu.sum/wall:0,(unsigned long long)(p->sent-p->perf_sent_base),(unsigned long long)(p->returned-p->perf_returned_base),(unsigned long long)(p->usb_errors-p->perf_errors_base));
    perf_print("copy_wall",&p->copy_wall);perf_print("copy_cpu",&p->copy_cpu);
    perf_print("capture_complete_to_dequeue",&p->source_age);perf_print("dequeue_to_submit",&p->dq_to_submit);
    perf_print("output_qbuf",&p->qbuf_cost);perf_print("usb_queue_to_return",&p->usb_residence);perf_print("submit_gap",&p->submit_gap);
}
/* Keep the historical total, but do not confuse returned ERROR buffers with
 * ownership violations or ioctl failures. Sample details, never the counters. */
static void usb_error(struct platform *p,enum usb_error_kind kind,
                      const struct v4l2_buffer *b,int error)
{
    static const char *const names[]={"buffer","ownership","dqbuf","event","send"};
    int saved=errno;
    uint64_t count=++p->usb_error_kind[kind];p->usb_errors++;
    if(count<=8 || !(count&(count-1))) {
        fprintf(stderr,"USB error kind=%s count=%llu ns=%llu errno=%d streaming=%u on=%u",
            names[kind],(unsigned long long)count,(unsigned long long)now_ns(),
            error,p->session.streaming,p->out.on);
        if(b) {
            fprintf(stderr," index=%u flags=0x%x bytesused=%u sequence=%u",
                b->index,b->flags,b->bytesused,b->sequence);
            if(b->index<p->usb.count && b->index<SC130_MAX_BUFFERS)
                fprintf(stderr," source_seq=%llu state=%u",
                    (unsigned long long)p->usb.slot[b->index].sequence,
                    (unsigned)p->usb.slot[b->index].state);
        }
        fputc('\n',stderr);
    }
    errno=saved;
}
static int find_capture(struct video *v)
{
    glob_t g={0};int ret=-1;
    if(glob("/dev/video*",0,NULL,&g)==0)for(size_t i=0;i<g.gl_pathc;i++) {
        if(open_video(v,g.gl_pathv[i],false)<0)continue;
        struct v4l2_capability c={0};
        if(!xioctl(v->fd,VIDIOC_QUERYCAP,&c) && !strncmp((char*)c.driver,"sc130_capture_v2",15)){ret=0;break;}
        close(v->fd);v->fd=-1;
    }
    globfree(&g);if(ret)errno=ENODEV;return ret;
}
static int recycle(struct platform *p)
{
    for(unsigned i=0;i<p->cap.count;i++)if(broker_can_queue(&p->broker,i)) {
        if(queue_buffer(&p->cap,i,0)<0)return -1;
        if(broker_queue(&p->broker,i)<0){errno=EPROTO;return -1;}
    }
    return 0;
}
static void usb_stop(struct platform *p,bool disconnect)
{
    free_buffers(&p->out);policy_reset(&p->usb);p->usb_sent_valid=false;
    memset(p->static_filled,0,sizeof(p->static_filled));memset(p->submitted_ns,0,sizeof(p->submitted_ns));
    uvc_session_stop(&p->session,disconnect);
    fprintf(stderr,"USB stopped; capture and DP continue\n");
}
static void usb_detach(struct platform *p)
{
    usb_stop(p,true);
    if(p->out.fd>=0)close(p->out.fd);
    p->out.fd=-1;
}
static int usb_attach(struct platform *p,const char *outpath,const char *soft)
{
    if((outpath?open_video(&p->out,outpath,true):find_output(&p->out))<0)return -1;
    for(unsigned t=UVC_EVENT_FIRST;t<=UVC_EVENT_LAST;t++) {
        struct v4l2_event_subscription sub={.type=t};
        if(xioctl(p->out.fd,VIDIOC_SUBSCRIBE_EVENT,&sub)<0)goto fail;
    }
    if(soft && write_text(soft,"connect")<0)goto fail;
    fprintf(stderr,"USB ready default120; capture independent of host\n");return 0;
fail:
    usb_detach(p);return -1;
}
static int usb_events(struct platform *p)
{
    struct v4l2_event e;
    while(xioctl(p->out.fd,VIDIOC_DQEVENT,&e)==0) {
        struct uvc_event u;memcpy(&u,e.u.data,sizeof(u));
        if(e.type==UVC_EVENT_CONNECT) {
            p->session.superspeed=u.speed==USB_SPEED_SUPER || u.speed==USB_SPEED_SUPER_PLUS;
            fprintf(stderr,"USB CONNECT speed=%u superspeed=%u\n",u.speed,p->session.superspeed);
        } else if(e.type==UVC_EVENT_SETUP) {
            struct uvc_request_data d;uvc_handle_setup(&p->session,&u.req,&d);
            if(xioctl(p->out.fd,UVCIOC_SEND_RESPONSE,&d)<0)return -1;
        } else if(e.type==UVC_EVENT_DATA) {
            if(uvc_handle_data(&p->session,&u.data)<0) {
                struct uvc_request_data stall={.length=-EL2HLT};
                if(xioctl(p->out.fd,UVCIOC_SEND_RESPONSE,&stall)<0)return -1;
                fprintf(stderr,"USB rejected invalid control DATA\n");
            }
        } else if(e.type==UVC_EVENT_STREAMON) {
            if(uvc_session_start(&p->session)<0){errno=EPROTO;return -1;}
            if(alloc_buffers(&p->out)<0)return -1;
            policy_init(&p->usb,p->out.count,p->session.fps);
            p->usb_sent_valid=false;
            p->perf_started=false;p->last_submit=0;
            p->copy_wall=(struct perf_metric){0};p->copy_cpu=(struct perf_metric){0};
            p->source_age=(struct perf_metric){0};p->dq_to_submit=(struct perf_metric){0};
            p->qbuf_cost=(struct perf_metric){0};p->usb_residence=(struct perf_metric){0};p->submit_gap=(struct perf_metric){0};
            p->perf_sent_base=p->sent;p->perf_returned_base=p->returned;p->perf_errors_base=p->usb_errors;
            fprintf(stderr,"USB STREAMON fps=%u copy; capture already running\n",p->session.fps);
        } else if(e.type==UVC_EVENT_STREAMOFF || e.type==UVC_EVENT_DISCONNECT)
            usb_stop(p,e.type==UVC_EVENT_DISCONNECT);
    }
    return errno==ENOENT || errno==EAGAIN || errno==EINTR?0:-1;
}
static int usb_complete(struct platform *p)
{
    if(!p->out.on)return 0;
    struct v4l2_buffer b={.type=p->out.type,.memory=V4L2_MEMORY_MMAP};
    while(xioctl(p->out.fd,VIDIOC_DQBUF,&b)==0) {
        if(policy_usb_done(&p->usb,b.index)<0) {
            usb_error(p,USB_ERR_OWNERSHIP,&b,EPROTO);errno=EPROTO;return -1;
        }
        if(p->profile && p->submitted_ns[b.index]) {
            perf_add(&p->usb_residence,now_ns()-p->submitted_ns[b.index]);p->submitted_ns[b.index]=0;
        }
        p->returned++;
        if(p->profile && p->perf_started){p->perf_end=now_ns();p->perf_cpu_end=cpu_ns();}
        if(b.flags&V4L2_BUF_FLAG_ERROR)usb_error(p,USB_ERR_BUFFER,&b,0);
    }
    if(errno==EAGAIN || errno==EINTR)return 0;
    usb_error(p,USB_ERR_DQBUF,NULL,errno);return -1;
}
/* Diagnostic operates exclusively on the copied UVC output buffer. */
static void usb_sequence_marker(uint8_t *output,uint32_t sequence,bool enabled)
{
    if(!enabled)return;
    const uint8_t y[8]={0x53,0x43,0x31,0x30,
        (uint8_t)sequence,(uint8_t)(sequence>>8),
        (uint8_t)(sequence>>16),(uint8_t)(sequence>>24)};
    for(unsigned i=0;i<8;i++){output[2*i]=y[i];output[2*i+1]=128;}
}
static int usb_send(struct platform *p,uint64_t n)
{
    int latest=p->broker.latest;
    if(!p->session.streaming || latest<0 || n<p->usb.next_ns)return 0;
    uint32_t seq=p->meta[latest].sequence;
    if(!sc130_accept_sequence(seq,false,0,p->usb_sent_valid,p->usb_sent_sequence))return 0;
    int slot=-1;
    for(unsigned i=0;i<p->usb.count;i++)if(p->usb.slot[i].state==FREE){slot=(int)i;break;}
    if(slot<0)return 0;
    if(p->profile && !p->perf_started) {
        p->perf_started=true;p->perf_start=now_ns();p->perf_cpu_start=cpu_ns();
    }
    if(!p->static_usb || !p->static_filled[slot]) {
        uint64_t w=p->profile?now_ns():0,c=p->profile?cpu_ns():0;
        memcpy(p->out.b[slot].ptr,p->cap.b[latest].ptr,SC130_SIZE);
        if(p->profile){uint64_t end=now_ns();perf_add(&p->copy_wall,end-w);perf_add(&p->copy_cpu,cpu_ns()-c);}
        p->static_filled[slot]=true;
    }
    usb_sequence_marker(p->out.b[slot].ptr,seq,p->usb_sequence_marker);
    if(policy_capture(&p->usb,(unsigned)slot)<0 ||
       policy_complete(&p->usb,(unsigned)slot,seq,n,true)<0){errno=EPROTO;return -1;}
    int id=policy_take_latest(&p->usb,n);
    if(id<0)return -1;
    uint64_t submit=p->profile?now_ns():0;
    if(queue_buffer(&p->out,(unsigned)id,n)<0)return -1;
    if(p->profile) {
        uint64_t end=now_ns();perf_add(&p->qbuf_cost,end-submit);
        if(p->dequeued_ns[latest])perf_add(&p->dq_to_submit,end-p->dequeued_ns[latest]);
        if(p->last_submit)perf_add(&p->submit_gap,end-p->last_submit);
        p->last_submit=end;p->submitted_ns[id]=submit;
    }
    p->sent++;if(p->profile){p->perf_end=now_ns();p->perf_cpu_end=cpu_ns();}
    p->usb_sent_sequence=seq;p->usb_sent_valid=true;p->latest_used=true;
    if(!p->out.on) {
        if(xioctl(p->out.fd,VIDIOC_STREAMON,&p->out.type)<0)return -1;
        p->out.on=true;
    }
    return 0;
}
static int capture_frames(struct platform *p)
{
    struct v4l2_buffer b={.type=p->cap.type,.memory=V4L2_MEMORY_MMAP};
    while(xioctl(p->cap.fd,VIDIOC_DQBUF,&b)==0) {
        p->captured++;
        if(b.index>=p->cap.count || broker_dequeue(&p->broker,b.index)<0){errno=EPROTO;return -1;}
        if((b.flags&V4L2_BUF_FLAG_ERROR) || b.bytesused!=SC130_SIZE)p->invalid++;
        else if(!sc130_accept_sequence(b.sequence,p->have_sequence,p->sequence,false,0))p->dropped++;
        else {
            if(p->broker.latest>=0 && !p->latest_used)p->dropped++;
            if(broker_latest(&p->broker,b.index)<0){errno=EPROTO;return -1;}
            p->meta[b.index]=b;p->sequence=b.sequence;p->have_sequence=true;p->latest_used=false;
            if(p->profile && p->session.streaming && p->perf_started){uint64_t t=now_ns(),complete=(uint64_t)b.timestamp.tv_sec*1000000000+(uint64_t)b.timestamp.tv_usec*1000;
                p->dequeued_ns[b.index]=t;if(t>=complete)perf_add(&p->source_age,t-complete);}

        }
        if(recycle(p)<0)return -1;
    }
    return errno==EAGAIN || errno==EINTR?0:-1;
}
static int dp_stop(struct platform *p)
{
    if(!p->dp)return 0;
    int r=dp_display_close(p->dp);
    if(r){p->dp_fault=true;return r;}
    p->dp=NULL;p->dp_fault=false;
    for(unsigned i=0;i<p->broker.count;i++)if(p->broker.dp[i])broker_dp_release(&p->broker,i);
    return 0;
}
static void dp_attach(struct platform *p)
{
    p->dp=dp_display_open();if(!p->dp)return;
    for(unsigned i=0;i<p->cap.count;i++) {
        if(p->exported[i]<0) {
            struct v4l2_exportbuffer e={.type=p->cap.type,.index=i,.flags=O_CLOEXEC};
            if(xioctl(p->cap.fd,VIDIOC_EXPBUF,&e)<0) {
                perror("capture EXPBUF");p->dp_fault=true;dp_stop(p);return;
            }
            p->exported[i]=e.fd;
        }
        int r=dp_display_import(p->dp,i,p->exported[i],SC130_STRIDE);
        if(r) {fprintf(stderr,"DP import index=%u failed=%d\n",i,r);p->dp_fault=true;dp_stop(p);return;}
    }
    fprintf(stderr,"DP imported %u persistent capture buffers\n",p->cap.count);
}
static int dp_events(struct platform *p)
{
    if(!p->dp || p->dp_fault)return 0;
    for(;;) {
        int retired=-1,r=dp_display_dispatch(p->dp,&retired);
        if(r<0){fprintf(stderr,"DP dispatch failed=%d\n",r);p->dp_fault=true;return 0;}
        if(!r)break;
        p->dp_flips++;
        if(retired>=0 && broker_dp_release(&p->broker,(unsigned)retired)<0){errno=EPROTO;return -1;}
    }
    return recycle(p);
}
static int dp_send(struct platform *p)
{
    int i=p->broker.latest;
    if(!p->dp || p->dp_fault || i<0 || p->broker.dp[i] || !dp_display_ready(p->dp))return 0;
    if(broker_dp_hold(&p->broker,(unsigned)i)<0){errno=EPROTO;return -1;}
    int r=dp_display_present(p->dp,(unsigned)i);
    if(r) {
        broker_dp_release(&p->broker,(unsigned)i);
        if(r!=-EAGAIN && r!=-EBUSY){fprintf(stderr,"DP present failed=%d\n",r);p->dp_fault=true;}
    } else p->latest_used=true;
    return 0;
}
/* Called only from the main loop: latest retention prevents capture QBUF
 * throughout the synchronous read. No pixels or capture ownership are changed. */
static int snapshot_latest(struct platform *p,const char *path)
{
    int i=p->broker.latest;
    if(i<0)return -ENODATA;
    if(!path || !*path || (unsigned)i>=p->broker.count || p->broker.queued[i] ||
       !p->cap.b[i].ptr || p->cap.b[i].length<SC130_SIZE ||
       p->meta[i].bytesused!=SC130_SIZE)return -EINVAL;
    size_t len=strlen(path);
    char *temp=malloc(len+40);
    if(!temp)return -ENOMEM;
    snprintf(temp,len+40,"%s.tmp.%ld",path,(long)getpid());
    FILE *out=fopen(temp,"wb");
    if(!out){int error=-errno;free(temp);return error;}
    size_t bytes=fwrite(p->cap.b[i].ptr,1,SC130_SIZE,out);
    int error=bytes==SC130_SIZE?0:-(errno?errno:EIO);
    if(fclose(out)!=0 && !error)error=-errno;
    if(!error && rename(temp,path)!=0)error=-errno;
    if(error)unlink(temp);
    else fprintf(stderr,"SNAPSHOT sequence=%u path=%s bytes=%u\n",p->meta[i].sequence,path,SC130_SIZE);
    free(temp);return error;
}
static void usage(const char *name)
{
    fprintf(stderr,"Usage: %s [--capture /dev/videoN] [--output /dev/videoN]\n"
        " [--soft-connect PATH] [--stream-interface N] [--disable-dp]\n [--snapshot PATH] (SIGUSR1 saves latest YUY2; default /tmp/sc130-frame.yuy2)\n [--usb-sequence-marker] (diagnostic USB copy only; default OFF)\n"
        " [--profile] (cumulative per-stream timing; quantiles are bucket upper bounds)\n"
        " [--profile-static-usb] (diagnostic frozen USB image, NOT live video)\n"
        "Camera and DP start at boot. USB defaults to120fps, accepts60fps.\n",name);
}
int main(int argc,char **argv)
{
    const char *cappath=NULL,*outpath=NULL,*soft=NULL,*snapshot="/tmp/sc130-frame.yuy2";bool use_dp=true,marker=false,profile=false,static_usb=false;unsigned stream_if=1;
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--profile"))profile=true;
        else if(!strcmp(argv[i],"--profile-static-usb")){profile=true;static_usb=true;}
        else if(!strcmp(argv[i],"--disable-dp"))use_dp=false;
        else if(!strcmp(argv[i],"--usb-sequence-marker"))marker=true;
        else if(!strcmp(argv[i],"--enable-120")){} /* compatibility; enabled by default */
        else if(!strcmp(argv[i],"--help")){usage(argv[0]);return 0;}
        else if(i+1<argc && !strcmp(argv[i],"--capture"))cappath=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--output"))outpath=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--soft-connect"))soft=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--snapshot"))snapshot=argv[++i];
        else if(i+1<argc && !strcmp(argv[i],"--stream-interface"))stream_if=(unsigned)strtoul(argv[++i],NULL,10);
        else {usage(argv[0]);return 2;}
    }
    if(stream_if>255){usage(argv[0]);return 2;}
    signal(SIGINT,stop_signal);signal(SIGTERM,stop_signal);signal(SIGUSR1,snapshot_signal);
    struct platform p={.cap={.fd=-1},.out={.fd=-1},.usb_sequence_marker=marker};
    p.profile=profile;p.static_usb=static_usb;p.perf_start=now_ns();p.perf_cpu_start=cpu_ns();
    if(static_usb)fprintf(stderr,"DIAGNOSTIC STATIC USB: output slots copied once; USB image is NOT live; DP stays live\n");
    for(unsigned i=0;i<16;i++)p.exported[i]=-1;
    policy_init(&p.usb,0,120);uvc_session_init(&p.session,true,stream_if);
    if(marker)fprintf(stderr,"USB sequence marker enabled: Y[0..7]=SC10 + source sequence LE32; USB output copy only\n");
    int result=1;
    if((cappath?open_video(&p.cap,cappath,false):find_capture(&p.cap))<0){perror("open capture");goto finish;}
    if(start_capture(&p.cap)<0){perror("start continuous capture");goto finish;}
    broker_init(&p.broker,p.cap.count);
    fprintf(stderr,"CAPTURE ALWAYS ON buffers=%u; independent of USB host\n",p.cap.count);
    uint64_t retry=0,lastlog=now_ns();
    while(!quitting) {
        uint64_t n=now_ns();
        if(n>=retry) {
            if(p.dp_fault)dp_stop(&p);
            if(use_dp && !p.dp)dp_attach(&p);
            if(p.out.fd<0 && usb_attach(&p,outpath,soft)<0)fprintf(stderr,"USB unavailable; capture continues: %s\n",strerror(errno));
            if(recycle(&p)<0){perror("capture recycle");goto finish;}
            retry=n+2000000000ull;
        }
        struct pollfd f[3]={{.fd=p.cap.fd,.events=POLLIN},
            {.fd=p.out.fd,.events=POLLPRI},
            {.fd=p.dp && !p.dp_fault?dp_display_fd(p.dp):-1,.events=POLLIN}};
        bool inflight=false;
        for(unsigned i=0;i<p.usb.count;i++)inflight|=p.usb.slot[i].state==USB_IN_FLIGHT;
        if(p.out.on && inflight)f[1].events|=POLLOUT;
        int timeout=20;
        if(p.session.streaming && p.broker.latest>=0 && !p.usb_sent_valid)timeout=0;
        else if(p.session.streaming && p.broker.latest>=0 &&
            sc130_accept_sequence(p.meta[p.broker.latest].sequence,false,0,p.usb_sent_valid,p.usb_sent_sequence) && n<p.usb.next_ns) {
            timeout=(int)((p.usb.next_ns-n+999999)/1000000);if(timeout>20)timeout=20;
        }
        int r=poll(f,3,timeout);
        if(r<0){if(errno==EINTR)continue;perror("poll");goto finish;}
        if(f[1].revents&POLLPRI)if(usb_events(&p)<0) {
            usb_error(&p,USB_ERR_EVENT,NULL,errno);perror("USB events");if(soft)write_text(soft,"disconnect");usb_detach(&p);
        }
        if(f[1].revents&(POLLHUP|POLLNVAL))usb_detach(&p);
        if(p.out.fd>=0 && usb_complete(&p)<0){perror("USB completion");usb_detach(&p);}
        if(f[2].revents&(POLLERR|POLLHUP|POLLNVAL))p.dp_fault=true;
        if(dp_events(&p)<0){perror("DP ownership");goto finish;}
        if(capture_frames(&p)<0){perror("capture dequeue");goto finish;}
        if(snapshot_requested && p.broker.latest>=0) {
            snapshot_requested=0;
            int saved=snapshot_latest(&p,snapshot);
            if(saved)fprintf(stderr,"SNAPSHOT failed path=%s error=%s (%d)\n",snapshot,strerror(-saved),saved);
        }
        if(dp_send(&p)<0){perror("DP ownership");goto finish;}
        n=now_ns();
        if(p.out.fd>=0 && usb_send(&p,n)<0){usb_error(&p,USB_ERR_SEND,NULL,errno);perror("USB send");usb_detach(&p);}
        if(f[0].revents&(POLLHUP|POLLNVAL)){fprintf(stderr,"capture device removed\n");goto finish;}
        if(n-lastlog>=1000000000ull) {
            unsigned held=0;for(unsigned i=0;i<p.cap.count;i++)held+=p.broker.dp[i];
            fprintf(stderr,"stats ns=%llu capture=%llu sent=%llu returned=%llu invalid=%llu drop=%llu usb_error=%llu dp_flips=%llu seq=%u dp_held=%u usb_buf_error=%llu usb_owner_error=%llu usb_dq_error=%llu usb_event_error=%llu usb_send_error=%llu\n",
                (unsigned long long)n,(unsigned long long)p.captured,(unsigned long long)p.sent,
                (unsigned long long)p.returned,(unsigned long long)p.invalid,(unsigned long long)p.dropped,
                (unsigned long long)p.usb_errors,(unsigned long long)p.dp_flips,p.sequence,held,
                (unsigned long long)p.usb_error_kind[USB_ERR_BUFFER],
                (unsigned long long)p.usb_error_kind[USB_ERR_OWNERSHIP],
                (unsigned long long)p.usb_error_kind[USB_ERR_DQBUF],
                (unsigned long long)p.usb_error_kind[USB_ERR_EVENT],
                (unsigned long long)p.usb_error_kind[USB_ERR_SEND]);lastlog=n;
            profile_report(&p);
        }
    }
    result=0;
finish:
    profile_report(&p);
    if(soft)write_text(soft,"disconnect");
    usb_detach(&p);
    /* Stop acquisition but retain mappings/exports until scanout definitely stops. */
    if(p.cap.on){xioctl(p.cap.fd,VIDIOC_STREAMOFF,&p.cap.type);p.cap.on=false;}
    while(p.dp && dp_stop(&p)<0) {
        fprintf(stderr,"shutdown waits for DP disable; capture memory quarantined\n");
        struct timespec pause={.tv_sec=1};nanosleep(&pause,NULL);
    }
    for(unsigned i=0;i<16;i++)if(p.exported[i]>=0)close(p.exported[i]);
    free_buffers(&p.cap);
    if(p.cap.fd>=0)close(p.cap.fd);
    return result;
}



