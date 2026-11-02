/*
 * BLE reading packet (19 bytes at g_ble_packet), rebuilt after each reading.
 *
 *   [0]      0xF2 (packet type)
 *   [1..4]   serial number: YYMM prefix and 5-digit number, packed BCD
 *   [5]      main function; +0x80 if |value| has bit 17, +0x40 if bit 16
 *   [6]      main range; +0x80 overload (g_flag_8048), +0x40 negative,
 *            +0x10/+0x20 F/C for temperature (function 5)
 *   [7..8]   main value, |value| low 16 bits, big-endian
 *   [9..12]  secondary readout: type, flags (decimal point, +0x80
 *            overload/open, +0x40 negative), 16-bit magnitude
 *   [13]     bar graph state (0x2000804D), +0x10 for non-bar functions,
 *            +0x04 negative, +0x08 bar hidden
 *   [14]     bar graph length
 *   [15..17] symbol/status bits
 *   [18]     XOR of bytes 0..17
 *
 * Each main value is also kept in a 1000-entry history.
 */
#include "ble_packet.h"
#include "buzzer.h"
#include "ac_response.h"
#include "display_bargraph.h"
#include "display_state.h"
#include "display_symbols.h"
#include "display_text.h"
#include "display_update.h"
#include "dual_correction.h"
#include "front_end.h"
#include "hy3131_ctrl.h"
#include "lcd_driver.h"
#include "measure_derived.h"
#include "measure_funcs.h"
#include "measure_peak.h"
#include "range_select.h"
#include "rtc.h"
#include "settings.h"
#include "system_init.h"
#include "annunciators.h"

#define PACKET_LEN       19u
#define SECONDARY_FIELD  9u
#define HISTORY_LEN      1000u

static int dual_function(uint8_t f)
{
    return f == 13 || f == 14 || f == 15 || f == 22 || f == 23 || f == 24;
}

static int va_function(uint8_t f)
{
    return f == 16 || f == 18 || f == 20 || f == 17 || f == 19 || f == 21;
}

/* orig: 0x0800D100 - |value| as 16-bit big-endian at pos */
void ble_put_value(uint8_t pos, int32_t value)
{
    if (value < 0) {
        value = -value;
    }
    g_ble_packet[pos] = (uint8_t)((uint32_t)value >> 8);
    g_ble_packet[pos + 1] = (uint8_t)value;
}

static void put_field(uint8_t pos, uint8_t type, uint8_t flags, int32_t value)
{
    g_ble_packet[pos] = type;
    g_ble_packet[pos + 1] = flags;
    ble_put_value((uint8_t)(pos + 2), value);
}

/* orig: 0x0800C53A - bytes 1..4: serial number YYMM + 5 digits as packed BCD */
void ble_serial_fields(void)
{
    number_to_digits(g_serial_yymm);
    g_ble_packet[1] = (uint8_t)((g_digits[2] & 0xF) + (g_digits[1] << 4));
    g_ble_packet[2] = (uint8_t)((g_digits[3] * 10 + g_digits[4]) * 16);
    number_to_digits(g_serial_number);
    g_ble_packet[2] = (uint8_t)((g_digits[0] & 0xF) + g_ble_packet[2]);
    g_ble_packet[3] = (uint8_t)((g_digits[2] & 0xF) + (g_digits[1] << 4));
    g_ble_packet[4] = (uint8_t)((g_digits[4] & 0xF) + (g_digits[3] << 4));
}

/* orig: 0x0800C99E - internal temperature (0x64 C / 0x69 F) */
void ble_field_temperature(uint8_t pos)
{
    int16_t t;
    uint8_t flags = 1;

    g_ble_packet[pos] = (g_temp_unit_c != 0) ? 0x64 : 0x69;
    t = (int16_t)temp_for_display(g_ambient_temp);
    if (g_flag_80ae != 0) {
        flags = (uint8_t)(flags + 0x80);
    }
    if (t < 0) {
        flags = (uint8_t)(flags + 0x40);
    }
    g_ble_packet[pos + 1] = flags;
    ble_put_value((uint8_t)(pos + 2), t);
}

/* orig: 0x0800C9FC */
void ble_field_battery(uint8_t pos)
{
    put_field(pos, 0x6E, 1, g_bat_value);
}

/* orig: 0x0800CA2A */
void ble_field_apo(uint8_t pos)
{
    put_field(pos, (g_apo_disabled == 0) ? 0x78 : 0x7D, 2, 0);
}

/* orig: 0x0800CA64 */
void ble_field_contrast(uint8_t pos)
{
    put_field(pos, 0xA0, 0, g_lcd_contrast - 10);
}

/* orig: 0x0800CA96 */
void ble_field_year(uint8_t pos)
{
    put_field(pos, 0x82, 0, g_rtc_set_reg2);
}

/* orig: 0x0800CAC4 */
void ble_field_date(uint8_t pos)
{
    g_ble_packet[pos] = 0x87;
    g_ble_packet[pos + 1] = 0;
    g_ble_packet[pos + 2] = g_rtc_set_reg4;
    g_ble_packet[pos + 3] = g_rtc_set_reg6;
}

/* orig: 0x0800CB04 */
void ble_field_time(uint8_t pos)
{
    g_ble_packet[pos] = 0x8C;
    g_ble_packet[pos + 1] = 0;
    g_ble_packet[pos + 2] = g_rtc_set_reg10;
    g_ble_packet[pos + 3] = g_rtc_set_reg12;
}

/* orig: 0x0800D0C8 */
void ble_field_none(uint8_t pos)
{
    put_field(pos, 0, 0, 0);
}

/* orig: 0x0800CE32 - "bd" setting */
void ble_field_bd(uint8_t pos, uint8_t on)
{
    put_field(pos, (on == 0) ? 0x9C : 0x9B, 3, 0);
}

/* orig: 0x0800CE78 - frequency (type 6); flags = dp + 0x10 (+0x20 above decade 1) */
void ble_field_frequency(uint8_t pos, uint8_t dp, uint8_t overload, int32_t value)
{
    uint8_t flags = dp;

    g_ble_packet[pos] = 6;
    if (overload != 0) {
        flags = (uint8_t)(flags + 0x80);
    }
    if (value < 0) {
        flags = (uint8_t)(flags + 0x40);
    }
    flags = (uint8_t)(flags + 0x10);
    if (g_freq_decimal >= 2) {
        flags = (uint8_t)(flags + 0x20);
    }
    g_ble_packet[pos + 1] = flags;
    ble_put_value((uint8_t)(pos + 2), value);
}

static uint8_t dual_flags(uint8_t dp, int32_t value)
{
    if (g_disp_flag_80ca != 0 || g_disp_flag_80cb != 0) {
        dp = (uint8_t)(dp + 0x80);
    }
    if (value < 0) {
        dp = (uint8_t)(dp + 0x40);
    }
    return dp;
}

/*
 * orig: 0x0800CEFC - first dual reading (type 2 for functions 13..15, 1 for
 * 22..24); record it in its history when `record`. Only called for dual
 * functions (the original leaves the type byte undefined otherwise).
 */
void ble_field_dual_a(uint8_t pos, uint8_t dp, uint8_t record)
{
    g_ble_packet[pos] = (g_function >= 13 && g_function <= 15) ? 2 : 1;
    g_ble_packet[pos + 1] = dual_flags(dp, g_disp_eac);
    ble_put_value((uint8_t)(pos + 2), g_disp_eac);
    if (record != 0) {
        g_ble_history_a[g_ble_history_index] = g_disp_eac;
    }
}

/* orig: 0x0800CFB0 - second dual reading (type 0x10..0x15 by function) */
void ble_field_dual_b(uint8_t pos, uint8_t dp, uint8_t record)
{
    static const uint8_t type[] = { 0x10, 0x12, 0x14 };   /* functions 13, 14, 15 */
    static const uint8_t type_b[] = { 0x11, 0x13, 0x15 }; /* functions 22, 23, 24 */

    g_ble_packet[pos] = (g_function <= 15) ? type[g_function - 13] : type_b[g_function - 22];
    g_ble_packet[pos + 1] = dual_flags(dp, g_disp_eb0);
    ble_put_value((uint8_t)(pos + 2), g_disp_eb0);
    if (record != 0) {
        g_ble_history_b[g_ble_history_index] = g_disp_eb0;
    }
}

/* orig: 0x0800D07C - burden value (type 0x96) in the secondary field */
void ble_field_burden(void)
{
    g_ble_packet[SECONDARY_FIELD] = 0x96;
    g_ble_packet[SECONDARY_FIELD + 1] = (g_burden_shown < 0) ? 0x41 : 1;
    ble_put_value(SECONDARY_FIELD + 2, (g_burden_shown < 0) ? -(int32_t)g_burden_shown : g_burden_shown);
}

/* orig: 0x0800CE28 */
void ble_field_mode12(uint8_t pos)
{
    (void)pos;
    ble_field_burden();
}

/* orig: 0x0800CB44 - secondary field in display mode 10 */
void ble_field_mode10(uint8_t pos)
{
    uint8_t f = g_function;

    if (f == 4 || (f == 2 && g_flag_80ba == 0)) {
        ble_field_frequency(pos, g_dp_position, g_flag_8047, g_freq_raw / 10);
    } else if (f == 10) {
        put_field(pos, (uint8_t)(g_sel_80b6 - 0x56), 0, 0);
    } else if (f == 11) {
        put_field(pos, 0x0B, 0, (g_flag_80b3 == 0) ? 3 : 15);
    } else if (dual_function(f)) {
        if (g_flag_80c2 == 0) {
            ble_field_dual_a(pos, g_dp_position, 0);
        } else {
            ble_field_dual_b(pos, g_dp_position, 0);
        }
    } else if (va_function(f)) {
        ble_field_bd(pos, g_flag_80a5);
    } else {
        ble_field_none(pos);
    }
}

/* orig: 0x0800CCC8 - secondary field in display mode 11 */
void ble_field_mode11(uint8_t pos)
{
    uint8_t f = g_function;

    if (f == 2) {
        uint8_t flags = g_dp_position;

        g_ble_packet[pos] = 0xB4;
        if (g_flag_8046 != 0) {
            flags = (uint8_t)(flags + 0x80);
        }
        if (g_dbm_shown < 0) {
            flags = (uint8_t)(flags + 0x40);
        }
        g_ble_packet[pos + 1] = flags;
        ble_put_value((uint8_t)(pos + 2), (g_dbm_shown < 0) ? -g_dbm_shown : g_dbm_shown);
    } else if ((f == 16 || f == 18 || f == 20) && g_flag_80a5 == 0) {
        ble_field_frequency(pos, g_dp_position, g_flag_8047, g_freq_raw / 10);
    } else if (va_function(f) && g_flag_80a5 != 0) {
        ble_field_burden();
    } else if (dual_function(f)) {
        ble_field_bd(pos, g_flag_80a8);
    } else {
        ble_field_none(pos);
    }
}

/* orig: 0x0800C5DA - build g_ble_packet from the current state */
void ble_build_packet(void)
{
    uint32_t mag;
    uint8_t b;

    g_ble_packet[0] = 0xF2;
    ble_serial_fields();

    mag = (uint32_t)((g_sub_value < 0) ? -g_sub_value : g_sub_value);
    b = g_function;
    if (mag & (1u << 17)) {
        b = (uint8_t)(b + 0x80);
    }
    if (mag & (1u << 16)) {
        b = (uint8_t)(b + 0x40);
    }
    g_ble_packet[5] = b;

    b = g_range;
    if (g_function == 11 && g_flag_80b3 != 0) {
        b = 1;
    }
    if (g_flag_8048 != 0) {
        b = (uint8_t)(b + 0x80);
    }
    if (g_sub_value < 0) {
        b = (uint8_t)(b + 0x40);
    }
    if (g_function == 5) {
        b = (uint8_t)(b + ((g_temp_unit_c == 0) ? 0x10 : 0x20));
    }
    g_ble_packet[6] = b;

    mag = (uint32_t)((g_sub_value < 0) ? -g_sub_value : g_sub_value);
    g_ble_packet[7] = (uint8_t)(mag >> 8);
    g_ble_packet[8] = (uint8_t)mag;

    g_ble_history_index++;
    if (g_ble_history_index >= HISTORY_LEN) {
        g_ble_history_index = 0;
    }
    g_ble_history_main[g_ble_history_index] = g_sub_value;

    switch (g_mode_8037) {
    case 0:  ble_field_temperature(SECONDARY_FIELD); break;
    case 1:  ble_field_battery(SECONDARY_FIELD); break;
    case 2:  ble_field_apo(SECONDARY_FIELD); break;
    case 4:  ble_field_contrast(SECONDARY_FIELD); break;
    case 5:  ble_field_year(SECONDARY_FIELD); break;
    case 6:  ble_field_date(SECONDARY_FIELD); break;
    case 7:  ble_field_time(SECONDARY_FIELD); break;
    case 10: ble_field_mode10(SECONDARY_FIELD); break;
    case 11: ble_field_mode11(SECONDARY_FIELD); break;
    case 12: ble_field_mode12(SECONDARY_FIELD); break;
    default: ble_field_none(SECONDARY_FIELD); break;
    }

    b = g_flag_804d;
    if ((g_function > 5 && g_function < 9) || g_function == 12 || g_function == 5) {
        b = (uint8_t)(g_flag_804d + 0x10);
    }
    if (g_scaled_value < 0) {
        b = (uint8_t)(b + 0x04);
    }
    if (g_flag_804d == 3) {
        b = (uint8_t)(b + 0x08);
    }
    g_ble_packet[13] = b;
    g_ble_packet[14] = g_bar_length;

    b = (g_flag_80b9 != 0) ? 0x40 : 0;
    if (g_flag_80b7 != 0) {
        b = (uint8_t)(b + 0x20);
    }
    if (g_flag_80ba != 0) {
        b = (uint8_t)(b + 0x18);
    } else if (g_function == 16 || g_function == 18 || g_function == 20 || g_function == 2 ||
               g_function == 4 || g_function == 13 || g_function == 14 || g_function == 15) {
        b = (uint8_t)(b + 0x10);
    } else if (g_function == 17 || g_function == 19 || g_function == 21 || g_function == 1 ||
               g_function == 3 || g_function == 22 || g_function == 23 || g_function == 24) {
        b = (uint8_t)(b + 0x08);
    }
    if (autorange_shown() != 0) {
        b = (uint8_t)(b + 0x04);
    }
    if (g_apo_disabled == 0) {
        b = (uint8_t)(b + 0x02);
    }
    if (g_flag_803b != 0) {
        b = (uint8_t)(b + 0x01);
    }

    uint8_t s = (g_flag_802b != 0) ? 0x40 : 0;
    if (g_flag_80b1 != 0) {
        s = (uint8_t)(s + 0x20);
    }
    if (g_flag_8058 != 0) {
        s = (uint8_t)(s + 0x10);
    }
    if (g_function == 2 && g_mode_8037 == 11) {
        s = (uint8_t)(s + 0x08);
    }
    if (g_flag_80b7 != 0) {
        s = (uint8_t)(s + ((g_peak_select == 0) ? 0x01 : 0x02));
    } else if (g_apo_long != 0) {
        s = (uint8_t)(g_apo_long + s);
    }
    g_ble_packet[15] = b;
    g_ble_packet[16] = s;

    g_ble_packet[17] = (uint8_t)(display_partial_mode() * 4);
    if ((uint8_t)(g_logging_active & g_flag_7fe3) != 0 || g_menu_active != 0) {
        g_ble_packet[17] = (uint8_t)(g_ble_packet[17] + 0x10);
    }

    g_ble_checksum = g_ble_packet[0];
    for (uint8_t i = 1; i < PACKET_LEN - 1; i++) {
        g_ble_checksum ^= g_ble_packet[i];
    }
    g_ble_packet[PACKET_LEN - 1] = g_ble_checksum;
}

/* orig: 0x0800C5D2 */
void ble_send_reading(void)
{
    ble_build_packet();
}
