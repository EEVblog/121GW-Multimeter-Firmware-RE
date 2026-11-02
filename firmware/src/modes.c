/*
 * Measurement modes behind the keys: HOLD, REL, MIN/MAX, the 0x20008050
 * mode, MEM store/recall display, and the start of a calibration step.
 */
#include "modes.h"
#include "ac_response.h"
#include "annunciators.h"
#include "measure_counter.h"
#include "buzzer.h"
#include "hy3131_ctrl.h"
#include "calibration.h"
#include "display_bargraph.h"
#include "display_state.h"
#include "display_symbols.h"
#include "display_text.h"
#include "dual_correction.h"
#include "front_end.h"
#include "lcd_driver.h"
#include "measure_derived.h"
#include "measure_funcs.h"
#include "display_update.h"
#include "measure_peak.h"
#include "measure_scale.h"
#include "mem_records.h"
#include "display_update.h"
#include "numeric.h"
#include "range_select.h"
#include "settings.h"
#include "setup_menu.h"

#define DISPLAY_LIMIT  100000      /* readings outside +-99999 show OL */

/* Bargraph overload threshold per function and range (orig: 0x08025A3C) */
const int32_t g_bar_limit_table[25][7] = {
    {  6000, 50000, 50000, 50000, 50000, 50000, 50000},
    { 50000, 50000, 50000,  6000, 50000, 50000, 50000},
    { 50000, 50000, 50000,  6000, 50000, 50000, 50000},
    { 50000, 50000, 50000, 50000, 50000, 50000, 50000},
    { 50000, 50000, 50000, 50000, 50000, 50000, 50000},
    { 10000, 50000, 50000, 50000, 50000, 50000, 50000},
    { 99999, 99999, 99999, 99999, 99999, 99999, 50000},
    { 99999, 99999, 99999, 50000, 50000, 50000, 50000},
    {   990, 50000, 50000, 50000, 50000, 50000, 50000},
    { 50000, 50000, 50000, 50000, 50000, 50000, 50000},
    { 50000, 50000, 50000, 50000, 50000, 50000, 50000},
    { 30000, 15000, 50000, 50000, 50000, 50000, 50000},
    { 10000, 10000, 10000, 10000, 10000, 10000, 50000},
    { 25000, 25000, 25000, 25000, 50000, 50000, 50000},
    { 25000, 25000, 25000, 25000, 50000, 50000, 50000},
    { 25000, 25000, 50000, 50000, 50000, 50000, 50000},
    { 50000, 50000, 50000, 50000, 50000, 50000, 50000},
    { 50000, 50000, 50000, 50000, 50000, 50000, 50000},
    { 50000, 50000, 50000, 50000, 50000, 50000, 50000},
    { 50000, 50000, 50000, 50000, 50000, 50000, 50000},
    { 50000, 50000, 10000, 50000, 50000, 50000, 50000},
    { 50000, 50000, 10000, 50000, 50000, 50000, 50000},
    { 25000, 25000, 25000, 25000, 50000, 50000, 50000},
    { 25000, 25000, 25000, 25000, 50000, 50000, 50000},
    { 25000, 25000, 50000, 50000, 50000, 50000, 50000},
};

/* orig: 0x0800D5F4 - bargraph limit; function 11 takes column 0/1 by g_flag_80b3 */
void bar_limit_update(void)
{
    if (g_function == 11) {
        g_bar_limit = (uint32_t)g_bar_limit_table[g_function][(g_flag_80b3 != 0) ? 1 : 0];
    } else {
        g_bar_limit = (uint32_t)g_bar_limit_table[g_function][g_range];
    }
}

/* overload state of a MIN/MAX value: OL beyond -99999 / +99999 */
static void minmax_overload(int32_t value)
{
    if (value < -(DISPLAY_LIMIT - 1)) {
        g_peak_is_min = 1;
        g_flag_8046 = 1;
    } else if (value < DISPLAY_LIMIT) {
        g_flag_8046 = 0;
    } else {
        g_peak_is_min = 0;
        g_flag_8046 = 1;
    }
}

/*
 * orig: 0x0800E758 - the value MIN/MAX shows, with its overload state; the
 * held value (mode 4) only if with_hold is non-zero. Without a value the
 * argument is returned.
 */
int32_t minmax_value(int32_t with_hold)
{
    int32_t value = with_hold;

    switch (g_minmax_mode) {
    case 1:
        value = g_minmax_max;
        if (value >= DISPLAY_LIMIT) {
            g_peak_is_min = 0;
            g_flag_8046 = 1;
        } else {
            g_flag_8046 = 0;
        }
        break;
    case 2:
        value = g_minmax_min;
        if (value < -(DISPLAY_LIMIT - 1)) {
            g_peak_is_min = 1;
            g_flag_8046 = 1;
        } else if (value < DISPLAY_LIMIT) {
            g_flag_8046 = 0;
        }
        break;
    case 3:
        value = g_minmax_avg;
        if (g_minmax_count != 0) {
            g_flag_8046 = 0;
        }
        break;
    case 4:
        if ((uint8_t)with_hold != 0) {
            value = g_hold_value;
            minmax_overload(value);
        }
        break;
    default:
        break;
    }
    return value;
}

/* orig: 0x0800E604 - MIN/MAX: track max, min and the running average; show the selected one */
void minmax_update(void)
{
    int32_t value;

    if (g_minmax_mode == 0 || g_hold_active != 0) {
        return;
    }
    if (g_flag_8046 == 0) {
        value = g_derived_value;
        g_minmax_last = value;
        if (g_minmax_max < value) {
            g_minmax_max = value;
        }
        if (value < g_minmax_min) {
            g_minmax_min = value;
        }
        if (g_minmax_count == 0) {
            g_minmax_count = 1;
            g_minmax_avg = value;
            g_minmax_avg_rem = 0;
        } else {
            g_minmax_count++;
            if (g_minmax_count > 1000000) {
                g_minmax_count = 1000000;
            }
            /* exact running mean: the remainder carries what the division drops */
            g_minmax_avg_rem = (int32_t)((uint32_t)value - (uint32_t)g_minmax_avg
                                         + (uint32_t)g_minmax_avg_rem);
            g_minmax_avg = (int32_t)((uint32_t)m3_sdiv(g_minmax_avg_rem, g_minmax_count)
                                     + (uint32_t)g_minmax_avg);
            g_minmax_avg_rem = (int32_t)((uint32_t)g_minmax_avg_rem
                                         - (uint32_t)g_minmax_count
                                           * (uint32_t)m3_sdiv(g_minmax_avg_rem, g_minmax_count));
        }
    } else if (g_peak_is_min == 0) {
        g_minmax_max = DISPLAY_LIMIT;
        g_minmax_last = DISPLAY_LIMIT;
    } else {
        g_minmax_min = -DISPLAY_LIMIT;
        g_minmax_last = -DISPLAY_LIMIT;
    }
    value = minmax_value(0);
    if (g_minmax_mode < 4) {
        g_derived_value = value;
    }
}

/* orig: 0x0800E594 - REL: subtract the reference (taken from the next reading after REL) */
void rel_apply(void)
{
    if (g_hold_active != 0 || g_rel_active == 0 || g_flag_8046 != 0) {
        return;
    }
    if (g_rel_capture != 0) {
        g_rel_capture = 0;
        g_rel_reference = (g_minmax_mode == 0) ? g_derived_value : g_minmax_last;
    }
    g_derived_value = (int32_t)((uint32_t)g_derived_value - (uint32_t)g_rel_reference);
}

/* orig: 0x0801584C - HOLD off and the 0x20008050 mode on/off; clear_modes == 1 also ends MIN/MAX and REL */
void mode_8050_set(uint8_t on, uint8_t clear_modes)
{
    g_hold_active = 0;
    if (on != 0) {
        g_flag_8053 = 1;
        g_flag_8051 = 1;
        g_flag_8050 = 1;
        g_range_8056 = g_range;
        g_flag_8055 = 0;
    } else {
        g_flag_8053 = 0;
        g_flag_8051 = 0;
        g_flag_8050 = 0;
    }
    g_flag_8052 = 0;
    g_count_7fb2 = 0;
    if (clear_modes == 1) {
        sym_w7_b0_update(0);
        g_minmax_mode = 0;
        g_rel_active = 0;
        sym_w2_w4_b16_update(1);
    }
}

/* orig: 0x080153AC - leave the setup menu: default secondary mode, everything redrawn */
void menu_exit(void)
{
    g_menu_active = 0;
    secondary_mode_default();
    g_scaled_ready = 1;
    g_new_reading = 1;
    g_flag_8060 = 1;
    g_flag_8061 = 1;
    g_disp_flag_8062 = 1;
}

/* orig: 0x080159C8 - function 11: toggle and store g_flag_80b3 (front end and bargraph limit follow) */
void flag_80b3_toggle(void)
{
    g_flag_80b3 = (g_flag_80b3 != 1);
    settings_save_flag_80b3(g_flag_80b3);
    g_main_dp = main_dp_for_range(g_function, g_range);
    front_end_pe15_pf6(g_function);
    bar_limit_update();
    g_mode_8037 = 10;
    g_disp_flag_8062 = 1;
}

/* orig: 0x08015934 - while held, show the MIN/MAX value */
void minmax_show(void)
{
    int32_t value;

    if (g_hold_active == 0) {
        return;
    }
    value = minmax_value(1);
    if (g_flag_8046 == 0) {
        show_main_number(g_function, g_main_dp, value, 0);
    } else {
        show_ofl(0);
    }
}

/* orig: 0x080158D4 - MIN/MAX key: start (manual range), then cycle MAX, MIN, AVG, held */
void minmax_key(void)
{
    if (g_minmax_mode == 0) {
        minmax_start();
        g_manual_range_saved = g_manual_range;
        g_manual_range = 1;
        sym_w7_b0_update(0);
    } else {
        g_minmax_mode++;
        if (g_minmax_mode > 4) {
            g_minmax_mode = 1;
        }
    }
    minmax_show();
    sym_w5_b7_update(0);
    sym_b30_update(1);
}

/* orig: 0x08010B64 - store the current readings as the next MEM record (60, then wraps) */
void mem_log_store(void)
{
    g_log_due = 0;
    if (g_mem_count > 59) {
        g_mem_count = 0;
    }
    mem_store(g_mem_count);
    g_mem_count++;
    if (g_mem_count > 59) {
        g_flag_801c = 1;
        g_value_7d44 = 60;
    }
    g_log_count++;
    g_disp_flag_8062 = 1;
}

/* orig: 0x080170AC - recall position n: number on the secondary readout, reading on the main one */
void mem_recall_show(uint16_t n)
{
    show_secondary_number_dp(n, 0, 0);
    mem_recall((uint16_t)(mem_recall_index(n) - 1u));
    if (g_recall_value1 == INT32_MAX) {
        show_ofl(0);
    } else {
        show_main_number(g_function_7feb, g_recall_dp, g_recall_value1, 1);
    }
    lcd_update();
}

/* orig: 0x08011F38 - clear the four trim requests and trims; store them */
void cal_trims_reset(void)
{
    g_cal_trim_request[3] = 0;
    g_cal_trim_request[2] = 0;
    g_cal_trim_request[1] = 0;
    g_cal_trim_request[0] = 0;
    g_cal_trim[3] = 0;
    g_cal_trim[2] = 0;
    g_cal_trim[1] = 0;
    g_cal_trim[0] = 0;
    cal_store_trims(g_function, g_range, 100);
}

static void trim_request(uint8_t second, uint8_t first_index)
{
    if (second == 0) {
        g_cal_trim_request[first_index] = 1;
    } else {
        g_cal_trim_request[first_index + 1] = 1;
    }
    g_setting_238 = 5;
}

/*
 * orig: 0x0800F13C - for the frequency-calibrated functions (2, 4, 16, 18,
 * 20): request a trim when the input frequency is in a calibration band.
 * Returns 1 (trim requested), 2 (band not used by this function) or 0;
 * outside the bands the first pass clears all trims.
 */
uint8_t cal_freq_check(uint8_t second)
{
    uint8_t f = g_function;
    uint8_t result = 0;
    int32_t freq;

    if (f != 16 && f != 18 && f != 20 && f != 2 && f != 4) {
        return 0;
    }
    freq = scale_by_decimal(g_freq_decimal, g_freq_raw);
    f = g_function;
    if (f == 2 && g_range >= 2 && freq >= 8000 && freq <= 12000) {
        trim_request(second, 0);
        result = 1;
    } else if ((f == 2 || f == 4) && freq >= 90000) {
        trim_request(second, 0);
        result = 1;
    } else if ((f == 2 || f == 4) && freq >= 18000 && freq <= 22000) {
        if (f == 4) {
            result = 2;
        } else {
            trim_request(second, 2);
            result = 1;
        }
    } else if ((f == 16 || f == 18 || f == 20) && freq >= 8000 && freq <= 12000) {
        if (f == 20 || (f == 16 && g_range == 0) || (f == 18 && g_range == 0)) {
            result = 2;
        } else {
            trim_request(second, 0);
            result = 1;
        }
    } else if (second == 0) {
        cal_trims_reset();
    }
    return result;
}

/* orig: 0x080157E8 - samples averaged for a calibration step */
uint8_t cal_sample_count(void)
{
    uint8_t f = g_function;

    if (f == 13 || f == 14 || f == 15 || f == 22 || f == 23 || f == 24) {
        return 4;
    }
    if (f == 9 && g_range == 6) {
        return 4;
    }
    return 16;
}

/* orig: 0x08015768 - start calibration step 0 or 1 unless a trim was requested (see cal_freq_check) */
uint8_t cal_start(uint8_t which)
{
    uint8_t r;

    g_avg_flag_808c = 0;
    g_avg_done_808d = 0;
    g_cal_flag_808e = 0;
    g_cal_done_808f = 0;
    r = cal_freq_check(which);
    if (r == 0) {
        if (which == 0) {
            g_avg_flag_808c = 1;
        } else if (which == 1) {
            g_cal_flag_808e = 1;
        }
        g_avg_count = cal_sample_count();
        g_cal_sum64 = 0;
        g_avg_sum = 0;
    }
    return r;
}

/*
 * orig: 0x08014500 - MODE key: next function of the rotary position
 *   1->2 (2 toggles AC+DC), 3->4->5->3, 6->7->8->6, 9->10->11->12->9,
 *   13->17->16->22->13, 14->23->14, 15->24->15, 18->19->18, 20->21->20
 */
void mode_key(void)
{
    static const uint8_t next_function[25] = {
        0, 2, 0, 4, 5, 3, 7, 8, 6, 10, 11, 12, 9, 17, 23, 24, 22, 16, 19, 18, 21, 20, 13, 14, 15,
    };
    uint8_t changed = 0;
    uint8_t f;

    annunciator_sub_clear();
    f = g_function;
    if (f == 2) {
        acdc_mode_toggle();
        changed = 1;
    } else if (f != 0 && f < 25) {
        g_function = next_function[f];
        changed = 1;
    }
    g_flag_8074 = 0;
    if (g_function == 6 || g_function == 7 || g_function == 8) {
        g_flag_8074 = 1;
    }
    if (changed) {
        g_hold_active = 0;
        g_flag_8050 = 0;
        g_flag_804a = 1;
        settings_save_function();
    }
}
