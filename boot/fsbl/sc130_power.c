#ifndef SC130_POWER_TEST
#include "xil_io.h"
#endif
/* Runs before psu_init: neither the system counter nor sleep is available.
 * Bound MMIO polls rather than waiting on a timer which can still be in reset.
 * This is an iteration limit, not a calibrated timeout in microseconds. */
#ifndef SC130_POWER_POLL_LIMIT
#define SC130_POWER_POLL_LIMIT 10000000U
#endif
int sc130_power_up(unsigned mask)
{
    if (Xil_In32(0xffd80100U) & mask)
        return 1;
    /* ENABLE_SET and TRIGGER are write-only; SET preserves other enables. */
    Xil_Out32(0xffd80118U, mask);
    Xil_Out32(0xffd80120U, mask);
    for (unsigned i = 0; i < SC130_POWER_POLL_LIMIT; ++i) {
        if (Xil_In32(0xffd80100U) & mask)
            return 1;
    }
    return 0;
}
