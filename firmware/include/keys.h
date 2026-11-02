/*
 * Front-panel keys (PG8..PG15) and rotary switch (PG0..PG7).
 *
 * Both are active low with pull-ups. Raw samples store a pressed key or the
 * selected rotary position as a 1 bit:
 *   g_keys_raw   bit n = PG(8+n) low
 *   g_rotary_raw bit n = PG(7-n) low
 *
 * Key events (consumed by keys_dispatch_events()):
 *   short press - reported on release, or immediately when g_cfg_022e != 0
 *                 and g_flag_80ce == 0
 *   long press  - after 50 scan ticks of holding a single key, or the
 *                 PG11+PG13 combination (KEY_COMBO_UPDATE)
 *   auto-repeat - keys 0x40/0x80 (PG14/PG15) repeat while held in the menu
 */
#ifndef KEYS_H
#define KEYS_H

#include <stdint.h>

#define KEY_PG8   0x01u
#define KEY_PG9   0x02u
#define KEY_PG10  0x04u
#define KEY_PG11  0x08u
#define KEY_PG12  0x10u
#define KEY_PG13  0x20u
#define KEY_PG14  0x40u
#define KEY_PG15  0x80u
#define KEY_COMBO_UPDATE (KEY_PG11 | KEY_PG13)   /* same combination as the bootloader */

void keys_read_raw(void);
void rotary_read_raw(void);
uint8_t key_can_long_press(uint8_t code);
uint8_t key_is_single_key(uint8_t code);
uint8_t key_is_update_combo(uint8_t code);
void key_autorepeat(uint8_t held);
void key_scan(void);
void keys_dispatch_events(void);
void keys_task(void);

#endif /* KEYS_H */
