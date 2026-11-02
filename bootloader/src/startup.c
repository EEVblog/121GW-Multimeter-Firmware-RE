/*
 * Start-up code and vector table for the STM32L15xxD (bootloader).
 *
 * Mirrors the original: SystemInit() runs first, then RAM code is copied to
 * 0x20000000 and .bss is cleared, then main() runs. NMI, SVC, DebugMon,
 * PendSV and SysTick return immediately; faults and unused IRQs spin.
 */
#include <stdint.h>

extern uint32_t _estack;
extern uint32_t _sramfunc, _eramfunc, _siramfunc;
extern uint32_t _sdata, _edata, _sidata;
extern uint32_t _sbss, _ebss;

void SystemInit(void);
int main(void);

void Reset_Handler(void);
void Default_Handler(void);

/* Handlers that simply return.
 * orig: NMI 0x08004DD8, SVC 0x08004DE2, DebugMon 0x08004DE4, PendSV 0x08004DE6,
 *       SysTick 0x08004E00 */
void NMI_Handler(void) {}
void SVC_Handler(void) {}
void DebugMon_Handler(void) {}
void PendSV_Handler(void) {}
void SysTick_Handler(void) {}

/* Fault handlers spin.
 * orig: HardFault 0x08004DDA, MemManage 0x08004DDC, BusFault 0x08004DDE, UsageFault 0x08004DE0 */
void HardFault_Handler(void)  { for (;;) {} }
void MemManage_Handler(void)  { for (;;) {} }
void BusFault_Handler(void)   { for (;;) {} }
void UsageFault_Handler(void) { for (;;) {} }

#define WEAK_IRQ(name) void name(void) __attribute__((weak, alias("Default_Handler")))
WEAK_IRQ(WWDG_IRQHandler);
WEAK_IRQ(PVD_IRQHandler);
WEAK_IRQ(TAMPER_STAMP_IRQHandler);
WEAK_IRQ(RTC_WKUP_IRQHandler);
WEAK_IRQ(FLASH_IRQHandler);
WEAK_IRQ(RCC_IRQHandler);
WEAK_IRQ(EXTI0_IRQHandler);
WEAK_IRQ(EXTI1_IRQHandler);
WEAK_IRQ(EXTI2_IRQHandler);
WEAK_IRQ(EXTI3_IRQHandler);
WEAK_IRQ(EXTI4_IRQHandler);
WEAK_IRQ(DMA1_Channel1_IRQHandler);
WEAK_IRQ(DMA1_Channel2_IRQHandler);
WEAK_IRQ(DMA1_Channel3_IRQHandler);
WEAK_IRQ(DMA1_Channel4_IRQHandler);
WEAK_IRQ(DMA1_Channel5_IRQHandler);
WEAK_IRQ(DMA1_Channel6_IRQHandler);
WEAK_IRQ(DMA1_Channel7_IRQHandler);
WEAK_IRQ(ADC1_IRQHandler);
WEAK_IRQ(USB_HP_IRQHandler);
WEAK_IRQ(USB_LP_IRQHandler);
WEAK_IRQ(DAC_IRQHandler);
WEAK_IRQ(COMP_IRQHandler);
WEAK_IRQ(EXTI9_5_IRQHandler);
WEAK_IRQ(LCD_IRQHandler);
WEAK_IRQ(TIM9_IRQHandler);
WEAK_IRQ(TIM10_IRQHandler);
WEAK_IRQ(TIM11_IRQHandler);
WEAK_IRQ(TIM2_IRQHandler);
WEAK_IRQ(TIM3_IRQHandler);
WEAK_IRQ(TIM4_IRQHandler);
WEAK_IRQ(I2C1_EV_IRQHandler);
WEAK_IRQ(I2C1_ER_IRQHandler);
WEAK_IRQ(I2C2_EV_IRQHandler);
WEAK_IRQ(I2C2_ER_IRQHandler);
WEAK_IRQ(SPI1_IRQHandler);
WEAK_IRQ(SPI2_IRQHandler);
WEAK_IRQ(USART1_IRQHandler);
WEAK_IRQ(USART2_IRQHandler);
WEAK_IRQ(USART3_IRQHandler);
WEAK_IRQ(EXTI15_10_IRQHandler);
WEAK_IRQ(RTC_Alarm_IRQHandler);
WEAK_IRQ(USB_FS_WKUP_IRQHandler);
WEAK_IRQ(TIM6_IRQHandler);
WEAK_IRQ(TIM7_IRQHandler);
WEAK_IRQ(SDIO_IRQHandler);
WEAK_IRQ(TIM5_IRQHandler);
WEAK_IRQ(SPI3_IRQHandler);
WEAK_IRQ(UART4_IRQHandler);
WEAK_IRQ(UART5_IRQHandler);
WEAK_IRQ(DMA2_Channel1_IRQHandler);
WEAK_IRQ(DMA2_Channel2_IRQHandler);
WEAK_IRQ(DMA2_Channel3_IRQHandler);
WEAK_IRQ(DMA2_Channel4_IRQHandler);
WEAK_IRQ(DMA2_Channel5_IRQHandler);
WEAK_IRQ(AES_IRQHandler);
WEAK_IRQ(COMP_ACQ_IRQHandler);

typedef void (*vector_t)(void);

__attribute__((section(".isr_vector"), used))
const vector_t g_pfnVectors[16 + 57] = {
    (vector_t)&_estack,
    Reset_Handler,
    NMI_Handler,
    HardFault_Handler,
    MemManage_Handler,
    BusFault_Handler,
    UsageFault_Handler,
    0, 0, 0, 0,
    SVC_Handler,
    DebugMon_Handler,
    0,
    PendSV_Handler,
    SysTick_Handler,
    /* External interrupts, IRQ 0..56 */
    WWDG_IRQHandler,
    PVD_IRQHandler,
    TAMPER_STAMP_IRQHandler,
    RTC_WKUP_IRQHandler,
    FLASH_IRQHandler,
    RCC_IRQHandler,
    EXTI0_IRQHandler,
    EXTI1_IRQHandler,
    EXTI2_IRQHandler,
    EXTI3_IRQHandler,
    EXTI4_IRQHandler,
    DMA1_Channel1_IRQHandler,
    DMA1_Channel2_IRQHandler,
    DMA1_Channel3_IRQHandler,
    DMA1_Channel4_IRQHandler,
    DMA1_Channel5_IRQHandler,
    DMA1_Channel6_IRQHandler,
    DMA1_Channel7_IRQHandler,
    ADC1_IRQHandler,
    USB_HP_IRQHandler,
    USB_LP_IRQHandler,
    DAC_IRQHandler,
    COMP_IRQHandler,
    EXTI9_5_IRQHandler,
    LCD_IRQHandler,
    TIM9_IRQHandler,
    TIM10_IRQHandler,
    TIM11_IRQHandler,
    TIM2_IRQHandler,
    TIM3_IRQHandler,
    TIM4_IRQHandler,
    I2C1_EV_IRQHandler,
    I2C1_ER_IRQHandler,
    I2C2_EV_IRQHandler,
    I2C2_ER_IRQHandler,
    SPI1_IRQHandler,
    SPI2_IRQHandler,
    USART1_IRQHandler,
    USART2_IRQHandler,
    USART3_IRQHandler,
    EXTI15_10_IRQHandler,
    RTC_Alarm_IRQHandler,
    USB_FS_WKUP_IRQHandler,
    TIM6_IRQHandler,
    TIM7_IRQHandler,
    SDIO_IRQHandler,
    TIM5_IRQHandler,
    SPI3_IRQHandler,
    UART4_IRQHandler,
    UART5_IRQHandler,
    DMA2_Channel1_IRQHandler,
    DMA2_Channel2_IRQHandler,
    DMA2_Channel3_IRQHandler,
    DMA2_Channel4_IRQHandler,
    DMA2_Channel5_IRQHandler,
    AES_IRQHandler,
    COMP_ACQ_IRQHandler,
};

void Default_Handler(void)
{
    for (;;) {
    }
}

/* orig: 0x08004EEC (Reset_Handler) + 0x08004E98 (C start-up) */
void Reset_Handler(void)
{
    SystemInit();

    for (uint32_t *src = &_siramfunc, *dst = &_sramfunc; dst < &_eramfunc;) {
        *dst++ = *src++;
    }
    for (uint32_t *src = &_sidata, *dst = &_sdata; dst < &_edata;) {
        *dst++ = *src++;
    }
    for (uint32_t *dst = &_sbss; dst < &_ebss;) {
        *dst++ = 0;
    }

    (void)main();
    for (;;) {
    }
}
