/*
 * SETUP menu items and the start-up state.
 *
 * In the menu g_mode_8037 selects the item shown on the secondary readout:
 *   0 temperature unit   2 auto power-off   3 beep      4 LCD contrast
 *   5 year               6 month, day       7 hour, minute
 *   8 serial number      9 log interval
 *  10 per-function option (UP/DN for function 10, "bd" for the VA functions)
 *  11 "bd" for the dual functions
 * Key codes passed to the item handlers: 1 = up, 2 = down, 100 = store
 * (any other code also stores for most items).
 */
#ifndef SETUP_MENU_H
#define SETUP_MENU_H

#include <stdint.h>
#include "app_globals.h"

#define SETUP_KEY_UP     1u
#define SETUP_KEY_DOWN   2u
#define SETUP_KEY_STORE  100u

extern uint8_t g_flag_80b2;          /* 0x200080B2 */

void secondary_mode_default(void);
void setup_bd_dual_key(uint8_t key);
void setup_bd_va_key(uint8_t key);
void setup_updn_key(uint8_t key);
void setup_apo_key(uint8_t key);
void setup_beep_key(uint8_t key);
void setup_contrast_key(uint8_t key);
void setup_log_interval_key(uint8_t key);
void setup_year_key(uint8_t key);
void setup_date_key(uint8_t key);
void setup_time_key(uint8_t key);
void setup_serial_key(uint8_t key);
uint8_t setup_item_key(uint8_t key);

void rtc_load_clock(void);
void app_state_init(void);

#endif /* SETUP_MENU_H */
