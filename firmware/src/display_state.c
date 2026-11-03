/*
 * Display state helpers used by measurement processing.
 */
#include "display_state.h"
#include "lcd_driver.h"
#include "measure_scale.h"
#if APP_FIX_BUGS
#include "app_globals.h"
#include "display_update.h"
#endif

/* orig: 0x08017700 - COM1 SEG10 for functions 15 and 24: on for ranges 0 and 1 */
void lcd_bar_flag_dual(uint8_t function, uint8_t range)
{
    if (function == 24 || function == 15) {
        if (range < 2) {
            g_lcd_shadow[2] |= 0x400u;
        } else {
            g_lcd_shadow[2] &= ~0x400u;
        }
    }
}

/*
 * orig: 0x0800A560 - after a function/range change: clear the dual readouts
 * and redraw. The original sets g_new_reading, so the main loop
 * post-processes the zeroed g_derived_value as if it were a reading: it
 * uses up one of the g_setting_238 skipped readings, runs the voltage
 * warnings and feeds a quiet reading to lowz_detect(). With APP_FIX_BUGS
 * only a redraw is requested (main readout unless HOLD or auto-hold, as
 * function_change() does; range marks unless auto-hold, as the main loop
 * does after a reading).
 */
void measure_display_reset(void)
{
    g_disp_eac = 0;
    g_disp_eb0 = 0;
    g_disp_flag_80ca = 0;
    g_disp_flag_80cb = 0;
    g_disp_flag_80bf = 1;
    g_disp_flag_80c0 = 1;
    g_disp_flag_80c1 = 1;
#if APP_FIX_BUGS
    if (g_autohold_mode == 0) {
        g_flag_8061 = 1;
        if (g_hold_active == 0) {
            g_flag_805f = 1;
        }
    }
#else
    g_new_reading = 1;
#endif
    g_disp_flag_8062 = 1;
    lcd_set_secondary_dp(0);
}
