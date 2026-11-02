/*
 * Display: characters on the two 5-digit readouts, numbers and fixed texts.
 */
#ifndef DISPLAY_TEXT_H
#define DISPLAY_TEXT_H

#include <stdint.h>
#include "app_globals.h"

extern uint32_t g_edit_value;    /* 0x20007D18  setup value being edited (5 digits) */
extern int32_t g_sub_value;      /* 0x20007D70  last number shown on the main readout */
extern uint8_t g_flag_8076;      /* 0x20008076  main OFL/OPEN show " LEAD" instead */
extern uint8_t g_flag_80ae;      /* 0x200080AE  temperature shows " OPEN" */
extern uint8_t g_flag_80aa;      /* 0x200080AA  show_burden_value() shows "burdn" */
extern uint8_t g_flag_80d6;      /* 0x200080D6  edited pair: 0 = digits 0-1, else 3-4 */
extern uint8_t g_edit_digit;     /* 0x20008071  edited digit, counted from the right */
extern int16_t g_burden_shown;   /* 0x20007FCA  value last shown by show_burden_value() */
extern int32_t g_dbm_shown;      /* 0x20007E98  dBm last shown, in 10^-dp units */
extern uint8_t g_flag_80d5;      /* 0x200080D5  separator of show_pair_blink() */

void lcd_put_char(uint8_t c, uint8_t pos);
void lcd_put_text5(uint8_t secondary, const char *text);
void lcd_put_char_at(uint8_t pos, const char *c);
void lcd_put_text2(uint8_t pos, const char *text);
void show_lead(uint8_t secondary);
void show_dashes(uint8_t secondary);
void show_blank(uint8_t secondary);
void show_cal(void);
void show_lobat(void);
void show_sdend(void);
void show_sde_number(uint8_t n);
void show_burdn(void);
void show_lcd_contrast(uint8_t contrast);
void show_bat_number(uint16_t n);
void show_apo_setting(uint8_t off);
void show_beep_setting(uint8_t on);
void show_bd_setting(uint8_t on);
void show_3_or_15(void);
void show_updn_setting(uint8_t sel);
void main_show_end_text(uint8_t sel);
void show_edit_value(uint8_t update_now);
void show_year(uint8_t update_now, uint8_t yy);
void show_secondary_number(int32_t value);
void show_secondary_number_dp(int32_t value, uint8_t dp, uint8_t update_now);
void show_main_number(uint32_t unused, uint8_t dp, int32_t value, uint8_t check_804f);

void show_ofl(uint8_t secondary);
void show_open(uint8_t secondary);
void show_freq_secondary(void);
void show_in_number(uint16_t n);
void main_show_avg_count(uint8_t sel);
void show_pair_dash(uint8_t update_now, uint8_t a, uint8_t b);
void show_pair_blink(uint8_t update_now, uint8_t a, uint8_t b);

void show_temperature(int32_t c100);
void show_burden_value(int32_t value, uint8_t mode);
void show_edit_blink(void);
void show_dbm(void);

#endif /* DISPLAY_TEXT_H */
