/*
 * HY3131 AD1 (high-resolution ADC) results and per-function dispatch.
 */
#include "measure_ad1.h"
#include "measure_funcs.h"
#include "dual_correction.h"
#include "hy3131_bus.h"
#include "hy3131_ctrl.h"
#include "orig_calls.h"

#define HY_REG_AD1        0x00u   /* AD1<23:0>, LSB first */
#define AD1_SIGN_LIMIT    0x800000u
#define AD1_MASK          0xFFFFFFu

/*
 * orig: 0x080062F8
 * Values above 0x800000 are negative. The original subtracts 0xFFFFFF
 * rather than 0x1000000, so 0xFFFFFF reads as 0 (not -1); kept as is.
 */
int32_t ad1_read_value(void)
{
    const uint8_t *ad1 = &hy3131_rx_buffer[HY_REG_AD1];
    uint32_t raw = (uint32_t)ad1[0] + (uint32_t)ad1[1] * 0x100u + (uint32_t)ad1[2] * 0x10000u;

    g_ad1_raw24 = raw;
    if (raw > AD1_SIGN_LIMIT) {
        raw = 0u - (AD1_MASK - raw);
    }
    return (int32_t)raw;
}

/*
 * orig: 0x08006342
 * Running mean; returns 1 when complete. 16 samples, except function 9:
 * range 5 uses 1 sample and other ranges 8.
 */
uint8_t ad1_average(int32_t value)
{
    uint8_t samples = 16;
    int32_t sum = value;

    if (g_function == 9) {
        samples = (g_range == 5) ? 1u : 8u;
    }
    if (g_ad1_count != 0) {
        sum = value + g_ad1_sum;
    }
    g_ad1_sum = sum;
    g_ad1_count++;
    if (g_ad1_count >= samples) {
        g_ad1_average = g_ad1_sum / (int32_t)g_ad1_count;
        g_ad1_count = 0;
        g_ad1_sum = 0;
        return 1;
    }
    return 0;
}

/* orig: 0x08006540 */
void ad1_process_func0(void)
{
    if (g_flag_8098 == 0) {
        ad1_process_func1();
    } else {
        ad1_process_func2();
    }
}

/* orig: 0x080061E4 */
void hy3131_on_ad1_ready(void)
{
    g_ad1_value = ad1_read_value();
    g_ad1_value_copy = g_ad1_value;
    g_ad1_average_ready = ad1_average(g_ad1_value);

    switch (g_function) {
    case 22: case 23: case 24:
        ad1_process_func22();
        break;
    case 13: case 14: case 15:
        ad1_process_func13();
        break;
    case 0:
        ad1_process_func0();
        break;
    case 1:
        ad1_process_func1();
        break;
    case 2:
        if (g_flag_80ba == 0 || g_flag_80bd == 0) {
            ad1_process_func2();
        } else {
            ad1_process_func1();
        }
        break;
    case 3:
        ad1_process_func3();
        break;
    case 4:
        ad1_process_func4();
        break;
    case 5:
        ad1_process_func5();
        break;
    case 9:
        ad1_process_func9();
        break;
    case 10:
        ad1_process_func10();
        break;
    case 11:
        ad1_process_func11();
        break;
    case 16: case 18: case 20:
        ad1_process_func16_va();
        break;
    case 17: case 19: case 21:
        ad1_process_func17_va();
        break;
    default:
        break;
    }
}
