/*
 * Display: periodic update of both readouts, the bar graph and the symbols.
 *
 * display_update() runs from the main loop and redraws whatever the
 * measurement code and the key handlers flagged: the main readout (new
 * reading), the secondary readout (g_disp_flag_8062), the bar graph
 * (g_scaled_ready) and the symbol groups, then refreshes the LCD once.
 */
#include "display_update.h"
#include "annunciators.h"
#include "buzzer.h"
#include "calibration.h"
#include "display_bargraph.h"
#include "display_state.h"
#include "display_symbols.h"
#include "display_text.h"
#include "dual_correction.h"
#include "hy3131_ctrl.h"
#include "lcd_driver.h"
#include "measure_derived.h"
#include "measure_funcs.h"
#include "measure_scale.h"
#include "mem_records.h"
#include "range_select.h"
#include "rtc.h"

static int dual_function(uint8_t f)
{
    return f == 13 || f == 14 || f == 15 || f == 22 || f == 23 || f == 24;
}

static int va_function(uint8_t f)
{
    return f == 16 || f == 18 || f == 20 || f == 17 || f == 19 || f == 21;
}

static int freq_va_function(uint8_t f)
{
    return f == 16 || f == 18 || f == 20;
}

/* orig: 0x0801725E - status symbols, without refreshing the LCD */
void sym_status_update_all(void)
{
    sym_w5_b7_update(0);
    sym_w7_b7_update(0);
    sym_w0_b31_update(0);
    sym_w7_b0_update(0);
    sym_w2_w4_b16_update(0);
    sym_b30_update(0);
    sym_w0_b0_update(0);
    sym_w6_b0_b16_update(0);
    sym_w2_b31_update();
    sym_w1_w3_w5_b0_update();
    sym_w1_b7_update();
    annunciator_temp_unit();
    sym_w0_b16_update(0);
}

/* orig: 0x08017222 - redraw all symbols, the unit symbols and an empty bar graph */
void display_redraw_symbols(void)
{
    g_flag_8060 = 0;
    sym_status_update_all();
    sym_function_units_clear();
    sym_function_units(g_function, g_range, g_flag_8098, g_flag_8099);
    bargraph_reset();
    g_display_dirty = 1;
}

/*
 * orig: 0x08016E08 - main readout for a new reading: status texts of the
 * averaging/calibration steps first (each shown once), then OFL or the
 * reading (one digit less above 5 digits with g_rel_active).
 */
void main_display_update(void)
{
    int32_t v;
    uint8_t dp;

    g_new_reading = 0;
    g_flag_805f = 0;
    if (g_function == 9 && g_manual_range == 0 && g_flag_808a == 0 && g_minmax_mode == 0 &&
        g_count_805a != 0) {
        g_count_805a--;
    } else if (g_flag_8076 != 0) {
        show_lead(0);
        g_flag_8046 = 0;
    } else if (dual_function(g_function) && g_disp_flag_80bf != 0) {
        show_dashes(0);
    } else if (g_avg_flag_808c != 0) {
        g_flag_8046 = 0;
        main_show_avg_count(0);
    } else if (g_avg_done_808d != 0) {
        g_avg_done_808d = 0;
        g_flag_8046 = 0;
        main_show_end_text(0);
    } else if (g_cal_flag_808e != 0) {
        g_flag_8046 = 0;
        main_show_avg_count(1);
    } else if (g_flag_8090 != 0) {
        g_flag_8046 = 0;
        main_show_avg_count(4);
    } else if (g_cal_done_808f != 0) {
        g_cal_done_808f = 0;
        g_flag_8046 = 0;
        main_show_end_text(1);
    } else if (g_flag_8091 != 0) {
        g_flag_8046 = 0;
        main_show_avg_count(2);
    } else if (g_flag_8092 != 0) {
        g_flag_8092 = 0;
        g_flag_8046 = 0;
        main_show_end_text(2);
    } else if (g_flag_8093 != 0) {
        g_flag_8046 = 0;
        main_show_avg_count(3);
    } else if (g_flag_8094 != 0) {
        g_flag_8094 = 0;
        g_flag_8046 = 0;
        main_show_end_text(3);
    } else if ((g_setting_238 != 0 || g_flag_805c != 0) && g_function != 0) {
        if (g_autohold_mode == 0) {
            show_blank(0);
        }
        g_flag_8046 = 0;
    } else if (g_hold_active == 0 && g_flag_8046 == 1) {
        show_ofl(0);
        g_flag_8046 = 0;
    } else {
        v = g_derived_value;
        if (v < 100000 && v > -100000) {
            show_main_number(g_function, g_main_dp, v, 1);
        } else if (g_rel_active != 0) {
            dp = g_main_dp;
            if (dp != 0) {
                dp--;
            }
            show_main_number(g_function, dp, v / 10, 1);
            g_sub_value = v;
        }
    }
}

/*
 * orig: 0x0801797C - secondary readout: SD card and battery messages,
 * logging count, setting edit screens, the calibration banner, and per
 * g_mode_8037 the auxiliary value (burden, dBm, frequency, dual readings,
 * diode range...). Mode 0 and unknown modes show the internal temperature.
 */
void secondary_display_update(void)
{
    uint8_t f;

    g_disp_flag_8062 = 0;
    if (g_flag_801d != 0) {
        show_sdend();
        return;
    }
    if (g_flag_801b != 0) {
        if (g_flag_801b == 20) {
            show_lobat();
        } else {
            show_sde_number(g_flag_801b);
        }
        return;
    }
    if (g_mode_8037 == 100) {
        if (g_logging_active != 0) {
            if (g_log_count < 100000u) {
                g_lcd_shadow[4] &= ~0x20000u;
                show_secondary_number_dp((int32_t)g_log_count, 0, 0);
            } else {
                g_lcd_shadow[4] |= 0x20000u;
                show_secondary_number_dp((int32_t)(g_log_count / 10u), 2, 0);
            }
        } else {
            show_secondary_number_dp(g_value_7f9c, 0, 0);
        }
        return;
    }
    if ((g_flag_806e & (g_flag_806f ^ 1u)) != 0) {
        show_edit_blink();
        return;
    }
    if (g_flag_808a != 0 && g_count_8097 == 0) {
        show_cal();
        return;
    }
    if (g_flag_808a != 0 && g_count_8097 != 0) {
        g_count_8097--;
        g_mode_8037 = 0;
    }

    f = g_function;
    switch (g_mode_8037) {
    case 12:
        if (dual_function(f) && g_flag_80a8 != 0) {
            show_burden_value((g_sel_80c7 != 0) ? g_disp_eb0 * 10 : g_disp_eb0 * 100, g_sel_80c7);
        } else if (freq_va_function(f) && g_flag_80a5 != 0) {
            show_freq_secondary();
        }
        break;
    case 11:
        if (f == 2) {
            show_dbm();
        } else if (dual_function(f)) {
            show_bd_setting(g_flag_80a9);
        } else if (va_function(f) && g_flag_80a5 != 0) {
            show_burden_value(g_va_aux, 100);
        } else if (freq_va_function(f) && g_flag_80a5 == 0) {
            show_freq_secondary();
        }
        break;
    case 10:
        if (dual_function(f)) {
            if (g_flag_80c2 == 0) {
                dp_from_sel_c6();
                if (g_disp_flag_80c0 != 0) {
                    show_dashes(1);
                } else if (g_disp_flag_80ca != 0 || g_disp_flag_80cb != 0) {
                    show_ofl(1);
                } else {
                    show_secondary_number(g_disp_eac);
                }
            } else {
                dp_for_dual();
                if (g_disp_flag_80c1 != 0) {
                    show_dashes(1);
                } else if (g_disp_flag_80cb != 0 || g_disp_flag_80ca != 0) {
                    show_ofl(1);
                } else {
                    show_secondary_number(g_disp_eb0);
                }
            }
        } else if (f == 11) {
            show_3_or_15();
        } else if (f == 10) {
            show_updn_setting(g_sel_80b6);
        } else if (va_function(f)) {
            show_bd_setting(g_flag_80a6);
        } else if ((f == 2 && g_flag_80ba == 0) || f == 4) {
            show_freq_secondary();
        } else {
            g_mode_8037 = 0;
            show_temperature(g_ambient_temp);
        }
        break;
    case 4:
        show_lcd_contrast(g_lcd_contrast);
        break;
    case 7:
        if (g_flag_806e != 0) {
            show_pair_blink(0, g_rtc_edit_e0, g_rtc_edit_e1);
        } else {
            show_pair_blink(0, g_rtc_set_reg10, g_rtc_set_reg12);
        }
        break;
    case 6:
        if (g_flag_806e != 0) {
            show_pair_dash(0, g_rtc_edit_de, g_rtc_edit_df);
        } else {
            show_pair_dash(0, g_rtc_set_reg4, g_rtc_set_reg6);
        }
        break;
    case 5:
        show_year(0, (g_flag_806e != 0) ? g_rtc_edit_dd : g_rtc_set_reg2);
        break;
    case 8:
        show_edit_value(1);
        break;
    case 9:
        show_in_number((g_flag_806e != 0) ? g_log_interval_edit : g_log_interval_s);
        break;
    case 2:
        show_apo_setting(g_apo_disabled);
        break;
    case 3:
        show_beep_setting(g_beep_enabled);
        break;
    case 1:
        g_lcd_shadow[2] |= 0x20000u;
        show_bat_number(g_bat_value);
        break;
    default:
        g_mode_8037 = 0;
        show_temperature(g_ambient_temp);
        break;
    }
}

/* orig: 0x08016D34 - redraw what was flagged, then refresh the LCD (not in the menu) */
void display_update(void)
{
    if (g_menu_active != 0) {
        return;
    }
    if (g_scaled_ready != 0) {
        bargraph_update();
    }
    if ((g_new_reading | g_flag_805f) != 0) {
        main_display_update();
    }
    if (g_flag_8060 != 0) {
        display_redraw_symbols();
    }
    if (g_flag_8061 != 0) {
        sym_range_marks_update();
    }
    if (g_flag_804c != 0) {
        sym_function_units(g_function, g_range, g_flag_8098, g_flag_8099);
        g_display_dirty = 1;
    }
    if (g_mode_8037_shown != g_mode_8037 || g_flag_8063 != 0) {
        g_mode_8037_shown = g_mode_8037;
        g_flag_8063 = 0;
        annunciators_secondary();
    }
    if (g_disp_flag_8062 != 0) {
        secondary_display_update();
    }
    if (g_display_dirty != 0) {
        g_display_dirty = 0;
        lcd_update();
    }
}

/* orig: 0x0801772E - symbols for a recalled MEM record (its function and range) */
void display_recall_symbols(void)
{
    g_recall_dp = main_dp_for_range(g_function_7feb, g_range_7fec);
    sym_status_update_all();
    sym_function_units_clear();
    sym_function_units(g_function_7feb, g_range_7fec, g_flag_7fe5, g_flag_7fe6);
    bargraph_off();
    lcd_update();
}

/* orig: 0x0800D6E0 - secondary decimal point for the dual readings, then redraw */
void secondary_dp_refresh(void)
{
    if (g_flag_80c2 == 0) {
        dp_from_sel_c6();
    } else {
        dp_for_dual();
    }
    g_count_7fd8 = 0;
    g_flag_8063 = 1;
    g_disp_flag_8062 = 1;
}

/* orig: 0x0800D664 - dual functions: swap the secondary reading every 300 calls */
void dual_secondary_alternate(void)
{
    uint8_t f = g_function;

    if (f == 13 || f == 14 || f == 15 || f == 22 || f == 23 || f == 24) {
        g_count_7fd8++;
        if (g_count_7fd8 > 299) {
            g_flag_80c2 = (g_flag_80c2 != 1);
            secondary_dp_refresh();
        }
    }
}
