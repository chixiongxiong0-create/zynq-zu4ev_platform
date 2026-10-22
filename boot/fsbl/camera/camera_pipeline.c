#include "camera_pipeline.h"
#include "xil_io.h"
#include "xil_printf.h"
#include "sleep.h"

#define GPIO 0xff0a0000U
#define REQUEST 0x00070000U
#define R(off) Xil_In32(GPIO + (off))
#define W(off, val) Xil_Out32(GPIO + (off), (val))

static void request(unsigned bits)
{
    /* Bank3 MASK_DATA_MSW: only EMIO16..18 are modified. */
    W(0x1c, 0xfff80000U | bits);
}

static int wait_status(unsigned mask, unsigned expected, const char *step)
{
    for (unsigned i = 0; i < 100; ++i) {
        unsigned status = R(0x6c);
        if ((status & (mask | 0x00800000U)) == (expected | 0x00800000U)) {
            xil_printf("PIPELINE_%s_OK status=%08x\r\n", step, status);
            return 1;
        }
        usleep(100);
    }
    xil_printf("PIPELINE_%s_TIMEOUT status=%08x\r\n", step, R(0x6c));
    return 0;
}

int camera_pipeline_hold(void)
{
    /* Configure direction/output first, then release the PS fabric reset.
     * Read back rather than assuming a debug-loader GPIO write took effect. */
    W(0x344, R(0x344) | 0x80000000U);
    W(0x348, R(0x348) | 0x80000000U);
    W(0x54, R(0x54) | 0x80000000U);
    if (!(R(0x54) & 0x80000000U)) return 0;
    W(0x2c4, R(0x2c4) | REQUEST);
    W(0x2c8, R(0x2c8) | REQUEST);
    request(0);
    return wait_status(REQUEST, 0, "HELD");
}

int camera_pipeline_start(void)
{
    request(1);
    if (!wait_status(0x00190000U, 0x00090000U, "LITE")) return 0;
    request(3);
    if (!wait_status(0x001b0000U, 0x000b0000U, "RX")) return 0;
    /* Sensor remains in standby while D-PHY observes its idle lanes.
     * PHY clock lock is checked by the capture guards after stream-on. */
    usleep(2000);
    request(7);
    return wait_status(0x001f0000U, 0x000f0000U, "CAPTURE");
}
