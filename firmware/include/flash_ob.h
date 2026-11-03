/*
 * Flash status, option-byte and data-EEPROM unlock helpers (application copy).
 */
#ifndef FLASH_OB_H
#define FLASH_OB_H

#include <stdint.h>

typedef enum {
    FLASH_STATUS_BUSY = 1,
    FLASH_STATUS_ERROR_WRP = 2,
    FLASH_STATUS_ERROR_PROGRAM = 3,
    FLASH_STATUS_COMPLETE = 4,
    FLASH_STATUS_TIMEOUT = 5
} flash_status_t;

#define FLASH_TIMEOUT          0x8000u
#define FLASH_FLAGS_ALL        0x1F02u
#define FLASH_FLAGS_NO_OPTVUSR 0x0F02u

void flash_data_eeprom_unlock(void);
void flash_ob_unlock(void);
void flash_clear_flags(uint32_t flags);
flash_status_t flash_get_status(void);
flash_status_t flash_wait_for_last_operation(uint32_t timeout);
uint32_t flash_ob_get_bor(void);
flash_status_t flash_ob_bor_config(uint8_t bor_level);
void flash_ob_launch(void);
#if APP_FIX_BUGS
void flash_lock_all(void);
#endif

#endif /* FLASH_OB_H */
