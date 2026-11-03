/*
 * Program-flash and option-byte access for the STM32L1 (bootloader).
 */
#include "flash_if.h"
#include "board.h"
#include "stm32l1xx.h"

#define PECR_PELOCK      (1u << 0)
#define PECR_PRGLOCK     (1u << 1)
#define PECR_OPTLOCK     (1u << 2)
#define PECR_PROG        (1u << 3)
#define PECR_ERASE       (1u << 9)
#define PECR_OBL_LAUNCH  (1u << 18)

#define SR_BSY           (1u << 0)
#define SR_WRPERR        (1u << 8)
#define SR_ERROR_MASK    0x1E00u    /* PGAERR|SIZERR|OPTVERR|OPTVERRUSR */

#define PEKEY1   0x89ABCDEFu
#define PEKEY2   0x02030405u
#define PRGKEY1  0x8C9DAEBFu
#define PRGKEY2  0x13141516u
#define OPTKEY1  0xFBEAD9C8u
#define OPTKEY2  0x24252627u

/* orig: 0x08004580 */
static void flash_data_eeprom_unlock(void)
{
    if (FLASH->PECR & PECR_PELOCK) {
        FLASH->PEKEYR = PEKEY1;
        FLASH->PEKEYR = PEKEY2;
    }
}

/* orig: 0x080044F0 */
void flash_unlock(void)
{
    if (FLASH->PECR & PECR_PRGLOCK) {
        flash_data_eeprom_unlock();
        FLASH->PRGKEYR = PRGKEY1;
        FLASH->PRGKEYR = PRGKEY2;
    }
}

#if BL_FIX_BUGS
/*
 * Not in the original: undo flash_unlock() before the application starts.
 * PRGLOCK and OPTLOCK can only be set while PELOCK is clear, so they go
 * first; PELOCK then locks FLASH_PECR and the data EEPROM.
 */
void flash_lock(void)
{
    if ((FLASH->PECR & PECR_PELOCK) == 0) {
        FLASH->PECR |= PECR_PRGLOCK | PECR_OPTLOCK;
        FLASH->PECR |= PECR_PELOCK;
    }
}
#endif

/* orig: 0x080045A0 */
void flash_ob_unlock(void)
{
    if (FLASH->PECR & PECR_OPTLOCK) {
        flash_data_eeprom_unlock();
        FLASH->OPTKEYR = OPTKEY1;
        FLASH->OPTKEYR = OPTKEY2;
    }
}

/* orig: 0x08004632 */
void flash_clear_flags(uint32_t flags)
{
    FLASH->SR = flags;
}

/* orig: 0x08004638 */
static flash_status_t flash_get_status(void)
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

/* orig: 0x080046A4 */
flash_status_t flash_wait_for_last_operation(uint32_t timeout)
{
    flash_status_t status = flash_get_status();

    while (status == FLASH_STATUS_BUSY && timeout != 0) {
        status = flash_get_status();
        timeout--;
    }
    if (timeout == 0) {
        status = FLASH_STATUS_TIMEOUT;
    }
    return status;
}

/* orig: 0x08004516 */
flash_status_t flash_erase_page(uint32_t page_address)
{
    flash_status_t status = flash_wait_for_last_operation(FLASH_ERASE_PROGRAM_TIMEOUT);

    if (status == FLASH_STATUS_COMPLETE) {
        FLASH->PECR |= PECR_ERASE;
        FLASH->PECR |= PECR_PROG;
        *(volatile uint32_t *)page_address = 0;
        status = flash_wait_for_last_operation(FLASH_ERASE_PROGRAM_TIMEOUT);
        FLASH->PECR &= ~PECR_PROG;
        FLASH->PECR &= ~PECR_ERASE;
    }
    return status;
}

/* orig: 0x08004626 - BOR_LEV field of the user option byte */
uint32_t flash_ob_get_bor(void)
{
    return (FLASH->OBR >> 16) & 0x0Fu;
}

/* orig: 0x080045D8 */
flash_status_t flash_ob_bor_config(uint8_t bor_level)
{
    uint32_t user = (FLASH->OBR >> 16) & 0xF0u;   /* keep IWDG_SW/nRST_STOP/nRST_STDBY/nBFB2 */
    flash_status_t status = flash_wait_for_last_operation(FLASH_ERASE_PROGRAM_TIMEOUT);

    if (status == FLASH_STATUS_COMPLETE) {
        uint32_t value = user | bor_level;
        OB->USER = (~value << 16) | value;
    }
    return flash_wait_for_last_operation(FLASH_ERASE_PROGRAM_TIMEOUT);
}

/* orig: 0x080045C6 - reloads option bytes, which resets the MCU */
void flash_ob_launch(void)
{
    FLASH->PECR |= PECR_OBL_LAUNCH;
}

/* orig: 0x08004BA0 */
uint8_t flash_erase_application(uint32_t address)
{
    while (address <= APP_FLASH_LAST_PAGE) {
        if (flash_erase_page(address) != FLASH_STATUS_COMPLETE) {
            return 1;
        }
        address += FLASH_PAGE_SIZE;
    }
    return 0;
}

/* orig: 0x08004BC8
 * inlines: 0x08004B88 (veneer to the RAM copy of flash_program_half_page) */
uint32_t flash_write_words(uint32_t *address, const uint32_t *src, uint16_t word_count)
{
    const uint32_t *p = src;
    const uint32_t *end = src + word_count;
    uint32_t half_page[FLASH_HALF_PAGE_WORDS];

    while (p < end) {
        for (uint32_t i = 0; i < FLASH_HALF_PAGE_WORDS; i++) {
            half_page[i] = (p < end) ? *p++ : 0;
        }
        if (flash_program_half_page(*address, half_page) != FLASH_STATUS_COMPLETE) {
            return 1;
        }
        for (uint32_t i = 0; i < FLASH_HALF_PAGE_WORDS; i++) {
            if (((const volatile uint32_t *)*address)[i] != half_page[i]) {
                return 2;
            }
        }
        *address += FLASH_HALF_PAGE_WORDS * 4u;
    }
    return 0;
}
