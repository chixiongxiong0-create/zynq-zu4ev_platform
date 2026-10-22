#ifndef DP_DISPLAY_H
#define DP_DISPLAY_H
struct dp_display;
/* Linux DRM master, no other display owner. Returns NULL and sets errno. */
struct dp_display *dp_display_open(void);
int dp_display_fd(struct dp_display *d);
int dp_display_import(struct dp_display *d, unsigned capture_index,
                      int dmabuf_fd, unsigned stride);
int dp_display_ready(struct dp_display *d);
int dp_display_present(struct dp_display *d, unsigned capture_index);
/* A completed replacement releases its PREVIOUS active index, never the new one. */
int dp_display_dispatch(struct dp_display *d, int *retired_index);
/* On failure d remains live and buffers MUST remain quarantined; retry close. */
int dp_display_close(struct dp_display *d);
#endif
