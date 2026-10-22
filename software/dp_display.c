#define _GNU_SOURCE
#include "dp_display.h"
#include <drm/drm.h>
#include <drm/drm_mode.h>
#include <drm/drm_fourcc.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

/* Values of DRM plane 'type' property (stable UAPI enum). */
#define DP_PLANE_OVERLAY 0
#define DP_PLANE_PRIMARY 1
#define DP_SLOTS 16
#define DP_MAX 128
#define PTR(x) ((uint64_t)(uintptr_t)(x))
struct dp_slot { uint32_t handle, fb, stride; };
struct dp_prop { uint32_t object, type, id; char name[32]; };
struct dp_display {
    int fd, active, pending;
    uint32_t connector, crtc, primary, overlay, blob;
    struct dp_slot slots[DP_SLOTS], black;
    void *black_map;
    uint64_t black_size, serial, expected, events;
    uint32_t last_sequence;
    int have_sequence, enabled;
    unsigned char input[4096];
    size_t input_len;
    struct dp_prop cache[64];
    unsigned ncache;
};
struct dp_atomic {
    uint32_t objects[4], counts[4], props[64];
    uint64_t values[64];
    unsigned nobj, nprop;
    int error;
};
static int call(int fd, unsigned long req, void *arg)
{
    int r;
    do { r=ioctl(fd,req,arg); } while(r<0 && errno==EINTR);
    return r<0 ? -errno : 0;
}
static int property(struct dp_display *d, uint32_t object, uint32_t type,
                    const char *name, uint32_t *id, uint64_t *value)
{
    if(id && !value) for(unsigned i=0;i<d->ncache;i++)
        if(d->cache[i].object==object && d->cache[i].type==type && !strcmp(d->cache[i].name,name)) { *id=d->cache[i].id; return 0; }
    uint32_t ids[DP_MAX]; uint64_t vals[DP_MAX];
    struct drm_mode_obj_get_properties p={.obj_id=object,.obj_type=type};
    int r=call(d->fd,DRM_IOCTL_MODE_OBJ_GETPROPERTIES,&p);
    if(r) return r;
    if(p.count_props>DP_MAX) return -EOVERFLOW;
    p.props_ptr=PTR(ids); p.prop_values_ptr=PTR(vals);
    r=call(d->fd,DRM_IOCTL_MODE_OBJ_GETPROPERTIES,&p);
    if(r) return r;
    if(p.count_props>DP_MAX) return -EOVERFLOW;
    for(unsigned i=0;i<p.count_props;i++) {
        struct drm_mode_get_property q={.prop_id=ids[i]};
        r=call(d->fd,DRM_IOCTL_MODE_GETPROPERTY,&q);
        if(r) return r;
        if(!strcmp(q.name,name)) {
            fprintf(stderr,"DP property object=%u %s id=%u value=%llu\n",object,name,ids[i],(unsigned long long)vals[i]);
            if(id)*id=ids[i];
            if(value)*value=vals[i];
            if(id && d->ncache<64) {
                struct dp_prop *cp=&d->cache[d->ncache++];
                cp->object=object;cp->type=type;cp->id=ids[i];
                snprintf(cp->name,sizeof(cp->name),"%s",name);
            }
            return 0;
        }
    }
    return -ENOENT;
}
static void add(struct dp_display *d, struct dp_atomic *a, uint32_t object,
                uint32_t type, const char *name, uint64_t value)
{
    uint32_t prop; int r;
    if(a->error) return;
    r=property(d,object,type,name,&prop,NULL);
    if(r) { a->error=r; return; }
    if(!a->nobj || a->objects[a->nobj-1]!=object) {
        if(a->nobj==4) {a->error=-E2BIG;return;}
        a->objects[a->nobj++]=object;
    }
    if(a->nprop==64) {a->error=-E2BIG;return;}
    a->counts[a->nobj-1]++;
    a->props[a->nprop]=prop; a->values[a->nprop++]=value;
}
static int commit(struct dp_display *d,struct dp_atomic *a,uint32_t flags,uint64_t token)
{
    struct drm_mode_atomic req={.flags=flags,.count_objs=a->nobj,
        .objs_ptr=PTR(a->objects),.count_props_ptr=PTR(a->counts),
        .props_ptr=PTR(a->props),.prop_values_ptr=PTR(a->values),.user_data=token};
    if(a->error)return a->error;
    return call(d->fd,DRM_IOCTL_MODE_ATOMIC,&req);
}
static void plane(struct dp_display *d,struct dp_atomic *a,uint32_t id,uint32_t fb)
{
    add(d,a,id,DRM_MODE_OBJECT_PLANE,"FB_ID",fb);
    add(d,a,id,DRM_MODE_OBJECT_PLANE,"CRTC_ID",fb?d->crtc:0);
    if(!fb)return;
    add(d,a,id,DRM_MODE_OBJECT_PLANE,"SRC_X",0);
    add(d,a,id,DRM_MODE_OBJECT_PLANE,"SRC_Y",0);
    add(d,a,id,DRM_MODE_OBJECT_PLANE,"SRC_W",1280u<<16);
    add(d,a,id,DRM_MODE_OBJECT_PLANE,"SRC_H",1024u<<16);
    add(d,a,id,DRM_MODE_OBJECT_PLANE,"CRTC_X",0);
    add(d,a,id,DRM_MODE_OBJECT_PLANE,"CRTC_Y",0);
    add(d,a,id,DRM_MODE_OBJECT_PLANE,"CRTC_W",1280);
    add(d,a,id,DRM_MODE_OBJECT_PLANE,"CRTC_H",1024);
}
static int framebuffer(struct dp_display *d,struct dp_slot *s,uint32_t format)
{
    struct drm_mode_fb_cmd2 fb={.width=1280,.height=1024,.pixel_format=format};
    fb.handles[0]=s->handle; fb.pitches[0]=s->stride;
    int r=call(d->fd,DRM_IOCTL_MODE_ADDFB2,&fb);
    if(!r)s->fb=fb.fb_id;
    return r;
}
static void slot_free(struct dp_display *d,struct dp_slot *s)
{
    if(s->fb) call(d->fd,DRM_IOCTL_MODE_RMFB,&s->fb);
    if(s->handle) {struct drm_gem_close c={.handle=s->handle};call(d->fd,DRM_IOCTL_GEM_CLOSE,&c);}
    memset(s,0,sizeof(*s));
}
static int select_output(struct dp_display *d)
{
    struct drm_mode_card_res res={0};
    uint32_t conns[DP_MAX],crtcs[DP_MAX];
    int r=call(d->fd,DRM_IOCTL_MODE_GETRESOURCES,&res);
    if(r)return r;
    if(res.count_connectors>DP_MAX || res.count_crtcs>DP_MAX)return -EOVERFLOW;
    res.connector_id_ptr=PTR(conns);res.crtc_id_ptr=PTR(crtcs);
    /* No framebuffer/encoder arrays requested. */
    res.count_fbs=0;res.count_encoders=0;
    r=call(d->fd,DRM_IOCTL_MODE_GETRESOURCES,&res);
    if(r)return r;
    if(res.count_connectors>DP_MAX || res.count_crtcs>DP_MAX)return -EOVERFLOW;
    for(unsigned i=0;i<res.count_connectors;i++) {
        uint32_t enc[DP_MAX];struct drm_mode_modeinfo modes[DP_MAX];
        /* A zero mode capacity requests a forced DRM probe. On an absent DP
         * sink that waits for HPD and blocks this shared capture/USB loop.
         * Read cached hotplug state instead; kernel HPD work updates it. */
        struct drm_mode_get_connector c={.connector_id=conns[i],
            .count_modes=DP_MAX,.modes_ptr=PTR(modes)};
        r=call(d->fd,DRM_IOCTL_MODE_GETCONNECTOR,&c);if(r)continue;
        if(c.connector_type!=DRM_MODE_CONNECTOR_DisplayPort || c.connection!=1)continue;
        if(c.count_encoders>DP_MAX || c.count_modes>DP_MAX)return -EOVERFLOW;
        c.encoders_ptr=PTR(enc);c.modes_ptr=PTR(modes);c.count_props=0;
        c.count_modes=DP_MAX;
        r=call(d->fd,DRM_IOCTL_MODE_GETCONNECTOR,&c);if(r)return r;
        if(c.count_encoders>DP_MAX || c.count_modes>DP_MAX)return -EOVERFLOW;
        fprintf(stderr,"DP connector=%u modes=%u encoders=%u\n",c.connector_id,c.count_modes,c.count_encoders);
        for(unsigned m=0;m<c.count_modes;m++)
            fprintf(stderr,"DP EDID mode %s clock=%u totals=%ux%u\n",modes[m].name,modes[m].clock,modes[m].htotal,modes[m].vtotal);
        for(unsigned e=0;e<c.count_encoders;e++) {
            struct drm_mode_get_encoder en={.encoder_id=enc[e]};
            if(call(d->fd,DRM_IOCTL_MODE_GETENCODER,&en))continue;
            for(unsigned k=0;k<res.count_crtcs && k<32;k++)if(en.possible_crtcs&(1u<<k)) {
                d->connector=c.connector_id;d->crtc=crtcs[k];
                struct drm_mode_get_plane_res pr={0};uint32_t ids[DP_MAX];
                r=call(d->fd,DRM_IOCTL_MODE_GETPLANERESOURCES,&pr);if(r)return r;
                if(pr.count_planes>DP_MAX)return -EOVERFLOW;
                pr.plane_id_ptr=PTR(ids);
                r=call(d->fd,DRM_IOCTL_MODE_GETPLANERESOURCES,&pr);if(r)return r;
                if(pr.count_planes>DP_MAX)return -EOVERFLOW;
                for(unsigned j=0;j<pr.count_planes;j++) {
                    struct drm_mode_get_plane p={.plane_id=ids[j]};uint32_t fmts[DP_MAX];uint64_t type;
                    r=call(d->fd,DRM_IOCTL_MODE_GETPLANE,&p);if(r)continue;
                    if(!(p.possible_crtcs&(1u<<k)) || p.count_format_types>DP_MAX)continue;
                    p.format_type_ptr=PTR(fmts);
                    r=call(d->fd,DRM_IOCTL_MODE_GETPLANE,&p);if(r)return r;
                    if(p.count_format_types>DP_MAX)return -EOVERFLOW;
                    if(property(d,p.plane_id,DRM_MODE_OBJECT_PLANE,"type",NULL,&type))continue;
                    for(unsigned f=0;f<p.count_format_types;f++) {
                        if(type==DP_PLANE_PRIMARY && fmts[f]==DRM_FORMAT_RGB565)d->primary=p.plane_id;
                        if(type==DP_PLANE_OVERLAY && fmts[f]==DRM_FORMAT_YUYV)d->overlay=p.plane_id;
                    }
                }
                if(d->primary && d->overlay)return 0;
                d->primary=d->overlay=0;
            }
        }
    }
    return -ENODEV;
}
static void destroy(struct dp_display *d)
{
    for(unsigned i=0;i<DP_SLOTS;i++)slot_free(d,&d->slots[i]);
    if(d->black_map && d->black_map!=MAP_FAILED)munmap(d->black_map,d->black_size);
    /* Dumb handles need DESTROY_DUMB, not GEM_CLOSE. */
    if(d->black.fb)call(d->fd,DRM_IOCTL_MODE_RMFB,&d->black.fb);
    if(d->black.handle) {struct drm_mode_destroy_dumb x={.handle=d->black.handle};call(d->fd,DRM_IOCTL_MODE_DESTROY_DUMB,&x);}
    if(d->blob) {struct drm_mode_destroy_blob b={.blob_id=d->blob};call(d->fd,DRM_IOCTL_MODE_DESTROYPROPBLOB,&b);}
    if(d->fd>=0)close(d->fd);
    free(d);
}
struct dp_display *dp_display_open(void)
{
    struct dp_display *d=calloc(1,sizeof(*d));int r=-ENODEV;
    if(!d)return NULL;
    d->fd=-1;d->active=d->pending=-1;
    for(unsigned card=0;card<16;card++) {
        char path[64],name[64]={0};struct drm_version ver={.name_len=sizeof(name)-1,.name=name};
        snprintf(path,sizeof(path),"/dev/dri/card%u",card);
        d->fd=open(path,O_RDWR|O_CLOEXEC|O_NONBLOCK);if(d->fd<0)continue;
        if(!call(d->fd,DRM_IOCTL_VERSION,&ver) && !strcmp(name,"zynqmp-dpsub"))break;
        close(d->fd);d->fd=-1;
    }
    if(d->fd<0)goto fail;
    r=call(d->fd,DRM_IOCTL_SET_MASTER,NULL);if(r)goto fail;
    struct drm_set_client_cap cap={.capability=DRM_CLIENT_CAP_UNIVERSAL_PLANES,.value=1};
    r=call(d->fd,DRM_IOCTL_SET_CLIENT_CAP,&cap);if(r)goto fail;
    cap.capability=DRM_CLIENT_CAP_ATOMIC;
    r=call(d->fd,DRM_IOCTL_SET_CLIENT_CAP,&cap);if(r)goto fail;
    r=select_output(d);if(r)goto fail;
    struct drm_mode_create_dumb cb={.height=1024,.width=1280,.bpp=16};
    r=call(d->fd,DRM_IOCTL_MODE_CREATE_DUMB,&cb);if(r)goto fail;
    d->black.handle=cb.handle;d->black.stride=cb.pitch;d->black_size=cb.size;
    struct drm_mode_map_dumb mb={.handle=cb.handle};
    r=call(d->fd,DRM_IOCTL_MODE_MAP_DUMB,&mb);if(r)goto fail;
    d->black_map=mmap(NULL,cb.size,PROT_READ|PROT_WRITE,MAP_SHARED,d->fd,mb.offset);
    if(d->black_map==MAP_FAILED) {r=-errno;goto fail;}
    memset(d->black_map,0,cb.size);
    r=framebuffer(d,&d->black,DRM_FORMAT_RGB565);if(r)goto fail;
    /* Proven XVIDC_VM_1280x1024_120_P_RB timing; DRM clock is integer kHz. */
    struct drm_mode_modeinfo mode={.clock=187315,.hdisplay=1280,.hsync_start=1328,
        .hsync_end=1360,.htotal=1440,.vdisplay=1024,.vsync_start=1027,
        .vsync_end=1034,.vtotal=1084,.vrefresh=120,
        .flags=DRM_MODE_FLAG_PHSYNC|DRM_MODE_FLAG_NVSYNC,.type=DRM_MODE_TYPE_USERDEF};
    snprintf(mode.name,sizeof(mode.name),"1280x1024-120");
    struct drm_mode_create_blob blob={.data=PTR(&mode),.length=sizeof(mode)};
    r=call(d->fd,DRM_IOCTL_MODE_CREATEPROPBLOB,&blob);if(r)goto fail;
    d->blob=blob.blob_id;
    struct dp_atomic a={0};
    add(d,&a,d->connector,DRM_MODE_OBJECT_CONNECTOR,"CRTC_ID",d->crtc);
    add(d,&a,d->crtc,DRM_MODE_OBJECT_CRTC,"MODE_ID",d->blob);
    add(d,&a,d->crtc,DRM_MODE_OBJECT_CRTC,"ACTIVE",1);
    plane(d,&a,d->primary,d->black.fb);
    add(d,&a,d->primary,DRM_MODE_OBJECT_PLANE,"alpha",65535);
    plane(d,&a,d->overlay,0);
    r=commit(d,&a,DRM_MODE_ATOMIC_TEST_ONLY|DRM_MODE_ATOMIC_ALLOW_MODESET,0);if(r)goto fail;
    r=commit(d,&a,DRM_MODE_ATOMIC_ALLOW_MODESET,0);if(r)goto fail;
    d->enabled=1;
    fprintf(stderr,"DP enabled connector=%u crtc=%u primary=%u YUYV-overlay=%u mode=%s clock=%ukHz totals=1440x1084\n",d->connector,d->crtc,d->primary,d->overlay,mode.name,mode.clock);
    struct drm_mode_crtc actual={.crtc_id=d->crtc};
    if(!call(d->fd,DRM_IOCTL_MODE_GETCRTC,&actual))
        fprintf(stderr,"DP actual mode_valid=%u %ux%u clock=%ukHz\n",actual.mode_valid,actual.mode.hdisplay,actual.mode.vdisplay,actual.mode.clock);
    return d;
fail:
    fprintf(stderr,"DP open failed: %s (%d)\n",strerror(-r),r);
    destroy(d);errno=-r;return NULL;
}
int dp_display_fd(struct dp_display *d) {return d?d->fd:-1;}
int dp_display_ready(struct dp_display *d) {return d && d->pending<0;}
int dp_display_import(struct dp_display *d,unsigned index,int dmabuf_fd,unsigned stride)
{
    if(!d || index>=DP_SLOTS || dmabuf_fd<0 || stride<2560 || (stride&255))return -EINVAL;
    if(d->slots[index].fb)return d->slots[index].stride==stride?0:-EEXIST;
    struct drm_prime_handle p={.fd=dmabuf_fd};
    int r=call(d->fd,DRM_IOCTL_PRIME_FD_TO_HANDLE,&p);if(r)return r;
    d->slots[index].handle=p.handle;d->slots[index].stride=stride;
    r=framebuffer(d,&d->slots[index],DRM_FORMAT_YUYV);
    if(r)slot_free(d,&d->slots[index]);
    return r;
}
int dp_display_present(struct dp_display *d,unsigned index)
{
    if(!d || index>=DP_SLOTS || !d->slots[index].fb)return -EINVAL;
    if(d->pending>=0)return -EAGAIN;
    if(d->active==(int)index)return -EBUSY;
    struct dp_atomic a={0};
    plane(d,&a,d->overlay,d->slots[index].fb);
    /* Graphics is above video; transparent black reveals the YUYV overlay. */
    add(d,&a,d->primary,DRM_MODE_OBJECT_PLANE,"alpha",0);
    uint64_t token=d->serial+1;
    int r=commit(d,&a,DRM_MODE_ATOMIC_NONBLOCK|DRM_MODE_PAGE_FLIP_EVENT,token);
    if(r)return r;
    d->serial=d->expected=token;d->pending=(int)index;
    return 0;
}
static int dp_process_event(struct dp_display *d,const struct drm_event_vblank *ev,int *retired)
{
    if(d->pending<0 || ev->user_data!=d->expected)return -EPROTO;
    *retired=d->active;d->active=d->pending;d->pending=-1;
    if(d->have_sequence && ev->sequence-d->last_sequence>1)
        fprintf(stderr,"DP flip gap=%u sequence=%u\n",ev->sequence-d->last_sequence,ev->sequence);
    d->last_sequence=ev->sequence;d->have_sequence=1;d->events++;
    if(d->events==1 || !(d->events%1200))
        fprintf(stderr,"DP flips=%llu sequence=%u active=%d retired=%d\n",(unsigned long long)d->events,ev->sequence,d->active,*retired);
    return 1;
}
int dp_display_dispatch(struct dp_display *d,int *retired)
{
    if(!d || !retired)return -EINVAL;
    *retired=-1;
    for(;;) {
        if(d->input_len>=sizeof(struct drm_event)) {
            struct drm_event h;memcpy(&h,d->input,sizeof(h));
            if(h.length<sizeof(h) || h.length>sizeof(d->input))return -EPROTO;
            if(d->input_len>=h.length) {
                struct drm_event_vblank ev={0};
                if(h.type==DRM_EVENT_FLIP_COMPLETE && h.length<sizeof(ev))return -EPROTO;
                if(h.type==DRM_EVENT_FLIP_COMPLETE)memcpy(&ev,d->input,sizeof(ev));
                memmove(d->input,d->input+h.length,d->input_len-h.length);d->input_len-=h.length;
                if(h.type==DRM_EVENT_FLIP_COMPLETE)return dp_process_event(d,&ev,retired);
                continue;
            }
        }
        ssize_t n=read(d->fd,d->input+d->input_len,sizeof(d->input)-d->input_len);
        if(n<0) {if(errno==EINTR)continue;if(errno==EAGAIN)return 0;return -errno;}
        if(!n)return -EPIPE;
        d->input_len+=(size_t)n;
    }
}
int dp_display_close(struct dp_display *d)
{
    if(!d)return 0;
    if(d->enabled) {
        struct dp_atomic a={0};
        plane(d,&a,d->primary,0);plane(d,&a,d->overlay,0);
        add(d,&a,d->crtc,DRM_MODE_OBJECT_CRTC,"ACTIVE",0);
        add(d,&a,d->crtc,DRM_MODE_OBJECT_CRTC,"MODE_ID",0);
        add(d,&a,d->connector,DRM_MODE_OBJECT_CONNECTOR,"CRTC_ID",0);
        int r=commit(d,&a,DRM_MODE_ATOMIC_ALLOW_MODESET,0);
        if(r) {fprintf(stderr,"DP disable failed %d: buffers quarantined\n",r);return r;}
        d->enabled=0;
    }
    fprintf(stderr,"DP closed flips=%llu\n",(unsigned long long)d->events);
    destroy(d);return 0;
}




