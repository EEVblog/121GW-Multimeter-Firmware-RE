/*
 * Function/range change: HY3131 reconfiguration, calibration reload,
 * secondary display decimal point and display mode.
 */
#include "range_select.h"
#include "calibration.h"
#include "display_state.h"
#include "front_end.h"
#include "hy3131_ctrl.h"
#include "lcd_driver.h"
#include "measure_derived.h"

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

/* Secondary display decimal point position per range (index = range). */
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
 * Secondary display decimal point for a function/range (0 = none).
 * Ranges past the end of a function's list give 0.
 */
uint8_t sub_dp_for_range(uint8_t function, uint8_t range)
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
void sub_dp_update(void)
{
    g_sub_dp = sub_dp_for_range(g_function, g_range);
    lcd_set_sub_dp(g_sub_dp);
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
    sub_dp_update();
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
