/*
 * Secondary-display unit annunciators (bits in the LCD shadow, COM rows
 * 0..3 = g_lcd_shadow[0], [2], [4], [6]).
 */
#include "annunciators.h"
#include "calibration.h"
#include "ac_response.h"
#include "hy3131_ctrl.h"
#include "lcd_driver.h"
#include "measure_funcs.h"
#include "range_select.h"

#define SEG_17   (1u << 17)
#define SEG_18   (1u << 18)
#define SEG_11   (1u << 11)
#define SUB_UNIT_CLEAR_ROW0  0xFFF3FFFFu   /* segments 18, 19 */
#define SUB_UNIT_CLEAR_ROWS  0xDFF9FFFFu   /* segments 17, 18, 29 */

static int va_function(void)
{
    return g_function == 16 || g_function == 18 || g_function == 20
        || g_function == 17 || g_function == 19 || g_function == 21;
}

static int va_main_function(void)
{
    return g_function == 16 || g_function == 18 || g_function == 20;
}

/* orig: 0x0801A3A4 - degree C / F symbol (row 2 = C, row 3 = F) for function 5 */
void annunciator_temp_unit(void)
{
    if (g_function == 5) {
        if (g_temp_unit_c == 0) {
            g_lcd_shadow[4] &= ~SEG_11;
            g_lcd_shadow[6] |= SEG_11;
        } else {
            g_lcd_shadow[4] |= SEG_11;
            g_lcd_shadow[6] &= ~SEG_11;
        }
    } else {
        g_lcd_shadow[4] &= ~SEG_11;
        g_lcd_shadow[6] &= ~SEG_11;
    }
}

/* orig: 0x0801A6D8 - dual-readout functions: row 0 symbol (not for 15/24 in the upper range) */
void annunciator_dual(void)
{
    if (g_function == 14 || g_function == 13 || g_function == 23 || g_function == 22) {
        g_lcd_shadow[0] |= SEG_18;
    } else if ((g_function == 15 || g_function == 24) && g_sel_80c7 == 0) {
        g_lcd_shadow[0] |= SEG_18;
    }
    g_lcd_shadow[2] |= SEG_18;
}

/* orig: 0x0801A760 */
void annunciator_sub_clear(void)
{
    g_lcd_shadow[0] &= SUB_UNIT_CLEAR_ROW0;
    g_lcd_shadow[2] &= SUB_UNIT_CLEAR_ROWS;
    g_lcd_shadow[4] &= SUB_UNIT_CLEAR_ROWS;
    g_lcd_shadow[6] &= SUB_UNIT_CLEAR_ROWS;
}

/* Frequency unit: row 3, plus row 2 for decimals above 1. */
static void annunciator_freq(void)
{
    if (g_freq_decimal > 1) {
        g_lcd_shadow[4] |= SEG_17;
    }
    g_lcd_shadow[6] |= SEG_17;
}

/*
 * orig: 0x0801A440
 * Sets the secondary-display unit symbols for the current function and
 * secondary mode (g_mode_8037). Not in calibration; mode 100 clears them
 * only.
 */
void annunciators_secondary(void)
{
    if (g_flag_808a != 0) {
        return;
    }
    annunciator_sub_clear();
    if (g_mode_8037 == 100) {
        return;
    }
    if (va_function() && g_mode_8037 == 11 && g_flag_80a5 != 0) {
        g_lcd_shadow[0] |= SEG_18;
        g_lcd_shadow[2] |= SEG_17;
    } else if (g_mode_8037 == 12) {
        if (va_main_function() && g_flag_80a5 != 0) {
            annunciator_freq();
        }
    } else if (g_mode_8037 == 11) {
        if (g_function == 2) {
            g_lcd_shadow[0] |= SEG_18 | (SEG_18 << 1);
        } else if (va_main_function() && g_flag_80a5 == 0) {
            annunciator_freq();
        }
    } else if (g_mode_8037 == 10) {
        if (g_function == 11) {
            g_lcd_shadow[2] |= SEG_17;
            annunciator_temp_unit();
        } else if (g_function == 13 || g_function == 14 || g_function == 15
                   || g_function == 22 || g_function == 23 || g_function == 24) {
            if (g_flag_80c2 == 0) {
                g_lcd_shadow[2] |= SEG_17;
            } else {
                annunciator_dual();
            }
        } else if (g_function == 10) {
            g_lcd_shadow[6] |= SEG_18;
        } else if (g_function == 4 || (g_function == 2 && g_flag_80ba == 0)) {
            annunciator_freq();
        } else {
            annunciator_temp_unit();
        }
    } else if (g_mode_8037 == 1) {
        g_lcd_shadow[2] |= SEG_17;
    } else if (g_mode_8037 != 2) {
        annunciator_temp_unit();
    }
    g_display_dirty = 1;
}
