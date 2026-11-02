/*
 * Front-panel key actions (short and long presses).
 */
#ifndef KEY_HANDLERS_H
#define KEY_HANDLERS_H

#include <stdint.h>

extern uint8_t g_startup_key;            /* 0x200080CD  start-up screen action (1 CAL, 2/3 confirm) */
extern uint8_t g_cal_save_selected;      /* 0x20008088  start-up: SD calibration backup save chosen */
extern uint8_t g_cal_restore_selected;   /* 0x20008089  start-up: SD calibration restore chosen */
extern uint8_t g_manual_range_saved_rel; /* 0x200080ED  g_manual_range before REL */
extern uint8_t g_backlight_on;           /* 0x2000807B */
extern uint16_t g_backlight_ticks;       /* 0x20007FB8  backlight time-out */

void ble_power(void);
void key_short_press_handler(uint8_t key);
void key_long_press_handler(uint8_t key);

#endif /* KEY_HANDLERS_H */
