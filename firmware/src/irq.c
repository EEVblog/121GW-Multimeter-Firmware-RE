/*
 * Interrupt handlers (except the TIM2 scheduler tick and the USART2 BLE
 * receiver, which live with their subsystems).
 */
#include "irq.h"
#include "app_globals.h"
#include "board_io.h"
#include "buzzer.h"
#include "sd_card.h"
#include "stm32l1xx.h"

#define EXTI_LINE_RTC_WAKEUP   (1u << 20)
#define RTC_IT_WUT_ENABLE      0x4000u   /* RTC_CR WUTIE */
#define RTC_ISR_WUTF_BIT       0x0400u
#define RTC_ISR_INIT_BIT       0x0080u
#define ADC_SR_AWD_BIT         0x01u
#define ADC_CR1_AWDIE_BIT      0x40u
#define TIM_IT_UPDATE          0x0001u
#define BUZZER_PIN             7u

/* Cortex-M3 exceptions: faults hang, the others return, as in the original. */
/* orig: 0x0802427C */
void NMI_Handler(void) {}
/* orig: 0x0802427E */
void HardFault_Handler(void) { for (;;) {} }
/* orig: 0x08024280 */
void MemManage_Handler(void) { for (;;) {} }
/* orig: 0x08024282 */
void BusFault_Handler(void) { for (;;) {} }
/* orig: 0x08024284 */
void UsageFault_Handler(void) { for (;;) {} }
/* orig: 0x08024286 */
void SVC_Handler(void) {}
/* orig: 0x08024288 */
void DebugMon_Handler(void) {}
/* orig: 0x0802428A */
void PendSV_Handler(void) {}

/* orig: 0x0800ED0C - counts g_delay_ticks down to 0 */
void delay_tick(void)
{
    if (g_delay_ticks != 0) {
        g_delay_ticks--;
    }
}

/* orig: 0x080248B0 */
void SysTick_Handler(void)
{
    delay_tick();
}

/* orig: 0x080248B8 */
void SDIO_IRQHandler(void)
{
    (void)sd_process_irq();
}

/* orig: 0x080248C0 */
void DMA2_CH4_IRQHandler(void)
{
    sd_process_dma_irq();
}

/*
 * orig: 0x080248C8 - RTC wake-up timer
 * inlines: 0x080209FC (RTC_GetITStatus), 0x08020A46 (RTC_ClearITPendingBit),
 *          0x08020B6A (EXTI_ClearITPendingBit)
 */
void RTC_WKUP_IRQHandler(void)
{
    if ((RTC->CR & RTC_IT_WUT_ENABLE) != 0 && (RTC->ISR & RTC_ISR_WUTF_BIT) != 0) {
        RTC->ISR = (RTC->ISR & RTC_ISR_INIT_BIT) | ~(RTC_ISR_WUTF_BIT | RTC_ISR_INIT_BIT);
        EXTI->PR = EXTI_LINE_RTC_WAKEUP;
    }
}

/* Clear the pending EXTI lines in `lines`, one at a time as the original does */
static void exti_clear_lines(uint32_t first, uint32_t last)
{
    for (uint32_t line = first; line <= last; line++) {
        if (EXTI->PR & (1u << line)) {
            EXTI->PR = 1u << line;
        }
    }
}

/*
 * The EXTI handlers only clear their flags (the lines are wake-up sources).
 * orig: 0x080248E8; inlines: 0x08020B50 (EXTI_GetITStatus), 0x08020B6A (EXTI_ClearITPendingBit)
 */
void EXTI0_IRQHandler(void) { exti_clear_lines(0, 0); }
/* orig: 0x080248FC */
void EXTI1_IRQHandler(void) { exti_clear_lines(1, 1); }
/* orig: 0x08024910 */
void EXTI2_IRQHandler(void) { exti_clear_lines(2, 2); }
/* orig: 0x08024924 */
void EXTI3_IRQHandler(void) { exti_clear_lines(3, 3); }
/* orig: 0x08024938 */
void EXTI4_IRQHandler(void) { exti_clear_lines(4, 4); }
/* orig: 0x0802494C */
void EXTI9_5_IRQHandler(void) { exti_clear_lines(5, 9); }
/* orig: 0x080249A8 */
void EXTI15_10_IRQHandler(void) { exti_clear_lines(10, 15); }

/*
 * orig: 0x08024A24 - clears the analogue-watchdog interrupt
 * inlines: 0x08020314 (ADC_GetITStatus), 0x08020348 (ADC_ClearITPendingBit)
 */
void ADC1_IRQHandler(void)
{
    if ((ADC1->SR & ADC_SR_AWD_BIT) != 0 && (ADC1->CR1 & ADC_CR1_AWDIE_BIT) != 0) {
        ADC1->SR = ~(uint32_t)ADC_SR_AWD_BIT;
    }
}

/*
 * orig: 0x08024722 - 1 kHz tick: key timer, buzzer time-out and repeated
 * beeps (odd counts sound, even counts are the gaps).
 * inlines: 0x0802099E (TIM_ClearITPendingBit)
 */
void TIM3_IRQHandler(void)
{
    TIM3->SR = (uint16_t)~TIM_IT_UPDATE;
    g_tick_1khz = 1;
    if (g_cfg_022e != 0) {
        g_cfg_022e--;
    }
    if (g_buzzer_ticks == 0 || g_power_off_request != 0) {
        return;
    }
    if (--g_buzzer_ticks != 0) {
        return;
    }
    g_buzzer_on = 0;
    gpio_reset(GPIOE, BUZZER_PIN);
    if (g_beep_count != 0) {
        g_buzzer_ticks = g_beep_duration;
        g_beep_count--;
        if (g_beep_count % 2 == 1) {
            g_buzzer_on = 1;
            if (g_beep_enabled != 0) {
                gpio_set(GPIOE, BUZZER_PIN);
            }
        }
    }
}
