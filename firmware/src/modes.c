/*
 * Measurement modes behind the keys: HOLD, REL, MIN/MAX, the 0x20008050
 * mode, MEM store/recall display, and the start of a calibration step.
 */
#include "trim_correction.h"
#include "measure_ad1.h"
#include "temperature.h"
#include "sd_log.h"
#include "irq.h"
#include "ble_command.h"
#include "rtc.h"
#include "stm32l1xx.h"
#include "ble_packet.h"
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

/* orig: 0x0801584C - HOLD off and auto-hold mode on/off; clear_modes == 1 also ends MIN/MAX and REL */
void autohold_mode_set(uint8_t on, uint8_t clear_modes)
{
    g_hold_active = 0;
    if (on != 0) {
        g_flag_8053 = 1;
        g_flag_8051 = 1;
        g_autohold_mode = 1;
        g_range_8056 = g_range;
        g_flag_8055 = 0;
    } else {
        g_flag_8053 = 0;
        g_flag_8051 = 0;
        g_autohold_mode = 0;
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
uint8_t cal_step_samples(void)
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
        g_avg_count = cal_step_samples();
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
        g_autohold_mode = 0;
        g_flag_804a = 1;
        settings_save_function();
    }
}

/*
 * orig: 0x0800E2A0 - function 9 in auto range: a range step needs the same
 * direction twice within 100 ticks (returns 1 to hold back a step down
 * right after a step up)
 */
uint8_t range_step_blocked(uint8_t up)
{
    if (g_manual_range != 0 || g_flag_808a != 0 || g_function != 9) {
        g_flag_8035 = 0;
        return 0;
    }
    if (up == 0 && g_flag_8035 == 1) {
        return 1;
    }
    g_flag_8035 = (up == 0) ? 2 : 1;
    g_count_8036 = 100;
    return 0;
}

/*
 * orig: 0x0800E30C - auto-hold (with HOLD or auto-hold mode): after the
 * input drops below 1000 counts, the next reading that stays within 20
 * counts on two updates (and differs by more than 400 from the held one) is
 * captured, logged and confirmed with a short beep
 */
void autohold_update(void)
{
    int32_t v;
    uint8_t f;

    if ((g_hold_active == 0 && g_autohold_mode == 0) || g_flag_805c != 0) {
        return;
    }
    if (g_function == 9 && g_manual_range == 0 && g_range != 0 && g_range <= 5 && g_count_805a != 0) {
        return;
    }
    v = g_derived_value;
    f = g_function;
    if (!(((f == 9 || f == 10) && v > 999 && g_flag_8046 == 0)
          || (f != 9 && f != 10 && (v > 999 || v < -999)))) {
        g_flag_8052 = 1;
        g_flag_8051 = 0;
        g_count_7fb2 = 0;
        g_autohold_value = g_derived_value;
        g_new_reading = 0;
        return;
    }
    if (g_flag_8052 != 0) {
        g_flag_8052 = 0;
        g_flag_8051 = 0;
        g_count_7fb2 = 0;
    }
    if (g_flag_8051 == 0 && g_flag_8046 == 0) {
        g_new_reading = 0;
        return;
    }
    if (g_function != 9 && g_function != 10 && g_flag_8046 != 0) {
        return;
    }
    if ((g_function == 9 || g_function == 10) && g_flag_8046 != 0) {
        g_new_reading = 0;
        g_autohold_value = 1000000;
        g_autohold_armed = 0;
    } else if ((uint8_t)(g_autohold_force & (g_flag_8046 ^ 1u)) == 0
               && g_derived_value < (int32_t)((uint32_t)g_autohold_value + 400u)
               && (int32_t)((uint32_t)g_autohold_value - 400u) < g_derived_value) {
        g_new_reading = 0;
        g_autohold_armed = 0;
    } else {
        uint32_t magnitude = (uint32_t)g_derived_value;

        g_autohold_force = 0;
        if (g_derived_value < 0) {
            magnitude = 0u - (uint32_t)g_derived_value;
        }
        if (magnitude <= g_bar_limit / 10u + g_bar_limit) {
            if (g_autohold_armed == 0) {
                g_autohold_armed = 1;
                g_autohold_candidate = g_derived_value;
            } else if (g_derived_value < (int32_t)((uint32_t)g_autohold_candidate - 20u)
                       || (int32_t)((uint32_t)g_autohold_candidate + 20u) < g_derived_value) {
                g_autohold_armed = 0;
            } else {
                g_autohold_armed = 0;
                g_autohold_captured = g_derived_value;
                g_flag_8053 = 1;
                g_autohold_value = g_derived_value;
                g_range_8056 = g_range;
                g_flag_8055 = 0;
                if (g_logging_active != 0) {
                    g_log_due = 1;
                }
                buzzer_start_forced(50);
            }
        }
    }
    g_flag_8051 = 0;
    g_count_7fb2 = 0;
}

/*
 * orig: 0x0800E848 - warnings: g_flag_80b2 (hazardous voltage beep) beyond
 * 6000 counts in functions 0..2 (functions 1/2 in range 3 only),
 * g_flag_80b1 (warning symbol) when a reading exceeds the rating of the range
 */
void voltage_warnings(void)
{
    uint8_t f;

    g_flag_80b2 = 0;
    g_flag_80b1 = 0;
    f = g_function;
    if ((f == 1 || f == 2) && g_range == 3) {
        g_flag_80b2 = outside_limit(g_derived_value, 6000);
        if (g_flag_8046 != 0) {
            g_flag_80b2 = 1;
        }
    } else if (f == 0) {
        g_flag_80b2 = outside_limit(g_derived_value, 6000);
        if (g_flag_8046 != 0) {
            g_flag_80b2 = 1;
        }
    }
    f = g_function;
    if (f == 1 || f == 2) {
        if (g_range != 0) {
            if (g_range == 2) {
                g_flag_80b1 = outside_limit(g_derived_value, 3000);
            } else if (g_range < 2) {
                g_flag_80b1 = outside_limit(g_derived_value, 30000);
            } else if (g_range == 3) {
                g_flag_80b1 = outside_limit(g_derived_value, 300);
            }
        }
        if (g_range != 0 && g_flag_8046 != 0) {
            g_flag_80b1 = 1;
        }
    } else if (f == 13 || f == 14 || f == 15 || f == 22 || f == 23 || f == 24) {
        if (g_range == 1 || g_range == 3) {
            g_flag_80b1 = outside_limit(g_ref_7d7c, 30000);
            if (g_disp_flag_80ca != 0) {
                g_flag_80b1 = 1;
            }
        }
    } else if (f == 0) {
        g_flag_80b1 = outside_limit(g_derived_value, 300);
        if (g_flag_8046 != 0) {
            g_flag_80b1 = 1;
        }
    }
    sym_w1_b7_update();
}

/*
 * orig: 0x0800ED64 - frequency functions without the second reading: every
 * 100 ticks (2000 in function 0) alternate the counter preset 0x8000/0xE000
 * and reconfigure the HY3131
 */
void counter_preset_alternate(void)
{
    uint8_t f = g_function;
    uint16_t period;

    if (g_flag_80a5 != 0) {
        return;
    }
    if (!(((f == 16 || f == 18 || f == 20 || f == 2 || f == 4) && g_flag_80ba == 0)
          || (f == 0 && g_flag_8098 != 0))) {
        return;
    }
    period = (g_function == 0) ? 2000 : 100;
    g_counter_fc2++;
    if (period < g_counter_fc2) {
        g_counter_fc2 = 0;
        g_preset_phase = (g_preset_phase != 1);
        g_counter_preset = (g_preset_phase != 0) ? 0x8000u : 0xE000u;
        hy3131_configure_mode(g_function, g_range);
    }
}

/*
 * orig: 0x08006558 - function 0 (LowZ): five quiet readings in a row
 * (within +-100 counts, +-30 once detected) count down g_setting_239; at 0
 * the input detection g_flag_8098 toggles and the function is set up again
 */
void lowz_detect(void)
{
    int32_t limit = (g_flag_8099 == 0) ? 100 : 30;

    if (limit < g_derived_value || g_derived_value < -limit || g_flag_8046 != 0) {
        cal_setting_239_reset();
        if (g_flag_8099 == 0) {
            g_derived_value = 0;
            g_lowz_count--;
            if (g_lowz_count == 0) {
                g_flag_8099 = 1;
                g_flag_8060 = 1;
            }
        }
    } else {
        g_derived_value = 0;
        g_flag_8099 = 0;
        g_lowz_count = 5;
        g_flag_8060 = 1;
        g_setting_239--;
        if (g_setting_239 == 0) {
            g_flag_8098 = (g_flag_8098 == 0);
            cal_setting_239_reset();
            function_change(0);
        }
    }
}

/* orig: 0x0800DCFC - a new reading: auto-range, warnings, auto-hold, MIN/MAX, REL, overload latch */
void measure_postprocess(void)
{
    autorange();
    if (g_setting_238 == 0) {
        voltage_warnings();
    }
    autohold_update();
    minmax_update();
    rel_apply();
    g_flag_8048 = (g_flag_8046 != 0);
    overload_latch();
}

/*
 * orig: 0x0801417C - current input sockets: PE10/PE11 low (1 / 2) when a
 * plug is in. After 10 equal samples, the A socket (1) switches the current
 * functions between their mA and A variants, and a plug in a socket that
 * does not fit the function asks for the lead ("LEAd")
 */
void input_jack_check(void)
{
    uint8_t settled = 0;
    uint8_t changed = 0;
    uint8_t jack;
    uint8_t f;

    jack = ((uint16_t)GPIOE->IDR & (1u << 10)) == 0;
    if (((uint16_t)GPIOE->IDR & (1u << 11)) == 0) {
        jack = (uint8_t)(jack + 2);
    }
    if (g_jack_prev == jack) {
        g_jack_debounce--;
        if (g_jack_debounce == 0) {
            g_jack_debounce = 10;
            g_jack_amps = (jack == 1);
            settled = 1;
        }
    } else {
        g_jack_debounce = 10;
        g_flag_80cc = jack;
        g_jack_prev = jack;
    }

    f = g_function;
    if (f == 20 && g_jack_amps != 0) {
        g_function = 18;
        changed = 1;
    } else if (f == 21 && g_jack_amps != 0) {
        g_function = 19;
        changed = 1;
    } else if (f == 15 && g_jack_amps != 0) {
        g_function = 14;
        changed = 1;
    } else if (f == 24 && g_jack_amps != 0) {
        g_function = 23;
        changed = 1;
    } else if (f == 18 && g_jack_amps == 0) {
        g_function = 20;
        changed = 1;
    } else if (f == 19 && g_jack_amps == 0) {
        g_function = 21;
        changed = 1;
    } else if (f == 14 && g_jack_amps == 0) {
        g_function = 15;
        changed = 1;
    } else if (f == 23 && g_jack_amps == 0) {
        g_function = 24;
        changed = 1;
    }
    if (changed) {
        g_flag_804a = 1;
        function_change(1);
    }
    if (settled) {
        if (jack == 1 || jack == 2) {
            f = g_function;
            if (f == 18 || f == 20 || f == 19 || f == 21 || f == 14 || f == 15 || f == 23 || f == 24) {
                g_flag_8076 = 0;
            } else if (f == 16 || f == 17 || f == 13 || f == 22) {
                if (jack == 2) {
                    show_lead_request();
                } else {
                    g_flag_8076 = 0;
                }
            } else {
                show_lead_request();
            }
        } else {
            g_flag_8076 = 0;
        }
    }
}

/*
 * orig: 0x08015410 - next secondary display mode for the function (while
 * editing the serial number: next digit), and load the setup value to edit
 */
void secondary_mode_next(void)
{
    uint8_t f;

    if (g_mode_8037 != 8) {
        g_flag_806e = 0;
    }
    if (g_flag_806e != 0 && g_mode_8037 == 8) {
        if (g_edit_digit == 0) {
            g_edit_digit = 4;
        } else {
            g_edit_digit--;
        }
        g_edit_idle_ticks = 0;
    } else {
        f = g_function;
        if (f == 2 && g_flag_80ba == 0) {
            g_mode_8037++;
            if (g_mode_8037 > 11) {
                g_mode_8037 = 0;
            }
        } else if (f == 13 || f == 14 || f == 15 || f == 22 || f == 23 || f == 24) {
            g_mode_8037++;
            if (g_mode_8037 == 11) {
                g_flag_80a9 = g_flag_80a8;
            } else if (g_flag_80a8 == 0) {
                if (g_mode_8037 > 11) {
                    g_mode_8037 = 0;
                }
            } else if (g_mode_8037 > 12) {
                g_mode_8037 = 0;
            }
            if (g_mode_8037 == 10) {
                secondary_dp_refresh();
            }
            if (g_mode_8037 == 12) {
                g_flag_80aa = 100;
            }
        } else if (f == 16 || f == 18 || f == 20) {
            g_mode_8037++;
            if (g_mode_8037 == 10) {
                g_flag_80a6 = g_flag_80a5;
            } else if (g_mode_8037 > 11) {
                g_mode_8037 = 0;
            }
        } else if (f == 17 || f == 19 || f == 21) {
            g_mode_8037++;
            if (g_mode_8037 == 10) {
                g_flag_80a6 = g_flag_80a5;
            } else if (g_flag_80a5 == 0) {
                if (g_mode_8037 > 10) {
                    g_mode_8037 = 0;
                }
            } else if (g_mode_8037 > 11) {
                g_mode_8037 = 0;
            }
        } else if (f == 11 || f == 10 || f == 4) {
            g_mode_8037++;
            if (g_mode_8037 > 10) {
                g_mode_8037 = 0;
            }
        } else {
            g_mode_8037++;
            if (g_mode_8037 > 9) {
                g_mode_8037 = 0;
            }
        }
    }

    if (g_mode_8037 == 5) {
        g_rtc_edit_dd = g_rtc_set_reg2;
    } else if (g_mode_8037 == 6) {
        g_rtc_edit_de = g_rtc_set_reg4;
        g_rtc_edit_df = g_rtc_set_reg6;
        g_flag_80d6 = 0;
    } else if (g_mode_8037 == 7) {
        g_rtc_edit_e0 = g_rtc_set_reg10;
        g_rtc_edit_e1 = g_rtc_set_reg12;
        g_flag_80d6 = 0;
    } else if (g_flag_806e == 0 && g_mode_8037 == 8) {
        g_edit_value = g_serial_number;
    } else if (g_mode_8037 == 9) {
        g_log_interval_edit = g_log_interval_s;
    }
}
