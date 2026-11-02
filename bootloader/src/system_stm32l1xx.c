/*
 * Reset-time clock setup (called from Reset_Handler before C start-up).
 *
 * Resets RCC to MSI, then tries to run from the 4 MHz HSE crystal through the
 * PLL (x12 /3 = 16 MHz) at voltage range 1 with one flash wait state.
 * main() later switches SYSCLK to HSE directly (see clock_init()).
 */
#include "stm32l1xx.h"

uint32_t SystemCoreClock = 16000000u;

#define HSE_STARTUP_TIMEOUT  0x0500u

#define RCC_CR_MSION_BIT     (1u << 8)
#define RCC_CR_HSEON_BIT     (1u << 16)
#define RCC_CR_HSERDY_BIT    (1u << 17)
#define RCC_CR_PLLON_BIT     (1u << 24)
#define RCC_CR_PLLRDY_BIT    (1u << 25)
#define RCC_CFGR_SW_MASK     0x00000003u
#define RCC_CFGR_SW_PLL_VAL  0x00000003u
#define RCC_CFGR_SWS_MASK    0x0000000Cu
#define RCC_CFGR_SWS_PLL_VAL 0x0000000Cu
#define RCC_CFGR_PLL_MASK    0xFF02FFFFu   /* keeps everything but PLLSRC/PLLMUL/PLLDIV */
#define RCC_CFGR_PLL_HSE_X12_DIV3 0x00910000u
#define FLASH_ACR_LATENCY_BIT (1u << 0)
#define FLASH_ACR_PRFTEN_BIT (1u << 1)
#define FLASH_ACR_ACC64_BIT  (1u << 2)
#define RCC_APB1ENR_PWREN_BIT (1u << 28)
#define PWR_CR_VOS_RANGE1    0x00000800u
#define PWR_CSR_VOSF_BIT     (1u << 4)

/* orig: 0x08004CB6 */
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
        return;   /* no crystal: stay on MSI */
    }

    FLASH->ACR |= FLASH_ACR_ACC64_BIT;
    FLASH->ACR |= FLASH_ACR_PRFTEN_BIT;
    FLASH->ACR |= FLASH_ACR_LATENCY_BIT;

    RCC->APB1ENR |= RCC_APB1ENR_PWREN_BIT;
    PWR->CR = PWR_CR_VOS_RANGE1;
    while (PWR->CSR & PWR_CSR_VOSF_BIT) {
    }

    /* HCLK, PCLK2 and PCLK1 undivided (re-written unchanged, as the original). */
    RCC->CFGR = RCC->CFGR;
    RCC->CFGR = RCC->CFGR;
    RCC->CFGR = RCC->CFGR;

    RCC->CFGR &= RCC_CFGR_PLL_MASK;
    RCC->CFGR |= RCC_CFGR_PLL_HSE_X12_DIV3;
    RCC->CR |= RCC_CR_PLLON_BIT;
    while ((RCC->CR & RCC_CR_PLLRDY_BIT) == 0) {
    }

    RCC->CFGR &= ~RCC_CFGR_SW_MASK;
    RCC->CFGR |= RCC_CFGR_SW_PLL_VAL;
    while ((RCC->CFGR & RCC_CFGR_SWS_MASK) != RCC_CFGR_SWS_PLL_VAL) {
    }
}

/* orig: 0x08004C64 */
void SystemInit(void)
{
    RCC->CR |= RCC_CR_MSION_BIT;
    RCC->CFGR &= 0x88FFC00Cu;   /* SW, HPRE, PPRE1, PPRE2, MCOSEL, MCOPRE */
    RCC->CR &= 0xEEFEFFFEu;     /* HSION, HSEON, CSSON, PLLON */
    RCC->CR &= 0xFFFBFFFFu;     /* HSEBYP */
    RCC->CFGR &= 0xFF02FFFFu;   /* PLLSRC, PLLMUL, PLLDIV */
    RCC->CIR = 0;

    set_sys_clock();

    SCB->VTOR = FLASH_BASE;
}
