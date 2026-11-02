/*
 * User settings kept in the data EEPROM.
 */
#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>
#include "app_globals.h"

extern uint16_t g_serial_yymm;    /* 0x20007F9A  EEPROM 0x01: serial number prefix YYMM, default 1708 */
extern uint32_t g_serial_number;   /* 0x20007D14  EEPROM 0x03: serial number 0..99999 */

uint8_t settings_uncalibrated(void);
void settings_load_serial_yymm(void);
void settings_save_serial_yymm(void);
void settings_load_serial_number(void);
void settings_save_serial_number(void);
void acdc_mode_enter(void);
uint8_t settings_function_a(void);
uint8_t settings_function_b(void);
uint8_t settings_function_c(void);
uint8_t settings_function_d(void);
uint8_t settings_function_e(void);
uint8_t settings_function_f(void);
uint8_t settings_function_g(void);
void settings_write_function(uint8_t offset, uint8_t function);
void settings_save_function(void);
void settings_load_log_interval(void);
void settings_save_log_interval(uint16_t seconds);
void settings_load_contrast(void);
void settings_save_contrast(uint8_t contrast);
void settings_load_apo(void);
void settings_save_apo(uint8_t disabled);
void settings_load_temp_unit(void);
void settings_save_temp_unit(uint8_t celsius);
void settings_load_flag_80b3(void);
void settings_save_flag_80b3(uint8_t value);
void settings_load_sel_80b6(void);
void settings_save_sel_80b6(uint8_t value);
void settings_load_flag_80a8(void);
void settings_save_flag_80a8(uint8_t value);
void settings_load_beep(void);
void settings_save_beep(uint8_t enabled);
void cal_extra_store(uint8_t function, uint8_t skip, uint32_t value);
void cal_extra_clear_all(void);

uint8_t settings_marker_1f_missing(void);
uint8_t settings_read_byte(uint32_t offset);
void settings_write_byte(uint32_t offset, uint8_t value);

extern uint8_t g_flag_808b;      /* 0x2000808B */

void acdc_mode_toggle(void);
void settings_temp_unit_key(uint8_t key);
void settings_serial_yymm_from_clock(void);

#endif /* SETTINGS_H */
