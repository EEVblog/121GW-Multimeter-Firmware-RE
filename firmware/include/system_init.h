/*
 * Application start-up: clocks, power, GPIO, ADC, NVIC, timers.
 *
 * The application runs from MSI at 4.194 MHz with the HSE crystal off
 * (SystemInit briefly starts the 16 MHz HSE PLL, then system_init() switches
 * to MSI). HSI stays on for the ADC. TIM2 gives a ~99.8 Hz scheduler tick
 * and TIM3 ~1 kHz.
 */
#ifndef SYSTEM_INIT_H
#define SYSTEM_INIT_H

#include <stdint.h>

extern uint8_t g_woke_from_standby;   /* 0x20007FDE */
extern uint32_t g_bor_level;          /* 0x20007D0C */
extern uint8_t g_flag_80b9;           /* 0x200080B9  front-end variant for modes 2/4; role unknown */

void SystemInit(void);
void clock_init(uint8_t use_hse);
void rtc_clock_init(void);
void enable_gpio_clocks(void);
void rotary_pins_config(uint8_t selected, uint32_t fallback_pins);
void porte_inputs_init(void);
void adc_pins_analog(void);
void adc_init(void);
void nvic_init(void);
void tim2_init(void);
void tim3_init(void);
void front_end_select(uint8_t function);
void board_gpio_init(void);
void system_init(void);

#endif /* SYSTEM_INIT_H */
