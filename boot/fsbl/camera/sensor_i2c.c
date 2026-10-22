#include "xil_io.h"
#include "xil_printf.h"
#include "sleep.h"

#define GPIO_BASE 0xff0a0000U
#define SCL 1U
#define SDA 2U
#define R(off) Xil_In32(GPIO_BASE + (off))
#define W(off, value) Xil_Out32(GPIO_BASE + (off), (value))

#include "sc130_table.h"

static void pin(unsigned bit, int high)
{
    unsigned v = R(0x2c8);
    W(0x2c8, high ? (v & ~bit) : (v | bit));
    if (!high) W(0x18, 0xfffc0000U);
    usleep(50);
}

static int level(unsigned bit) { return (R(0x6c) & bit) != 0; }

static int clock_high(void)
{
    pin(SCL, 1);
    for (int i = 0; i < 100; ++i) {
        if (level(SCL)) return 1;
        usleep(50);
    }
    return 0;
}

static void stop(void)
{
    pin(SCL, 0); pin(SDA, 0); (void)clock_high(); pin(SDA, 1);
}

static int start(void)
{
    pin(SDA, 1);
    if (!clock_high() || !level(SDA)) return 0;
    pin(SDA, 0); pin(SCL, 0);
    return 1;
}

/* Return 1 for ACK, 0 for NACK, -1 for a stuck clock. */
static int send(unsigned byte)
{
    for (int i = 7; i >= 0; --i) {
        pin(SCL, 0); pin(SDA, (byte >> i) & 1U);
        if (!clock_high()) return -1;
        pin(SCL, 0);
    }
    pin(SDA, 1);
    if (!clock_high()) return -1;
    int ack = !level(SDA);
    pin(SCL, 0);
    return ack;
}

static int receive(unsigned *out)
{
    unsigned value = 0;
    pin(SDA, 1);
    for (int i = 0; i < 8; ++i) {
        if (!clock_high()) return 0;
        value = (value << 1) | (unsigned)level(SDA);
        pin(SCL, 0);
    }
    pin(SDA, 1); /* one-byte read ends with master NACK */
    if (!clock_high()) return 0;
    pin(SCL, 0);
    *out = value;
    return 1;
}

static int readreg(unsigned reg, unsigned *value)
{
    int ok = start();
    if (ok) ok = send(0x30U << 1) == 1;
    if (ok) ok = send(reg >> 8) == 1;
    if (ok) ok = send(reg & 255U) == 1;
    if (ok) ok = start();
    if (ok) ok = send((0x30U << 1) | 1U) == 1;
    if (ok) ok = receive(value);
    stop();
    return ok;
}

static int writereg(unsigned reg, unsigned value)
{
    unsigned bytes[] = {0x30U << 1, reg >> 8, reg & 255U, value};
    int ok = start(), failed_at = -1, failed_ack = -1;
    for (int i = 0; ok && i < 4; ++i) {
        int ack = send(bytes[i]);
        if (ack != 1) { failed_at = i; failed_ack = ack; ok = 0; }
    }
    stop();
    if (!ok && reg == 0x0103 && value == 1 && failed_at == 3 && failed_ack == 0) {
        unsigned reset = 255, stream = 255;
        usleep(10000);
        ok = readreg(0x0103, &reset) && readreg(0x0100, &stream) &&
             reset == 0 && (stream == 0 || stream == 1);
    }
    if (!ok) xil_printf("I2C_WRITE_FAIL reg=%04x stage=%d ack=%d\r\n", reg, failed_at, failed_ack);
    return ok;
}

#ifndef DP_SENSOR_HTS
#define DP_SENSOR_HTS 780U
#endif
#ifndef DP_SENSOR_VTS
#define DP_SENSOR_VTS 529U
#endif

int sensor_configure_standby(void)
{
    W(0x2c8, R(0x2c8) & ~3U);
    W(0x2c4, R(0x2c4) | 3U);
    W(0x18, 0xfffc0000U);
    usleep(50);
    if (!level(SCL) || !level(SDA)) { xil_printf("I2C_BUS_NOT_IDLE\r\n"); return 0; }
    unsigned id0, id1;
    if (!readreg(0x3107, &id0) || !readreg(0x3108, &id1)) {
        xil_printf("SENSOR_READ_FAIL\r\n"); return 0;
    }
    xil_printf("SENSOR_ID_CANDIDATES %02x %02x\r\n", id0, id1);
    unsigned count = sizeof(sensor_table) / sizeof(sensor_table[0]);
    for (unsigned i = 0; i < count; ++i) {
        unsigned value = sensor_table[i].value;
        switch (sensor_table[i].address) {
        case 0x320c: value = DP_SENSOR_HTS >> 8; break;
        case 0x320d: value = DP_SENSOR_HTS & 255; break;
        case 0x320e: value = DP_SENSOR_VTS >> 8; break;
        case 0x320f: value = DP_SENSOR_VTS & 255; break;
        default: break;
        }
        if (!writereg(sensor_table[i].address, value)) {
            (void)writereg(0x0100, 0);
            return 0;
        }
        if (sensor_table[i].delay_ms) usleep(1000U * sensor_table[i].delay_ms);
    }
    if (!writereg(0x0100, 0)) return 0;
    unsigned lanes, raw, clock, hts_hi, hts_lo, vts_hi, vts_lo;
    if (!readreg(0x3018, &lanes) || !readreg(0x3031, &raw) ||
        !readreg(0x3030, &clock) || !readreg(0x320c, &hts_hi) || !readreg(0x320d, &hts_lo) ||
        !readreg(0x320e, &vts_hi) || !readreg(0x320f, &vts_lo)) return 0;
    if (lanes != 0x30 || raw != 0x0a || clock != 0x02 || ((hts_hi << 8) | hts_lo) != DP_SENSOR_HTS ||
        ((vts_hi << 8) | vts_lo) != DP_SENSOR_VTS) {
        xil_printf("SENSOR_MODE_MISMATCH lanes=%02x raw=%02x hts=%d vts=%d\r\n",
                   lanes, raw, (hts_hi << 8) | hts_lo, (vts_hi << 8) | vts_lo);
        return 0;
    }
    xil_printf("SENSOR_MODE_OK clock3030=%02x HTS=%d VTS=%d\r\n",
               clock, (hts_hi << 8) | hts_lo, (vts_hi << 8) | vts_lo);
#ifdef DP_SENSOR_PATTERN
    /* SC130GS application guide V1.9, p28 table 4-12. Diagnostic only. */
    if (!writereg(0x4501, 0xc8) || !writereg(0x3400, 0x52) ||
        !writereg(0x5001, 0x04)) return 0;
    xil_printf("SENSOR_INCREMENTAL_PATTERN\r\n");
#endif
    xil_printf("SENSOR_STANDBY_READY writes=%d\r\n", count);
    return 1;
}

int sensor_stream_on(void) { return writereg(0x0100, 1); }

int sensor_verify_standby(void)
{
    const unsigned regs[] = {0x0100, 0x3018, 0x3030, 0x3031, 0x3208, 0x3209,
                             0x320a, 0x320b, 0x320c, 0x320d, 0x320e, 0x320f};
    const unsigned expected[] = {0, 0x30, 2, 0x0a, 5, 0, 4, 0,
        DP_SENSOR_HTS >> 8, DP_SENSOR_HTS & 255, DP_SENSOR_VTS >> 8, DP_SENSOR_VTS & 255};
    for (unsigned i = 0; i < sizeof(regs)/sizeof(regs[0]); ++i) {
        unsigned value;
        if (!readreg(regs[i], &value) || value != expected[i]) {
            xil_printf("SENSOR_STANDBY_VERIFY_FAIL reg=%04x\r\n", regs[i]); return 0;
        }
    }
    xil_printf("SENSOR_STANDBY_VERIFY_OK\r\n");
    return 1;
}
