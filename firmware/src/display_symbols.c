/*
 * Display: 7-segment font and individual LCD symbol setters.
 *
 * The LCD glass map is not known yet, so symbol setters are named by the
 * shadow word and segment bit they drive (g_lcd_shadow[2n] = COM row n)
 * and the flag they show. Each optionally refreshes the LCD.
 */
#include "display_symbols.h"
#include "buzzer.h"
#include "calibration.h"
#include "hy3131_ctrl.h"
#include "lcd_driver.h"
#include "measure_derived.h"
#include "system_init.h"
#include "temperature.h"
#include "measure_funcs.h"
#include "measure_peak.h"
#include "dual_correction.h"
#include "ac_response.h"

#define REFRESH 1u

/* orig: 0x08029184, 0x080290B8, 0x080290D4 - segment codes for '0'..'9', 'A'..'Z', 'a'..'z' */
static const uint8_t font_digit[10] = {
    0xBB, 0x11, 0xA7, 0x97, 0x1D, 0x9E, 0xBE, 0x1B, 0xBF, 0x9F
};
static const uint8_t font_upper[26] = {
    0x3F, 0xBC, 0xAA, 0xB5, 0xAE, 0x2E, 0x9F, 0x3D, 0x11, 0xB1, 0x2D, 0xA8, 0xBF,
    0x3B, 0xBB, 0x2F, 0x1F, 0x2A, 0x9E, 0xAC, 0xB9, 0xB9, 0xB9, 0x1C, 0x9D, 0x86
};
static const uint8_t font_lower[26] = {
    0x3F, 0xBC, 0xA4, 0xB5, 0xAE, 0x2E, 0x9F, 0x3C, 0x10, 0xB1, 0x2D, 0xA8, 0xBF,
    0x34, 0xB4, 0x2F, 0x1F, 0x24, 0x9E, 0xAC, 0xB0, 0xB0, 0xB0, 0x1C, 0x9D, 0x86
};

/* orig: 0x0801AFFC - 7-segment code for a character (unknown characters are blank) */
uint8_t char_to_segments(uint8_t c)
{
    if (c >= '0' && c <= '9') {
        return font_digit[c - '0'];
    }
    if (c >= 'A' && c <= 'Z') {
        return font_upper[c - 'A'];
    }
    if (c >= 'a' && c <= 'z') {
        return font_lower[c - 'a'];
    }
    switch (c) {
    case '-':  return 0x04;
    case '=':  return 0x82;
    case 0xFF: return 0xBF;   /* all segments */
    default:   return 0x00;
    }
}

static void maybe_refresh(uint8_t refresh)
{
    if (refresh == REFRESH) {
        lcd_update();
    }
}

static void set_bits(volatile uint32_t *word, uint32_t bits, int on)
{
    if (on) {
        *word |= bits;
    } else {
        *word &= ~bits;
    }
}

/* orig: 0x0801801E - decimal point from range bit 0x200080C6 */
void dp_from_sel_c6(void)
{
    g_dp_position = (g_sel_80c6 == 0) ? 4 : 3;
}

/* orig: 0x0801803C - decimal point for the dual-readout functions */
void dp_for_dual(void)
{
    if (g_function == 13 || g_function == 22) {
        g_dp_position = 4;
    } else if (g_function == 14 || g_function == 23) {
        g_dp_position = (g_sel_80c7 == 0) ? 4 : 3;
    } else {
        g_dp_position = (g_sel_80c7 == 0) ? 2 : 3;
    }
}

/* orig: 0x08018102 - decimal point position for a range (0 for ranges above 4) */
uint8_t dp_for_range(uint8_t range)
{
    switch (range) {
    case 0: return 2;
    case 1: return 1;
    case 2: return 3;
    case 3: return 2;
    case 4: return 1;
    default: return 0;
    }
}

/*
 * orig: 0x08018AD8
 * Temperature for display: 0.01 C (as int16) -> 0.1 C or F, with the same
 * last-digit rounding quirk as ad1_process_func5().
 */
int32_t temp_for_display(int32_t c100)
{
    int16_t t = (int16_t)c100;   /* the original truncates its argument to 16 bits */

    if (g_temp_unit_c == 0) {
        t = (int16_t)celsius_to_fahrenheit(t);
    }
    t = (int16_t)(t / 10);
    if (t % 10 > 4) {
        t = (int16_t)(t + 1);
    }
    return t;
}

/* orig: 0x0801A370 - shadow[1] bit 7 from 0x200080B1 */
void sym_w1_b7_update(void)
{
    set_bits(&g_lcd_shadow[1], 0x80u, g_flag_80b1 != 0);
}

/* orig: 0x08019CF0 - shadow[7] bit 7 from 0x2000803B */
void sym_w7_b7_update(uint8_t refresh)
{
    set_bits(&g_lcd_shadow[7], 0x80u, g_flag_803b != 0);
    maybe_refresh(refresh);
}

/* orig: 0x08019D2C - shadow[0] bit 31 from 0x2000802B */
void sym_w0_b31_update(uint8_t refresh)
{
    set_bits(&g_lcd_shadow[0], 0x80000000u, g_flag_802b != 0);
    maybe_refresh(refresh);
}

/* orig: 0x080177AC - range bar: shadow[0]/[2] bit 9 by range (< 2), shadow[4] bit 9 on */
void sym_b9_for_range(uint8_t range)
{
    if (range < 2) {
        g_lcd_shadow[0] &= ~0x200u;
        g_lcd_shadow[2] |= 0x200u;
    } else {
        g_lcd_shadow[0] |= 0x200u;
        g_lcd_shadow[2] &= ~0x200u;
    }
    g_lcd_shadow[4] |= 0x200u;
}

/* shadow[0] bits 8/11 by range: none below 2, bit 8 for 2..4, bit 11 from 5 */
static void sym_w0_b8_b11_for_range(uint8_t range)
{
    if (range < 5) {
        if (range < 2) {
            g_lcd_shadow[0] &= 0xFFFFF6FFu;
        } else {
            g_lcd_shadow[0] = (g_lcd_shadow[0] & 0xFFFFF7FFu) | 0x100u;
        }
    } else {
        g_lcd_shadow[0] = (g_lcd_shadow[0] & 0xFFFFFEFFu) | 0x800u;
    }
}

/* orig: 0x080177F2 - with shadow[2] bit 8 */
void sym_b8_b11_row1(uint8_t range)
{
    sym_w0_b8_b11_for_range(range);
    g_lcd_shadow[2] |= 0x100u;
}

/* orig: 0x08017858 - with shadow[4] bit 8 */
void sym_b8_b11_row2(uint8_t range)
{
    sym_w0_b8_b11_for_range(range);
    g_lcd_shadow[4] |= 0x100u;
}

/* orig: 0x0801A32A - shadow[0] bit 16 while logging (with 0x20007FE3) or in the menu */
void sym_w0_b16_update(uint8_t refresh)
{
    set_bits(&g_lcd_shadow[0], 0x10000u,
             (uint8_t)(g_logging_active & g_flag_7fe3) != 0 || g_menu_active != 0);
    maybe_refresh(refresh);
}

/* orig: 0x0801A028 - 0, or 1 / 2 for the partial-update and 0x2000804F states outside logging/menu */
uint8_t display_partial_mode(void)
{
    if ((uint8_t)(g_logging_active & (g_flag_8050 ^ 1u)) != 0 || g_menu_active != 0) {
        return 0;
    }
    if (g_flag_804f != 0) {
        return 2;
    }
    return (g_flag_8050 != 0) ? 1 : 0;
}

/* orig: 0x0801A110 - shadow[2] bit 31 from g_flag_80b9 (in the menu: 0x20007FE7) */
void sym_w2_b31_update(void)
{
    uint8_t on = (g_menu_active == 0) ? g_flag_80b9 : g_flag_7fe7;

    set_bits(&g_lcd_shadow[2], 0x80000000u, on != 0);
}

/* orig: 0x08019C5C - shadow[5] bit 7: on unless 0x20008039, logging or long APO (not during the APO warning) */
void sym_w5_b7_update(uint8_t refresh)
{
    if (g_apo_warning_active != 0) {
        return;
    }
    set_bits(&g_lcd_shadow[5], 0x80u, g_apo_disabled == 0 && g_logging_active == 0 && g_apo_long == 0);
    maybe_refresh(refresh);
}

/* orig: 0x08019F2E - shadow[0] bit 0 from 0x20008058 (off while logging or in the menu) */
void sym_w0_b0_update(uint8_t refresh)
{
    set_bits(&g_lcd_shadow[0], 0x1u,
             g_logging_active == 0 && g_menu_active == 0 && g_flag_8058 != 0);
    maybe_refresh(refresh);
}

/* orig: 0x08019DA2 - 1 when auto-range applies (outside calibration/logging, not functions 0, 5, 8, 10, 11, auto) */
uint8_t autorange_shown(void)
{
    if (g_flag_808a != 0) {
        return 0;
    }
    if ((uint8_t)(g_logging_active & (g_flag_8050 ^ 1u)) != 0) {
        return 0;
    }
    if (g_function == 11 || g_function == 10 || g_function == 5 || g_function == 0 || g_function == 8) {
        return 0;
    }
    return (g_flag_8042 == 0) ? 1 : 0;
}

/* orig: 0x08019268 - shadow[6] bit 7 / shadow[2] bit 11 by state 0..2, stored in 0x2000804D */
void sym_state_804d(uint8_t state)
{
    if (state == 0) {
        g_lcd_shadow[6] &= ~0x80u;
        g_lcd_shadow[2] &= ~0x800u;
        g_flag_804d = 0;
    } else if (state == 2) {
        g_lcd_shadow[6] |= 0x80u;
        g_lcd_shadow[2] |= 0x800u;
        g_flag_804d = 2;
    } else if (state < 2) {
        g_lcd_shadow[6] |= 0x80u;
        g_lcd_shadow[2] &= ~0x800u;
        g_flag_804d = 1;
    }
}

/* orig: 0x0801A07C - shadow[6] bits 0 and 16 from g_flag_80b7 (in the menu: 0x20007FE8) */
void sym_w6_b0_b16_update(uint8_t refresh)
{
    uint8_t on = (g_menu_active == 0) ? g_flag_80b7 : g_flag_7fe8;

    set_bits(&g_lcd_shadow[6], 0x10001u, on != 0);
    maybe_refresh(refresh);
}

/* orig: 0x08019D68 - shadow[7] bit 0 while autorange_shown() */
void sym_w7_b0_update(uint8_t refresh)
{
    set_bits(&g_lcd_shadow[7], 0x1u, autorange_shown() != 0);
    maybe_refresh(refresh);
}

/* orig: 0x08019FA2 - shadow[2]/[4] bit 16 from display_partial_mode() */
void sym_w2_w4_b16_update(uint8_t refresh)
{
    uint8_t mode = display_partial_mode();

    set_bits(&g_lcd_shadow[2], 0x10000u, mode == 1);
    set_bits(&g_lcd_shadow[4], 0x10000u, mode == 1 || mode == 2);
    maybe_refresh(refresh);
}

/*
 * orig: 0x08019E24 - bit 30 of shadow[2]/[4]/[6]: with g_flag_80b7 by
 * g_peak_select, otherwise by g_apo_long 1..4; none while logging or in
 * the menu.
 */
void sym_b30_update(uint8_t refresh)
{
    const uint32_t b30 = 0x40000000u;

    g_lcd_shadow[6] &= ~b30;
    g_lcd_shadow[4] &= ~b30;
    g_lcd_shadow[2] &= ~b30;
    if (g_logging_active == 0 && g_menu_active == 0) {
        if (g_flag_80b7 != 0) {
            if (g_peak_select != 0) {
                g_lcd_shadow[6] |= b30;
            } else {
                g_lcd_shadow[4] |= b30;
            }
        } else {
            switch (g_apo_long) {
            case 1: g_lcd_shadow[4] |= b30; break;
            case 2: g_lcd_shadow[6] |= b30; break;
            case 3: g_lcd_shadow[2] |= b30; break;
            case 4:
                g_lcd_shadow[6] |= b30;
                g_lcd_shadow[4] |= b30;
                g_lcd_shadow[2] |= b30;
                break;
            default: break;
            }
        }
    }
    maybe_refresh(refresh);
}

/*
 * orig: 0x0801A17C - bit 0 of shadow[1]/[3]/[5]: all with g_flag_80ba, else
 * [1] for functions 2, 4, 16, 18, 20 and [5] for 1, 3, 17, 19, 21. The menu
 * uses its copies of the flag and the function.
 */
void sym_w1_w3_w5_b0_update(void)
{
    uint8_t all = (g_menu_active == 0) ? g_flag_80ba : g_flag_7fe9;
    uint8_t f = (g_menu_active == 0) ? g_function : g_function_7feb;

    if (all != 0) {
        g_lcd_shadow[5] |= 1u;
        g_lcd_shadow[3] |= 1u;
        g_lcd_shadow[1] |= 1u;
        return;
    }
    g_lcd_shadow[5] &= ~1u;
    g_lcd_shadow[3] &= ~1u;
    g_lcd_shadow[1] &= ~1u;
    if (f == 16 || f == 18 || f == 20 || f == 2 || f == 4) {
        g_lcd_shadow[1] |= 1u;
    }
    if (f == 17 || f == 19 || f == 21 || f == 1 || f == 3) {
        g_lcd_shadow[5] |= 1u;
    }
}

/* orig: 0x0801776E - range marks for functions 6, 9 and 12 */
void sym_range_marks_update(void)
{
    g_flag_8061 = 0;
    if (g_function == 6) {
        sym_b8_b11_row2(g_range);
    } else if (g_function == 9) {
        sym_b8_b11_row1(g_range);
    } else if (g_function == 12) {
        sym_b9_for_range(g_range);
    }
    g_display_dirty = 1;
}

/* orig: 0x080180AA - secondary decimal point from the frequency decade (functions 2, 4, 16, 18, 20) */
void secondary_dp_for_freq(void)
{
    g_dp_position = 0;
    if (g_function == 16 || g_function == 18 || g_function == 20 ||
        g_function == 2 || g_function == 4) {
        g_dp_position = dp_for_range(g_freq_decimal);
    }
    lcd_set_secondary_dp(g_dp_position);
}

/*
 * orig: 0x0801912C - shadow[6] bit 8 when hide == 1 (state 3), otherwise
 * shadow[4] bit 7 and the 0x2000804D state for the function and range.
 */
void sym_range_state_update(uint8_t hide)
{
    if (hide == 1) {
        g_lcd_shadow[6] |= 0x100u;
        g_flag_804d = 3;
        g_lcd_shadow[4] &= ~0x80u;
        g_lcd_shadow[6] &= ~0x80u;
        g_lcd_shadow[2] &= ~0x800u;
        return;
    }
    g_lcd_shadow[6] &= ~0x100u;
    g_lcd_shadow[4] |= 0x80u;
    g_flag_804d = 0;
    switch (g_function) {
    case 1: case 2: case 11: case 18: case 19:
        sym_state_804d(g_range);
        break;
    case 3: case 4: case 16: case 17:
        sym_state_804d((uint8_t)(g_range + 1));
        break;
    case 9:
        if (g_range < 2) {
            sym_state_804d((uint8_t)(g_range + 1));
        } else if (g_range < 5) {
            sym_state_804d((uint8_t)(g_range - 2));
        } else {
            sym_state_804d((uint8_t)(g_range - 5));
        }
        break;
    case 10:
        sym_state_804d(2);
        break;
    case 20: case 21:
        sym_state_804d((g_range == 0) ? 2 : 0);
        break;
    default:
        break;
    }
}

/*
 * orig: 0x080172A8 - unit symbols for a function. Function 0 also shows
 * shadow[5] or [1] bit 0 (by alt) when show is set.
 */
void sym_function_units(uint8_t function, uint8_t range, uint8_t alt, uint8_t show)
{
    g_flag_804c = 0;
    switch (function) {
    case 0:
        g_lcd_shadow[3] &= ~1u;
        if (show == 0) {
            g_lcd_shadow[5] &= ~1u;
            g_lcd_shadow[1] &= ~1u;
        } else if (alt == 0) {
            g_lcd_shadow[5] |= 1u;
        } else {
            g_lcd_shadow[1] |= 1u;
        }
        g_lcd_shadow[0] |= 0x40000000u;
        g_lcd_shadow[4] |= 0x400u;
        break;
    case 1:
    case 2:
        g_lcd_shadow[4] |= 0x400u;
        break;
    case 3:
    case 4:
        g_lcd_shadow[2] |= 0x400u;
        g_lcd_shadow[4] |= 0x400u;
        break;
    case 6:
        sym_b8_b11_row2(range);
        break;
    case 7:
        g_lcd_shadow[6] |= 1u;
        break;
    case 8:
        g_lcd_shadow[0] |= 0x20000u;
        break;
    case 9:
        sym_b8_b11_row1(range);
        break;
    case 10:
        g_lcd_shadow[4] |= 0x80000000u;
        g_lcd_shadow[2] |= 0x100u;
        break;
    case 11:
        g_lcd_shadow[6] |= 0x80000000u;
        g_lcd_shadow[4] |= 0x400u;
        break;
    case 12:
        sym_b9_for_range(range);
        break;
    case 13:
    case 22:
        g_lcd_shadow[0] |= 0x200u;
        g_lcd_shadow[4] |= 0x400u;
        g_lcd_shadow[6] |= 0x400u;
        g_lcd_shadow[(function == 13) ? 1 : 5] |= 1u;
        break;
    case 14:
    case 23:
        g_lcd_shadow[2] |= 0x400u;
        g_lcd_shadow[4] |= 0x400u;
        g_lcd_shadow[6] |= 0x400u;
        g_lcd_shadow[(function == 14) ? 1 : 5] |= 1u;
        break;
    case 15:
    case 24:
        if (range < 2) {
            g_lcd_shadow[2] |= 0x400u;
        }
        g_lcd_shadow[4] |= 0x400u;
        g_lcd_shadow[6] |= 0x400u;
        g_lcd_shadow[(function == 15) ? 1 : 5] |= 1u;
        break;
    case 16:
    case 17:
        g_lcd_shadow[0] |= 0x200u;
        g_lcd_shadow[6] |= 0x400u;
        break;
    case 18:
    case 19:
        g_lcd_shadow[2] |= 0x400u;
        g_lcd_shadow[6] |= 0x400u;
        break;
    case 20:
    case 21:
        if (range == 0) {
            g_lcd_shadow[2] |= 0x400u;
        }
        g_lcd_shadow[6] |= 0x400u;
        break;
    default:
        break;
    }
}

/*
 * orig: 0x08019B48 - clear the unit symbols sym_function_units() sets
 * (shadow[6] bit 0 only without g_flag_80b7, which also drives it).
 */
void sym_function_units_clear(void)
{
    g_lcd_shadow[0] &= ~(0x60000000u | 0x00020000u | 0x00000B00u);
    g_lcd_shadow[4] &= ~(0x80000000u | 0x00000700u);
    g_lcd_shadow[6] &= ~(0x80000000u | 0x00000400u);
    if (g_flag_80b7 == 0) {
        g_lcd_shadow[6] &= ~1u;
    }
    g_lcd_shadow[2] &= ~0x00000700u;
}
