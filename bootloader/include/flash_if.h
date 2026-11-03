/*
 * Program-flash and option-byte access for the STM32L1 (bootloader).
 */
#ifndef FLASH_IF_H
#define FLASH_IF_H

#include <stdint.h>

/* Status codes returned by the flash routines (values as in the original image). */
typedef enum {
    FLASH_STATUS_BUSY = 1,
    FLASH_STATUS_ERROR_WRP = 2,
    FLASH_STATUS_ERROR_PROGRAM = 3,
    FLASH_STATUS_COMPLETE = 4,
    FLASH_STATUS_TIMEOUT = 5
} flash_status_t;

#define FLASH_ERASE_PROGRAM_TIMEOUT  0x8000u

/* FLASH_SR error/end flags */
#define FLASH_FLAGS_ALL         0x1F02u  /* EOP|WRPERR|PGAERR|SIZERR|OPTVERR|OPTVERRUSR */
#define FLASH_FLAGS_NO_OPTVUSR  0x0F02u  /* EOP|WRPERR|PGAERR|SIZERR|OPTVERR */

void flash_unlock(void);
#if BL_FIX_BUGS
void flash_lock(void);
#endif
void flash_ob_unlock(void);
void flash_clear_flags(uint32_t flags);
flash_status_t flash_wait_for_last_operation(uint32_t timeout);
flash_status_t flash_erase_page(uint32_t page_address);
flash_status_t flash_ob_bor_config(uint8_t bor_level);
void flash_ob_launch(void);
uint32_t flash_ob_get_bor(void);

/* Runs from RAM (copied there at start-up). */
flash_status_t flash_program_half_page(uint32_t address, const uint32_t *data);

/* Erases every application page from 'address' to APP_FLASH_LAST_PAGE.
 * Returns 0 on success, 1 if a page erase failed. */
uint8_t flash_erase_application(uint32_t address);

/* Programs 'word_count' words from 'src' at *address in zero-padded half pages,
 * verifying each one and advancing *address.
 * Returns 0 on success, 1 on a program error, 2 on a verify mismatch. */
uint32_t flash_write_words(uint32_t *address, const uint32_t *src, uint16_t word_count);

#endif /* FLASH_IF_H */
