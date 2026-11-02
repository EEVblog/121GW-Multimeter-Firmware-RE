/*
 * HY3131 peak-hold results (functions 2 and 4 with the RMS converter,
 * g_flag_80b7 set).
 */
#include "measure_peak.h"
#include "board_io.h"
#include "calibration.h"
#include "hy3131_bus.h"
#include "hy3131_ctrl.h"
#include "measure_ad1.h"
#include "measure_derived.h"
#include "measure_funcs.h"
#include "measure_scale.h"

#define HY_REG_PEAK_CTRL   0x29u
#define HY_R29_PEAK_ENABLE 0x04u      /* cleared briefly to restart peak detection */
#define PEAK_MASK          0x3FFFFu   /* 18-bit peak values */
#define PEAK_LIMIT         0x40000u

/* 24-bit little-endian value from the register burst. */
static uint32_t rx24(unsigned index)
{
    return hy3131_rx_buffer[index] + hy3131_rx_buffer[index + 1] * 0x100u
           + hy3131_rx_buffer[index + 2] * 0x10000u;
}

/*
 * orig: 0x0800A7FC
 * Reads the maximum (R0x11..13) and minimum (R0x0E..10) peak registers and
 * restarts peak detection. For functions 2/4 in peak mode, either the
 * maximum (g_peak_select clear: zero offset, gain, held maximum unless in
 * calibration; or averaging/calibration) or the inverted minimum
 * (g_peak_select set, held minimum) becomes the reading. Holding is skipped
 * while g_setting_238 is non-zero.
 */
void hy3131_on_peak_hold(void)
{
    uint8_t r29;

    if (g_function == 13 || g_function == 14 || g_function == 15) {
        return;
    }
    g_peak_raw_max = rx24(0x11);
    g_peak_raw_min = rx24(0x0E);
    r29 = g_hy_r29 & (uint8_t)~HY_R29_PEAK_ENABLE;
    hy3131_write(&r29, HY_REG_PEAK_CTRL, 1);
    delay_loop(10);
    r29 = g_hy_r29;
    hy3131_write(&r29, HY_REG_PEAK_CTRL, 1);

    if (!((g_function == 2 || g_function == 4) && g_flag_80b7 == 1)) {
        return;
    }
    if (g_peak_select != 0) {
        g_derived_value = (int32_t)(~g_peak_raw_min & PEAK_MASK);
        if (g_setting_238 == 0) {
            if (g_peak_min < (uint32_t)g_derived_value) {
                g_peak_min = (uint32_t)g_derived_value;
                g_peak_min_raw = g_peak_raw_min;
            } else {
                g_derived_value = (int32_t)g_peak_min;
            }
        }
        g_peak_is_min = 1;
        return;
    }
    if (g_peak_raw_max >= PEAK_LIMIT) {
        return;
    }
    g_derived_value = (int32_t)g_peak_raw_max;
    if (g_avg_flag_808c != 0) {
        g_avg16_result = average16(g_derived_value);
    } else if (g_cal_flag_808e != 0) {
        calibrate_zero_and_gain(g_derived_value, g_avg16_result);
    } else {
        if (g_setting_238 == 0) {
            g_derived_value = subtract_zero_offset(g_derived_value);
            if (g_derived_value < 0) {
                g_derived_value = 0;
            }
            g_derived_value = apply_gain(g_derived_value);
            if (g_flag_808a == 0) {
                if (g_peak_max < (uint32_t)g_derived_value) {
                    g_peak_max = (uint32_t)g_derived_value;
                } else {
                    g_derived_value = (int32_t)g_peak_max;
                }
            }
        }
        g_peak_is_min = 0;
    }
    g_new_reading = 1;
    g_scaled_ready = 1;
    g_scaled_value = g_derived_value;
}
