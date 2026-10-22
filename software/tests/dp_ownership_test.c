#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
static int dp_test_ioctl(int fd, unsigned long request, ...);
#define ioctl dp_test_ioctl
#include "../dp_display.c"
#undef ioctl
static int injected;
static int probe_only;
static int dp_test_ioctl(int fd, unsigned long request, ...) {
    (void)fd; (void)request;
    if(probe_only) {
        va_list ap;va_start(ap,request);void *arg=va_arg(ap,void*);va_end(ap);
        if(request==DRM_IOCTL_MODE_GETRESOURCES) {
            struct drm_mode_card_res *r=arg;
            r->count_connectors=1;r->count_crtcs=1;
            if(r->connector_id_ptr)*(uint32_t*)(uintptr_t)r->connector_id_ptr=10;
            if(r->crtc_id_ptr)*(uint32_t*)(uintptr_t)r->crtc_id_ptr=20;
            return 0;
        }
        assert(request==DRM_IOCTL_MODE_GETCONNECTOR);
        struct drm_mode_get_connector *c=arg;
        /* count_modes=0 forces a synchronous, potentially seconds-long probe. */
        assert(c->count_modes>0 && c->modes_ptr!=0);
        c->connection=2;c->connector_type=DRM_MODE_CONNECTOR_DisplayPort;
        c->count_modes=0;c->count_encoders=0;
        return 0;
    }
    if (injected) { errno = injected; return -1; }
    return 0;
}
int main(void) {
    struct dp_display absent={.fd=9};
    probe_only=1;assert(select_output(&absent)==-ENODEV);probe_only=0;
    struct dp_display d = { .fd=9, .active=-1, .pending=-1 };
    struct drm_event_vblank ev = { .base={.type=DRM_EVENT_FLIP_COMPLETE,
        .length=sizeof(ev)}, .user_data=1, .sequence=7 };
    int retired=99;
    const char *names[]={"FB_ID","CRTC_ID","SRC_X","SRC_Y","SRC_W","SRC_H","CRTC_X","CRTC_Y","CRTC_W","CRTC_H","alpha"};
    for(unsigned i=0;i<11;i++) {d.cache[i].object=0;d.cache[i].type=DRM_MODE_OBJECT_PLANE;d.cache[i].id=i+1;strcpy(d.cache[i].name,names[i]);} d.ncache=11;
    d.slots[0].fb=100; d.slots[1].fb=101;
    assert(dp_display_ready(&d));
    injected=EIO;
    assert(dp_display_present(&d,0)==-EIO);
    assert(d.active==-1 && d.pending==-1);
    injected=0;
    assert(dp_display_present(&d,0)==0);
    assert(d.active==-1 && d.pending==0);
    assert(dp_display_present(&d,1)==-EAGAIN);
    assert(dp_process_event(&d,&ev,&retired)==1 && retired==-1);
    assert(d.active==0 && d.pending==-1);
    assert(dp_display_present(&d,0)==-EBUSY);
    assert(dp_display_present(&d,1)==0);
    ev.user_data=2;
    assert(dp_process_event(&d,&ev,&retired)==1 && retired==0);
    assert(d.active==1 && d.pending==-1);
    /* A duplicate/stale completion must never release current scanout. */
    assert(dp_process_event(&d,&ev,&retired)==-EPROTO);
    assert(d.active==1 && d.pending==-1);
    /* Disable failure must preserve the fd, imports and active ownership. */
    const char *cn[]={"ACTIVE","MODE_ID"};
    for(unsigned i=0;i<2;i++){struct dp_prop *p=&d.cache[d.ncache++];p->type=DRM_MODE_OBJECT_CRTC;p->id=20+i;strcpy(p->name,cn[i]);}
    struct dp_prop *cp=&d.cache[d.ncache++];cp->type=DRM_MODE_OBJECT_CONNECTOR;cp->id=30;strcpy(cp->name,"CRTC_ID");
    d.enabled=1;injected=EIO;
    assert(dp_display_close(&d)==-EIO);
    assert(d.enabled && d.active==1 && d.fd==9 && d.slots[1].fb==101);
    injected=0;
    assert(dp_display_import(&d,16,3,2560)==-EINVAL);
    assert(dp_display_import(&d,2,3,2561)==-EINVAL);
    /* Real stream parser preserves partial records and retires only on completion. */
    int pipes[2];assert(pipe(pipes)==0);d.fd=pipes[0];
    assert(fcntl(d.fd,F_SETFL,O_NONBLOCK)==0);
    assert(dp_display_present(&d,0)==0);ev.user_data=3;ev.sequence=8;
    assert(write(pipes[1],&ev,3)==3);
    assert(dp_display_dispatch(&d,&retired)==0 && d.pending==0);
    assert(write(pipes[1],((char*)&ev)+3,sizeof(ev)-3)==(ssize_t)sizeof(ev)-3);
    assert(dp_display_dispatch(&d,&retired)==1 && retired==1 && d.active==0);
    assert(dp_display_dispatch(&d,&retired)==0);
    close(pipes[0]);close(pipes[1]);
    puts("dp ownership tests passed");
    return 0;
}



