/*
 * User settings kept in the data EEPROM (offsets from 0x08080000).
 *
 *   0x01..02  g_value_7f9a (16-bit BE, default 1708)
 *   0x03..06  g_saved_value (32-bit BE, 0..99999)
 *   0x08..0E  last function chosen at each rotary position (see below)
 *   0x10      temperature unit (1 = Celsius)
 *   0x12      diode test voltage selection (g_flag_80b3)
 *   0x14      g_sel_80b6 (0..3)
 *   0x16      auto power-off disabled
 *   0x18..19  log interval in seconds (16-bit BE, 1..999)
 *   0x1A      LCD contrast (12..17, default 14)
 *   0x1B      g_flag_80a8
 *   0x1E      beep enabled
 *   0x26      0xA5 once calibrated
 *
 * Unknown or out-of-range EEPROM contents fall back to defaults.
 */
#include "settings.h"
#include "system_init.h"
#include "ac_response.h"
#include "buzzer.h"
#include "calibration.h"
#include "display_update.h"
#include "eeprom.h"
#include "front_end.h"
#include "hy3131_ctrl.h"
#include "lcd_driver.h"
#include "measure_derived.h"
#include "measure_funcs.h"
#include "trim_correction.h"

#define EE_VALUE_7F9A    0x01u
#define EE_SAVED_VALUE   0x03u
#define EE_FUNCTION_A    0x08u   /* functions 1, 2 (0x82 = 2 with AC+DC) */
#define EE_FUNCTION_B    0x09u   /* 3..5 */
#define EE_FUNCTION_C    0x0Au   /* 6..8 */
#define EE_FUNCTION_D    0x0Bu   /* 9..12 */
#define EE_FUNCTION_E    0x0Cu   /* 15 or 24 */
#define EE_FUNCTION_F    0x0Du   /* 13, 16, 17, 22 */
#define EE_FUNCTION_G    0x0Eu   /* 18..21, read back as 18 or 19 */
#define EE_TEMP_UNIT     0x10u
#define EE_FLAG_80B3     0x12u
#define EE_SEL_80B6      0x14u
#define EE_APO_DISABLED  0x16u
#define EE_LOG_INTERVAL  0x18u
#define EE_CONTRAST      0x1Au
#define EE_FLAG_80A8     0x1Bu
#define EE_BEEP          0x1Eu
#define EE_CAL_MARKER    0x26u
#define FUNCTION_ACDC    0x82u   /* function 2 with AC+DC, as stored */
#define EXTRA_VALID      0xA5u

/* orig: 0x08010F10 - 1 until the calibration marker is set */
uint8_t settings_uncalibrated(void)
{
    return eeprom_read_byte(EE_CAL_MARKER) != 0xA5u;
}

/* orig: 0x08010F4C */
void settings_load_value_7f9a(void)
{
    g_value_7f9a = eeprom_read_u16_be(EE_VALUE_7F9A);
    if (g_value_7f9a == 0) {
        g_value_7f9a = 1708;
    }
}

/* orig: 0x08010F74 */
void settings_save_value_7f9a(void)
{
    eeprom_write_u16_be(EE_VALUE_7F9A, g_value_7f9a);
}

/* orig: 0x08010F8C */
void settings_load_saved_value(void)
{
    g_saved_value = eeprom_read_u32_be(EE_SAVED_VALUE);
    if (g_saved_value >= 100000u) {
        g_saved_value = 0;
    }
}

/* orig: 0x08010FB8 */
void settings_save_saved_value(void)
{
    eeprom_write_u32_be(EE_SAVED_VALUE, g_saved_value);
}

/* orig: 0x0801472C - select AC+DC: clear its phase state and the frequency */
void acdc_mode_enter(void)
{
    g_flag_80ba = 1;
    g_flag_80b9 = 0;
    g_freq_raw_acdc = 0;
    g_freq_decimal_acdc = 0;
    g_flag_80bd = 0;
    g_acdc_have_a = (g_flag_80bd != 0) ? 1 : 0;
    g_acdc_have_b = g_acdc_have_a;
    g_acdc_settle = 0;
    g_vec_a = 0;
    g_vec_b = 0;
}

/* orig: 0x08010FF0 - function 1 or 2 (a stored AC+DC also enters AC+DC mode) */
uint8_t settings_function_a(void)
{
    uint8_t v = eeprom_read_byte(EE_FUNCTION_A);

    if (v == FUNCTION_ACDC) {
        acdc_mode_enter();
        return 2;
    }
    return (v == 1 || v == 2) ? v : 1;
}

/* orig: 0x08011020 */
uint8_t settings_function_b(void)
{
    uint8_t v = eeprom_read_byte(EE_FUNCTION_B);

    return (v == 3 || v == 4 || v == 5) ? v : 3;
}

/* orig: 0x08011046 */
uint8_t settings_function_c(void)
{
    uint8_t v = eeprom_read_byte(EE_FUNCTION_C);

    return (v == 6 || v == 7 || v == 8) ? v : 6;
}

/* orig: 0x0801106C */
uint8_t settings_function_d(void)
{
    uint8_t v = eeprom_read_byte(EE_FUNCTION_D);

    return (v == 9 || v == 10 || v == 11 || v == 12) ? v : 9;
}

/* orig: 0x08011098 */
uint8_t settings_function_e(void)
{
    uint8_t v = eeprom_read_byte(EE_FUNCTION_E);

    return (v == 24 || v == 15) ? v : 24;
}

/* orig: 0x080110B8 */
uint8_t settings_function_f(void)
{
    uint8_t v = eeprom_read_byte(EE_FUNCTION_F);

    return (v == 17 || v == 16 || v == 13 || v == 22) ? v : 17;
}

/* orig: 0x080110E4 - 19 for stored 19/21 (and unknown values), 18 for 18/20 */
uint8_t settings_function_g(void)
{
    uint8_t v = eeprom_read_byte(EE_FUNCTION_G);

    if (v == 19 || v == 21) {
        return 19;
    }
    if (v == 18 || v == 20) {
        return 18;
    }
    return 19;
}

/* orig: 0x08011118 - function 2 is stored as 0x82 while AC+DC is selected */
void settings_write_function(uint8_t offset, uint8_t function)
{
    if (offset == EE_FUNCTION_A && function == 2 && g_flag_80ba != 0) {
        function = FUNCTION_ACDC;
    }
    eeprom_write_byte(offset, function);
}

/* orig: 0x08011154 - remember the current function for its rotary position */
void settings_save_function(void)
{
    uint8_t f = g_function;

    switch (f) {
    case 1: case 2:
        settings_write_function(EE_FUNCTION_A, f);
        break;
    case 3: case 4: case 5:
        settings_write_function(EE_FUNCTION_B, f);
        break;
    case 6: case 7: case 8:
        settings_write_function(EE_FUNCTION_C, f);
        break;
    case 9: case 10: case 11: case 12:
        settings_write_function(EE_FUNCTION_D, f);
        break;
    case 14: case 15: case 23: case 24:
        settings_write_function(EE_FUNCTION_E, (f == 24 || f == 23) ? 24 : 15);
        break;
    case 13: case 16: case 17: case 22:
        settings_write_function(EE_FUNCTION_F, f);
        break;
    case 18: case 19: case 20: case 21:
        settings_write_function(EE_FUNCTION_G, f);
        break;
    default:
        break;
    }
}

/* orig: 0x08011218 */
void settings_load_log_interval(void)
{
    g_log_interval_s = eeprom_read_u16_be(EE_LOG_INTERVAL);
    if (g_log_interval_s >= 1000u) {
        g_log_interval_s = 1;
    }
}

/* orig: 0x08011248 */
void settings_save_log_interval(uint16_t seconds)
{
    eeprom_write_u16_be(EE_LOG_INTERVAL, seconds);
}

/* orig: 0x08011258 */
void settings_load_contrast(void)
{
    uint8_t v = eeprom_read_byte(EE_CONTRAST);

    g_lcd_contrast = (v >= 12 && v < 18) ? v : 14;
}

/* orig: 0x08011294 */
void settings_save_contrast(uint8_t contrast)
{
    eeprom_write_byte(EE_CONTRAST, contrast);
}

/* orig: 0x080112A4 */
void settings_load_apo(void)
{
    g_apo_disabled = (eeprom_read_byte(EE_APO_DISABLED) == 1) ? 1 : 0;
}

/* orig: 0x080112EC */
void settings_save_apo(uint8_t disabled)
{
    eeprom_write_byte(EE_APO_DISABLED, disabled);
}

/* orig: 0x080112FC */
void settings_load_temp_unit(void)
{
    g_temp_unit_c = eeprom_read_byte(EE_TEMP_UNIT);
    if (g_temp_unit_c > 1) {
        g_temp_unit_c = 0;
    }
}

/* orig: 0x08011330 */
void settings_save_temp_unit(uint8_t celsius)
{
    eeprom_write_byte(EE_TEMP_UNIT, celsius);
}

/* orig: 0x08011340 */
void settings_load_flag_80b3(void)
{
    g_flag_80b3 = (eeprom_read_byte(EE_FLAG_80B3) == 1) ? 1 : 0;
}

/* orig: 0x08011384 */
void settings_save_flag_80b3(uint8_t value)
{
    eeprom_write_byte(EE_FLAG_80B3, value);
}

/* orig: 0x08011394 */
void settings_load_sel_80b6(void)
{
    g_sel_80b6 = eeprom_read_byte(EE_SEL_80B6);
    if (g_sel_80b6 > 3) {
        g_sel_80b6 = 0;
    }
}

/* orig: 0x080113CC */
void settings_save_sel_80b6(uint8_t value)
{
    eeprom_write_byte(EE_SEL_80B6, value);
}

/* orig: 0x080113DC */
void settings_load_flag_80a8(void)
{
    g_flag_80a8 = (eeprom_read_byte(EE_FLAG_80A8) == 1) ? 1 : 0;
}

/* orig: 0x08011400 */
void settings_save_flag_80a8(uint8_t value)
{
    eeprom_write_byte(EE_FLAG_80A8, value);
}

/* orig: 0x08011410 */
void settings_load_beep(void)
{
    g_beep_enabled = (eeprom_read_byte(EE_BEEP) == 1) ? 1 : 0;
}

/* orig: 0x08011438 */
void settings_save_beep(uint8_t enabled)
{
    eeprom_write_byte(EE_BEEP, enabled);
}

/*
 * orig: 0x080114BC - store the extra offset of a VA function (16..21) with
 * its 0xA5 valid marker; only when skip is 0.
 */
void cal_extra_store(uint8_t function, uint8_t skip, uint32_t value)
{
    if ((function == 16 || function == 18 || function == 20 ||
         function == 17 || function == 19 || function == 21) && skip == 0) {
        uint16_t addr = cal_extra_address(function);

        eeprom_write_byte(addr, EXTRA_VALID);
        eeprom_write_u32_be((uint32_t)addr + 1u, value);
    }
}

/* orig: 0x0801150E - zero the extra offsets of functions 16..21 */
void cal_extra_clear_all(void)
{
    for (uint8_t f = 16; f <= 21; f++) {
        cal_extra_store(f, 0, 0);
    }
}
