/*
 * Flash status, option-byte and data-EEPROM unlock helpers (application copy).
 * Same behaviour as the bootloader's versions.
 */
#include "flash_ob.h"
#include "stm32l1xx.h"

#define PECR_PELOCK      (1u << 0)
#define PECR_PRGLOCK     (1u << 1)
#define PECR_OPTLOCK     (1u << 2)
#define PECR_OBL_LAUNCH  (1u << 18)
#define SR_BSY           (1u << 0)
#define SR_WRPERR        (1u << 8)
#define SR_ERROR_MASK    0x1E00u

/* orig: 0x0801F9FC */
void flash_data_eeprom_unlock(void)
{
    if (FLASH->PECR & PECR_PELOCK) {
        FLASH->PEKEYR = 0x89ABCDEFu;
        FLASH->PEKEYR = 0x02030405u;
    }
}

#if APP_FIX_BUGS
/*
 * Not in the original: lock program memory and the option bytes, then
 * FLASH_PECR and the data EEPROM. The original bootloader starts the
 * application with everything unlocked, and the application only sets
 * PELOCK after its first EEPROM write. PRGLOCK and OPTLOCK can only be set
 * while PELOCK is clear; once PELOCK is set they are already set too (after
 * reset, or by a bootloader built with BL_FIX_BUGS).
 */
void flash_lock_all(void)
{
    if ((FLASH->PECR & PECR_PELOCK) == 0) {
        FLASH->PECR |= PECR_PRGLOCK | PECR_OPTLOCK;
        FLASH->PECR |= PECR_PELOCK;
    }
}
#endif

/* orig: 0x0801FA68 */
void flash_ob_unlock(void)
{
    if (FLASH->PECR & PECR_OPTLOCK) {
        flash_data_eeprom_unlock();
        FLASH->OPTKEYR = 0xFBEAD9C8u;
        FLASH->OPTKEYR = 0x24252627u;
    }
}

/* orig: 0x0801FAFA */
void flash_clear_flags(uint32_t flags)
{
    FLASH->SR = flags;
}

/* orig: 0x0801FB00 */
flash_status_t flash_get_status(void)
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

/* orig: 0x0801FB60 */
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

/* orig: 0x0801FAEE */
uint32_t flash_ob_get_bor(void)
{
    return (FLASH->OBR >> 16) & 0x0Fu;
}

/* orig: 0x0801FAA0 */
flash_status_t flash_ob_bor_config(uint8_t bor_level)
{
    uint32_t user = (FLASH->OBR >> 16) & 0xF0u;
    flash_status_t status = flash_wait_for_last_operation(FLASH_TIMEOUT);

    if (status == FLASH_STATUS_COMPLETE) {
        uint32_t value = user | bor_level;
        OB->USER = (~value << 16) | value;
    }
    return flash_wait_for_last_operation(FLASH_TIMEOUT);
}

/* orig: 0x0801FA8E - reloads the option bytes, which resets the MCU */
void flash_ob_launch(void)
{
    FLASH->PECR |= PECR_OBL_LAUNCH;
}
