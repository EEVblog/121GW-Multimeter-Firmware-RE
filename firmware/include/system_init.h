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
#include "stm32l1xx.h"

/* Bit-band alias of one bit of a peripheral register */
#define BITBAND_PERIPH(reg, bit)     (*(volatile uint32_t *)(0x42000000u + (((uint32_t)(reg) - 0x40000000u) << 5) + ((bit) << 2)))

/* Byte 2 of RCC_CR holds HSEON/HSERDY/HSEBYP; byte 1 of RCC_CSR holds LSEON/LSEBYP. */
#define RCC_CR_BYTE2  (*(volatile uint8_t *)((uint32_t)&RCC->CR + 2))
#define RCC_CSR_BYTE1 (*(volatile uint8_t *)((uint32_t)&RCC->CSR + 1))

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

void gpio_clocks_disable(void);
void nvic_enable_irq(uint8_t irq);
void nvic_irq_init(unsigned irq, uint32_t preempt, uint32_t sub);

#endif /* SYSTEM_INIT_H */
