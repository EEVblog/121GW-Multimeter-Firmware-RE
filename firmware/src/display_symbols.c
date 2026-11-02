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

/* orig: 0x0801801E - secondary digit count from range bit 0x200080C6 */
void sub_digits_from_sel_c6(void)
{
    g_sub_digits = (g_sel_80c6 == 0) ? 4 : 3;
}

/* orig: 0x0801803C - secondary digit count for the dual-readout functions */
void sub_digits_for_dual(void)
{
    if (g_function == 13 || g_function == 22) {
        g_sub_digits = 4;
    } else if (g_function == 14 || g_function == 23) {
        g_sub_digits = (g_sel_80c7 == 0) ? 4 : 3;
    } else {
        g_sub_digits = (g_sel_80c7 == 0) ? 2 : 3;
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
    set_bits(&g_lcd_shadow[5], 0x80u, g_flag_8039 == 0 && g_logging_active == 0 && g_apo_long == 0);
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
