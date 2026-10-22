#ifndef CAMERA_PIPELINE_H
#define CAMERA_PIPELINE_H
/* Startup only. Runtime reuse requires DMA/HPM traffic to be quiescent first. */
int camera_pipeline_hold(void);
int camera_pipeline_start(void);
#endif
