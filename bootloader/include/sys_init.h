/*
 * Bootloader hardware bring-up.
 */
#ifndef SYS_INIT_H
#define SYS_INIT_H

#include <stdint.h>

/* Set by main() before system_init(): select HSE as SYSCLK, and whether to
 * apply voltage scaling range 1. */
extern uint8_t g_use_hse;
extern uint8_t g_set_voltage_range;

/* Set to 1 by the TIM2 interrupt on every update event. */
extern volatile uint8_t g_tick_flag;

void system_init(void);
void tim2_enable(int enable);

#endif /* SYS_INIT_H */
