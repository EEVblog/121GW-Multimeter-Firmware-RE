/*
 * Application start-up: clocks, power, GPIO, ADC, NVIC, timers.
 */
#include "system_init.h"
#include "board_io.h"
#include "flash_ob.h"
#include "stm32l1xx.h"

#define HSE_STARTUP_TIMEOUT    0x5000u

#define RCC_CR_HSION_BIT       (1u << 0)
#define RCC_CR_HSIRDY_BIT      (1u << 1)
#define RCC_CR_MSION_BIT       (1u << 8)
#define RCC_CR_HSEON_BIT       (1u << 16)
#define RCC_CR_HSERDY_BIT      (1u << 17)
#define RCC_CR_PLLON_BIT       (1u << 24)
#define RCC_CR_PLLRDY_BIT      (1u << 25)
#define RCC_CFGR_SW_MASK       0x3u
#define RCC_SYSCLK_MSI         0x0u
#define RCC_SYSCLK_HSE         0x2u
#define RCC_SYSCLK_PLL         0x3u
#define RCC_CFGR_SWS_PLL_VAL   0xCu
#define RCC_CFGR_PLL_HSE_X12_DIV3 0x00910000u
#define RCC_CSR_LSION_BIT      (1u << 0)
#define RCC_CSR_LSIRDY_BIT     (1u << 1)
#define RCC_CSR_RTCSEL_MASK    0x30000u
#define RCC_CSR_RTCEN_BIT      (1u << 22)
#define RCC_CSR_RTCRST_BIT     (1u << 23)
#define RCC_AHBENR_GPIO_ALL    0xFFu
#define RCC_AHBENR_GPIOG       0x80u
#define RCC_APB1ENR_PWR_BIT    (1u << 28)
#define RCC_APB1ENR_APP        0x10000200u   /* PWR | LCD */
#define RCC_APB1ENR_TIM2_BIT   (1u << 0)
#define RCC_APB1ENR_TIM3_BIT   (1u << 1)
#define RCC_APB2ENR_APP        0x00000201u   /* ADC1 | SYSCFG */
#define RCC_APB2RSTR_ADC1      0x00000200u
#define FLASH_ACR_LATENCY_BIT  (1u << 0)
#define FLASH_ACR_PRFTEN_BIT   (1u << 1)
#define FLASH_ACR_ACC64_BIT    (1u << 2)
#define PWR_CR_DBP_BIT         (1u << 8)
#define PWR_CR_VOS_RANGE1      0x00000800u
#define PWR_FLAG_WU            0x01u
#define PWR_FLAG_SB            0x02u
#define PWR_CSR_VOSF_BIT       (1u << 4)
#define DBGMCU_CR_LOWPOWER     0x7u          /* debug in sleep, stop and standby */

#define IRQ_TIM2               28
#define IRQ_TIM3               29
#define IRQ_SDIO               45
#define IRQ_DMA2_CHANNEL4      53

#define TIM_PRESCALER          260u
#define TIM2_PERIOD            160u          /* 4.194 MHz / 261 / 161 = ~99.8 Hz */
#define TIM3_PERIOD            15u           /* 4.194 MHz / 261 / 16 = ~1 kHz */

#define FRONT_END_NONE         100u

/* ---- Reset-time clock set-up ---------------------------------------------- */

/* orig: 0x08025FC2 - as the bootloader's, with a 10x longer HSE timeout */
static void set_sys_clock(void)
{
    uint32_t startup_counter = 0;

    RCC->CR |= RCC_CR_HSEON_BIT;
    do {
        startup_counter++;
        if (RCC->CR & RCC_CR_HSERDY_BIT) {
            break;
        }
    } while (startup_counter != HSE_STARTUP_TIMEOUT);

    if ((RCC->CR & RCC_CR_HSERDY_BIT) == 0) {
        return;
    }

    FLASH->ACR |= FLASH_ACR_ACC64_BIT;
    FLASH->ACR |= FLASH_ACR_PRFTEN_BIT;
    FLASH->ACR |= FLASH_ACR_LATENCY_BIT;

    RCC->APB1ENR |= RCC_APB1ENR_PWR_BIT;
    PWR->CR = PWR_CR_VOS_RANGE1;
    while (PWR->CSR & PWR_CSR_VOSF_BIT) {
    }

    RCC->CFGR = RCC->CFGR;   /* HCLK, PCLK2, PCLK1 undivided */
    RCC->CFGR = RCC->CFGR;
    RCC->CFGR = RCC->CFGR;

    RCC->CFGR &= 0xFF02FFFFu;
    RCC->CFGR |= RCC_CFGR_PLL_HSE_X12_DIV3;
    RCC->CR |= RCC_CR_PLLON_BIT;
    while ((RCC->CR & RCC_CR_PLLRDY_BIT) == 0) {
    }

    RCC->CFGR &= ~RCC_CFGR_SW_MASK;
    RCC->CFGR |= RCC_SYSCLK_PLL;
    while ((RCC->CFGR & 0xCu) != RCC_CFGR_SWS_PLL_VAL) {
    }
}

/* orig: 0x08025F70 */
void SystemInit(void)
{
    RCC->CR |= RCC_CR_MSION_BIT;
    RCC->CFGR &= 0x88FFC00Cu;
    RCC->CR &= 0xEEFEFFFEu;
    RCC->CR &= 0xFFFBFFFFu;
    RCC->CFGR &= 0xFF02FFFFu;
    RCC->CIR = 0;

    set_sys_clock();

    SCB->VTOR = FLASH_BASE;   /* main() moves it to 0x08006000 */
}

/* ---- Clocks ---------------------------------------------------------------- */

/* orig: 0x0801BFC6 */
void enable_gpio_clocks(void)
{
    RCC->AHBENR |= RCC_AHBENR_GPIO_ALL;
}

/* orig: 0x0801FD8A */
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

/*
 * orig: 0x0801BF1A; inlines: 0x0801FFF8 (RCC_GetFlagStatus)
 * use_hse == 0 (the only use in the application): MSI at 4.194 MHz, HSI on
 * (for the ADC), LSE and HSE off. Hangs if the HSE refuses to stop.
 */
void clock_init(uint8_t use_hse)
{
    if (use_hse == 0) {
        RCC->CR |= RCC_CR_HSION_BIT;
        while ((RCC->CR & RCC_CR_HSIRDY_BIT) == 0) {
        }
        RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_MASK) | RCC_SYSCLK_MSI;
        RCC->ICSCR = (RCC->ICSCR & 0xFFFF1FFFu) | RCC_ICSCR_MSIRANGE_6;
        RCC_CSR_BYTE1 = 0;    /* LSE off */
        RCC_CSR_BYTE1 = 0;
        RCC_CR_BYTE2 = 0;     /* HSE off */
        RCC_CR_BYTE2 = 0;
        if (RCC->CR & RCC_CR_HSERDY_BIT) {
            for (;;) {
            }
        }
    } else {
        RCC_CR_BYTE2 = 0;
        RCC_CR_BYTE2 = 1;
        if (rcc_wait_for_hse_startup()) {
            RCC->CFGR = (RCC->CFGR & ~RCC_CFGR_SW_MASK) | RCC_SYSCLK_HSE;
            RCC->CR &= ~RCC_CR_HSION_BIT;
        }
    }

    enable_gpio_clocks();
    RCC->APB1ENR |= RCC_APB1ENR_APP;
    RCC->APB2ENR |= RCC_APB2ENR_APP;
}

/*
 * orig: 0x0801BF92 - RTC domain (and LCD) clocked from LSI; as the bootloader's.
 * inlines: 0x0801FDFC (RCC_LSICmd), 0x0801FF34 (RCC_RTCCLKConfig), 0x0801FF70
 * (RCC_RTCCLKCmd), 0x0801FF78 (RCC_RTCResetCmd), 0x0802079C (PWR_RTCAccessCmd)
 */
void rtc_clock_init(void)
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

/* ---- GPIO -------------------------------------------------------------------- */

/*
 * orig: 0x0801C2C6
 * Rotary inputs PG0..PG7 with pull-ups, except the pin of the selected
 * position (one-hot code as in g_rotary_raw); then keys PG8..PG15. For a
 * code that is not one-hot the original uses whatever its caller left in r2
 * as the pin mask: that value is passed explicitly as fallback_pins.
 */
void rotary_pins_config(uint8_t selected, uint32_t fallback_pins)
{
    uint32_t pins;

    switch (selected) {
    case 0x00: pins = 0xFF; break;
    case 0x01: pins = 0x7F; break;
    case 0x02: pins = 0xBF; break;
    case 0x04: pins = 0xDF; break;
    case 0x08: pins = 0xEF; break;
    case 0x10: pins = 0xF7; break;
    case 0x20: pins = 0xFB; break;
    case 0x40: pins = 0xFD; break;
    case 0x80: pins = 0xFE; break;
    default:   pins = fallback_pins; break;
    }
    gpio_init_pins(GPIOG, (uint16_t)pins, GPIO_MODE_IN, GPIO_SPEED_40MHZ, GPIO_PUSH_PULL,
                   GPIO_PULLUP);
    gpio_init_pins(GPIOG, 0xFF00, GPIO_MODE_IN, GPIO_SPEED_40MHZ, GPIO_PUSH_PULL, GPIO_PULLUP);
}

/* orig: 0x0801C368 - PE10, PE11 inputs without pull */
void porte_inputs_init(void)
{
    gpio_init_pins(GPIOE, 0x0C00, GPIO_MODE_IN, GPIO_SPEED_40MHZ, GPIO_PUSH_PULL, GPIO_NOPULL);
}

/* orig: 0x0801C43E - ADC inputs PA4, PA5, PF11, PF13, PF14 */
void adc_pins_analog(void)
{
    gpio_init_pins(GPIOA, 0x0030, GPIO_MODE_ANALOG, GPIO_SPEED_40MHZ, GPIO_PUSH_PULL, GPIO_NOPULL);
    gpio_init_pins(GPIOF, 0x6800, GPIO_MODE_ANALOG, GPIO_SPEED_40MHZ, GPIO_PUSH_PULL, GPIO_NOPULL);
}

/*
 * orig: 0x0800FC60
 * Analogue front-end switching per measurement function (PD0, PF4, PF5,
 * PC11, PD7). FRONT_END_NONE (100) selects the idle state.
 */
void front_end_select(uint8_t function)
{
    switch (function) {
    case 0x00: case 0x02: case 0x04: case 0x0D: case 0x0E: case 0x0F:
    case 0x10: case 0x12: case 0x14:
        if ((function == 0x02 || function == 0x04) && g_flag_80b9 == 1) {
            gpio_reset(GPIOD, 0);
            gpio_reset(GPIOF, 4);
            gpio_set(GPIOF, 5);
        } else {
            gpio_reset(GPIOD, 0);
            gpio_set(GPIOF, 4);
            gpio_reset(GPIOF, 5);
        }
        break;
    default:
        gpio_set(GPIOD, 0);
        gpio_set(GPIOF, 4);
        gpio_set(GPIOF, 5);
        break;
    }

    if (function == 0x09) {
        gpio_reset(GPIOC, 11);
    } else {
        gpio_set(GPIOC, 11);
    }
    if (function == 0x02) {
        gpio_reset(GPIOD, 7);
    } else {
        gpio_set(GPIOD, 7);
    }
}

static void output_pins(GPIO_TypeDef *port, uint16_t pins)
{
    gpio_init_pins(port, pins, GPIO_MODE_OUT, GPIO_SPEED_2MHZ, GPIO_PUSH_PULL, GPIO_NOPULL);
}

/* orig: 0x0801C056 - control outputs and their initial levels (roles mostly unknown) */
void board_gpio_init(void)
{
    enable_gpio_clocks();
    rotary_pins_config(0, 0);
    porte_inputs_init();

    output_pins(GPIOD, 0x0083);
    gpio_set(GPIOD, 1);
    gpio_set(GPIOD, 7);

    output_pins(GPIOE, 0xB3A0);
    gpio_set(GPIOE, 9);
    gpio_reset(GPIOE, 8);
    gpio_reset(GPIOE, 7);
    gpio_reset(GPIOE, 12);
    gpio_reset(GPIOE, 13);
    gpio_reset(GPIOE, 5);

    /* HY3131 bus: CK, CS, DI outputs; DO input with pull-up */
    output_pins(GPIOF, 1u << 2);
    output_pins(GPIOF, 1u << 0);
    output_pins(GPIOF, 1u << 1);
    gpio_init_pins(GPIOF, 1u << 3, GPIO_MODE_IN, GPIO_SPEED_40MHZ, GPIO_PUSH_PULL, GPIO_PULLUP);
    gpio_set(GPIOF, 1);
    gpio_set(GPIOF, 2);
    gpio_reset(GPIOF, 0);

    /* RTC bus: CLK, DATA */
    output_pins(GPIOB, 0x00C0);
    gpio_reset(GPIOB, 6);
    gpio_reset(GPIOB, 7);

    /* PC11 (front end), PC13 (RTC CE) */
    output_pins(GPIOC, 0x2800);
    gpio_reset(GPIOC, 13);

    output_pins(GPIOF, 0x8070);
    gpio_reset(GPIOF, 15);

    output_pins(GPIOH, 1u << 2);

    output_pins(GPIOA, 0x1800);
    gpio_set(GPIOA, 12);

    gpio_reset(GPIOE, 15);
    gpio_reset(GPIOF, 6);
    gpio_reset(GPIOA, 11);
    gpio_reset(GPIOH, 2);

    front_end_select(FRONT_END_NONE);
}

/* ---- ADC, NVIC, timers --------------------------------------------------------- */

/*
 * orig: 0x08013DD4; inlines: 0x08020094 (ADC_DeInit), 0x080200B4 (ADC_Init)
 * 12-bit, single conversion, software trigger, right-aligned, one channel.
 * The original leaves the external-trigger-edge field of its init struct
 * uninitialised; in the start-up sequence that stack slot always holds 0
 * (checked in emulation with different RAM contents), so 0 is used here.
 */
void adc_init(void)
{
    RCC->APB2RSTR |= RCC_APB2RSTR_ADC1;
    RCC->APB2RSTR &= ~RCC_APB2RSTR_ADC1;

    ADC1->CR1 = (ADC1->CR1 & 0xFCFFFEFFu) | 0u;          /* resolution 12-bit, no scan */
    ADC1->CR2 = (ADC1->CR2 & 0xC0FFF7FDu) | 0u;          /* no trigger, right aligned, single */
    ADC1->SQR1 = (ADC1->SQR1 & 0xFE0FFFFFu) | (0u << 20); /* one conversion */

    ADC1->CR2 |= ADC_CR2_ADON;
    while ((ADC1->SR & ADC_SR_ADONS) == 0) {
    }
}

/* orig: 0x08020876 (enable path of NVIC_Init) */
void nvic_irq_init(unsigned irq, uint32_t preempt, uint32_t sub)
{
    uint32_t group_shift = (0x700u - (SCB->AIRCR & 0x700u)) >> 8;
    uint32_t priority = (preempt << (4u - group_shift)) | (sub & (0x0Fu >> group_shift));

    NVIC->IP[irq] = (uint8_t)(priority << 4);
    NVIC->ISER[irq >> 5] = 1u << (irq & 0x1F);
}

/* orig: 0x0801BFDE - priority group 2; inlines: 0x0802086C (NVIC_PriorityGroupConfig) */
void nvic_init(void)
{
    SCB->AIRCR = 0x05FA0000u | 0x500u;
    nvic_irq_init(IRQ_SDIO, 0, 0);
    nvic_irq_init(IRQ_DMA2_CHANNEL4, 0, 0);
    nvic_irq_init(IRQ_TIM2, 2, 2);
    nvic_irq_init(IRQ_TIM3, 1, 1);
}

/* TIM_TimeBaseInit + clear update flag + enable update interrupt + start */
static void timer_start(TIM_TypeDef *tim, uint32_t period)
{
    uint16_t cr1 = (uint16_t)tim->CR1;
    cr1 = (cr1 & 0xFF8Fu) | 0u;    /* count up */
    cr1 = (cr1 & 0xFCFFu) | 0u;    /* clock division 1 */
    tim->CR1 = cr1;
    tim->ARR = period;
    tim->PSC = TIM_PRESCALER;
    tim->EGR = TIM_EGR_UG;

    tim->SR = (uint16_t)~TIM_SR_UIF;
    tim->DIER |= TIM_DIER_UIE;
    tim->CR1 |= TIM_CR1_CEN;
}

/* orig: 0x0801C486 - ~99.8 Hz scheduler tick; inlines: 0x08020908 (TIM_TimeBaseInit), 0x08020988 (TIM_ITConfig) */
void tim2_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM2_BIT;
    timer_start(TIM2, TIM2_PERIOD);
}

/* orig: 0x0801C4D4 - ~1 kHz */
void tim3_init(void)
{
    RCC->APB1ENR |= RCC_APB1ENR_TIM3_BIT;
    timer_start(TIM3, TIM3_PERIOD);
}

/* ---- Top level ------------------------------------------------------------------ */

/* orig: 0x0801BE76; inlines: 0x08020848 (DBGMCU_Config) */
void system_init(void)
{
    int woke = (PWR->CSR & PWR_FLAG_WU) != 0;
    if (woke) {
        RCC->APB1ENR |= RCC_APB1ENR_PWR_BIT;
        PWR->CR |= PWR_FLAG_SB << 2;    /* clear the standby flag */
    }
    g_woke_from_standby = (uint8_t)woke;

    flash_clear_flags(FLASH_FLAGS_ALL);

    /* Force the brown-out reset level to 0, as the bootloader does. */
    g_bor_level = flash_ob_get_bor();
    if (g_bor_level & 0x0F) {
        flash_ob_unlock();
        flash_clear_flags(FLASH_FLAGS_NO_OPTVUSR);
        (void)flash_ob_bor_config(0);
        flash_ob_launch();
    }

    PWR->CR = ((PWR->CR & 0xFFFFE7FFu) | PWR_CR_VOS_RANGE1) & 0xFFFFFFF3u;
    while (PWR->CSR & PWR_CSR_VOSF_BIT) {
    }

    clock_init(0);
    rtc_clock_init();
    DBGMCU->CR |= DBGMCU_CR_LOWPOWER;
    board_gpio_init();
    adc_pins_analog();
    adc_init();
    nvic_init();
    tim2_init();
    tim3_init();
    enable_gpio_clocks();
}

/* orig: 0x0801BFD2 - GPIO port clocks off (RCC_AHBPeriphClockCmd(0xFF, DISABLE)) */
void gpio_clocks_disable(void)
{
    RCC->AHBENR &= ~0xFFu;
}

/* orig: 0x0801BE60 - enable an interrupt (irq taken as signed, as the original) */
void nvic_enable_irq(uint8_t irq)
{
    NVIC->ISER[(uint32_t)(int32_t)(int8_t)irq >> 5] = 1u << (irq & 0x1Fu);
}
