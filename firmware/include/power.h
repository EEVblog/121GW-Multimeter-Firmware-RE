/*
 * Power-off (STOP mode) and the BLE module's UART.
 *
 * Power-off shuts the external circuitry, timers, BLE UART and ADC down,
 * drops the core to the 65 kHz MSI clock in voltage range 3, makes every
 * unused pin analog and arms EXTI wake-up on the rotary switch lines (all
 * but the current position) and the keys, then enters STOP mode. On wake-up
 * the meter starts again from system_init().
 */
#ifndef POWER_H
#define POWER_H

#include <stdint.h>
#include "app_globals.h"

extern uint8_t g_rotary_pos;             /* 0x20008065  debounced rotary code (one-hot, as g_rotary_raw) */
extern uint8_t g_resumed_from_power_off; /* 0x20008038 */

void lcd_off(void);
void timers_off(void);
void ble_uart_off(void);
void ble_uart_on(void);
void adc_off(void);
void pvd_off(void);
void clocks_minimal(void);
void systick_off(void);
void oscillators_off(void);
void pins_all_analog(void);
uint8_t exti_pin_source(uint8_t line);
uint32_t exti_line_bit(uint8_t line);
uint8_t exti_irq_for_line(uint8_t line);
void exti_wake_line(uint8_t line);
void wake_sources_config(void);
void power_circuits_off(void);
void power_down_prepare(void);
void power_off(void);

/* Not yet rebuilt */
void startup_screen(void);

#endif /* POWER_H */
