/*
 * Bootloader hardware bring-up: option bytes, clocks, GPIO, NVIC, TIM2, LCD.
 */
#include "sys_init.h"
#include "board.h"
#include "flash_if.h"
#include "gpio.h"
#include "lcd.h"
#include "stm32l1xx.h"

uint8_t g_use_hse;                 /* orig: 0x200008A0 */
uint8_t g_set_voltage_range;       /* orig: 0x200008A1 */
volatile uint8_t g_tick_flag;      /* orig: 0x200008A2 */

#define RCC_CR_HSION_BIT       (1u << 0)
#define RCC_CR_HSIRDY_BIT      (1u << 1)
#define RCC_CR_HSERDY_BIT      (1u << 17)
#define RCC_CFGR_SW_MASK       0x3u
#define RCC_SYSCLK_HSI         0x1u
#define RCC_SYSCLK_HSE         0x2u
#define RCC_CSR_LSION_BIT      (1u << 0)
#define RCC_CSR_LSIRDY_BIT     (1u << 1)
#define RCC_CSR_RTCSEL_MASK    0x30000u
/* RCC_ICSCR_MSIRANGE_6 (4.194 MHz) and RCC_CSR_RTCSEL_LSI come from the CMSIS header. */
#define RCC_CSR_RTCEN_BIT      (1u << 22)
#define RCC_CSR_RTCRST_BIT     (1u << 23)
#define RCC_AHBENR_GPIO_ALL    0x000000FFu
#define RCC_APB1ENR_BOOT       0x90000200u  /* COMP | PWR | LCD */
#define RCC_APB1ENR_TIM2_BIT   (1u << 0)
#define RCC_APB2ENR_BOOT       0x00000201u  /* ADC1 | SYSCFG */
#define PWR_CR_DBP_BIT         (1u << 8)
#define PWR_CR_VOS_MASK        0xFFFFE7FFu
#define PWR_CR_VOS_RANGE1      0x00000800u
#define PWR_CSR_VOSF_BIT       (1u << 4)
#define HSE_STARTUP_TIMEOUT    0x0500u

#define IRQ_TIM2               28
#define IRQ_SDIO               45
#define IRQ_DMA2_CHANNEL4      53
#define NVIC_PRIORITY_GROUP_2  0x500u

/* Byte 2 of RCC_CR holds HSEON/HSERDY/HSEBYP. */
#define RCC_CR_BYTE2 (*(volatile uint8_t *)((uint32_t)&RCC->CR + 2))

static void output_pins(GPIO_TypeDef *port, uint16_t pins)
{
    gpio_init_pins(port, pins, GPIO_MODE_OUT, GPIO_SPEED_2MHZ, GPIO_PUSH_PULL, GPIO_NOPULL);
}

static void input_pullup_pins(GPIO_TypeDef *port, uint16_t pins)
{
    gpio_init_pins(port, pins, GPIO_MODE_IN, GPIO_SPEED_40MHZ, GPIO_PUSH_PULL, GPIO_PULLUP);
}

/* orig: 0x08001B10 */
static int rcc_wait_for_hse_startup(void)
{
    uint32_t counter = 0;
    int ready;

    do {
        ready = (RCC->CR & RCC_CR_HSERDY_BIT) != 0;
        counter++;
    } while (counter != HSE_STARTUP_TIMEOUT && !ready);

    return (RCC->CR & RCC_CR_HSERDY_BIT) != 0;
}

/* orig: 0x080041CA
 * inlines: 0x08001B00 RCC_HSEConfig, 0x08001B50 RCC_MSIRangeConfig, 0x08001B68 RCC_HSICmd,
 *          0x08001B7C RCC_SYSCLKConfig, 0x08001C58 RCC_GetFlagStatus,
 *          0x08001BE0/0x08001BFE/0x08001C1C RCC_AHB/APB2/APB1PeriphClockCmd,
 *          0x0800426E (enable all GPIO clocks) */
static void clock_init(void)
{
    if (g_use_hse == 0) {
        RCC->CR |= RCC_CR_HSION_BIT;
        while ((RCC->CR & RCC_CR_HSIRDY_BIT) == 0) {
        }
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_MASK) | RCC_SYSCLK_HSI;
        RCC->ICSCR = (RCC->ICSCR & 0xFFFF1FFFu) | RCC_ICSCR_MSIRANGE_6;
        RCC_CR_BYTE2 = 0;   /* HSE off */
        RCC_CR_BYTE2 = 0;
        if (RCC->CR & RCC_CR_HSERDY_BIT) {
            for (;;) {
            }
        }
    } else {
        RCC_CR_BYTE2 = 0;
        RCC_CR_BYTE2 = 1;   /* HSE on */
        if (rcc_wait_for_hse_startup()) {
            RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_MASK) | RCC_SYSCLK_HSE;
            RCC->CR &= ~RCC_CR_HSION_BIT;
        }
    }

    RCC->AHBENR |= RCC_AHBENR_GPIO_ALL;
    RCC->APB1ENR |= RCC_APB1ENR_BOOT;
    RCC->APB2ENR |= RCC_APB2ENR_BOOT;
}

/* orig: 0x0800423A - RTC domain clock from LSI (also clocks the LCD)
 * inlines: 0x080046E0 PWR_RTCAccessCmd, 0x08001BD8 RCC_RTCResetCmd, 0x08001B72 RCC_LSICmd,
 *          0x08001BD0 RCC_RTCCLKCmd, 0x08001B94 RCC_RTCCLKConfig */
static void rtc_clock_init(void)
{
    PWR->CR |= PWR_CR_DBP_BIT;
    RCC->CSR |= RCC_CSR_RTCRST_BIT;
    RCC->CSR &= ~RCC_CSR_RTCRST_BIT;
    RCC->CSR |= RCC_CSR_LSION_BIT;
    while ((RCC->CSR & RCC_CSR_LSIRDY_BIT) == 0) {
    }
    RCC->CSR |= RCC_CSR_RTCEN_BIT;
    RCC->CSR &= ~RCC_CSR_RTCSEL_MASK;
    RCC->CSR |= RCC_CSR_RTCSEL_LSI;
}

/* orig: 0x080042D4 */
static void gpio_init(void)
{
    /* Keys: all of port G as inputs with pull-ups. */
    input_pullup_pins(GPIOG, 0xFFFF);

    /* Port E control outputs (analogue switching etc., roles to be confirmed). */
    output_pins(GPIOE, 0x33F0);
    gpio_set(GPIOE, 9);
    gpio_set(GPIOE, 8);
    gpio_reset(GPIOE, 7);
    gpio_reset(GPIOE, 12);
    gpio_reset(GPIOE, 13);
    gpio_reset(GPIOE, 5);
    gpio_reset(GPIOE, 4);
    gpio_reset(GPIOE, 6);

    output_pins(GPIOF, 1u << 2);
    output_pins(GPIOF, 1u << 0);
    output_pins(GPIOF, 1u << 1);
    input_pullup_pins(GPIOF, 1u << 3);
    gpio_set(GPIOF, 1);
    gpio_set(GPIOF, 2);
    gpio_reset(GPIOF, 0);

    output_pins(GPIOF, 0x87C0);
    gpio_set(GPIOF, 9);
    gpio_reset(GPIOF, 7);
    gpio_reset(GPIOF, 6);
    gpio_reset(GPIOF, 10);
    gpio_set(GPIOF, 8);
    gpio_set(GPIOF, 15);

    output_pins(GPIOD, 1u << 7);
    gpio_reset(GPIOD, 7);
}

/* orig: 0x08004732 (enable path of NVIC_Init) */
static void nvic_enable(unsigned irq, uint32_t preempt, uint32_t sub)
{
    uint32_t group_shift = (0x700u - (SCB->AIRCR & 0x700u)) >> 8;
    uint32_t priority = (preempt << (4u - group_shift)) | (sub & (0x0Fu >> group_shift));

    NVIC->IP[irq] = (uint8_t)(priority << 4);
    NVIC->ISER[irq >> 5] = 1u << (irq & 0x1F);
}

/* orig: 0x0800427A
 * inlines: 0x08004728 NVIC_PriorityGroupConfig */
static void nvic_init(void)
{
    SCB->AIRCR = 0x05FA0000u | NVIC_PRIORITY_GROUP_2;
    nvic_enable(IRQ_SDIO, 0, 0);
    nvic_enable(IRQ_DMA2_CHANNEL4, 0, 0);
    nvic_enable(IRQ_TIM2, 2, 2);
}

/* orig: 0x08004814 */
void tim2_enable(int enable)
{
    if (enable) {
        TIM2->CR1 |= TIM_CR1_CEN;
    } else {
        TIM2->CR1 &= (uint16_t)~TIM_CR1_CEN;
    }
}

/* orig: 0x080044A0 - update interrupt at 4 MHz / 1001 / 161 = ~24.8 Hz
 * inlines: 0x080047B0 TIM_TimeBaseInit, 0x08004846 TIM_ClearITPendingBit,
 *          0x08004830 TIM_ITConfig */
static void tim2_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2_BIT;

    uint16_t cr1 = (uint16_t)TIM2->CR1;
    cr1 = (cr1 & 0xFF8Fu) | 0x0000u;   /* count up */
    cr1 = (cr1 & 0xFCFFu) | 0x0000u;   /* clock division 1 */
    TIM2->CR1 = cr1;
    TIM2->ARR = 160;
    TIM2->PSC = 1000;
    TIM2->EGR = TIM_EGR_UG;

    TIM2->SR = (uint16_t)~TIM_SR_UIF;
    TIM2->DIER |= TIM_DIER_UIE;
    tim2_enable(1);
}

/* orig: 0x08004168
 * inlines: 0x080046E8 PWR_VoltageScalingConfig, 0x08004700 PWR_GetFlagStatus */
void system_init(void)
{
    /* Force the brown-out reset level to 0 (off); relaunching the option
     * bytes resets the MCU, after which this test passes. */
    if (flash_ob_get_bor() & 0x0F) {
        flash_ob_unlock();
        flash_clear_flags(FLASH_FLAGS_NO_OPTVUSR);
        (void)flash_ob_bor_config(0);
        flash_ob_launch();
    }

    clock_init();
    rtc_clock_init();

    if (g_set_voltage_range == 1) {
        PWR->CR = ((PWR->CR & PWR_CR_VOS_MASK) | PWR_CR_VOS_RANGE1) & 0xFFFFFFF3u;
        while (PWR->CSR & PWR_CSR_VOSF_BIT) {
        }
    }

    gpio_init();
    nvic_init();
    tim2_init();
    lcd_init();
    RCC->AHBENR |= RCC_AHBENR_GPIO_ALL;
}
