/*
 * Start-up screen and the main loop.
 */
#include "app_globals.h"
#include "battery.h"
#include "ble_command.h"
#include "ble_packet.h"
#include "buzzer.h"
#include "calibration.h"
#include "display_state.h"
#include "display_symbols.h"
#include "display_text.h"
#include "display_update.h"
#include "front_end.h"
#include "hy3131_bus.h"
#include "hy3131_ctrl.h"
#include "irq.h"
#include "key_handlers.h"
#include "keys.h"
#include "lcd_driver.h"
#include "mem_records.h"
#include "dual_correction.h"
#include "measure_counter.h"
#include "measure_derived.h"
#include "measure_funcs.h"
#include "measure_scale.h"
#include "modes.h"
#include "power.h"
#include "range_select.h"
#include "rtc.h"
#include "sd_card.h"
#include "sd_log.h"
#include "settings.h"
#include "setup_menu.h"
#include "system_init.h"
#include "stm32l1xx.h"

#define STARTUP_TICKS    2000u     /* TIM3 ticks (~2 s) the version screen stays */
#define APO_WARN_SECONDS 60u

/*
 * orig: 0x0800EA3A - version screen with the start-up key combinations:
 * MODE (CAL, confirmed with REL: calibration mode), MEM (SdCAL, then REL
 * save or PEAK restore of the calibration backup, confirmed with MODE),
 * SETUP (serial prefix from the clock), RANGE (all segments)
 */
void startup_screen(void)
{
    front_end_pd1_set(0, 0);
    g_flag_80b3 = 0;
    front_end_pe15_pf6(0);
    front_end_pa11(0);
    front_end_ph2(0);
    front_end_pa12(0);
    front_end_pe12(0);
    front_end_pe5(0, 0);
    show_version();
    lcd_update();
    g_cfg_022e = STARTUP_TICKS;
    hy3131_configure_mode(5, 0);

    for (;;) {
        if (g_cfg_022e == 0 && g_flag_80ce != 4) {
            break;
        }
        if (g_task_flag_a == 0) {
            continue;
        }
        g_task_flag_a = 0;
        keys_task();
        if (g_startup_key != 0) {
            if (g_startup_key == 1) {
                show_cal1();
                show_cal();
                lcd_update();
            } else if (g_startup_key == 2) {
                g_flag_808a = 1;
                g_disp_flag_8062 = 1;
                cal_extra_clear_all();
                g_cfg_022e = 0;
                break;
            } else if (g_startup_key == 3) {
                g_flag_808b = 1;
                g_disp_flag_8062 = 1;
                g_rtc_edit_dd = g_rtc_set_reg2;
                g_cfg_022e = 0;
                break;
            }
        } else if (g_flag_80ce == 1) {
            show_sd_cal();
        } else if (g_flag_80ce == 2) {
            if (g_cal_save_selected != 0) {
                show_sd_save();
            } else if (g_cal_restore_selected != 0) {
                show_sd_set();
            }
        } else if (g_flag_80ce == 3) {
            if (g_cal_save_selected != 0) {
                if (cal_backup_save() != 0) {
                    show_error();
                } else {
                    show_saved();
                }
                g_flag_80ce = 4;
            } else if (g_cal_restore_selected != 0) {
                if (cal_backup_restore() != 0) {
                    show_error();
                } else {
                    show_saved();
                }
                g_flag_80ce = 4;
            }
        }
    }
    g_flag_80ce = 0;
    g_startup_key = 0;
    g_cfg_022e = 0;
    g_cal_restore_selected = 0;
    g_cal_save_selected = g_cal_restore_selected;
    lcd_clear_shadow();
    lcd_update();
}

/* once a second: clock, logging interval, auto power-off */
static void every_second(void)
{
    rtc_load_clock();
    g_flag_7ffa = 0;
    if (g_logging_active != 0 && g_log_interval_s == 0 && g_autohold_mode == 0) {
        g_log_tick_count = 0;
    }
    if (g_logging_active != 0 && g_log_interval_s != 0 && g_autohold_mode == 0) {
        g_log_tick_count++;
        if (g_log_interval_s <= g_log_tick_count) {
            g_log_tick_count = 0;
            g_log_due = 1;
        }
    }
    if (g_apo_disabled == 0 && g_minmax_mode == 0 && g_logging_active == 0) {
        g_apo_seconds_left--;
        if (g_apo_seconds_left == 0) {
            g_power_off_request = 1;
        } else if (g_apo_seconds_left == APO_WARN_SECONDS) {
            g_apo_warn_request = 1;
        }
    }
    if (g_apo_warn_request != 0) {
        g_apo_warn_request = 0;
        g_apo_warn_beeps = 5;
    }
    if (g_apo_warn_beeps != 0) {
        buzzer_start_forced(500);
        g_apo_warn_beeps--;
    }
    g_apo_warning_active = g_apo_seconds_left < APO_WARN_SECONDS;
    if (g_apo_warning_active != 0) {
        sym_w5_b7_blink();
    }
}

/* orig: 0x08024B40; inlines: 0x080208E0 (NVIC_SetVectorTable) */
int main(void)
{
    SCB->VTOR = (0x6000u & 0x1FFFFF80u) | FLASH_BASE;
    system_init();
    detect_pf8_low();
    app_state_init();
    lcd_init(g_lcd_contrast, 1);
    startup_screen();

    for (;;) {
        hy3131_poll();
        if (g_new_reading != 0) {
            measure_postprocess();
            if (g_function == 0) {
                lowz_detect();
            }
            g_flag_8061 = 1;
        }
        display_update();
        if (g_task_flag_a != 0) {
            g_task_flag_a = 0;
            rotary_task();
            keys_task();
            input_jack_check();
            counter_preset_alternate();
            dual_secondary_alternate();
            rtc_poll_second();
        }
        if (g_task_flag_b != 0) {
            g_task_flag_b = 0;
            rtc_load_clock();
            adc_task();
            log_capture_values();
        }
        if (g_task_flag_c != 0) {
            g_task_flag_c = 0;
        }
        if (g_flag_802c != 0) {
            g_flag_802c = 0;
            if (g_ble_enabled != 0) {
                ble_send_reading();
            }
        }
        if (g_task_flag_d != 0) {
            g_task_flag_d = 0;
            if (g_flag_806e != 0) {
                setting_edit_timeout();
            }
        }
        if (g_task_flag_1s != 0) {
            g_task_flag_1s = 0;
            every_second();
        }
        if ((uint8_t)(g_logging_active & g_log_due) != 0) {
            mem_log_store();
        }
        if (g_ble_rx_ready != 0) {
            ble_handle_command();
        }
        if (g_flag_801c != 0) {
            if (log_flush_retry() != 0) {
                g_logging_active = 0;
                sd_power_off();
                menu_exit();
            }
            g_flag_801c = 0;
        }
        if (g_flag_804a != 0 || g_flag_804b != 0) {
            function_change(1);
            if (g_function != 6 && g_function != 7 && g_function != 8 && g_function != 12
                && g_autohold_mode != 0) {
                g_new_reading = 0;
                g_flag_8060 = 0;
                g_disp_flag_8062 = 0;
                g_autohold_force = 1;
            }
        }
        if (g_power_off_request != 0) {
            power_off();
        }
    }
}
