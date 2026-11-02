/*
 * Power-off (STOP mode) and the BLE module's UART.
 *
 * The ST library calls of the original are written out as register accesses;
 * single-bit library writes keep the bit-band accesses of the original.
 */
#include "power.h"
#include "board_io.h"
#include "ble_command.h"
#include "hy3131_bus.h"
#include "irq.h"
#include "lcd_driver.h"
#include "setup_menu.h"
#include "system_init.h"
#include "stm32l1xx.h"

#define BUZZER_PIN             7u
#define BLE_POWER_PIN          15u       /* PF15 */

#define RCC_AHBENR_GPIOD       0x08u
#define RCC_AHBENR_GPIOG       0x80u
#define RCC_APB1ENR_TIM2       (1u << 0)
#define RCC_APB1ENR_TIM3       (1u << 1)
#define RCC_APB1ENR_LCD        (1u << 9)
#define RCC_APB1ENR_USART2     (1u << 17)
#define RCC_APB1ENR_PWR        (1u << 28)
#define RCC_APB2ENR_SYSCFG     (1u << 0)
#define RCC_APB2ENR_ADC1       (1u << 9)

#define PWR_CR_VOS_RANGE3      0x1800u
#define USART_CR1_OVER8_BIT    0x8000u
#define USART_IT_RXNE          0x0525u   /* ST library code: CR1 bit 5 */

#define IRQ_USART2             38
#define EXTI_PORT_GPIOG        7u
#define EXTI_TRIGGER_FALLING   0x0Cu

#define FRONT_END_NONE         100u
#define BLE_BAUD               19200u

/* 16-bit access to a USART register, as the ST library does */
#define USART_REG16(usart, off) (*(volatile uint16_t *)((uint32_t)(usart) + (off)))

/* ---- clock frequencies (RCC_GetClocksFreq) ------------------------------------------ */

typedef struct {
    uint32_t sysclk;
    uint32_t hclk;
    uint32_t pclk1;
    uint32_t pclk2;
} rcc_clocks_t;

static const uint8_t pll_mul_table[9] = {3, 4, 6, 8, 12, 16, 24, 32, 48};
static const uint8_t prescaler_shift[16] = {0, 0, 0, 0, 1, 2, 3, 4, 1, 2, 3, 4, 6, 7, 8, 9};

/* orig: 0x0801FE2A (RCC_GetClocksFreq, with the library's 8 MHz HSE value) */
void rcc_get_clocks(rcc_clocks_t *clocks)
{
    uint32_t sws = RCC->CFGR & 0xCu;

    if (sws == 0x4u) {
        clocks->sysclk = 16000000u;
    } else if (sws == 0x8u) {
        clocks->sysclk = 8000000u;
    } else if (sws == 0xCu) {
        uint32_t cfgr = RCC->CFGR;
        uint32_t div = ((RCC->CFGR & 0xC00000u) >> 22) + 1u;
        uint32_t mul = pll_mul_table[(cfgr & 0x3C0000u) >> 18];

        if ((RCC->CFGR & 0x10000u) == 0) {
            clocks->sysclk = (mul * 16000000u) / div;
        } else {
            clocks->sysclk = (mul * 8000000u) / div;
        }
    } else {
        clocks->sysclk = (1u << ((((RCC->ICSCR & 0xFFFFu) >> 13)) + 1u)) * 0x8000u;
    }
    clocks->hclk = clocks->sysclk >> prescaler_shift[(RCC->CFGR & 0xF0u) >> 4];
    clocks->pclk1 = clocks->hclk >> prescaler_shift[(RCC->CFGR & 0x700u) >> 8];
    clocks->pclk2 = clocks->hclk >> prescaler_shift[(RCC->CFGR & 0x3800u) >> 11];
}

/* ---- USART set-up (USART_Init) ------------------------------------------------------------ */

typedef struct {
    uint32_t baud;
    uint16_t word_length;
    uint16_t stop_bits;
    uint16_t parity;
    uint16_t mode;
    uint16_t flow_control;
} usart_config_t;

/* orig: 0x0801CAEC (USART_Init) */
void usart_init(USART_TypeDef *usart, const usart_config_t *cfg)
{
    rcc_clocks_t clocks;
    uint32_t apb_clock;
    uint32_t div;
    uint32_t brr;
    uint32_t fraction;

    USART_REG16(usart, 0x10) = (uint16_t)((USART_REG16(usart, 0x10) & ~0x3000u) | cfg->stop_bits);
    USART_REG16(usart, 0x0C) = (uint16_t)((USART_REG16(usart, 0x0C) & 0xFFFFE9F3u)
                                          | cfg->word_length | cfg->parity | cfg->mode);
    USART_REG16(usart, 0x14) = (uint16_t)((USART_REG16(usart, 0x14) & ~0x300u) | cfg->flow_control);

    rcc_get_clocks(&clocks);
    apb_clock = (usart == USART1) ? clocks.pclk2 : clocks.pclk1;

    if (USART_REG16(usart, 0x0C) & USART_CR1_OVER8_BIT) {
        div = (apb_clock * 25u) / (cfg->baud * 2u);
    } else {
        div = (apb_clock * 25u) / (cfg->baud * 4u);
    }
    brr = (div / 100u) << 4;
    fraction = div - 100u * (brr >> 4);
    if (USART_REG16(usart, 0x0C) & USART_CR1_OVER8_BIT) {
        brr |= ((fraction * 8u + 50u) / 100u) & 0x7u;
    } else {
        brr |= ((fraction * 16u + 50u) / 100u) & 0xFu;
    }
    USART_REG16(usart, 0x08) = (uint16_t)brr;
}

/* ---- shutting things down ----------------------------------------------------------------- */

/* orig: 0x0801C908 - blank the display and stop the LCD; inlines: 0x080203FA (LCD_Cmd) */
void lcd_off(void)
{
    lcd_clear_shadow();
    lcd_update();
    BITBAND_PERIPH(&LCD->CR, 0) = 0;
    while (lcd_flag_set(LCD_SR_ENS) == 1) {
    }
    RCC->APB1ENR &= ~RCC_APB1ENR_LCD;
}

/* orig: 0x0801C92E; inlines: 0x0802096C (TIM_Cmd), 0x0801FFBC (RCC_APB1PeriphClockCmd) */
void timers_off(void)
{
    volatile uint16_t *tim2_cr1 = (volatile uint16_t *)&TIM2->CR1;
    volatile uint16_t *tim3_cr1 = (volatile uint16_t *)&TIM3->CR1;

    *tim2_cr1 &= 0xFFFEu;
    RCC->APB1ENR &= ~RCC_APB1ENR_TIM2;
    *tim3_cr1 &= 0xFFFEu;
    RCC->APB1ENR &= ~RCC_APB1ENR_TIM3;
}

/* orig: 0x0801CC08 (USART_ITConfig) */
static void usart_it_config(USART_TypeDef *usart, uint16_t it, uint8_t enable)
{
    uint32_t reg = (uint8_t)it >> 5;
    uint32_t bit = 1u << (it & 0x1Fu);
    volatile uint32_t *cr;

    if (reg == 1) {
        cr = &usart->CR1;
    } else if (reg == 2) {
        cr = &usart->CR2;
    } else {
        cr = &usart->CR3;
    }
    if (enable != 0) {
        *cr |= bit;
    } else {
        *cr &= ~bit;
    }
}

/* orig: 0x0801C954 - BLE module off: power pin low, USART2 stopped */
void ble_uart_off(void)
{
    gpio_reset(GPIOF, BLE_POWER_PIN);
    usart_it_config(USART2, USART_IT_RXNE, 0);
    usart_enable(USART2, 0);
    RCC->APB1ENR &= ~RCC_APB1ENR_USART2;
}

/* orig: 0x0801C390 - BLE module on: power pin high, USART2 (PD5/PD6) at 19200 baud, receive interrupt */
void ble_uart_on(void)
{
    usart_config_t cfg;

    gpio_set(GPIOF, BLE_POWER_PIN);
    RCC->AHBENR |= RCC_AHBENR_GPIOD;
    gpio_init_pins(GPIOD, 0x0060, GPIO_MODE_AF, GPIO_SPEED_40MHZ, GPIO_PUSH_PULL, GPIO_PULLUP);
    gpio_set_af(GPIOD, 5, 7);
    gpio_set_af(GPIOD, 6, 7);
    RCC->APB1ENR |= RCC_APB1ENR_USART2;

    cfg.baud = BLE_BAUD;
    cfg.word_length = 0;
    cfg.stop_bits = 0;
    cfg.parity = 0;
    cfg.flow_control = 0;
    cfg.mode = 0x000C;          /* receive and transmit */
    usart_init(USART2, &cfg);
    usart_enable(USART2, 1);
    usart_it_config(USART2, USART_IT_RXNE, 1);
    nvic_enable_irq(IRQ_USART2);
}

/* orig: 0x0801C97E; inlines: 0x08020104 (ADC_Cmd), 0x0801FF9E (RCC_APB2PeriphClockCmd) */
void adc_off(void)
{
    ADC1->CR2 &= ~1u;
    RCC->APB2ENR &= ~RCC_APB2ENR_ADC1;
}

/* orig: 0x0801C9A4; inlines: 0x080207A4 (PWR_PVDCmd) */
void pvd_off(void)
{
    BITBAND_PERIPH(&PWR->CR, 4) = 0;
}

/* orig: 0x0801C9AE - ultra-low-power mode and only GPIOA/GPIOC and PWR clocked; inlines: 0x080207AC (PWR_UltraLowPowerCmd) */
void clocks_minimal(void)
{
    BITBAND_PERIPH(&PWR->CR, 9) = 1;
    RCC->AHBENR = 0x05u;
    RCC->AHBLPENR = 0x05u;
    RCC->APB1ENR = RCC_APB1ENR_PWR;
    RCC->APB2ENR = 0;
}

/* orig: 0x0801C9D8 - SysTick off, wake-up flag cleared; inlines: 0x08020822 (PWR_ClearFlag) */
void systick_off(void)
{
    SysTick->CTRL = 0;
    SysTick->CTRL &= ~2u;
    PWR->CR |= PWR_CR_CWUF;
}

/* orig: 0x0801CA0C - HSI, HSE and LSE off; inlines: 0x0801FDE2 (RCC_HSICmd), 0x0801FD7A (RCC_HSEConfig), 0x0801FDEC (RCC_LSEConfig) */
void oscillators_off(void)
{
    BITBAND_PERIPH(&RCC->CR, 0) = 0;
    RCC_CR_BYTE2 = 0;
    RCC_CR_BYTE2 = 0;
    RCC_CSR_BYTE1 = 0;
    RCC_CSR_BYTE1 = 0;
}

/* orig: 0x0801CA22 - every pin analog, except PA13/PA14 (SWD); then the port clocks off */
void pins_all_analog(void)
{
    enable_gpio_clocks();
    gpio_init_pins(GPIOB, 0xFFFF, GPIO_MODE_ANALOG, GPIO_SPEED_400KHZ, GPIO_PUSH_PULL, GPIO_NOPULL);
    gpio_init_pins(GPIOC, 0xFFFF, GPIO_MODE_ANALOG, GPIO_SPEED_400KHZ, GPIO_PUSH_PULL, GPIO_NOPULL);
    gpio_init_pins(GPIOG, 0xFFFF, GPIO_MODE_ANALOG, GPIO_SPEED_400KHZ, GPIO_PUSH_PULL, GPIO_NOPULL);
    gpio_init_pins(GPIOH, 0xFFFF, GPIO_MODE_ANALOG, GPIO_SPEED_400KHZ, GPIO_PUSH_PULL, GPIO_NOPULL);
    gpio_init_pins(GPIOD, 0xFFFF, GPIO_MODE_ANALOG, GPIO_SPEED_400KHZ, GPIO_PUSH_PULL, GPIO_NOPULL);
    gpio_init_pins(GPIOE, 0xFFFF, GPIO_MODE_ANALOG, GPIO_SPEED_400KHZ, GPIO_PUSH_PULL, GPIO_NOPULL);
    gpio_init_pins(GPIOF, 0xFFFF, GPIO_MODE_ANALOG, GPIO_SPEED_400KHZ, GPIO_PUSH_PULL, GPIO_NOPULL);
    gpio_init_pins(GPIOA, 0x9FFF, GPIO_MODE_ANALOG, GPIO_SPEED_400KHZ, GPIO_PUSH_PULL, GPIO_NOPULL);
    gpio_clocks_disable();
}

/* ---- wake-up sources ----------------------------------------------------------------------- */

/* orig: 0x0801C852 - EXTI pin source of a line (the same number) */
uint8_t exti_pin_source(uint8_t line)
{
    return line;
}

/* orig: 0x0801C7AA - EXTI line mask (lines above 15 are returned as given) */
uint32_t exti_line_bit(uint8_t line)
{
    return (line < 16u) ? (1u << line) : line;
}

/* orig: 0x0801C756 - interrupt number of an EXTI line */
uint8_t exti_irq_for_line(uint8_t line)
{
    if (line <= 4u) {
        return (uint8_t)(6u + line);          /* EXTI0..EXTI4 */
    }
    if (line <= 9u) {
        return 23;                            /* EXTI9_5 */
    }
    if (line <= 15u) {
        return 40;                            /* EXTI15_10 */
    }
    return line;
}

/*
 * orig: 0x0801C6F4 - wake-up on a falling edge of PGn
 * inlines: 0x08020A74 (SYSCFG_EXTILineConfig), 0x08020AC8 (EXTI_Init, interrupt mode, falling edge)
 */
void exti_wake_line(uint8_t line)
{
    uint8_t source = exti_pin_source(line);
    uint32_t bit;
    volatile uint32_t *exticr = &SYSCFG->EXTICR[source >> 2];
    unsigned shift = (source & 3u) << 2;

    *exticr &= ~(0xFu << shift);
    *exticr |= EXTI_PORT_GPIOG << shift;

    bit = exti_line_bit(line);
    EXTI->IMR &= ~bit;
    EXTI->EMR &= ~bit;
    EXTI->IMR |= bit;
    EXTI->RTSR &= ~bit;
    EXTI->FTSR &= ~bit;
    EXTI->FTSR |= bit;

    nvic_irq_init(exti_irq_for_line(line), 0, line);
}

/*
 * orig: 0x0801C61C - PG0..PG7 (rotary switch, except the line of the current
 * position) and PG8..PG15 (keys) wake the meter. The original passes the
 * RCC_AHBENR value it has just written as rotary_pins_config()'s fallback.
 */
void wake_sources_config(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIOG;
    rotary_pins_config(g_rotary_pos, RCC->AHBENR);
    RCC->APB2ENR |= RCC_APB2ENR_SYSCFG;

    for (int line = 7; line >= 0; line--) {
        if (g_rotary_pos != (uint8_t)(1u << (7 - line))) {
            exti_wake_line((uint8_t)line);
        }
    }
    for (uint8_t line = 8; line < 16; line++) {
        exti_wake_line(line);
    }
}

/* orig: 0x0801C584 - external circuits off */
void power_circuits_off(void)
{
    gpio_reset(GPIOE, BUZZER_PIN);
    gpio_reset(GPIOE, 8);
    lcd_off();
    gpio_set(GPIOB, 6);          /* RTC bus */
    gpio_set(GPIOB, 7);
    gpio_reset(GPIOB, 6);
    hy3131_bus_idle();
    front_end_select(FRONT_END_NONE);
    gpio_reset(GPIOE, 12);
    gpio_reset(GPIOE, 13);
    gpio_reset(GPIOE, 5);
    gpio_reset(GPIOA, 11);
    gpio_reset(GPIOA, 12);
    gpio_reset(GPIOH, 2);
    gpio_reset(GPIOE, 15);
    gpio_reset(GPIOF, 6);
    ble_uart_off();
    adc_off();
    gpio_reset(GPIOE, 9);
}

/*
 * orig: 0x0801C522
 * inlines: 0x0801FD1C (RCC_DeInit), 0x080207B4 (PWR_VoltageScalingConfig),
 * 0x08020808 (PWR_GetFlagStatus), 0x0801FDCA (RCC_MSIRangeConfig),
 * 0x0801FE06 (RCC_SYSCLKConfig), 0x0801FE1E (RCC_GetSYSCLKSource),
 * 0x080209A8 (RTC_ITConfig)
 */
void power_down_prepare(void)
{
    power_circuits_off();
    timers_off();
    pvd_off();
    clocks_minimal();
    systick_off();

    RCC->CR |= 0x100u;                       /* MSI on */
    RCC->CFGR &= 0x88FFC00Cu;
    RCC->CR &= 0xEEFEFFFEu;
    RCC->CR &= 0xFFFBFFFFu;
    RCC->CFGR &= 0xFF02FFFFu;
    RCC->CIR = 0;

    RCC->APB1ENR |= RCC_APB1ENR_PWR;
    PWR->CR = ((PWR->CR & 0xFFFFE7FFu) | PWR_CR_VOS_RANGE3) & 0xFFFFFFF3u;
    while ((PWR->CSR & PWR_CSR_VOSF) != 0) {
    }
    RCC->ICSCR = (RCC->ICSCR & 0xFFFF1FFFu) | 0u;   /* MSI range 0 (65.536 kHz) */
    RCC->CFGR = (RCC->CFGR & 0xFFFFFFFCu) | 0u;     /* system clock MSI */
    while ((RCC->CFGR & 0xCu) != 0) {
    }

    RTC->WPR = 0xCA;                         /* wake-up timer interrupt off */
    RTC->WPR = 0x53;
    RTC->CR &= ~RTC_CR_WUTIE;
    RTC->TAFCR &= ~(RTC_CR_WUTIE & 4u);
    RTC->WPR = 0xFF;

    oscillators_off();
    pins_all_analog();
    wake_sources_config();
}

/* orig: 0x08024ECA - power off; returns after a wake-up with the meter restarted; inlines: 0x080207CC (PWR_EnterSTOPMode) */
void power_off(void)
{
    power_off_beep();
    g_power_off_request = 0;
    g_ble_enabled = 0;
    power_down_prepare();

    PWR->CR = (PWR->CR & 0xFFFFFFFCu) | PWR_CR_LPSDSR;
    SCB->SCR |= SCB_SCR_SLEEPDEEP_Msk;
    __WFE();
    SCB->SCR &= ~SCB_SCR_SLEEPDEEP_Msk;

    g_resumed_from_power_off = 1;
    system_init();
    app_state_init();
    lcd_init(g_lcd_contrast, 1);
    startup_screen();
}
