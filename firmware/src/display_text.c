/*
 * Display: characters on the two 5-digit readouts, numbers and fixed texts.
 *
 * The LCD has ten 7-segment digit positions: 0..4 form the secondary readout
 * (most significant first) and 5..9 the main readout. A character's
 * segment code (char_to_segments) holds two segment columns per COM row:
 * bits 7..6 for COM0, 5..4 for COM1, 3..2 for COM2 and 1..0 for COM3. In
 * COM0 the bit-6 column is the decimal point to the left of the digit; the
 * font never sets it, and only some positions clear it.
 */
#include "display_text.h"
#include "display_symbols.h"
#include "front_end.h"
#include "lcd_driver.h"
#include "measure_peak.h"
#include "measure_derived.h"
#include "measure_funcs.h"
#include "dual_correction.h"
#include "numeric.h"
#include "measure_scale.h"
#include "ac_response.h"

/*
 * One shadow-word update for a digit position:
 * word = (word & keep) | (segments shifted by shift) & bits.
 */
struct digit_field {
    uint8_t word;
    int8_t shift;     /* left shift, negative = right shift */
    uint32_t keep;
    uint32_t bits;
};

/*
 * orig: 0x0801A950 (code). Masks are as the original applies them; for
 * the SEG32+ words (odd indices) they also clear bits 12..31, which no
 * segment uses.
 */
static const uint8_t digit_field_count[10] = { 4, 8, 4, 4, 4, 4, 4, 4, 4, 4 };
static const struct digit_field digit_fields[10][8] = {
    /* 0 */ {
        { 0,  21, 0xEFFFFFFFu, 0x18000000u },
        { 2,  23, 0xE7FFFFFFu, 0x18000000u },
        { 4,  25, 0xE7FFFFFFu, 0x18000000u },
        { 6,  27, 0xE7FFFFFFu, 0x18000000u },
    },
    /* 1: left column in SEG0..31, right column in SEG32+ */ {
        { 0,  19, 0xFFFFFFFFu, 0x02000000u },
        { 1,   1, 0x00000EFFu, 0x00000100u },
        { 2,  21, 0xFDFFFFFFu, 0x02000000u },
        { 3,   3, 0x00000EFFu, 0x00000100u },
        { 4,  23, 0xFDFFFFFFu, 0x02000000u },
        { 5,   5, 0x00000EFFu, 0x00000100u },
        { 6,  25, 0xFDFFFFFFu, 0x02000000u },
        { 7,   7, 0x00000EFFu, 0x00000100u },
    },
    /* 2 */ {
        { 0,  17, 0xFEFFFFFFu, 0x01800000u },
        { 2,  19, 0xFE7FFFFFu, 0x01800000u },
        { 4,  21, 0xFE7FFFFFu, 0x01800000u },
        { 6,  23, 0xFE7FFFFFu, 0x01800000u },
    },
    /* 3 */ {
        { 0,  15, 0xFFBFFFFFu, 0x00600000u },
        { 2,  17, 0xFF9FFFFFu, 0x00600000u },
        { 4,  19, 0xFF9FFFFFu, 0x00600000u },
        { 6,  21, 0xFF9FFFFFu, 0x00600000u },
    },
    /* 4 */ {
        { 0,  13, 0xFFEFFFFFu, 0x00180000u },
        { 2,  15, 0xFFE7FFFFu, 0x00180000u },
        { 4,  17, 0xFFE7FFFFu, 0x00180000u },
        { 6,  19, 0xFFE7FFFFu, 0x00180000u },
    },
    /* 5 */ {
        { 1,  -1, 0x00000FBFu, 0x00000060u },
        { 3,   1, 0x00000F9Fu, 0x00000060u },
        { 5,   3, 0x00000F9Fu, 0x00000060u },
        { 7,   5, 0x00000F9Fu, 0x00000060u },
    },
    /* 6 */ {
        { 1,  -3, 0x00000FEFu, 0x00000018u },
        { 3,  -1, 0x00000FE7u, 0x00000018u },
        { 5,   1, 0x00000FE7u, 0x00000018u },
        { 7,   3, 0x00000FE7u, 0x00000018u },
    },
    /* 7 */ {
        { 1,  -5, 0x00000FFBu, 0x00000006u },
        { 3,  -3, 0x00000FF9u, 0x00000006u },
        { 5,  -1, 0x00000FF9u, 0x00000006u },
        { 7,   1, 0x00000FF9u, 0x00000006u },
    },
    /* 8 */ {
        { 0,   8, 0xFFFF7FFFu, 0x0000C000u },
        { 2,  10, 0xFFFF3FFFu, 0x0000C000u },
        { 4,  12, 0xFFFF3FFFu, 0x0000C000u },
        { 6,  14, 0xFFFF3FFFu, 0x0000C000u },
    },
    /* 9 */ {
        { 0,   6, 0xFFFFDFFFu, 0x00003000u },
        { 2,   8, 0xFFFFCFFFu, 0x00003000u },
        { 4,  10, 0xFFFFCFFFu, 0x00003000u },
        { 6,  12, 0xFFFFCFFFu, 0x00003000u },
    },
};

/* orig: 0x080291B0.. - fixed 5-character texts */
static const char txt_blank[5] = { ' ', ' ', ' ', ' ', ' ' };
static const char txt_apo_on[5] = { 'A', 'P', 'O', 'o', 'N' };
static const char txt_apo_off[5] = { 'A', 'P', 'O', 'o', 'F' };
static const char txt_b_on[5] = { 'b', '-', 'O', 'n', ' ' };
static const char txt_b_off[5] = { 'b', '-', 'O', 'F', 'F' };
static const char txt_bd_on[5] = { 'b', 'd', 'O', 'n', ' ' };
static const char txt_bd_off[5] = { 'b', 'd', 'O', 'F', 'F' };
static const char txt_15[5] = { ' ', ' ', ' ', '1', '5' };
static const char txt_3[5] = { ' ', ' ', ' ', ' ', '3' };
static const char txt_dn_30[5] = { 'D', 'N', ' ', '3', '0' };
static const char txt_up_30[5] = { 'U', 'P', ' ', '3', '0' };
static const char txt_dn_300[5] = { 'D', 'N', '3', '0', '0' };
static const char txt_up_300[5] = { 'U', 'P', '3', '0', '0' };
static const char txt_off_e[5] = { 'O', 'F', 'F', '-', 'E' };
static const char txt_g_end[5] = { 'G', '-', 'E', 'N', 'D' };
static const char txt_400_e[5] = { '4', '0', '0', '-', 'E' };
static const char txt_5k_end[5] = { '5', 'K', 'E', 'N', 'D' };
static const char txt_cal[5] = { ' ', 'C', 'A', 'L', ' ' };
static const char txt_lead[5] = { ' ', 'L', 'E', 'A', 'D' };
static const char txt_dashes[5] = { '-', '-', '-', '-', '-' };

#define SEC_MINUS_W  4
#define SEC_MINUS    0x20000000u     /* shadow[4] bit 29 */
#define MAIN_MINUS_W   3
#define MAIN_MINUS     0x00000080u     /* shadow[3] bit 7 */

static void set_minus(int secondary, int on)
{
    uint32_t *w = secondary ? &g_lcd_shadow[SEC_MINUS_W] : &g_lcd_shadow[MAIN_MINUS_W];
    uint32_t bit = secondary ? SEC_MINUS : MAIN_MINUS;

    if (on) {
        *w |= bit;
    } else {
        *w &= ~bit;
    }
}

static void finish(uint8_t update_now)
{
    if (update_now == 0) {
        g_display_dirty = 1;
    } else {
        lcd_update();
    }
}

/* orig: 0x0801A950 - draw one character at digit position 0..9 (others ignored) */
void lcd_put_char(uint8_t c, uint8_t pos)
{
    uint32_t seg = char_to_segments(c);
    uint8_t i;

    if (pos > 9) {
        return;
    }
    for (i = 0; i < digit_field_count[pos]; i++) {
        const struct digit_field *f = &digit_fields[pos][i];
        uint32_t v = (f->shift >= 0) ? (seg << f->shift) : (seg >> -f->shift);

        g_lcd_shadow[f->word] = (g_lcd_shadow[f->word] & f->keep) | (v & f->bits);
    }
}

/* orig: 0x08019AE0 - five characters on the secondary (secondary != 0) or main readout */
void lcd_put_text5(uint8_t secondary, const char *text)
{
    uint8_t pos = (secondary == 0) ? 5 : 0;
    uint8_t i;

    for (i = 0; i < 5; i++) {
        lcd_put_char((uint8_t)text[i], (uint8_t)(pos + i));
    }
}

/* orig: 0x08019B12 */
void lcd_put_char_at(uint8_t pos, const char *c)
{
    lcd_put_char((uint8_t)*c, pos);
}

/* orig: 0x08019B24 - two characters from position pos */
void lcd_put_text2(uint8_t pos, const char *text)
{
    lcd_put_char((uint8_t)text[0], pos);
    lcd_put_char((uint8_t)text[1], (uint8_t)(pos + 1));
}

/* orig: 0x08019A74 */
void show_lead(uint8_t secondary)
{
    lcd_put_text5(secondary, txt_lead);
}

/* orig: 0x08019A86 */
void show_dashes(uint8_t secondary)
{
    lcd_put_text5(secondary, txt_dashes);
}

/* orig: 0x08019A98 - blank a readout and its minus sign */
void show_blank(uint8_t secondary)
{
    g_peak_is_min = 0;
    set_minus(secondary == 1, 0);
    lcd_put_text5(secondary, txt_blank);
}

/* orig: 0x08019880 */
void show_cal(void)
{
    set_minus(1, 0);
    lcd_set_secondary_dp(0);
    lcd_put_text5(1, txt_cal);
    g_display_dirty = 1;
}

static void show_secondary_text(const char *text)
{
    g_digit_text[0] = text[0];
    g_digit_text[1] = text[1];
    g_digit_text[2] = text[2];
    g_digit_text[3] = text[3];
    g_digit_text[4] = text[4];
    lcd_put_text5(1, g_digit_text);
    g_display_dirty = 1;
}

/* orig: 0x080198B0 */
void show_lobat(void)
{
    g_lcd_shadow[4] &= ~0x20000u;
    show_secondary_text("LoBAt");
}

/* orig: 0x080198FC */
void show_sdend(void)
{
    g_lcd_shadow[4] &= ~0x20000u;
    show_secondary_text("SdEnd");
}

/* orig: 0x08019948 - "SdE" and a two-digit number (tens are right only up to 19) */
void show_sde_number(uint8_t n)
{
    g_lcd_shadow[4] &= ~0x20000u;
    g_digit_text[0] = 'S';
    g_digit_text[1] = 'd';
    g_digit_text[2] = 'E';
    if (n < 10) {
        g_digit_text[3] = '0';
    } else {
        g_digit_text[3] = (char)(n / 10 + '0');
        n = (uint8_t)(n - 10);
    }
    g_digit_text[4] = (char)(n + '0');
    lcd_put_text5(1, g_digit_text);
    g_display_dirty = 1;
}

/* orig: 0x08018356 */
void show_burdn(void)
{
    lcd_set_secondary_dp(0);
    show_secondary_text("burdn");
}

/* orig: 0x08018410 - "Lcd-" and the contrast setting 10..17 as 0..7 */
void show_lcd_contrast(uint8_t contrast)
{
    lcd_set_secondary_dp(0);
    g_digit_text[0] = 'L';
    g_digit_text[1] = 'c';
    g_digit_text[2] = 'd';
    g_digit_text[3] = '-';
    g_digit_text[4] = (char)(contrast - LCD_CONTRAST_MIN + '0');
    lcd_put_text5(1, g_digit_text);
    g_display_dirty = 1;
}

/* orig: 0x0801839A - "BAT" and two digits (0..99) */
void show_bat_number(uint16_t n)
{
    g_dp_position = 1;
    lcd_set_secondary_dp(g_dp_position);
    if (n >= 100) {
        n = 99;
    }
    number_to_digits(n);
    g_digit_text[0] = 'B';
    g_digit_text[1] = 'A';
    g_digit_text[2] = 'T';
    g_digit_text[3] = (char)(g_digits[3] + '0');
    g_digit_text[4] = (char)(g_digits[4] + '0');
    lcd_put_text5(1, g_digit_text);
    g_display_dirty = 1;
}

/* orig: 0x08018460 - "APO.oN" / "APO.oF" */
void show_apo_setting(uint8_t off)
{
    lcd_set_secondary_dp(2);
    lcd_put_text5(1, (off == 0) ? txt_apo_on : txt_apo_off);
    g_display_dirty = 1;
}

/* orig: 0x08018490 - "b-OFF" / "b-On" */
void show_beep_setting(uint8_t on)
{
    lcd_set_secondary_dp(0);
    lcd_put_text5(1, (on == 0) ? txt_b_off : txt_b_on);
    g_display_dirty = 1;
}

/* orig: 0x080186AC - "bd.OFF" / "bd.On" */
void show_bd_setting(uint8_t on)
{
    lcd_set_secondary_dp(3);
    lcd_put_text5(1, (on == 0) ? txt_bd_off : txt_bd_on);
    g_display_dirty = 1;
}

/* orig: 0x080186E0 - "3" or "15" from g_flag_80b3 */
void show_3_or_15(void)
{
    lcd_set_secondary_dp(0);
    lcd_put_text5(1, (g_flag_80b3 == 0) ? txt_3 : txt_15);
    g_display_dirty = 1;
}

/* orig: 0x0801871C - "DN 30", "UP 30", "DN300", "UP300" for 0..3 */
void show_updn_setting(uint8_t sel)
{
    set_minus(1, 0);
    lcd_set_secondary_dp(0);
    if (sel == 0) {
        lcd_put_text5(1, txt_dn_30);
    } else if (sel == 2) {
        lcd_put_text5(1, txt_dn_300);
    } else if (sel < 2) {
        lcd_put_text5(1, txt_up_30);
    } else if (sel == 3) {
        lcd_put_text5(1, txt_up_300);
    }
    g_display_dirty = 1;
}

/* orig: 0x08019738 - main "OFF-E", "G-END", "400-E", "5KEND" for 0..3 */
void main_show_end_text(uint8_t sel)
{
    set_minus(0, 0);
    if (sel == 0) {
        lcd_put_text5(0, txt_off_e);
    } else if (sel == 2) {
        lcd_put_text5(0, txt_400_e);
    } else if (sel < 2) {
        lcd_put_text5(0, txt_g_end);
    } else if (sel == 3) {
        lcd_put_text5(0, txt_5k_end);
    }
    g_display_dirty = 1;
}

/* orig: 0x080189B0 - the setup value being edited, no decimal point */
void show_edit_value(uint8_t update_now)
{
    lcd_set_secondary_dp(0);
    number_to_digits(g_edit_value);
    digits_to_text(4);
    lcd_put_text5(1, g_digit_text);
    finish(update_now);
}

/* orig: 0x08018788 - year 2000 + yy */
void show_year(uint8_t update_now, uint8_t yy)
{
    lcd_set_secondary_dp(0);
    number_to_digits((uint32_t)yy + 2000u);
    digits_to_text(0);
    lcd_put_text5(1, g_digit_text);
    finish(update_now);
}

/* orig: 0x080181B6 - signed number on the secondary readout at g_dp_position */
void show_secondary_number(int32_t value)
{
    int32_t mag = (value < 0) ? -value : value;

    lcd_set_secondary_dp(g_dp_position);
    number_to_digits((uint32_t)mag);
    digits_to_text(g_dp_position);
    lcd_put_text5(1, g_digit_text);
    set_minus(1, value < 0);
    g_display_dirty = 1;
}

/* orig: 0x08017F28 - signed number on the secondary readout */
void show_secondary_number_dp(int32_t value, uint8_t dp, uint8_t update_now)
{
    if (value < 0) {
        value = -value;
        set_minus(1, 1);
    } else {
        set_minus(1, 0);
    }
    lcd_set_secondary_dp(dp);
    number_to_digits((uint32_t)value);
    digits_to_text(dp);
    lcd_put_text5(1, g_digit_text);
    finish(update_now);
}

/*
 * orig: 0x08017100 - signed number on the main readout; skipped while
 * g_hold_active is set if check_804f. The first argument is unused.
 */
void show_main_number(uint32_t unused, uint8_t dp, int32_t value, uint8_t check_804f)
{
    (void)unused;
    if (check_804f != 0 && g_hold_active != 0) {
        return;
    }
    lcd_set_main_dp(dp);
    set_minus(0, value < 0);
    number_to_digits((uint32_t)((value < 0) ? -value : value));
    digits_to_text(dp);
    lcd_put_text5(0, g_digit_text);
    g_display_dirty = 1;
    g_sub_value = value;
}

static const char txt_ofl[5] = { ' ', 'O', 'F', 'L', ' ' };
static const char txt_open[5] = { ' ', 'O', 'P', 'E', 'N' };

/* orig: 0x080199B4 - " OFL " (main: " LEAD" instead while g_flag_8076; minus from g_peak_is_min) */
void show_ofl(uint8_t secondary)
{
    if (secondary == 0 && g_flag_8076 != 0) {
        show_lead(0);
        return;
    }
    if (secondary == 1) {
        set_minus(1, 0);
    } else {
        set_minus(0, g_peak_is_min != 0);
    }
    lcd_put_text5(secondary, txt_ofl);
}

/* orig: 0x08019A22 - " OPEN" (main: " LEAD" instead while g_flag_8076) */
void show_open(uint8_t secondary)
{
    if (secondary == 0 && g_flag_8076 != 0) {
        show_lead(0);
        return;
    }
    set_minus(secondary == 1, 0);
    lcd_put_text5(secondary, txt_open);
}

/* orig: 0x08017F94 - frequency (0.1 units of g_freq_raw) on the secondary readout */
void show_freq_secondary(void)
{
    secondary_dp_for_freq();
    show_secondary_number_dp(g_freq_raw / 10, g_dp_position, 0);
}

/* orig: 0x080184D4 - "In" and a number up to 3 digits, right aligned */
void show_in_number(uint16_t n)
{
    uint16_t rest = n;

    lcd_set_secondary_dp(0);
    g_digit_text[0] = 'I';
    g_digit_text[1] = 'n';
    if (n >= 100) {
        g_digit_text[2] = (char)(n / 100 + '0');
        rest = (uint16_t)(n % 100);
    } else {
        g_digit_text[2] = ' ';
    }
    if (n >= 10) {
        g_digit_text[3] = (char)(rest / 10 + '0');
    } else {
        g_digit_text[3] = ' ';
    }
    g_digit_text[4] = (char)(rest % 10 + '0');
    lcd_put_text5(1, g_digit_text);
    g_display_dirty = 1;
}

/*
 * orig: 0x08019698 - main "-x-nn": x is O, G, 4, 5, 3 for sel 0..4
 * (unchanged for larger sel), nn is g_avg_count (tens right only to 19).
 */
void main_show_avg_count(uint8_t sel)
{
    static const char sel_char[5] = { 'O', 'G', '4', '5', '3' };
    uint8_t n = g_avg_count;

    set_minus(0, 0);
    g_digit_text[0] = '-';
    if (sel < 5) {
        g_digit_text[1] = sel_char[sel];
    }
    g_digit_text[2] = '-';
    if (n < 10) {
        g_digit_text[3] = '0';
    } else {
        g_digit_text[3] = (char)(n / 10 + '0');
        n = (uint8_t)(n - 10);
    }
    g_digit_text[4] = (char)(n + '0');
    lcd_put_text5(0, g_digit_text);
    g_display_dirty = 1;
}

/* Two 2-digit numbers (as their last two decimal digits) around a separator */
static void show_pair(uint8_t update_now, uint8_t a, char sep, uint8_t b)
{
    lcd_set_secondary_dp(0);
    number_to_digits(a);
    g_digits[3] += '0';
    g_digit_text[0] = (char)g_digits[3];
    g_digits[4] += '0';
    g_digit_text[1] = (char)g_digits[4];
    g_digit_text[2] = sep;
    number_to_digits(b);
    g_digits[3] += '0';
    g_digit_text[3] = (char)g_digits[3];
    g_digits[4] += '0';
    g_digit_text[4] = (char)g_digits[4];
    lcd_put_text5(1, g_digit_text);
    finish(update_now);
}

/* orig: 0x080187D8 - "aa-bb" */
void show_pair_dash(uint8_t update_now, uint8_t a, uint8_t b)
{
    show_pair(update_now, a, '-', b);
}

/* orig: 0x080188A0 - "aa bb" / "aa-bb" with the separator from g_flag_80d5 */
void show_pair_blink(uint8_t update_now, uint8_t a, uint8_t b)
{
    show_pair(update_now, a, (g_flag_80d5 == 0) ? ' ' : '-', b);
}

/*
 * orig: 0x08018A00 - temperature (0.01 C, truncated to 16 bits) on the
 * secondary readout as "-12.3c" / "45.6F", or " OPEN" with g_flag_80ae.
 * Leading blanks become character 0 (also blank).
 */
void show_temperature(int32_t c100)
{
    int16_t t;
    int neg = 0;

    set_minus(1, 0);
    t = (int16_t)temp_for_display((int16_t)c100);
    if (g_flag_80ae != 0) {
        lcd_set_secondary_dp(0);
        show_open(1);
        return;
    }
    lcd_set_secondary_dp(2);
    if (t < 0) {
        neg = 1;
        t = (int16_t)-t;
    }
    number_to_digits((uint32_t)(t * 10));
    digits_to_text(2);
    if (neg) {
        if (g_digits[1] == ' ') {
            g_digit_text[1] = '-';
        } else {
            g_digit_text[0] = '-';
        }
    }
    if (g_digit_text[0] == ' ') {
        g_digit_text[0] = 0;
        if (g_digit_text[1] == ' ') {
            g_digit_text[1] = 0;
        }
    }
    g_digit_text[4] = (g_temp_unit_c != 0) ? 'c' : 'F';
    lcd_put_text5(1, g_digit_text);
    g_display_dirty = 1;
}

/*
 * orig: 0x08018228 - "b" and a burden-type value on the secondary readout
 * with one decimal: mode 100 shows the value as is (" OFL" with
 * g_flag_80a7), mode 0 rounds value/1000, other modes round value/100.
 * "burdn" instead while g_flag_80aa.
 */
void show_burden_value(int32_t value, uint8_t mode)
{
    int32_t mag = value;
    int32_t q;
    int neg = 0;

    if (g_flag_80aa != 0) {
        show_burdn();
        g_display_dirty = 1;
        return;
    }
    if (mag < 0) {
        mag = (int32_t)(0u - (uint32_t)mag);
        neg = 1;
    }
    if (mode == 100) {
        q = mag;
    } else if (mode == 0) {
        q = mag / 1000;
        if (mag % 1000 >= 500) {
            q++;
        }
    } else {
        q = mag / 100;
        if (mag % 100 >= 50) {
            q++;
        }
    }
    g_burden_shown = (int16_t)q;
    if (neg) {
        g_burden_shown = (int16_t)-g_burden_shown;
    }
    if (mode == 100 && g_flag_80a7 != 0) {
        g_digit_text[1] = ' ';
        g_digit_text[2] = 'O';
        g_digit_text[3] = 'F';
        g_digit_text[4] = 'L';
    } else {
        number_to_digits((uint32_t)q);
        g_dp_position = 1;
        lcd_set_secondary_dp(g_dp_position);
        digits_to_text(g_dp_position);
    }
    set_minus(1, neg);
    g_digit_text[0] = 'b';
    lcd_put_text5(1, g_digit_text);
    g_display_dirty = 1;
}

/*
 * orig: 0x0801813A - blink phase of a setting being edited: blank the
 * edited pair (modes 6, 7: digits 0-1 or 3-4 by g_flag_80d6), the edited
 * digit (mode 8: digit 4 - g_edit_digit) or the whole secondary readout.
 */
void show_edit_blink(void)
{
    static const char blank2[2] = { ' ', ' ' };     /* orig: 0x0801840C */
    static const char blank1[1] = { ' ' };          /* orig: 0x0801845C */

    set_minus(1, 0);
    lcd_set_secondary_dp(0);
    if (g_mode_8037 == 7 || g_mode_8037 == 6) {
        lcd_put_text2((g_flag_80d6 == 0) ? 0 : 3, blank2);
    } else if (g_mode_8037 == 8) {
        lcd_put_char_at((uint8_t)(4 - g_edit_digit), blank1);
    } else {
        lcd_put_text5(1, txt_blank);
    }
    g_display_dirty = 1;
}

/*
 * orig: 0x08018590 - dBm on the secondary readout with 0..3 decimals by
 * magnitude (NaN takes 3, as the IAR compares treat it as smaller), or
 * " OFL" on overload.
 */
void show_dbm(void)
{
    float x;
    uint32_t n;

    if (g_flag_8046 != 0) {
        show_ofl(1);
        return;
    }
    if (g_dbm < 0.0f) {
        set_minus(1, 1);
        x = -g_dbm;
    } else {
        set_minus(1, 0);
        x = g_dbm;
    }
    if (x >= 1000.0f) {
        g_dp_position = 0;
        n = iar_f2u(x);
    } else if (x >= 100.0f) {
        g_dp_position = 1;
        n = iar_f2u(10.0f * x);
    } else if (x >= 10.0f) {
        g_dp_position = 2;
        n = iar_f2u(100.0f * x);
    } else {
        g_dp_position = 3;
        n = iar_f2u(1000.0f * x);
    }
    g_dbm_shown = (g_dbm < 0.0f) ? -(int32_t)n : (int32_t)n;
    lcd_set_secondary_dp(g_dp_position);
    number_to_digits(n);
    digits_to_text(g_dp_position);
    lcd_put_text5(1, g_digit_text);
    g_display_dirty = 1;
}

/* orig: 0x080143E4 - the main readout shows " LEAD" from now on */
void show_lead_request(void)
{
    g_flag_8076 = 1;
}

/* orig: 0x08017092 - power-off screen: clear the LCD, then "-oFF-" on the main readout */
void show_power_off(void)
{
    static const char txt_off[5] = { '-', 'o', 'F', 'F', '-' };   /* orig: 0x080291A8 */

    lcd_clear_shadow();
    lcd_update();
    lcd_put_text5(0, txt_off);
    lcd_update();
}

static const char txt_sd_cal[5] = { 'S', 'd', 'C', 'A', 'L' };
static const char txt_sd_sav[5] = { 'S', 'd', 'S', 'A', 'v' };
static const char txt_sd_set[5] = { 'S', 'd', 'S', 'E', 't' };
static const char txt_saved[5] = { 'S', 'A', 'v', 'E', 'd' };
static const char txt_err[5] = { '-', 'E', 'r', 'r', '-' };
static const char txt_cal1[5] = { ' ', 'C', 'A', 'L', '1' };

/* A message on the main readout (no minus), with its decimal point, shown at once */
static void main_message(const char *text, uint8_t dp)
{
    set_minus(0, 0);
    lcd_put_text5(0, text);
    lcd_set_main_dp(dp);
    lcd_update();
}

/* orig: 0x08019792 - "Sd.CAL" */
void show_sd_cal(void)
{
    main_message(txt_sd_cal, 3);
}

/* orig: 0x080197B6 - "Sd.SAv" */
void show_sd_save(void)
{
    main_message(txt_sd_sav, 3);
}

/* orig: 0x080197E0 - "Sd.SEt" */
void show_sd_set(void)
{
    main_message(txt_sd_set, 3);
}

/* orig: 0x08019808 */
void show_saved(void)
{
    main_message(txt_saved, 0);
}

/* orig: 0x0801982C */
void show_error(void)
{
    main_message(txt_err, 0);
}

/* orig: 0x08019854 - " CAL1" (drawn with the next display refresh) */
void show_cal1(void)
{
    set_minus(0, 0);
    lcd_put_text5(0, txt_cal1);
    g_display_dirty = 1;
}

/* orig: 0x0801A854 - firmware version "V-2.05" on a cleared display */
void show_version(void)
{
    lcd_clear_shadow();
    number_to_digits(205);
    digits_to_text(0);
    g_digit_text[0] = 'V';
    g_digit_text[1] = '-';
    lcd_put_text5(0, g_digit_text);
    lcd_set_main_dp(2);
    g_display_dirty = 1;
}

/* orig: 0x0801A89C - every segment on (display test) */
void lcd_all_segments_on(void)
{
    for (unsigned com = 0; com < 4; com++) {
        g_lcd_shadow[2 * com] = 0xFFFFFFFFu;
        g_lcd_shadow[2 * com + 1] |= 0xFFFu;
    }
    g_display_dirty = 1;
}

/* orig: 0x0801F7AC - decimal text of n, ending at g_number_text[15] (NUL) */
void number_to_text(uint32_t n)
{
    char *p = &g_number_text[15];

    *p = 0;
    do {
        *--p = (char)('0' + n % 10u);
        n /= 10u;
    } while (n != 0);
}
