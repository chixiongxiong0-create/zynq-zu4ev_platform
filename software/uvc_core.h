#ifndef SC130_UVC_CORE_H
#define SC130_UVC_CORE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <linux/usb/ch9.h>
#include <linux/usb/g_uvc.h>
#define SC130_WIDTH 1280u
#define SC130_HEIGHT 1024u
#define SC130_STRIDE 2560u
#define SC130_SIZE 2621440u
#define SC130_MAX_BUFFERS 16
#define SC130_CTRL_LEN 34
enum frame_state { FREE, CAPTURING, READY, USB_IN_FLIGHT };
struct frame_slot { enum frame_state state; uint64_t sequence, eof_ns; };
struct policy {
    struct frame_slot slot[SC130_MAX_BUFFERS];
    unsigned count, fps;
    uint64_t next_ns, dropped;
};
void policy_init(struct policy *, unsigned count, unsigned fps);
int policy_capture(struct policy *, unsigned id);
int policy_complete(struct policy *, unsigned id, uint64_t seq, uint64_t ns, bool valid);
int policy_take_latest(struct policy *, uint64_t ns);
int policy_usb_done(struct policy *, unsigned id);
void policy_reset(struct policy *);
bool sc130_accept_sequence(uint32_t candidate,bool held_valid,uint32_t held,bool sent_valid,uint32_t sent);
struct uvc_session {
    bool enable120, superspeed, committed, streaming;
    unsigned fps, stream_if;
    uint8_t request_error;
    uint8_t probe[SC130_CTRL_LEN], commit[SC130_CTRL_LEN];
    unsigned pending_selector;
};
void uvc_session_init(struct uvc_session *, bool enable120, unsigned stream_if);
void uvc_session_reset(struct uvc_session *);
void uvc_session_stop(struct uvc_session *, bool disconnect);
int uvc_handle_setup(struct uvc_session *, const struct usb_ctrlrequest *, struct uvc_request_data *);
int uvc_handle_data(struct uvc_session *, const struct uvc_request_data *);
int uvc_session_start(struct uvc_session *);
void sc130_pattern(uint8_t *, uint64_t frame);
#endif
