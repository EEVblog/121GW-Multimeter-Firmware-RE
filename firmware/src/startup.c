/*
 * Start-up code and vector table of the application (linked at 0x08006000,
 * entered from the SD bootloader).
 *
 * As the original: SystemInit() runs first; then RAM 0x20001240..0x200080FB
 * is cleared and the initialised variables are copied to 0x20000000 (the
 * original IAR runtime does both from a table: __iar_zero_init3 and
 * __iar_lz77_init_single3); then main() runs. The stack is the 4 KB below
 * 0x20001240, which neither step touches.
 */
#include <stdint.h>
#include "irq.h"
#include "system_init.h"

#define STACK_TOP        0x20001240u
#define ZERO_START       0x20001240u
#define ZERO_BYTES       0x6EBCu
#define INIT_START       0x20000000u

extern const uint8_t g_ram_init_image[574];

int main(void);
void Reset_Handler(void);
void Default_Handler(void);
void USART2_IRQHandler(void);
void TIM2_IRQHandler(void);

/*
 * orig: 0x08029174 (Reset_Handler)
 * inlines: 0x0802910C (__cmain), 0x08029126 (__low_level_init),
 * 0x08028FE4 (__iar_data_init3), 0x08028CDC (__iar_zero_init3),
 * 0x080287C0 (__iar_lz77_init_single3)
 * main() does not return; the original would end in exit() (0x0802912A,
 * 0x08029130, 0x0802913C: a semihosting breakpoint loop).
 */
void Reset_Handler(void)
{
    volatile uint32_t *zero = (volatile uint32_t *)ZERO_START;
    volatile uint8_t *init = (volatile uint8_t *)INIT_START;

    SystemInit();
    for (uint32_t i = 0; i < ZERO_BYTES / 4u; i++) {
        zero[i] = 0;
    }
    for (uint32_t i = 0; i < sizeof g_ram_init_image; i++) {
        init[i] = g_ram_init_image[i];
    }
    (void)main();
    for (;;) {
    }
}

/* orig: 0x08029298..0x08029340 - unused interrupts spin */
void Default_Handler(void)
{
    for (;;) {
    }
}

#define WEAK_IRQ(name) void name(void) __attribute__((weak, alias("Default_Handler")))
WEAK_IRQ(WWDG_IRQHandler);
WEAK_IRQ(PVD_IRQHandler);
WEAK_IRQ(TAMPER_STAMP_IRQHandler);
WEAK_IRQ(FLASH_IRQHandler);
WEAK_IRQ(RCC_IRQHandler);
WEAK_IRQ(DMA1_Channel1_IRQHandler);
WEAK_IRQ(DMA1_Channel2_IRQHandler);
WEAK_IRQ(DMA1_Channel3_IRQHandler);
WEAK_IRQ(DMA1_Channel4_IRQHandler);
WEAK_IRQ(DMA1_Channel5_IRQHandler);
WEAK_IRQ(DMA1_Channel6_IRQHandler);
WEAK_IRQ(DMA1_Channel7_IRQHandler);
WEAK_IRQ(USB_HP_IRQHandler);
WEAK_IRQ(USB_LP_IRQHandler);
WEAK_IRQ(DAC_IRQHandler);
WEAK_IRQ(COMP_IRQHandler);
WEAK_IRQ(LCD_IRQHandler);
WEAK_IRQ(TIM9_IRQHandler);
WEAK_IRQ(TIM10_IRQHandler);
WEAK_IRQ(TIM11_IRQHandler);
WEAK_IRQ(TIM4_IRQHandler);
WEAK_IRQ(I2C1_EV_IRQHandler);
WEAK_IRQ(I2C1_ER_IRQHandler);
WEAK_IRQ(I2C2_EV_IRQHandler);
WEAK_IRQ(I2C2_ER_IRQHandler);
WEAK_IRQ(SPI1_IRQHandler);
WEAK_IRQ(SPI2_IRQHandler);
WEAK_IRQ(USART1_IRQHandler);
WEAK_IRQ(USART3_IRQHandler);
WEAK_IRQ(RTC_Alarm_IRQHandler);
WEAK_IRQ(USB_FS_WKUP_IRQHandler);
WEAK_IRQ(TIM6_IRQHandler);
WEAK_IRQ(TIM7_IRQHandler);
WEAK_IRQ(TIM5_IRQHandler);
WEAK_IRQ(SPI3_IRQHandler);
WEAK_IRQ(UART4_IRQHandler);
WEAK_IRQ(UART5_IRQHandler);
WEAK_IRQ(DMA2_Channel1_IRQHandler);
WEAK_IRQ(DMA2_Channel2_IRQHandler);
WEAK_IRQ(DMA2_Channel3_IRQHandler);
WEAK_IRQ(DMA2_Channel5_IRQHandler);
WEAK_IRQ(AES_IRQHandler);
WEAK_IRQ(COMP_ACQ_IRQHandler);

typedef void (*vector_t)(void);

/* orig: vector table at 0x08006000 */
__attribute__((section(".isr_vector"), used))
const vector_t g_vectors[16 + 57] = {
    (vector_t)STACK_TOP,
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
    /* IRQ 0..56 */
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
    DMA2_CH4_IRQHandler,
    DMA2_Channel5_IRQHandler,
    AES_IRQHandler,
    COMP_ACQ_IRQHandler,
};
