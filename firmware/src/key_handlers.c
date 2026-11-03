/*
 * Front-panel key actions.
 *
 * Key codes (one bit per key, PG8..PG15):
 *   0x01 MODE   0x02 SETUP   0x04 MIN/MAX   0x08 MEM
 *   0x10 RANGE  0x20 HOLD    0x40 REL       0x80 PEAK
 * Long presses also accept MEM+HOLD (0x28).
 *
 * While the start-up screen is shown (g_cfg_022e running) some keys select
 * the start-up actions instead: calibration mode, SD calibration backup
 * save/restore, segment test, set the serial prefix from the clock.
 */
#include "key_handlers.h"
#include "app_globals.h"
#include "ble_command.h"
#include "ble_packet.h"
#include "buzzer.h"
#include "calibration.h"
#include "display_state.h"
#include "display_symbols.h"
#include "display_text.h"
#include "display_update.h"
#include "dual_correction.h"
#include "board_io.h"
#include "hy3131_ctrl.h"
#include "irq.h"
#include "system_init.h"
#include "lcd_driver.h"
#include "measure_counter.h"
#include "measure_derived.h"
#include "measure_funcs.h"
#include "measure_peak.h"
#include "measure_scale.h"
#include "mem_records.h"
#include "modes.h"
#include "power.h"
#include "range_select.h"
#include "sd_log.h"
#include "settings.h"
#include "setup_menu.h"
#include "temperature.h"

#define BACKLIGHT_PIN  8u      /* PE8 */

/* orig: 0x0800C51A - BLE module on or off as g_ble_enabled says, and its symbol */
void ble_power(void)
{
    if (g_ble_enabled == 0) {
        ble_uart_off();
    } else {
        ble_uart_on();
    }
    sym_w0_b31_update(1);
}

/* MEM key (also reached from SETUP in calibration mode, as in the original) */
static void key_mem(void)
{
    if (g_cfg_022e != 0) {
        if (g_startup_key == 0 && g_flag_80ce == 0) {
            g_flag_80ce = 1;
            g_cfg_022e = 5000;
            beep_short();
        }
        return;
    }
    if (g_flag_808a != 0) {
        if ((uint8_t)(g_avg_flag_808c | g_cal_flag_808e) != 0) {
            beep_triple();
        } else if (range_fixed_function(1) != 0) {
            beep_triple();
        } else if (cal_start(1) == 2) {
            beep_triple();
        } else {
            beep_short();
        }
        return;
    }
#if APP_FIX_BUGS
    /* not while a setting is edited: recall would open over the edit and
     * REL/PEAK would keep stepping the setting instead of the records */
    if (g_logging_active != 0 || g_flag_806e != 0) {
#else
    if (g_logging_active != 0) {
#endif
        beep_triple();
        return;
    }
    if (g_mem_count == 0) {
        beep_triple();
        return;
    }
    g_menu_active = (g_menu_active != 1);
    if (g_menu_active != 0) {
        g_menu_idle_ticks = 0;
        g_mode_8037 = 100;
        g_value_7f9c = g_mem_last;
        lcd_set_secondary_dp(0);
        mem_restore_function();
        display_recall_symbols();
        mem_recall_show(g_value_7f9c);
        mem_clock_restore_a();
        mem_clock_restore_b();
    } else {
        menu_exit();
    }
    sym_w0_b16_update(1);
    beep_short();
}

/* REL key: on (reference = next reading, the held value or the auto-hold value) / off */
static void key_rel(void)
{
    if (g_cfg_022e == 0) {
        if (g_flag_806e != 0) {
            (void)setup_item_key(SETUP_KEY_UP);
        } else if (g_menu_active != 0) {
            g_value_7f9c++;
            if (g_mem_span < g_value_7f9c) {
                g_value_7f9c = 1;
            }
            g_menu_idle_ticks = 0;
            mem_recall_show(g_value_7f9c);
        } else {
            if (g_function == 0) {
                beep_triple();
                return;
            }
#if APP_FIX_BUGS
            /* REL off in HOLD showed the held relative value as absolute
             * and restored the manual-range state under HOLD, so leaving
             * HOLD left the range manual: not allowed, as RANGE in HOLD */
            if (g_rel_active != 0 && g_hold_active != 0) {
                beep_triple();
                return;
            }
#endif
            if ((uint8_t)(g_flag_8048 & (g_rel_active ^ 1u)) == 0) {
                g_rel_active = (g_rel_active != 1);
                if (g_rel_active != 0) {
                    g_manual_range_saved_rel = g_manual_range;
                    g_manual_range = 1;
                    if (g_hold_active != 0) {
                        g_rel_reference = g_hold_value;
                        show_main_number(g_function, g_main_dp, 0, 0);
                    } else if (g_autohold_mode != 0) {
                        g_rel_reference = g_autohold_captured;
                        show_main_number(g_function, g_main_dp, 0, 0);
                    } else {
                        g_rel_capture = 1;
                    }
                } else {
                    g_manual_range = (g_manual_range_saved_rel != 0);
                    g_rel_reference = 0;
                    if (g_hold_active != 0) {
                        show_main_number(g_function, g_main_dp, g_hold_value, 0);
                    } else if (g_autohold_mode != 0) {
                        g_autohold_value = g_autohold_captured;
                        show_main_number(g_function, g_main_dp, g_autohold_captured, 0);
                    }
                }
            }
            sym_w7_b0_update(0);
            g_flag_8053 = 1;
            sym_w0_b0_update(1);
        }
    } else if (g_startup_key == 1 && g_flag_80ce == 0) {
        g_startup_key = 2;
        g_cfg_022e = 500;
    } else if (g_startup_key == 0 && g_flag_80ce == 1) {
        g_flag_80ce = 2;
        g_cal_save_selected = 1;
        g_cfg_022e = 5000;
    }
    beep_short();
}

/* HOLD key: hold; pressed again, auto-hold (or off for functions 6, 7, 8, 12 and in MIN/MAX) */
static void key_hold(void)
{
    if (g_logging_active != 0 || g_menu_active != 0) {
        beep_triple();
        return;
    }
    if (g_hold_active == 0 && g_autohold_mode == 0) {
        g_hold_active = 1;
        g_hold_value = g_sub_value;
        if (g_minmax_mode == 0) {
            g_manual_range_saved = g_manual_range;
            g_manual_range = 1;
            sym_w7_b0_update(0);
        } else {
            g_hold_value = g_minmax_last;
        }
    } else if (g_hold_active == 0) {
        g_hold_active = 0;
        g_autohold_mode = 0;
    } else {
        uint8_t f = g_function;

        if (f == 6 || f == 7 || f == 8 || f == 12 || g_minmax_mode != 0) {
            g_hold_active = 0;
            g_autohold_mode = 0;
        } else {
            autohold_mode_set(1, 0);
        }
        if (g_minmax_mode == 0) {
            g_manual_range = g_manual_range_saved;
            sym_w7_b0_update(0);
        }
    }
    sym_w7_b0_update(0);
    sym_w2_w4_b16_update(1);
    beep_short();
}

/* PEAK key: functions 2 and 4 toggle peak capture (manual range); in the menu/setup it steps down */
static void key_peak(void)
{
    if (g_cfg_022e != 0 && g_startup_key == 0 && g_flag_80ce == 1) {
        g_flag_80ce = 2;
        g_cal_restore_selected = 1;
        g_cfg_022e = 5000;
    }
    if (g_flag_806e != 0) {
        (void)setup_item_key(SETUP_KEY_DOWN);
        beep_short();
        return;
    }
    if (g_menu_active != 0) {
        if (g_value_7f9c == 1) {
            g_value_7f9c = g_mem_span;
        } else {
            g_value_7f9c--;
        }
        mem_recall_show(g_value_7f9c);
        g_menu_idle_ticks = 0;
        beep_short();
        return;
    }
    if ((g_function != 2 || g_flag_80ba != 0) && g_function != 4) {
        beep_triple();
        return;
    }
#if APP_FIX_BUGS
    /* PEAK sets the function up again: that ended logging, and in HOLD
     * blanked the readout with HOLD still lit. Refused, as MODE in logging
     * and RANGE in HOLD */
    if (g_logging_active != 0 || g_hold_active != 0) {
        beep_triple();
        return;
    }
#endif
    g_flag_80b7 ^= 1u;
    g_peak_select = 0;
    g_flag_804a = 1;
    g_manual_range = (g_flag_80b7 != 0);
    if (g_manual_range != 0) {
        g_minmax_mode = 0;
    }
    g_peak_max = 0;
    g_peak_min = 0;
    sym_w6_b0_b16_update(1);
    beep_short();
}

/* orig: 0x08014C3C */
void key_short_press_handler(uint8_t key)
{
    switch (key) {
    case 0x01:                                          /* MODE */
        if (g_logging_active != 0 || g_menu_active != 0) {
            beep_triple();
            return;
        }
        if (g_cfg_022e == 0) {
            mode_key();
        } else if (g_startup_key == 0 && g_flag_80ce == 0) {
            g_startup_key = 1;
            g_cfg_022e = 2000;
        } else if (g_startup_key == 0 && g_flag_80ce == 2) {
            g_cfg_022e = 5000;
            g_flag_80ce = 3;
            return;
        }
        beep_short();
        return;

    case 0x02:                                          /* SETUP */
        if (g_cfg_022e != 0) {
            if (g_startup_key == 0 && g_flag_80ce == 0) {
                g_cfg_022e = 500;
                g_startup_key = 3;
            }
            return;
        }
        if (g_logging_active != 0 || g_menu_active != 0) {
            beep_triple();
            return;
        }
        if (g_flag_808a == 0) {
            secondary_mode_next();
            beep_short();
            g_disp_flag_8062 = 1;
            return;
        }
        if (range_fixed_function(0) != 0) {
            beep_triple();
            return;
        }
        if (cal_start(0) == 2) {
            beep_triple();
            return;
        }
        key_mem();
        return;

    case 0x04:                                          /* MIN/MAX */
        if (g_logging_active != 0 || g_menu_active != 0 || g_flag_808a != 0 || g_function == 0) {
            beep_triple();
            return;
        }
        if ((g_hold_active == 0 || g_minmax_mode != 0) && g_autohold_mode == 0 && g_flag_80b7 == 0) {
            minmax_key();
            beep_short();
        } else {
            beep_triple();
        }
        return;

    case 0x08:                                          /* MEM */
        key_mem();
        return;

    case 0x10:                                          /* RANGE */
        if (g_cfg_022e == 0) {
            if (g_logging_active != 0 || g_menu_active != 0 || g_minmax_mode != 0) {
                beep_triple();
            } else if (g_function == 11) {
                flag_80b3_toggle();
                beep_short();
            } else {
                range_key();
            }
            return;
        }
        if (g_startup_key == 0 && g_flag_80ce == 0) {
            g_cfg_022e = 2000;
            lcd_all_segments_on();
            lcd_update();
        }
        return;

    case 0x20:                                          /* HOLD */
        key_hold();
        return;

    case 0x40:                                          /* REL */
        key_rel();
        return;

    case 0x80:                                          /* PEAK */
        key_peak();
        return;

    default:
        return;
    }
}

/* orig: 0x0801622C */
void key_long_press_handler(uint8_t key)
{
    switch (key) {
    case 0x01:                                          /* MODE: backlight */
        if (g_backlight_on != 0) {
            gpio_reset(GPIOE, BACKLIGHT_PIN);
        } else {
            g_backlight_ticks = 120;
            gpio_set(GPIOE, BACKLIGHT_PIN);
        }
        g_backlight_on = (g_backlight_on == 0);
        beep_long();
        break;

    case 0x02:                                          /* SETUP: edit the setting shown / store it */
        if (g_logging_active != 0 || g_menu_active != 0) {
            beep_triple();
        } else if (setup_item_key(0) == 0) {
            beep_triple();
        } else {
            g_flag_806e = (g_flag_806e != 1);
            g_edit_idle_ticks = 0;
            g_edit_digit = 4;
            beep_long();
            if (g_flag_806e == 0) {
                (void)setup_item_key(SETUP_KEY_STORE);
            }
        }
        break;

    case 0x04:                                          /* MIN/MAX: off */
        if (g_flag_808a != 0 || g_minmax_mode == 0) {
            beep_triple();
        } else {
            g_minmax_mode = 4;
            minmax_show();
            if (g_hold_active == 0) {
                g_manual_range = g_manual_range_saved;
                sym_w7_b0_update(0);
            }
            g_minmax_mode = 0;
            sym_b30_update(0);
            sym_w5_b7_update(0);
            beep_long();
        }
        break;

    case 0x08:                                          /* MEM: logging; calibration: temperature/zero step */
        if (g_flag_808a == 0) {
            log_toggle(0);
        } else {
            if (g_function == 9 && g_range == 6) {
                g_flag_8090 = 1;
                g_avg_count = 16;
            } else {
                g_cal_temp_request = 1;
            }
            beep_long();
        }
        break;

    case 0x10:                                          /* RANGE: back to auto range */
        if (g_logging_active != 0 || g_menu_active != 0) {
            beep_triple();
        } else {
            if (g_flag_808a == 0) {
                g_manual_range = 0;
                g_range = 0;
                dual_rows_for_range(g_function, 0);
                g_flag_804b = 1;
            }
            sym_w7_b0_update(1);
            beep_long();
        }
        break;

    case 0x28:                                          /* MEM+HOLD: logging in auto-hold mode */
        if (g_flag_808a == 0) {
            log_toggle(1);
        } else {
            beep_triple();
        }
        break;

    case 0x40:                                          /* REL: 1 kHz filter (functions 2, 4) / VA zero offset */
        if (g_logging_active != 0 || g_menu_active != 0 || g_flag_806e != 0) {
            beep_triple();
        } else if (g_function == 2 || g_function == 4) {
            if (g_flag_80ba == 0) {
                g_flag_80b9 ^= 1u;
                g_flag_80ba = 0;
                g_flag_804a = 1;
                beep_short();
            } else {
                beep_triple();
            }
        } else if ((g_function == 16 || g_function == 18 || g_function == 20 || g_function == 17
                    || g_function == 19 || g_function == 21) && g_range == 0) {
            g_cal_extra_offset = g_derived_value + g_cal_extra_offset;
            cal_extra_store(g_function, 0, (uint32_t)g_derived_value);
            beep_long();
        } else {
            beep_triple();
        }
        break;

    case 0x80:                                          /* PEAK: BLE on/off */
        if (g_flag_806e == 0) {
            g_ble_enabled = (g_ble_enabled != 1);
            g_clock_set_by_ble = 0;
            g_input_event_pending = 0;
            g_input_event_code = 0;
            ble_power();
            g_ble_history_index = 0;
            beep_long();
        } else {
            beep_triple();
        }
        break;

    default:
        beep_triple();
        break;
    }
}
