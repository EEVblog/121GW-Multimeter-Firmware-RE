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

extern volatile uint8_t g_task_flag_b;   /* 0x200080D1  every 10 ticks */
extern volatile uint8_t g_task_flag_c;   /* 0x200080D2  never set */
extern volatile uint8_t g_task_flag_d;   /* 0x200080D3  every 50 ticks */
extern uint8_t g_flag_8035;              /* 0x20008035 */
extern uint8_t g_count_8036;             /* 0x20008036  clears g_flag_8035 when it runs out */
extern uint16_t g_menu_idle_ticks;       /* 0x20007FAA  the menu closes after 1000 */
extern uint32_t g_tick_div10;            /* 0x20007F00 */
extern uint32_t g_tick_div50;            /* 0x20007F04 */
extern uint32_t g_tick_div100;           /* 0x20007F08 */
extern uint8_t g_flag_802c;              /* 0x2000802C  set at tick 40 of 50 */
extern uint8_t g_half_second_phase;      /* 0x20008077 */

void TIM2_IRQHandler(void);

#endif /* IRQ_H */
