#include "uvc_core.h"
#include <errno.h>
#include <string.h>
#include <endian.h>
#include "dual_core.h"
bool sc130_accept_sequence(uint32_t candidate,bool held_valid,uint32_t held,bool sent_valid,uint32_t sent)
{ return (!held_valid || dual_newer(candidate,held)) && (!sent_valid || dual_newer(candidate,sent)); }
void policy_init(struct policy *p, unsigned count, unsigned fps)
{ memset(p,0,sizeof(*p)); p->count=count; p->fps=fps; }
int policy_capture(struct policy *p,unsigned id)
{
    if (id>=p->count || p->slot[id].state!=FREE) return -EINVAL;
    for (unsigned i=0;i<p->count;i++) if(p->slot[i].state==CAPTURING) return -EBUSY;
    p->slot[id].state=CAPTURING; return 0;
}
int policy_complete(struct policy *p,unsigned id,uint64_t seq,uint64_t ns,bool valid)
{
    if(id>=p->count || p->slot[id].state!=CAPTURING) return -EINVAL;
    p->slot[id]=(struct frame_slot){valid?READY:FREE,seq,ns};
    if(!valid) p->dropped++;
    return 0;
}
int policy_take_latest(struct policy *p,uint64_t ns)
{
    int best=-1;
    if(!p->fps || ns<p->next_ns) return -1;
    for(unsigned i=0;i<p->count;i++) if(p->slot[i].state==READY &&
        (best<0 || dual_newer((uint32_t)p->slot[i].sequence,(uint32_t)p->slot[best].sequence))) best=(int)i;
    if(best<0) return -1;
    for(unsigned i=0;i<p->count;i++) if((int)i!=best && p->slot[i].state==READY) {
        p->slot[i].state=FREE; p->dropped++;
    }
    p->slot[best].state=USB_IN_FLIGHT;
    uint64_t period=(1000000000ull+p->fps/2)/p->fps;
    p->next_ns=p->next_ns?p->next_ns+((ns-p->next_ns)/period+1)*period:ns+period;
    return best;
}
int policy_usb_done(struct policy *p,unsigned id)
{
    if(id>=p->count || p->slot[id].state!=USB_IN_FLIGHT) return -EINVAL;
    p->slot[id].state=FREE;return 0;
}
void policy_reset(struct policy *p)
{ for(unsigned i=0;i<p->count && i<SC130_MAX_BUFFERS;i++) p->slot[i].state=FREE; p->next_ns=0; }
static void put32(uint8_t *b,uint32_t v)
{ for(unsigned i=0;i<4;i++) b[i]=(uint8_t)(v>>(8*i)); }
static uint32_t get32(const uint8_t *b)
{ return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24); }
static void control(uint8_t *b,unsigned fps)
{
    memset(b,0,SC130_CTRL_LEN);b[0]=1;b[2]=1;b[3]=1;
    put32(b+4,fps==120?83333:166667);put32(b+18,SC130_SIZE);
    put32(b+22,49152);put32(b+26,48000000);b[30]=0;b[31]=1;b[32]=1;b[33]=1;
}
void uvc_session_init(struct uvc_session *s,bool enable120,unsigned stream_if)
{
    memset(s,0,sizeof(*s));s->enable120=enable120;s->stream_if=stream_if;s->fps=enable120?120:60;
    control(s->probe,s->fps);control(s->commit,s->fps);
}
void uvc_session_reset(struct uvc_session *s)
{ uvc_session_init(s,s->enable120,s->stream_if); }
void uvc_session_stop(struct uvc_session *s,bool disconnect)
{ if(disconnect)uvc_session_reset(s);else {s->streaming=false;s->pending_selector=0;} }
int uvc_handle_setup(struct uvc_session *s,const struct usb_ctrlrequest *r,struct uvc_request_data *d)
{
    unsigned idx=le16toh(r->wIndex),v=le16toh(r->wValue),len=le16toh(r->wLength),sel=v>>8;
    memset(d,0,sizeof(*d));d->length=-EL2HLT;s->pending_selector=0;
    /* Request Error Code Control on the otherwise control-free VC interface. */
    if(idx==0 && v==0x200 && r->bRequestType==0xa1 && r->bRequest==0x81 && len) {
        d->length=1;d->data[0]=s->request_error;s->request_error=0;return 0;
    }
    if(idx!=s->stream_if || (v&255) || (sel!=1 && sel!=2)) {s->request_error=6;return -EINVAL;}
    if(r->bRequest==1) {
        if(r->bRequestType!=0x21 || len!=SC130_CTRL_LEN || s->streaming) {s->request_error=7;return -EINVAL;}
        s->pending_selector=sel;d->length=SC130_CTRL_LEN;return 0;
    }
    if(r->bRequestType!=0xa1 || !len) {s->request_error=7;return -EINVAL;}
    switch(r->bRequest) {
    case 0x81: memcpy(d->data,sel==1?s->probe:s->commit,SC130_CTRL_LEN);d->length=SC130_CTRL_LEN;break;
    case 0x83: control(d->data,60);d->length=SC130_CTRL_LEN;break;
    case 0x87: control(d->data,s->enable120?120:60);d->length=SC130_CTRL_LEN;break;
    case 0x82: control(d->data,s->enable120?120:60);d->length=SC130_CTRL_LEN;break;
    case 0x84: memset(d->data,0,SC130_CTRL_LEN);d->length=SC130_CTRL_LEN;break;
    case 0x85: d->data[0]=SC130_CTRL_LEN;d->length=2;break;
    case 0x86: d->data[0]=3;d->length=1;break;
    default: s->request_error=7;return -EINVAL;
    }
    if((unsigned)d->length>len) d->length=(int)len;
    return 0;
}
int uvc_handle_data(struct uvc_session *s,const struct uvc_request_data *d)
{
    unsigned sel=s->pending_selector;s->pending_selector=0;
    if(!sel || d->length!=SC130_CTRL_LEN || s->streaming || d->data[2]!=1 || d->data[3]!=1) {
        s->request_error=4;return -EINVAL;
    }
    uint32_t interval=get32(d->data+4);
    if(interval!=166667 && !(interval==83333 && s->enable120)) {s->request_error=4;return -EINVAL;}
    unsigned fps=interval==83333?120:60;
    if(sel==1) control(s->probe,fps);
    else { control(s->commit,fps);s->fps=fps;s->committed=true; }
    return 0;
}
int uvc_session_start(struct uvc_session *s)
{
    if(!s->superspeed || !s->committed || s->streaming) return -EINVAL;
    s->streaming=true;return 0;
}
void sc130_pattern(uint8_t *b,uint64_t frame)
{
    for(unsigned y=0;y<SC130_HEIGHT;y++) for(unsigned x=0;x<SC130_WIDTH;x+=2) {
        unsigned pos=y*SC130_STRIDE+x*2;
        uint8_t value=(uint8_t)(16+(x/160)*219/7);
        /* Small moving marker leaves black/white corner probes deterministic. */
        if(y>480 && y<544 && x/2==(frame%640)) value=235;
        b[pos]=value;b[pos+1]=128;b[pos+2]=value;b[pos+3]=128;
    }
}

