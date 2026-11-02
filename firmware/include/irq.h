/*
 * Interrupt handlers.
 */
#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>

extern volatile uint32_t g_delay_ticks;   /* 0x20007D10  counted down by SysTick */
extern uint8_t g_tick_1khz;               /* 0x200080CF  set by every TIM3 tick */
extern uint8_t g_power_off_request;       /* 0x2000803A */

void delay_tick(void);
void NMI_Handler(void);
void HardFault_Handler(void);
void MemManage_Handler(void);
void BusFault_Handler(void);
void UsageFault_Handler(void);
void SVC_Handler(void);
void DebugMon_Handler(void);
void PendSV_Handler(void);
void SysTick_Handler(void);
void SDIO_IRQHandler(void);
void DMA2_CH4_IRQHandler(void);
void RTC_WKUP_IRQHandler(void);
void EXTI0_IRQHandler(void);
void EXTI1_IRQHandler(void);
void EXTI2_IRQHandler(void);
void EXTI3_IRQHandler(void);
void EXTI4_IRQHandler(void);
void EXTI9_5_IRQHandler(void);
void EXTI15_10_IRQHandler(void);
void ADC1_IRQHandler(void);
void TIM3_IRQHandler(void);

extern volatile uint8_t g_task_flag_a;   /* 0x200080D0 */

void power_off_beep(void);

#endif /* IRQ_H */
