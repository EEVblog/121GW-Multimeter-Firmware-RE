/*
 * Display state helpers used by measurement processing.
 */
#include "display_state.h"
#include "lcd_driver.h"
#include "measure_scale.h"

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

/* orig: 0x0800A560 */
void measure_display_reset(void)
{
    g_disp_eac = 0;
    g_disp_eb0 = 0;
    g_disp_flag_80ca = 0;
    g_disp_flag_80cb = 0;
    g_disp_flag_80bf = 1;
    g_disp_flag_80c0 = 1;
    g_disp_flag_80c1 = 1;
    g_new_reading = 1;
    g_disp_flag_8062 = 1;
    lcd_set_secondary_dp(0);
}
