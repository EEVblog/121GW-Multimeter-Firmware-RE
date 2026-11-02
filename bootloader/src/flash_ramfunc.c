/*
 * Half-page flash programming. This code must execute from RAM: the original
 * image copies it to 0x20000000 at start-up, and the linker script places the
 * .ramfunc section in the same way.
 */
#include "flash_if.h"
#include "stm32l1xx.h"

#define RAMFUNC __attribute__((section(".ramfunc"), noinline, long_call))

#define PECR_PROG          (1u << 3)
#define PECR_FPRG          (1u << 10)
#define SR_BSY             (1u << 0)
#define SR_WRPERR          (1u << 8)
#define SR_ERROR_MASK      0x1E00u
#define ACTLR_DISMCYCINT   (1u << 0)

/* orig: 0x080050CA (RAM copy) */
static RAMFUNC flash_status_t ram_get_status(void)
{
    if (FLASH->SR & SR_BSY) {
        return FLASH_STATUS_BUSY;
    }
    if (FLASH->SR & SR_WRPERR) {
        return FLASH_STATUS_ERROR_WRP;
    }
    if ((FLASH->SR & SR_ERROR_MASK) == 0) {
        return FLASH_STATUS_COMPLETE;
    }
    return FLASH_STATUS_ERROR_PROGRAM;
}

/* orig: 0x08005110 (RAM copy) */
static RAMFUNC flash_status_t ram_wait_for_last_operation(uint32_t timeout)
{
    flash_status_t status = ram_get_status();

    while (status == FLASH_STATUS_BUSY && timeout != 0) {
        status = ram_get_status();
        timeout--;
    }
    if (timeout == 0) {
        status = FLASH_STATUS_TIMEOUT;
    }
    return status;
}

/* orig: 0x08005048 (runs at 0x20000000) */
RAMFUNC flash_status_t flash_program_half_page(uint32_t address, const uint32_t *data)
{
    flash_status_t status;

    /* Stop interrupts splitting multi-cycle instructions during the program. */
    SCnSCB->ACTLR |= ACTLR_DISMCYCINT;

    status = ram_wait_for_last_operation(FLASH_ERASE_PROGRAM_TIMEOUT);
    if (status == FLASH_STATUS_COMPLETE) {
        FLASH->PECR |= PECR_FPRG;
        FLASH->PECR |= PECR_PROG;
        for (uint32_t i = 0; i < 32u; i++) {
            ((volatile uint32_t *)address)[i] = *data++;
        }
        status = ram_wait_for_last_operation(FLASH_ERASE_PROGRAM_TIMEOUT);
        FLASH->PECR &= ~PECR_PROG;
        FLASH->PECR &= ~PECR_FPRG;
    }

    SCnSCB->ACTLR &= ~ACTLR_DISMCYCINT;
    return status;
}
