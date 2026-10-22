#include "xfsbl_hw.h"
#include "xfsbl_hooks.h"
#include "xil_printf.h"
#include "camera_pipeline.h"
extern int sc130_power_up(unsigned mask);
extern int sensor_configure_standby(void);
extern int sensor_verify_standby(void);
extern int sensor_stream_on(void);
static unsigned Sc130PlLoads, Sc130Ready;
u32 Sc130AfterBitstream(void)
{
 ++Sc130PlLoads;
 xil_printf("SINGLE_PL count=%d\r\n",Sc130PlLoads);
 if(Sc130PlLoads!=1U || !sc130_power_up(0x00800000U)) return XFSBL_FAILURE;
 if(!camera_pipeline_hold() || !sensor_configure_standby() ||
    !sensor_verify_standby() || !camera_pipeline_start() ||
    !sensor_stream_on()) return XFSBL_FAILURE;
 Sc130Ready=1;
 xil_printf("SENSOR_READY SINGLE_PL count=1\r\n");
 return XFSBL_SUCCESS;
}
u32 Sc130BeforeHandoff(void)
{
 return (Sc130PlLoads==1U && Sc130Ready) ? XFSBL_SUCCESS : XFSBL_FAILURE;
}
