/*
 * HY3131 RMS converter results.
 */
#include "measure_rms.h"
#include "hy3131_bus.h"
#include "hy3131_ctrl.h"

#define HY_REG_RMS        0x09u   /* RMS<39:0>, 5 bytes, LSB first */
#define RMS_AVERAGE_COUNT 4u

/*
 * Integer square root (floor). The original computes (int)sqrt((double)n);
 * for any 32-bit n this is the same value: the double square root is
 * correctly rounded, and an irrational root lies at least ~2^-17 from the
 * next integer, far more than one double ulp, so truncation never differs.
 */
uint32_t isqrt_u32(uint32_t n)
{
    uint32_t root = 0;
    uint32_t bit = 1u << 30;

    while (bit > n) {
        bit >>= 2;
    }
    while (bit != 0) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

/* orig: 0x0800827C */
int32_t rms_read_value(void)
{
    const uint8_t *rms = &hy3131_rx_buffer[HY_REG_RMS];

    g_rms_raw = (uint32_t)rms[1] + (uint32_t)rms[3] * 0x10000u + (uint32_t)rms[4] * 0x1000000u
              + (uint32_t)rms[2] * 0x100u;
    g_rms_raw_low = rms[0];
    return (int32_t)(isqrt_u32(g_rms_raw) << 3);
}

/* orig: 0x080082D4 - returns 1 when a 4-sample mean is available */
uint8_t rms_average(uint32_t value)
{
    uint32_t sum = value;

    if (g_rms_count != 0) {
        sum = value + g_rms_sum;
    }
    g_rms_sum = sum;
    g_rms_count++;
    if (g_rms_count >= RMS_AVERAGE_COUNT) {
        g_rms_average = g_rms_sum / g_rms_count;
        g_rms_count = 0;
        g_rms_sum = 0;
        return 1;
    }
    return 0;
}

/* Per-function hooks; all are empty in the original image. */
/* orig: 0x0800A5BA */
void rms_post_func2(void)
{
}

/* orig: 0x0800A5B2 */
void rms_post_func0(void)
{
    rms_post_func2();
}

/* orig: 0x0800A7F8 */
void rms_post_func4(void)
{
}

/* orig: 0x0800A7FA */
void rms_post_func16(void)
{
}

/* orig: 0x08008208 */
void hy3131_on_rms_ready(void)
{
    g_rms_value = (uint32_t)rms_read_value();
    g_rms_value_copy = g_rms_value;
    g_rms_average_ready = rms_average(g_rms_value) != 0;

    switch (g_function) {
    case 0:  rms_post_func0(); break;
    case 2:  rms_post_func2(); break;
    case 4:  rms_post_func4(); break;
    case 16: case 18: case 20: rms_post_func16(); break;
    default: break;
    }
}
