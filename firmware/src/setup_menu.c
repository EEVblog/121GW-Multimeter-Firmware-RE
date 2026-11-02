/*
 * SETUP menu items and the start-up state.
 */
#include "setup_menu.h"
#include "ble_command.h"
#include "board_io.h"
#include "buzzer.h"
#include "calibration.h"
#include "display_symbols.h"
#include "display_text.h"
#include "display_update.h"
#include "display_state.h"
#include "front_end.h"
#include "hy3131_ctrl.h"
#include "lcd_driver.h"
#include "measure_derived.h"
#include "measure_funcs.h"
#include "mem_records.h"
#include "power.h"
#include "range_select.h"
#include "rtc.h"
#include "settings.h"
#include "system_init.h"
#include "trim_correction.h"

static int dual_function(uint8_t f)
{
    return f == 13 || f == 14 || f == 15 || f == 22 || f == 23 || f == 24;
}

static int va_function(uint8_t f)
{
    return f == 16 || f == 18 || f == 20 || f == 17 || f == 19 || f == 21;
}

/*
 * orig: 0x0800D718 - secondary mode for the function: 10 (its auxiliary
 * value) where it has one, else 0; 5 (year) while the serial prefix follows
 * the clock
 */
void secondary_mode_default(void)
{
    uint8_t f = g_function;

    if (g_flag_808b != 0) {
        g_mode_8037 = 5;
    } else if (f == 2 && g_flag_80ba == 0) {
        g_mode_8037 = 10;
    } else if (f == 4) {
        g_mode_8037 = 10;
    } else if (dual_function(f)) {
        g_mode_8037 = 10;
    } else if (va_function(f)) {
        g_mode_8037 = 10;
        g_flag_80aa = 100;
    } else {
        g_mode_8037 = 0;
    }
}

static uint8_t toggled(uint8_t flag)
{
    return (flag == 1) ? 0 : 1;
}

/* orig: 0x08016190 - dual functions: "bd" (secondary shows the burden value) */
void setup_bd_dual_key(uint8_t key)
{
    if (key == SETUP_KEY_STORE) {
        g_flag_80a8 = g_flag_80a9;
        settings_save_flag_80a8(g_flag_80a9);
    } else {
        g_flag_80a9 = toggled(g_flag_80a9);
    }
    g_disp_flag_8062 = 1;
}

/* orig: 0x080160E8 - VA functions: "bd" */
void setup_bd_va_key(uint8_t key)
{
    if (key == SETUP_KEY_STORE) {
        g_flag_80a5 = g_flag_80a6;
        g_flag_80c4 = 0;
        g_flag_80c5 = 0;
        if (g_function == 17 || g_function == 19 || g_function == 21) {
            reconfigure_borrow_f1(g_flag_80c4 != 0);
        } else {
            reconfigure_borrow_f2(g_flag_80c4 != 0);
        }
    } else {
        g_flag_80a6 = toggled(g_flag_80a6);
    }
    g_disp_flag_8062 = 1;
}

/* orig: 0x08015C20 - function 10: setting 0..3 */
void setup_updn_key(uint8_t key)
{
    if (key == SETUP_KEY_UP) {
        g_sel_80b6++;
        if (g_sel_80b6 > 3) {
            g_sel_80b6 = 0;
        }
    } else if (key == SETUP_KEY_DOWN) {
        if (g_sel_80b6 == 0) {
            g_sel_80b6 = 3;
        } else {
            g_sel_80b6--;
        }
    } else {
        settings_save_sel_80b6(g_sel_80b6);
    }
    g_disp_flag_8062 = 1;
}

/* orig: 0x08015A22 - auto power-off on/off */
void setup_apo_key(uint8_t key)
{
    if (key == SETUP_KEY_UP || key == SETUP_KEY_DOWN) {
        g_apo_disabled = (g_apo_disabled != 1);
        sym_w5_b7_update(1);
    } else {
        settings_save_apo(g_apo_disabled);
    }
    g_disp_flag_8062 = 1;
}

/* orig: 0x08015A74 - beep on/off */
void setup_beep_key(uint8_t key)
{
    if (key == SETUP_KEY_UP || key == SETUP_KEY_DOWN) {
        g_beep_enabled = toggled(g_beep_enabled);
    } else {
        settings_save_beep(g_beep_enabled);
    }
    g_disp_flag_8062 = 1;
}

/* orig: 0x08015B54 - LCD contrast 10..17, applied at once */
void setup_contrast_key(uint8_t key)
{
    if (key == SETUP_KEY_UP) {
        if (g_lcd_contrast == 17) {
            g_lcd_contrast = 10;
        } else {
            g_lcd_contrast++;
        }
    } else if (key == SETUP_KEY_DOWN) {
        if (g_lcd_contrast == 10) {
            g_lcd_contrast = 17;
        } else {
            g_lcd_contrast--;
        }
    } else {
        settings_save_contrast(g_lcd_contrast);
    }
    lcd_init(g_lcd_contrast, 0);
    g_disp_flag_8062 = 1;
}

/* orig: 0x08015AC0 - log interval 0..999 s */
void setup_log_interval_key(uint8_t key)
{
    if (key == SETUP_KEY_UP) {
        if (g_log_interval_edit < 999) {
            g_log_interval_edit++;
        } else {
            g_log_interval_edit = 0;
        }
    } else if (key == SETUP_KEY_DOWN) {
        if (g_log_interval_edit == 0) {
            g_log_interval_edit = 999;
        } else {
            g_log_interval_edit--;
        }
    } else {
        g_log_interval_s = g_log_interval_edit;
        settings_save_log_interval(g_log_interval_edit);
    }
    g_disp_flag_8062 = 1;
}

/* orig: 0x08015C94 - year 00..99 */
void setup_year_key(uint8_t key)
{
    if (key == SETUP_KEY_UP) {
        g_rtc_edit_dd++;
        if (g_rtc_edit_dd > 99) {
            g_rtc_edit_dd = 0;
        }
    } else if (key == SETUP_KEY_DOWN) {
        if (g_rtc_edit_dd == 0) {
            g_rtc_edit_dd = 99;
        } else {
            g_rtc_edit_dd--;
        }
    } else {
        g_rtc_set_reg2 = g_rtc_edit_dd;
        g_rtc_set_reg6 = rtc_clamp_day(g_rtc_edit_dd, g_rtc_set_reg4, g_rtc_set_reg6);
        rtc_write_datetime();
        settings_serial_yymm_from_clock();
    }
    g_disp_flag_8062 = 1;
}

/* orig: 0x08015D40 - month 1..12, then (g_flag_80d6 set) day 1..length of the month */
void setup_date_key(uint8_t key)
{
    if (g_flag_80d6 == 0) {
        if (key == SETUP_KEY_UP) {
            g_rtc_edit_de++;
            if (g_rtc_edit_de >= 13) {
                g_rtc_edit_de = 1;
            }
        } else if (key == SETUP_KEY_DOWN) {
            if (g_rtc_edit_de < 2) {
                g_rtc_edit_de = 12;
            } else {
                g_rtc_edit_de--;
            }
        } else {
            g_rtc_set_reg4 = g_rtc_edit_de;
            g_rtc_set_reg6 = rtc_clamp_day(g_rtc_set_reg2, g_rtc_edit_de, g_rtc_set_reg6);
            rtc_write_datetime();
            settings_serial_yymm_from_clock();
            g_flag_80d6 = 1;
            g_flag_806e = 1;
        }
    } else {
        uint8_t days = rtc_days_in_month(g_rtc_set_reg2, g_rtc_set_reg4);

        if (key == SETUP_KEY_UP) {
            g_rtc_edit_df++;
            if (days < g_rtc_edit_df) {
                g_rtc_edit_df = 1;
            }
        } else if (key == SETUP_KEY_DOWN) {
            if (g_rtc_edit_df < 2) {
                g_rtc_edit_df = days;
            } else {
                g_rtc_edit_df--;
            }
        } else {
            g_rtc_set_reg6 = g_rtc_edit_df;
            g_rtc_set_reg6 = rtc_clamp_day(g_rtc_set_reg2, g_rtc_set_reg4, g_rtc_set_reg6);
            rtc_write_datetime();
            g_flag_80d6 = 0;
        }
    }
    g_disp_flag_8062 = 1;
}

/* orig: 0x08015E74 - hour 0..23, then (g_flag_80d6 set) minute 0..59 */
void setup_time_key(uint8_t key)
{
    if (g_flag_80d6 == 0) {
        if (key == SETUP_KEY_UP) {
            g_rtc_edit_e0++;
            if (g_rtc_edit_e0 > 23) {
                g_rtc_edit_e0 = 0;
            }
        } else if (key == SETUP_KEY_DOWN) {
            if (g_rtc_edit_e0 == 0) {
                g_rtc_edit_e0 = 23;
            } else {
                g_rtc_edit_e0--;
            }
        } else {
            g_rtc_set_reg10 = g_rtc_edit_e0;
            rtc_write_datetime();
            g_flag_80d6 = 1;
            g_flag_806e = 1;
        }
    } else if (key == SETUP_KEY_UP) {
        g_rtc_edit_e1++;
        if (g_rtc_edit_e1 > 59) {
            g_rtc_edit_e1 = 0;
        }
    } else if (key == SETUP_KEY_DOWN) {
        if (g_rtc_edit_e1 == 0) {
            g_rtc_edit_e1 = 59;
        } else {
            g_rtc_edit_e1--;
        }
    } else {
        g_rtc_set_reg12 = g_rtc_edit_e1;
        rtc_write_datetime();
        g_flag_80d6 = 0;
    }
    g_disp_flag_8062 = 1;
}

/* orig: 0x08015F7C - serial number: up/down change digit g_edit_digit (0..4, from the right) */
void setup_serial_key(uint8_t key)
{
    uint32_t digit[5];

    if (key == SETUP_KEY_STORE) {
        g_serial_number = g_edit_value;
        settings_save_serial_number();
    } else {
        digit[0] = g_edit_value % 10u;
        digit[1] = (g_edit_value % 100u) / 10u;
        digit[2] = (g_edit_value % 1000u) / 100u;
        digit[3] = (g_edit_value % 10000u) / 1000u;
        digit[4] = g_edit_value / 10000u;
        if (key == SETUP_KEY_UP) {
            digit[g_edit_digit]++;
            if (digit[g_edit_digit] > 9) {
                digit[g_edit_digit] = 0;
            }
        } else if (digit[g_edit_digit] == 0) {
            digit[g_edit_digit] = 9;
        } else {
            digit[g_edit_digit]--;
        }
        g_edit_value = digit[0] + digit[1] * 10u + digit[2] * 100u + digit[4] * 10000u
                       + digit[3] * 1000u;
    }
    g_disp_flag_8062 = 1;
}

/*
 * orig: 0x080165F4 - a key for the setup item shown; key 0 does nothing and
 * returns 1
 */
uint8_t setup_item_key(uint8_t key)
{
    uint8_t f = g_function;
    uint8_t idle = 0;

    g_edit_idle_ticks = 0;
    if (dual_function(f) && g_mode_8037 == 11) {
        if (key == 0) {
            idle = 1;
        } else {
            setup_bd_dual_key(key);
        }
    }

    if (va_function(g_function) && g_mode_8037 == 10) {
        if (key == 0) {
            idle = 1;
        } else {
            setup_bd_va_key(key);
        }
    } else if (g_function == 10 && g_mode_8037 == 10) {
        if (key == 0) {
            idle = 1;
        } else {
            setup_updn_key(key);
        }
    } else {
        void (*handler)(uint8_t);

        switch (g_mode_8037) {
        case 0:  handler = settings_temp_unit_key; break;
        case 2:  handler = setup_apo_key; break;
        case 3:  handler = setup_beep_key; break;
        case 4:  handler = setup_contrast_key; break;
        case 9:  handler = setup_log_interval_key; break;
        case 5:  handler = setup_year_key; break;
        case 6:  handler = setup_date_key; break;
        case 7:  handler = setup_time_key; break;
        case 8:  handler = setup_serial_key; break;
        default: handler = 0; break;
        }
        if (handler != 0) {
            if (key == 0) {
                idle = 1;
            } else {
                handler(key);
            }
        }
    }
    return idle;
}

static uint8_t rtc_field(uint8_t address, uint8_t bcd_max, uint8_t max, uint8_t fallback)
{
    uint8_t bcd = rtc_read_register(address);
    uint8_t value;

    if (bcd == 0 || bcd > bcd_max) {
        return fallback;
    }
    value = bcd_to_bin(bcd);
    return (value == 0 || value > max) ? fallback : value;
}

static uint8_t rtc_field0(uint8_t address, uint8_t bcd_limit, uint8_t max, uint8_t fallback)
{
    uint8_t bcd = rtc_read_register(address);
    uint8_t value;

    if (bcd >= bcd_limit) {
        return fallback;
    }
    value = bcd_to_bin(bcd);
    return (value > max) ? fallback : value;
}

/*
 * orig: 0x0800FF18 - read the clock from the RTC chip; implausible fields
 * become 2010-07-01 12:00:00
 */
void rtc_load_clock(void)
{
    g_rtc_set_reg2 = rtc_field(2, 0x99, 99, 0x10);
    delay_loop(5);
    g_rtc_set_reg4 = rtc_field(4, 0x12, 12, 7);
    delay_loop(5);
    g_rtc_set_reg6 = rtc_field(6, 0x31, 31, 1);
    delay_loop(5);
    g_rtc_set_reg10 = rtc_field0(10, 0x24, 23, 12);
    delay_loop(5);
    g_rtc_set_reg12 = rtc_field0(12, 0x5A, 59, 0);
    delay_loop(5);
    g_rtc_set_reg14 = rtc_field0(14, 0x5A, 59, 0);
}

/* orig: 0x08024DE0 - clock, calibration and settings loaded; modes reset */
void app_state_init(void)
{
    rtc_load_clock();
    g_flag_7fe0 = settings_uncalibrated();
    g_flag_8087 = settings_marker_1f_missing();
    cal_load(200, 0);
    settings_load_log_interval();
    settings_load_contrast();
    settings_load_apo();
    settings_load_temp_unit();
    settings_load_flag_80b3();
    settings_load_sel_80b6();
    settings_load_flag_80a8();
    settings_load_beep();
    settings_load_serial_yymm();
    settings_load_serial_number();
    mem_restore_counts();
    apo_reset();

    g_flag_803b = 0;
    g_range = 0;
    g_function = 0;
    g_flag_808a = 0;
    g_flag_808b = 0;
    secondary_mode_default();
    g_minmax_mode = 0;
    g_hold_active = 0;
    g_autohold_mode = 0;
    g_rel_active = 0;
    g_flag_80b7 = 0;
    g_flag_80b9 = 0;
    g_flag_80b3 = 0;
    g_flag_80b1 = 0;
    g_flag_80b2 = 0;
    g_flag_80ba = 0;
    g_flag_80c4 = 0;
    g_flag_80c5 = 0;
    g_rotary_pos = 0;
    g_rotary_raw = 0;
    if (g_resumed_from_power_off != 0) {
        g_key_hold_ticks = 51;
        g_key_lockout_ticks = 10;
    }
    g_input_event_pending = 0;
    g_input_event_code = 0;
    lcd_reapply_pulse_duration();
}
