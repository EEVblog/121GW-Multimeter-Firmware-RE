/*
 * Function/range change: HY3131 reconfiguration, calibration reload,
 * main display decimal point and display mode.
 */
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

/* orig: 0x0800DB48 - display mode (0..5) for the current function/range */
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
    return (uint8_t)(g_flag_808a | g_flag_8042) == 0 && g_minmax_mode == 0;
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
