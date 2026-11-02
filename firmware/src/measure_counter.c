/*
 * HY3131 counter results: frequency, period, duty cycle and the
 * counter-based functions (6, 7, 8, 12).
 */
#include "measure_counter.h"
#include "calibration.h"
#include "hy3131_bus.h"
#include "hy3131_ctrl.h"
#include "measure_derived.h"
#include "measure_peak.h"
#include "measure_scale.h"
#include "numeric.h"
#include "display_state.h"
#include "annunciators.h"
#include "filters.h"
#include "measure_funcs.h"
#include "range_select.h"
#include "trim_correction.h"
#include "ac_response.h"
#include "dual_correction.h"

#define COUNTER_FULL      0x1000000u   /* 24-bit counter */
#define FUNC6_CAL_SCALE   150000000ull

/* 24-bit little-endian value from the register burst. */
static uint32_t rx24(unsigned index)
{
    return hy3131_rx_buffer[index] + hy3131_rx_buffer[index + 1] * 0x100u
           + hy3131_rx_buffer[index + 2] * 0x10000u;
}

/*
 * orig: 0x0800AB48
 * Captures the counters (CTC at R0x1B, CTB at R0x18, CTA at R0x15) and
 * restarts them. The reference count is CTC plus the preset's distance
 * to the 24-bit wrap.
 */
void counter_capture(void)
{
    g_ctr_c = rx24(0x1B);
    g_ctr_b = rx24(0x18);
    g_ctr_a = rx24(0x15);
    g_ctr_preset_span = g_counter_preset;
    g_ctr_preset_span = g_ctr_preset_span << 8;
    g_ctr_preset_span = COUNTER_FULL - g_ctr_preset_span;
    g_ctr_ref = g_ctr_preset_span + g_ctr_c;
    hy3131_restart_counter();
}

/* orig: 0x0800B6D4 - counts per millisecond of the reference (hardware divide: /0 = 0) */
void counter_rate_per_ms(void)
{
    g_ctr_rate = m3_udiv(g_ctr_a, m3_udiv(g_ctr_ref, 1000));
}

/* orig: 0x0800B2D8 - function 8 */
void counter_process_func8(void)
{
    g_peak_is_min = 0;
    counter_rate_per_ms();
    g_derived_value = (int32_t)g_ctr_rate;
    g_new_reading = 1;
}

/* orig: 0x0800B6F0 - function 6 calibration: gain from the reference and CTB counts */
void counter_calibrate_func6(void)
{
    g_cal_flag_808e = 0;
    g_cal_func6_gain = (int32_t)iar_uldiv((uint64_t)g_ctr_ref * FUNC6_CAL_SCALE,
                                          (uint32_t)(g_ctr_b * 100u));
    cal_store_value(CAL_INDEX_GAIN, g_cal_func6_gain);
}

/* orig: 0x0800B610 - function 6 value: gain * CTB * 10 / (reference / 10) */
void counter_value_func6(void)
{
    uint64_t product;

    if (g_cal_flag_808e != 0 && g_function == 6) {
        counter_calibrate_func6();
    }
    product = (uint64_t)(int64_t)g_cal_func6_gain * g_ctr_b;
    g_ctr_value6 = iar_uldiv(product * 10u, g_ctr_ref / 10u);
}

/* orig: 0x0800B67C - function 7 value: reference * 100000 / (gain / 1000 * CTB) */
void counter_value_func7(void)
{
    uint64_t scale = iar_uldiv((uint64_t)(int64_t)g_cal_func6_gain, 1000u);

    g_ctr_value7 = iar_uldiv((uint64_t)g_ctr_ref * 100000u, scale * g_ctr_b);
}

/*
 * orig: 0x0800B244
 * Function 7 range from a selector: 0, 1, 2 divide the value by 1, 10,
 * 100; 9 selects range 2 and flags an overload.
 */
void counter_set_range_func7(uint8_t selector)
{
    switch (selector) {
    case 0:
        g_range = 0;
        g_derived_value = (int32_t)iar_uldiv(g_ctr_value7, 1u);
        break;
    case 1:
        g_range = 1;
        g_derived_value = (int32_t)iar_uldiv(g_ctr_value7, 10u);
        break;
    case 2:
        g_range = 2;
        g_derived_value = (int32_t)iar_uldiv(g_ctr_value7, 100u);
        break;
    case 9:
        g_range = 2;
        g_flag_8046 = 1;
        break;
    default:
        break;
    }
}

/*
 * orig: 0x0800F034
 * As average16() for unsigned samples: sums into the shared 64-bit sum and
 * returns the mean when the count reaches 0, else 0.
 */
int32_t average16_unsigned(uint32_t sample)
{
    g_cal_sum64 += (int64_t)(uint64_t)sample;
    g_avg_count--;
    if (g_avg_count != 0) {
        return 0;
    }
    g_avg_flag_808c = 0;
    g_avg_done_808d = 1;
    g_new_reading = 1;
    g_avg16_unsigned = (int32_t)iar_ldiv(g_cal_sum64, 16);
    return g_avg16_unsigned;
}

/*
 * orig: 0x0800F6C0
 * Zero/gain calibration for the counter functions (unsigned samples):
 * zero = reference - (mean - reference) / 9 (unsigned divide), then
 * function 12: gain = (g_cal_ref_e68 - zero) / 10000, otherwise from the
 * span via gain_select_for_range().
 */
void calibrate_counter(uint32_t sample, int32_t reference)
{
    int32_t mean;

    g_cal_sum64 += (int64_t)(uint64_t)sample;
    g_avg_count--;
    if (g_avg_count != 0) {
        return;
    }
    g_cal_flag_808e = 0;
    g_cal_done_808f = 1;
    g_new_reading = 1;
    mean = (int32_t)iar_ldiv(g_cal_sum64, 16);
    g_zero_offset = reference - (int32_t)((uint32_t)(mean - reference) / 9u);
    cal_store_value(CAL_INDEX_OFFSET, g_zero_offset);
    if (g_function == 12) {
        g_gain = (float)(uint32_t)(g_cal_ref_e68 - g_zero_offset) / 10000.0f;
    } else {
        g_span = (float)(uint32_t)(mean - g_zero_offset);
        gain_select_for_range(0);
    }
    cal_store_value(CAL_INDEX_GAIN, iar_f2i(g_gain));
}

#define FREQ_DECADE_OVER 9u

/*
 * orig: 0x0800AEE8
 * Scales the function 6 value by 10^decade (rounded: half the divisor is
 * added to the stored value first) and stores it either as the reading and
 * range (dest 0) or as the frequency and its decimal for all three
 * frequency users. Decade 9 flags an overload (0x20008047 for dest 1, else
 * 0x20008046).
 * Deviation: for decade 9 the original stores an uninitialised register as
 * the value (the display shows the overload); here the value is left
 * unchanged and only the decimal (4) is stored.
 */
void freq_scale_store(uint8_t dest, uint8_t decade)
{
    static const uint32_t divisor[5] = { 1u, 10u, 100u, 1000u, 10000u };
    uint32_t value = 0;
    uint8_t decimal = 0;
    int have_value = 1;

    if (decade <= 4) {
        decimal = decade;
        g_ctr_value6 += divisor[decade] / 2u;
        value = (uint32_t)iar_uldiv(g_ctr_value6, divisor[decade]);
    } else {
        have_value = 0;
        if (decade == FREQ_DECADE_OVER) {
            decimal = 4;
            if (dest == 1) {
                g_flag_8047 = 1;
            } else {
                g_flag_8046 = 1;
            }
        }
    }
    if (dest == 0) {
        if (have_value) {
            g_derived_value = (int32_t)value;
        }
        g_range = decimal;
    } else {
        if (have_value) {
            g_freq_raw = (int32_t)value;
            g_freq_raw_acdc = (int32_t)value;
            g_freq_raw_f0 = (int32_t)value;
        }
        g_freq_decimal = decimal;
        g_freq_decimal_acdc = decimal;
        g_freq_decimal_f0 = decimal;
    }
}

/* Auto-range decade for a value, with 80 % hysteresis towards the current one. */
static uint8_t freq_auto_decade(uint64_t v, uint8_t current)
{
    if (v >= 100000000u) {
        return 4;
    }
    if (current == 4 && v >= 80000000u) {
        return 4;
    }
    if (v >= 10000000u) {
        return 3;
    }
    if (current == 3 && v >= 8000000u) {
        return 3;
    }
    if (v >= 1000000u) {
        return 2;
    }
    if (current == 2 && v >= 800000u) {
        return 2;
    }
    if (v >= 100000u) {
        return 1;
    }
    if (current == 1 && v >= 80000u) {
        return 1;
    }
    return 0;
}

/*
 * orig: 0x0800AC1C
 * Frequency from the counters (function 6 value). dest 0 is the main
 * reading (function 6, range given), dest 1 the secondary frequency
 * (decimal given). With g_flag_8042 set (manual range, outside
 * calibration) the main reading uses the selected range and flags an
 * overload above it; otherwise the decade is chosen automatically.
 */
void freq_process(uint8_t dest, uint8_t range)
{
    static const uint32_t manual_limit[5] = { 100000u, 1000000u, 10000000u, 100000000u, 1000000000u };

    counter_value_func6();
    g_peak_is_min = 0;
    g_flag_8046 = 0;
    if (dest == 0 && (uint8_t)(g_flag_808a | (g_flag_8042 ^ 1u)) == 0) {
        if (range <= 4) {
            if (g_ctr_value6 < manual_limit[range]) {
                freq_scale_store(0, range);
            } else {
                g_flag_8046 = 1;
            }
        }
    } else if (g_ctr_value6 < 1000000000u) {
        freq_scale_store(dest, freq_auto_decade(g_ctr_value6, range));
    } else {
        freq_scale_store(dest, FREQ_DECADE_OVER);
    }
    if (dest == 0) {
        g_new_reading = 1;
        g_sub_dp = sub_dp_for_range(g_function, g_range);
    }
}

/* Function 7 auto-range selector with 80 % hysteresis (9 = overload). */
static uint8_t func7_auto_selector(uint64_t v)
{
    if (v >= 10000000u) {
        return 9;
    }
    if (v >= 1000000u) {
        return 2;
    }
    if (g_range == 2 && v >= 800000u) {
        return 2;
    }
    if (v >= 100000u) {
        return 1;
    }
    if (g_range == 1 && v >= 80000u) {
        return 1;
    }
    return 0;
}

/*
 * orig: 0x0800B070 - function 7
 * Value = function 7 value * counts per ms / 1000, then manual range
 * (g_flag_8042 outside calibration; overload above the range) or
 * auto-range over ranges 0..2.
 */
void counter_process_func7(void)
{
    g_peak_is_min = 0;
    counter_value_func7();
    counter_rate_per_ms();
    g_ctr_value7 = iar_uldiv(g_ctr_value7 * g_ctr_rate, 1000u);
    if ((uint8_t)(g_flag_808a | (g_flag_8042 ^ 1u)) == 0) {
        static const uint32_t limit[3] = { 100000u, 1000000u, 10000000u };
        if (g_range <= 2) {
            if (g_ctr_value7 < limit[g_range]) {
                counter_set_range_func7(g_range);
            } else {
                g_flag_8046 = 1;
            }
        }
    } else {
        counter_set_range_func7(func7_auto_selector(g_ctr_value7));
    }
    g_new_reading = 1;
    g_sub_dp = sub_dp_for_range(g_function, g_range);
}

/*
 * orig: 0x0800B33C - function 12
 * Ratio CTA / CTB (x100 for ranges 2..4 and above 5) into g_cal_ref_e68.
 * Ranges 0/1 subtract the zero offset and divide by the gain (or average /
 * calibrate); other ranges divide by the gain or run the gain calibration.
 * The result is averaged with a window of 1/50 (range 5: 1/10) of the
 * value, at least 20; small readings in ranges 2..4 read 0. With the
 * manual-range flag in range 4, g_f12_skip readings are held back.
 */
void counter_process_func12(void)
{
    uint16_t window;

    g_peak_is_min = 0;
    if (g_range == 5 || g_range < 2) {
        g_cal_ref_e68 = (int32_t)m3_udiv(g_ctr_a, g_ctr_b);
    } else {
        g_cal_ref_e68 = (int32_t)m3_udiv(g_ctr_a * 100u, g_ctr_b);
    }
    if (g_range < 2) {
        if (g_avg_flag_808c != 0) {
            g_f12_avg = average16_unsigned((uint32_t)g_cal_ref_e68);
        } else if (g_cal_flag_808e != 0) {
            calibrate_counter((uint32_t)g_cal_ref_e68, g_f12_avg);
        } else {
            if ((uint32_t)g_zero_offset < (uint32_t)g_cal_ref_e68) {
                g_cal_ref_e68 = g_cal_ref_e68 - g_zero_offset;
            } else {
                g_cal_ref_e68 = 0;
            }
            if (g_gain != 0.0f && g_cal_ref_e68 != 0) {
                g_cal_ref_e68 = (int32_t)iar_f2u(iar_fdiv((float)(uint32_t)g_cal_ref_e68, g_gain));
            }
        }
    } else if (g_cal_flag_808e == 0) {
        g_cal_ref_e68 = (int32_t)iar_f2u(iar_fdiv((float)(uint32_t)g_cal_ref_e68, g_gain));
    } else {
        cal_gain_stage(g_cal_ref_e68);
    }

    window = (uint16_t)((uint32_t)g_cal_ref_e68 / (g_range == 5 ? 10u : 50u));
    if (window < 20) {
        window = 20;
    }
    g_derived_value = moving_average_b(g_cal_ref_e68, window, 10);
    if (g_range >= 2 && g_range < 5 && g_derived_value < 21) {
        g_derived_value = 0;
    }
    g_func12_count_copy = g_func12_count;
    if ((g_flag_8042 & (g_flag_808a ^ 1u)) != 0 && g_range == 4 && g_f12_skip != 0) {
        g_f12_skip--;
    } else {
        g_new_reading = 1;
    }
    func12_set_interval();
}

/*
 * orig: 0x0800AA00
 * Counter interrupt: captures and restarts the counters, then processes
 * them per function (frequency for 0/2/4/16/18/20 and 6, functions 7, 8,
 * 12) and updates the secondary-display units.
 */
void hy3131_on_counter_ready(void)
{
    if (g_function == 13 || g_function == 14 || g_function == 15) {
        return;
    }
    counter_capture();
    g_counter_fc2 = 0;
    switch (g_function) {
    case 0: case 2: case 4: case 16: case 18: case 20:
        freq_process(1, g_freq_decimal);
        break;
    case 6:
        freq_process(0, g_range);
        break;
    case 7:
        counter_process_func7();
        break;
    case 8:
        counter_process_func8();
        break;
    case 12:
        counter_process_func12();
        break;
    default:
        break;
    }
    g_holdoff = 0;
    if (g_function == 6 || g_function == 7 || g_function == 8) {
        g_flag_8060 = 1;
    }
    if ((g_function == 16 || g_function == 18 || g_function == 20
         || (g_function == 2 && g_flag_80ba == 0) || g_function == 4) && g_function != 0) {
        annunciators_secondary();
    }
    if ((g_function == 16 || g_function == 18 || g_function == 20 || g_function == 2 || g_function == 4)
        && g_flag_80a5 == 0) {
        g_disp_flag_8062 = 1;
    }
}

/*
 * orig: 0x0800B540 - function 12, polled while measuring
 * On a measurement timeout (g_flag_80a3): restarts the interval; with
 * auto-range moves up to range 2 or 5 (back to range 0 once if
 * g_flag_80a2 is set), otherwise flags an overload. Manual range 4 holds
 * back the next 2 readings.
 */
void func12_timeout_poll(void)
{
    if (g_flag_80a3 == 0) {
        return;
    }
    g_flag_80a3 = 0;
    func12_set_interval();
    if ((g_flag_8042 & (g_flag_808a ^ 1u)) != 0 && g_range == 4) {
        g_f12_skip = 2;
    }
    if ((uint8_t)(g_flag_8042 | g_flag_808a) != 0) {
        g_flag_8046 = 1;
        g_new_reading = 1;
        return;
    }
    if (g_range < 2) {
        g_range = 2;
    } else if (g_range < 5) {
        g_range = 5;
    }
    g_flag_807c = 0;
    g_flag_804a = 0;
    g_flag_804b = 1;
    if (g_range == 2 && g_flag_80a2 != 0) {
        g_flag_80a2 = 0;
        g_flag_8046 = 0;
        g_range = 0;
    }
}
