/*
 * Function/range change: HY3131 reconfiguration, calibration reload,
 * main display decimal point and the readings skipped after a change.
 *
 * Build options (both off by default, which keeps the image bit-equivalent):
 *   APP_FIX_BUGS        bar limit refreshed by range_change(), overload state
 *                       reset in function_change() (no fake reading any more)
 *   APP_AUTORANGE_FAST  autorange() jumps several ranges at once
 */
#include "annunciators.h"
#include "measure_peak.h"
#include "modes.h"
#include "display_bargraph.h"
#include "display_text.h"
#include "display_update.h"
#include "trim_correction.h"
#include "dual_correction.h"
#include "measure_ad1.h"
#include "temperature.h"
#include "setup_menu.h"
#include "sd_log.h"
#include "irq.h"
#include "system_init.h"
#include "range_select.h"
#include "calibration.h"
#include "display_state.h"
#include "front_end.h"
#include "hy3131_ctrl.h"
#include "lcd_driver.h"
#include "measure_derived.h"
#include "buzzer.h"
#include "measure_funcs.h"
#include "measure_scale.h"
#include "measure_counter.h"
#include "display_symbols.h"
#if APP_FIX_BUGS
#include "ble_packet.h"
#endif

/*
 * orig: 0x0800D81E
 * Reprograms the HY3131 for the current function/range. With load_cal set
 * it also reloads the calibration (plus the function 6 record, and the
 * trims for functions 0, 2, 4, 16, 18 and 20).
 */
void range_configure(uint8_t load_cal)
{
    hy3131_configure_mode(g_function, g_range);
    if (load_cal == 1) {
        cal_load(g_function, g_range);
        cal_load_extra(g_function, g_range);
    }
    if (load_cal == 1 && g_function != 6) {
        cal_load(6, 0);
    }
    if (load_cal == 1 && (g_function == 16 || g_function == 18 || g_function == 20
                          || g_function == 2 || g_function == 4)) {
        cal_load_trims();
    }
    if (load_cal == 1 && g_function == 0) {
        cal_load_trims();
    }
}

/* orig: 0x0800D80C */
void range_select(uint8_t load_cal)
{
    range_configure(load_cal);
    func12_set_interval();
}

/* Main display decimal point position per range (index = range). */
static const uint8_t dp_a[] = { 3, 2, 4, 3, 2, 4, 3 };   /* functions 3, 4, 6, 9, 16, 17 */
static const uint8_t dp_b[] = { 4, 3, 2, 1, 2 };         /* functions 1, 2, 7, 18, 19 */
static const uint8_t dp_c[] = { 3, 2, 4, 3, 2 };         /* function 12 */
static const uint8_t dp_d[] = { 3, 2, 2, 1 };            /* functions 14, 23 */
static const uint8_t dp_e[] = { 2, 1, 1 };               /* functions 13, 22 */
static const uint8_t dp_f[] = { 1, 0, 3, 2 };            /* functions 15, 24 */
static const uint8_t dp_g[] = { 2, 4, 3 };               /* functions 20, 21 */

#define DP_FROM(table, range) \
    ((range) < sizeof(table) ? (table)[(range)] : 0u)

/*
 * orig: 0x0800D920
 * Main display decimal point for a function/range (0 = none).
 * Ranges past the end of a function's list give 0.
 */
uint8_t main_dp_for_range(uint8_t function, uint8_t range)
{
    switch (function) {
    case 0: case 5: case 8: case 25:
        return 1;
    case 10:
        return 2;
    case 11:
        return (g_flag_80b3 != 0) ? 3 : 4;
    case 3: case 4: case 6: case 9: case 16: case 17:
        return DP_FROM(dp_a, range);
    case 1: case 2: case 7: case 18: case 19:
        return DP_FROM(dp_b, range);
    case 12:
        return DP_FROM(dp_c, range);
    case 14: case 23:
        return DP_FROM(dp_d, range);
    case 13: case 22:
        return DP_FROM(dp_e, range);
    case 15: case 24:
        return DP_FROM(dp_f, range);
    case 20: case 21:
        return DP_FROM(dp_g, range);
    default:
        return 0;
    }
}

/* orig: 0x080171FA */
void main_dp_update(void)
{
    g_main_dp = main_dp_for_range(g_function, g_range);
    lcd_set_main_dp(g_main_dp);
    lcd_update();
}

/*
 * orig: 0x0800DB48 - g_setting_238 for the current function/range: how many
 * readings after a function/range change autorange() skips (no auto-range,
 * no voltage warnings, main readout blanked except in function 0) while the
 * HY3131 settles
 */
void mode_238_select(void)
{
    if ((g_function == 16 || g_function == 18 || g_function == 20
         || g_function == 17 || g_function == 19 || g_function == 21) && g_flag_80a5 != 0) {
        g_setting_238 = (g_flag_80c4 == 0) ? 3 : 0;
    } else if (g_function == 2) {
        g_setting_238 = 3;
    } else if (g_function == 4) {
        g_setting_238 = 4;
    } else if (g_function == 6 || g_function == 7 || g_function == 8) {
        g_setting_238 = 0;
    } else if (g_function == 9) {
        g_setting_238 = (g_range >= 5) ? 2 : 1;
    } else if (g_function == 10) {
        g_setting_238 = 0;
    } else if (g_function == 1) {
        g_setting_238 = 2;
    } else if (g_function == 3) {
        g_setting_238 = 1;
    } else if (g_function == 0 && g_flag_8098 == 0) {
        g_setting_238 = 5;
    } else if (g_function == 12 && g_range <= 5) {
        g_setting_238 = 1;
    } else {
        g_setting_238 = 3;
    }
    if (g_function == 2 && g_flag_80ba != 0) {
        g_setting_238 = 1;
    }
    if (g_function == 2 && g_flag_80b7 != 0) {
        g_setting_238 = 5;
    }
}

/*
 * orig: 0x0800D8C2
 * Full function/range change: resets the row-select state, reconfigures
 * with calibration, then refreshes the front end and display state.
 */
void range_change(void)
{
    g_flag_80c3 = 1;
    g_flag_80c5 = 0;
    g_flag_80c4 = 0;
    range_select(1);
#if APP_FIX_BUGS
    bar_limit_update();   /* functions 15/24: the limit differs by range */
#endif
    measure_display_reset();
    front_end_pe5(g_function, g_range);
    mode_238_select();
    lcd_bar_flag_dual(g_function, g_range);
    main_dp_update();
}

/* Reconfigure for the current function/range, or for `function` range 0. */
static void reconfigure(uint8_t borrow, uint8_t function)
{
    if (borrow == 0) {
        hy3131_configure_mode(g_function, g_range);
        cal_load(g_function, g_range);
        front_end_configure(g_function, g_range);
    } else {
        hy3131_configure_mode(function, 0);
        cal_load(function, 0);
        front_end_configure(function, 0);
    }
    mode_238_select();
}

/*
 * orig: 0x08006774
 * Functions 16/18/20: switches the front end to function 2 range 0 while
 * `borrow` is set, else back to the selected function/range.
 */
void reconfigure_borrow_f2(uint8_t borrow)
{
    reconfigure(borrow, 2);
    g_counter_fc2 = 0;
}

/* orig: 0x080080C8 - as reconfigure_borrow_f2() with function 1 (functions 17/19/21) */
void reconfigure_borrow_f1(uint8_t borrow)
{
    reconfigure(borrow, 1);
}

#define AUTORANGE_UP       55000
#define AUTORANGE_DOWN_A   3200
#define AUTORANGE_DOWN_B   4000
#define AUTORANGE_UP_LOW   11000   /* functions 15/24 while in the upper range */
#define AUTORANGE_DOWN_LOW 400

static int autorange_allowed(void)
{
    return (uint8_t)(g_flag_808a | g_manual_range) == 0 && g_minmax_mode == 0;
}

/* |value| for functions 22..24 (wrapping, as the original), else value. */
static int32_t magnitude_for_function(int32_t value)
{
    if ((g_function == 22 || g_function == 23 || g_function == 24) && value < 0) {
        return (int32_t)(0u - (uint32_t)value);
    }
    return value;
}

/*
 * orig: 0x0800A260
 * Auto-range on the first reading of a dual-readout function: above
 * 55000 sets range bit 0x200080C6 (and flags the overload), below 3200
 * clears it; the range then becomes sel_code() and is applied.
 */
void autorange_primary(int32_t value)
{
    int32_t mag;

    g_disp_flag_80ca = 0;
    g_disp_flag_80c0 = 0;
    mag = magnitude_for_function(value);
    if (mag > AUTORANGE_UP) {
        g_disp_flag_80ca = 1;
        if (autorange_allowed() && g_sel_80c6 == 0) {
            g_sel_80c6 = 1;
            g_flag_80c8 = 1;
            g_range = sel_code();
            range_change();
        }
    } else if (mag < AUTORANGE_DOWN_A && autorange_allowed() && g_sel_80c6 != 0) {
        g_sel_80c6 = 0;
        g_range = sel_code();
        range_change();
    }
    g_disp_eac = value;
    if (g_mode_8037 == 10) {
        g_disp_flag_8062 = 1;
    }
}

/*
 * orig: 0x0800A358
 * Auto-range on the second reading (range bit 0x200080C7): up above 55000
 * and down below 4000, or 11000/400 for functions 15 and 24 while the bit
 * is set. Functions 13 and 22 show the reading divided by 100 (bit clear)
 * or 10 (bit set).
 */
void autorange_secondary(int32_t value)
{
    int32_t up = AUTORANGE_UP;
    int32_t down = AUTORANGE_DOWN_B;
    int32_t mag;

    if ((g_function == 15 || g_function == 24) && g_sel_80c7 != 0) {
        up = AUTORANGE_UP_LOW;
        down = AUTORANGE_DOWN_LOW;
    }
    mag = magnitude_for_function(value);
    g_disp_flag_80cb = 0;
    g_disp_flag_80c1 = 0;
    if (mag > up) {
        g_disp_flag_80cb = 1;
        if (autorange_allowed() && g_sel_80c7 == 0) {
            g_sel_80c7 = 1;
            g_range = sel_code();
            range_change();
        }
    } else if (mag < down && autorange_allowed() && g_sel_80c7 != 0) {
        g_sel_80c7 = 0;
        g_range = sel_code();
        range_change();
    }
    if (g_function == 13 || g_function == 22) {
        if (g_sel_80c7 == 0) {
            g_disp_eb0 = value / 100;
        } else if (g_sel_80c7 == 1) {
            g_disp_eb0 = value / 10;
        }
        g_disp_eb4 = value;
    } else {
        g_disp_eb0 = value;
    }
    if (g_mode_8037 == 11) {
        g_disp_flag_8062 = 1;
    }
}

/*
 * orig: 0x08015728 - 1 for functions 0, 7, 8, and for 6, 11, 12 in ranges
 * 2 and up unless ignore_range is set
 */
uint8_t range_fixed_function(uint8_t ignore_range)
{
    uint8_t f = g_function;

    if (f == 0 || f == 7 || f == 8) {
        return 1;
    }
    if (f == 6 || f == 11 || f == 12) {
        return (ignore_range == 0 && g_range >= 2) ? 1 : 0;
    }
    return 0;
}

/*
 * orig: 0x0801485A - dual functions (13-15, 22-24): the two row selects of a
 * range; functions 15 and 24 use select 2 for row 1
 */
void dual_rows_for_range(uint8_t function, uint8_t range)
{
    if (function == 13 || function == 14 || function == 15 || function == 22 || function == 23
        || function == 24) {
        if (range == 0) {
            g_sel_80c6 = 0;
            g_sel_80c7 = 0;
        } else if (range == 1) {
            g_sel_80c6 = 1;
            g_sel_80c7 = 0;
        } else if (range == 2) {
            g_sel_80c6 = 0;
            g_sel_80c7 = 1;
        } else if (range == 3) {
            g_sel_80c6 = 1;
            g_sel_80c7 = 1;
        }
        if ((function == 15 || function == 24) && g_sel_80c7 == 1) {
            g_sel_80c7 = 2;
        }
    }
}

/*
 * orig: 0x08014796 - RANGE key: the first press switches to manual range,
 * later presses step through the function's ranges (not in HOLD or the
 * auto-hold mode, nor for single-range functions)
 */
void range_key(void)
{
    if (g_hold_active != 0 || g_autohold_mode != 0) {
        beep_triple();
        return;
    }
    if (g_flag_808a == 0 && g_manual_range == 0) {
        if (g_range_count[g_function] < 2) {
            beep_triple();
        } else {
            if (g_manual_range == 0) {
                g_manual_range = 1;
                sym_w7_b0_update(1);
            }
            beep_short();
        }
    } else if (g_range_count[g_function] < 2) {
        beep_triple();
    } else {
        g_range++;
        if (g_range >= g_range_count[g_function]) {
            g_range = 0;
        }
        g_flag_804b = 1;
        dual_rows_for_range(g_function, g_range);
        beep_short();
    }
}

#if APP_FIX_BUGS
/*
 * The original's measure_display_reset() faked a new reading, so the zeroed
 * g_derived_value was post-processed once. measure_display_reset() now only
 * requests a redraw; of what that post-processing did, only the overload
 * state is kept: autorange() dropped g_flag_8046 of the old configuration
 * (for the functions it does that for) and the latches followed. In
 * auto-hold mode function_change() discarded the fake reading, so nothing
 * changes there.
 */
static void reconfig_overload_reset(void)
{
    uint8_t f = g_function;

    if (g_autohold_mode != 0) {
        return;
    }
    if (!(f == 12 && (uint8_t)(g_manual_range & g_flag_8046) != 0)
        && f != 6 && f != 7 && f != 8 && f != 13 && f != 14 && f != 15
        && f != 22 && f != 23 && f != 24) {
        g_flag_8046 = 0;
    }
    g_flag_8048 = (g_flag_8046 != 0);
    overload_latch();
}
#endif

/*
 * orig: 0x0800D2D0 - set up the current function and range: clears the
 * measurement state; after a rotary/MODE change (g_flag_804a) also resets
 * the range, auto-hold and setup items, and (stop_logging == 1) ends logging
 */
void function_change(uint8_t stop_logging)
{
#if APP_FIX_BUGS
    /* a calibration step in progress belongs to the old function/range:
     * abandon it, or its partial sum would be stored in the new record */
    g_avg_flag_808c = 0;
    g_cal_flag_808e = 0;
    g_flag_8090 = 0;
    g_avg_count = 0;
    g_avg_sum = 0;
    g_cal_sum64 = 0;
#endif
    if (g_function == 9 && g_range != 0 && g_range < 6) {
        g_count_805a = 2;
    }
    g_f12_skip = 0;
    g_flag_809a = 0;
    g_flag_80c5 = 0;
    g_flag_80c4 = 0;
    g_sub_value = 0;
    g_ad1_average_ready = 0;
    g_ad1_count = 0;
    g_ref_7d7c = 0;
    g_level_7d80 = 0;
    g_freq_raw_f0 = 0;
    g_freq_decimal_f0 = 0;
    if (g_function == 2 && g_flag_80ba != 0) {
        g_vec_a = 0;
        g_vec_b = 0;
    }
    if (g_flag_804a != 0) {
        g_flag_80a2 = 0;
        g_sel_80c6 = 0;
        g_sel_80c7 = 0;
        g_flag_80c2 = 0;
        g_flag_80c8 = 0;
        g_flag_80c9 = 0;
        g_manual_range = (g_function == 11);
        secondary_mode_default();
        g_flag_80a6 = 0;
        g_flag_80a5 = 0;
        g_flag_806e = 0;
        if ((g_function == 2 && g_flag_80ba == 0) || g_function == 4) {
            if (g_flag_80b7 == 0) {
                g_range = 0;
            } else {
                g_manual_range = 1;
            }
        } else if (g_function == 9 && g_flag_808a == 0) {
            g_range = 6;
        } else {
            g_range = 0;
        }
#if APP_FIX_BUGS
        /* PEAK keeps the range, which the new function may not have (VAC
         * range 2/3 -> ACmV with 2 ranges): its tables and calibration
         * record would be read past their end */
        if (g_range >= g_range_count[g_function]) {
            g_range = 0;
        }
#endif
        g_autohold_mode = 0;
        g_flag_8051 = 0;
        g_flag_8052 = 0;
        g_flag_8053 = 0;
        g_count_7fb2 = 0;
        if (stop_logging == 1 && g_logging_active != 0) {
            log_stop(1);
            g_logging_active = 0;
        }
        menu_exit();
    }
    g_counter_fc2 = 0;
    g_holdoff = 0;
    g_flag_80b1 = 0;
    g_flag_80b2 = 0;
    if (g_function != 2 && g_function != 4) {
        g_flag_80b9 = 0;
    }
    if (g_function != 2) {
        g_flag_80ba = 0;
    }
    g_rel_active = 0;
    g_minmax_mode = 0;
    if ((g_function != 2 || g_flag_80ba != 0) && g_function != 4) {
        g_flag_80b7 = 0;
    }
    g_peak_max = 0;
    g_peak_min = 0;
    g_flag_8063 = 1;
    g_flag_8060 = 1;
    g_disp_flag_8062 = 1;
    g_flag_804c = 0;
    if (g_hold_active == 0 && g_autohold_mode == 0) {
        g_flag_805f = 1;
    }
    mode_238_select();
    main_dp_update();
    bar_limit_update();
    range_select(1);
    if (g_function != 9 || g_range != 6) {
        g_derived_value = 0;
        g_scaled_value = 0;
    }
    measure_display_reset();
#if APP_FIX_BUGS
    reconfig_overload_reset();
#endif
    front_end_configure(g_function, g_range);
    if (g_flag_804a != 0) {
        apo_reset();
        buzzer_start(300);
    }
    g_flag_804a = 0;
    g_flag_804b = 0;
    if (g_autohold_mode != 0) {
        g_flag_8061 = 0;
        g_flag_8060 = 0;
        g_scaled_ready = 0;
        g_new_reading = 0;
    }
}

#if APP_AUTORANGE_FAST
/*
 * Multi-range auto-range steps. Only for the functions whose ranges are all
 * a decade apart in counts (1, 2, 3, 4, 9, 16..21); every other function
 * keeps single steps. The new range is set up by function_change() as for
 * a single step (g_flag_804b), including the readings skipped after it.
 */

/* counts added to a reading before ranging down: offset and noise of the
 * coarse reading taken in the higher range */
#define AUTORANGE_DOWN_NOISE 5u

/* |g_ad1_average| at or above this is taken as a clipped ADC: straight to
 * the top range. Placeholder; the real clip level must be measured on the
 * meter (log g_ad1_average at 2x/10x/100x full scale and open circuit). */
#define AUTORANGE_AD1_RAIL   0x7F0000u

static int autorange_decade_function(uint8_t f)
{
    return f == 1 || f == 2 || f == 3 || f == 4 || f == 9
           || f == 16 || f == 17 || f == 18 || f == 19 || f == 20 || f == 21;
}

/* 90 % of the bargraph limit of `range` of the current function */
static uint32_t autorange_limit_90(uint8_t range)
{
    return (uint32_t)g_bar_limit_table[g_function][range] * 9u / 10u;
}

/*
 * Range to go to for a reading above the upper limit: the top range when
 * the ADC is at the rail, else the lowest higher range whose 90 % limit
 * holds the reading (magnitude / 10^k). A clipped reading that is not
 * caught here underestimates, which only costs another step later.
 */
static uint8_t autorange_up_target(uint32_t magnitude)
{
    uint8_t top = (uint8_t)(g_range_count[g_function] - 1u);
    uint8_t target = (uint8_t)(g_range + 1u);
    uint32_t ad1 = (uint32_t)g_ad1_average;
    uint32_t divisor = 10u;

    if (!autorange_decade_function(g_function)) {
        return target;
    }
    if (g_ad1_average < 0) {
        ad1 = 0u - ad1;
    }
    if (ad1 >= AUTORANGE_AD1_RAIL) {
        return top;
    }
    while (target < top && magnitude / divisor > autorange_limit_90(target)) {
        target++;
        divisor *= 10u;   /* at most 10^6: ranges 0..6 */
    }
    return target;
}

/*
 * Range to go to for a reading below the lower limit: down k ranges, the
 * largest k whose 90 % limit holds the reading plus 1/8 and the noise
 * allowance, times 10^k. One range down is always taken (the caller's
 * condition already holds for it).
 */
static uint8_t autorange_down_target(uint32_t magnitude)
{
    uint8_t target = (uint8_t)(g_range - 1u);
    uint64_t needed;

    if (!autorange_decade_function(g_function)) {
        return target;
    }
    /* the reading with its margin, in counts of range `target` */
    needed = ((uint64_t)magnitude + magnitude / 8u + AUTORANGE_DOWN_NOISE) * 10u;
    while (target > 0u && needed * 10u <= autorange_limit_90((uint8_t)(target - 1u))) {
        needed *= 10u;
        target--;
    }
    return target;
}
#endif

/*
 * orig: 0x0800DE24 - auto-range and overload: up a range above 110 %
 * (1/12 more after an overload) of the bargraph limit, down below 10 %
 * (1 % for function 12 range 5, 50 % for functions 20/21 range 2); OL in
 * the top range or with manual range. g_setting_238 readings after a
 * function/range change are skipped (see mode_238_select()).
 * APP_AUTORANGE_FAST: the step can span several ranges (see above).
 */
void autorange(void)
{
    uint8_t f = g_function;
    uint32_t upper;
    uint32_t lower;
    uint32_t magnitude;

    if (f == 12 && (uint8_t)(g_manual_range & g_flag_8046) != 0) {
        g_setting_238 = 0;
        g_flag_805c = 0;
        return;
    }
    if (f != 6 && f != 7 && f != 8 && f != 13 && f != 14 && f != 15 && f != 22 && f != 23 && f != 24) {
        g_flag_8046 = 0;
    }
    if (g_setting_238 != 0) {
        g_setting_238--;
        g_overload_prev = 0;
        g_flag_805c = 1;
        return;
    }
    g_flag_805c = 0;
    if (g_flag_808a != 0) {
        g_overload_prev = 0;
        return;
    }
    f = g_function;
    if (f == 13 || f == 14 || f == 15 || f == 22 || f == 23 || f == 24) {
        return;
    }
    if (f == 6 || f == 7 || f == 8) {
        g_peak_is_min = 0;
        return;
    }

    upper = g_bar_limit;
    if (f != 11) {
        upper = (g_overload_prev == 0) ? g_bar_limit / 10u + g_bar_limit : g_bar_limit / 12u + g_bar_limit;
    }
    lower = (f == 12 && g_range == 5) ? g_bar_limit / 100u : g_bar_limit / 10u;
    if ((f == 20 || f == 21) && g_range == 2) {
        lower *= 5u;
    }
    g_peak_is_min = g_derived_value < 0;
    magnitude = (uint32_t)g_derived_value;
    if (g_peak_is_min) {
        magnitude = 0u - (uint32_t)g_derived_value;
    }

    if (g_minmax_mode == 0 && g_manual_range == 0 && g_range_count[g_function] > 1) {
        if (upper < magnitude) {
            if ((g_hold_active == 0
                 || (g_autohold_mode != 0 && g_function != 6 && g_function != 7 && g_function != 8
                     && g_function != 12))
                && (int32_t)g_range < (int32_t)g_range_count[g_function] - 1) {
                if (range_step_blocked(1) == 0) {
#if APP_AUTORANGE_FAST
                    g_range = autorange_up_target(magnitude);
#else
                    g_range++;
#endif
                    g_flag_804b = 1;
                    g_flag_805c = 1;
                    if (g_autohold_mode != 0) {
                        g_autohold_value = 0;
                    }
                }
            } else {
                g_flag_8046 = 1;
                if ((g_autohold_mode & 1u) != 0 && g_function == 9 && g_range == 6) {
                    g_flag_8055 = 1;
                }
            }
        } else if ((g_autohold_mode & g_flag_8055 & 1u) == 0 || g_function != 9 || g_range != 6) {
            if (magnitude < lower) {
                if (g_hold_active == 0 && g_range != 0 && range_step_blocked(0) == 0) {
                    if (g_function == 12 && g_range == 2 && magnitude < 11) {
                        g_flag_80a2 = 1;
                    }
#if APP_AUTORANGE_FAST
                    g_range = autorange_down_target(magnitude);
#else
                    g_range--;
#endif
                    g_flag_804b = 1;
                    g_flag_805c = 1;
                    if (g_autohold_mode != 0) {
                        g_autohold_value = 0;
                    }
                }
            } else {
                g_flag_8035 = 0;
            }
        } else {
            g_flag_8055 = 0;
            g_range = g_range_8056;
            g_flag_804b = 1;
            g_flag_805c = 1;
        }
        g_overload_prev = (g_flag_8046 != 0) ? 1 : 0;
    } else if (g_function == 5) {
        if (g_temp_status == 1) {
            g_peak_is_min = 0;
            g_flag_8046 = 1;
        } else if (g_temp_status == 2) {
            g_peak_is_min = 1;
            g_flag_8046 = 1;
        }
    } else if (upper < magnitude) {
        g_flag_8046 = 1;
    }
}
