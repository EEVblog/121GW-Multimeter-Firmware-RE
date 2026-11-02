/*
 * Shared measurement scaling.
 */
#include "measure_scale.h"
#include "hy3131_ctrl.h"
#include "system_init.h"
#include "numeric.h"
#include "calibration.h"
#include "orig_calls.h"

#define AVERAGE_SAMPLES 16u

/* orig: 0x0800F368 - negative and zero results become 0 */
int32_t subtract_zero_offset(int32_t value)
{
    value -= g_zero_offset;
    if (value < 1) {
        value = 0;
    }
    return value;
}

/* orig: 0x0800F648 */
float select_gain(void)
{
    switch (g_function) {
    case 13: case 14: case 15: case 22: case 23: case 24:
        return g_flag_80c4 == 0 ? g_gain_e2c : g_gain_e30;
    default:
        return g_gain;
    }
}

/* orig: 0x0800F3B8 - raw / gain, rounded by adding 0.5 and truncating */
int32_t apply_gain(int32_t raw)
{
    float gain = select_gain();
    float q;

    if (gain == 0.0f) {
        q = 0.0f;
    } else if ((g_function == 2 || g_function == 4) && g_flag_80b9 == 1) {
        q = (float)raw / (gain / 10.0f);
    } else {
        q = (float)raw / gain;
    }
    return iar_d2i((double)q + 0.5);
}

/* orig: 0x0800F0AC - returns the 16-sample mean once complete, else 0 */
int32_t average16(int32_t value)
{
    int32_t mean = 0;
    int32_t sum = value;

    if (g_avg_count != AVERAGE_SAMPLES) {
        sum = value + g_avg_sum;
    }
    g_avg_sum = sum;
    g_avg_count--;
    if (g_avg_count == 0) {
        g_avg_flag_808c = 0;
        g_avg_done_808d = 1;
        mean = g_avg_sum / (int32_t)AVERAGE_SAMPLES;
        g_average16 = mean;
    }
    g_new_reading = 1;
    return mean;
}

/* orig: 0x0800FB1E */
void gain_set_divisor(uint32_t unused, float divisor)
{
    (void)unused;
    g_gain = g_span / divisor;
}

/* orig: 0x0800FB38 */
void gain_set_50000(uint32_t unused)
{
    (void)unused;
    g_gain = g_span / 50000.0f;
}

/* orig: 0x0800FB58 */
void gain_set_30000(uint32_t unused)
{
    (void)unused;
    g_gain = g_span / 30000.0f;
}

/* orig: 0x0800FB78 */
void gain_set_10000(uint32_t unused)
{
    (void)unused;
    g_gain = g_span / 10000.0f;
}

/* orig: 0x0800FB98 */
void gain_set_6000(uint32_t unused)
{
    (void)unused;
    g_gain = g_span / 6000.0f;
}

/* orig: 0x0800FBB8 - span / (30000 * sqrt 2), in double precision */
void gain_set_ac_peak(void)
{
    g_gain = (float)((double)g_span / 42426.4068);
}

static void gain_by_range_3(void)
{
    if (g_range == 3) {
        gain_set_6000(0);
    } else {
        gain_set_50000(0);
    }
}

/*
 * orig: 0x0800F9F0
 * Gain divisor for the current function/range. 'alternate' only matters for
 * functions 15 and 24. Functions 0, 6, 7, 8 and above 24 leave the gain alone.
 */
void gain_select_for_range(uint8_t alternate)
{
    switch (g_function) {
    case 2: case 4:
        if (g_flag_80b7 != 0) {
            gain_set_ac_peak();
            return;
        }
        /* fall through */
    case 1:
        if (g_range == 2) {
            gain_set_30000(0);
            return;
        }
        gain_by_range_3();
        return;
    case 3:
        gain_by_range_3();
        return;
    case 5: case 9: case 10: case 13: case 14: case 16: case 17: case 18: case 19:
    case 22: case 23:
        gain_set_50000(0);
        return;
    case 11:
        gain_set_30000(0);
        return;
    case 12:
        gain_set_divisor(0, g_range == 5 ? 1000.0f : 10000.0f);
        return;
    case 15: case 24:
        if (alternate == 0 || g_range < 2) {
            gain_set_50000(0);
        } else {
            gain_set_10000(0);
        }
        return;
    case 20: case 21:
        if (g_range == 2) {
            gain_set_10000(0);
        } else {
            gain_set_50000(0);
        }
        return;
    default:
        return;
    }
}

/* orig: 0x0800F2FC - dual offsets for functions 13..15 and 22..24 */
int32_t remove_offset(int32_t value)
{
    switch (g_function) {
    case 13: case 14: case 15: case 22: case 23: case 24:
        return value - (g_flag_80c4 == 0 ? g_cal_dual_offset_a : g_cal_dual_offset_b);
    default:
        return value - g_zero_offset;
    }
}

/* orig: 0x0800EEF0 - function 11 is passed through unchanged */
int32_t offset_stage(int32_t value)
{
    if (g_function != 11) {
        value = (g_avg_flag_808c == 0) ? remove_offset(value) : cal_zero_stage(value);
    }
    return value;
}

/* orig: 0x0800F37E */
int32_t gain_stage(int32_t value)
{
    if (g_flag_8090 == 0 && g_cal_flag_808e == 0) {
        return apply_gain(value);
    }
    return cal_gain_stage(value);
}

/* orig: 0x08012F28 - 0: /1000, 1: /100, 2: /10, 4: *10, other: unchanged */
int32_t scale_by_decimal(uint8_t mode, int32_t value)
{
    switch (mode) {
    case 0: return value / 1000;
    case 1: return value / 100;
    case 2: return value / 10;
    case 4: return (int32_t)((uint32_t)value * 10u);   /* wraps, as the original */
    default: return value;
    }
}

/* orig: 0x080148F6 - 1: only 80C6 set, 2: only 80C7 set, 3: both, 0: neither */
uint8_t sel_code(void)
{
    if (g_sel_80c6 != 0 && g_sel_80c7 == 0) {
        return 1;
    }
    if (g_sel_80c6 == 0 && g_sel_80c7 != 0) {
        return 2;
    }
    if (g_sel_80c6 != 0 && g_sel_80c7 != 0) {
        return 3;
    }
    return 0;
}
