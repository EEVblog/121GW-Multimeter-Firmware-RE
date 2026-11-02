/*
 * Data EEPROM access (calibration and settings).
 */
#include "eeprom.h"
#include "flash_ob.h"
#include "stm32l1xx.h"

#define PECR_PELOCK (1u << 0)
#define PECR_FTDW   (1u << 8)

/* orig: 0x0801204C - reads the word at the offset and keeps its low byte */
uint8_t eeprom_read_byte_raw(uint32_t offset)
{
    return (uint8_t)*(volatile uint32_t *)(EEPROM_BASE + offset);
}

/* orig: 0x0801203A */
uint8_t eeprom_read_byte(uint32_t offset)
{
    return eeprom_read_byte_raw(offset);
}

/* orig: 0x0801FA2E (DATA_EEPROM_FastProgramByte) */
uint8_t eeprom_program_byte_raw(volatile uint8_t *address, uint8_t value)
{
    flash_status_t status = flash_wait_for_last_operation(FLASH_TIMEOUT);

    if (status == FLASH_STATUS_COMPLETE) {
        FLASH->PECR &= ~PECR_FTDW;
        *address = value;
        status = flash_wait_for_last_operation(FLASH_TIMEOUT);
    }
    return (uint8_t)status;
}

/* orig: 0x0801FA1C */
void data_eeprom_lock(void)
{
    FLASH->PECR |= PECR_PELOCK;
}

/* orig: 0x080120F0 */
uint8_t eeprom_program_byte(uint32_t offset, uint8_t value)
{
    flash_data_eeprom_unlock();
    uint8_t status = eeprom_program_byte_raw((volatile uint8_t *)(EEPROM_BASE + offset), value);
    data_eeprom_lock();
    return status;
}

/* orig: 0x08012148 - up to 10 attempts until the program completes */
void eeprom_write_byte_retry(uint32_t offset, uint8_t value)
{
    for (int32_t attempt = 0; attempt < (int32_t)EEPROM_WRITE_RETRIES; attempt++) {
        if (eeprom_program_byte(offset, value) == FLASH_STATUS_COMPLETE) {
            return;
        }
    }
}

/* orig: 0x080120D0 */
void eeprom_write_byte(uint32_t offset, uint8_t value)
{
    eeprom_write_byte_retry(offset, value);
}

/* orig: 0x08012068 - most significant byte first */
void eeprom_write_u32_be(uint32_t offset, uint32_t value)
{
    eeprom_write_byte_retry(offset, (uint8_t)(value >> 24));
    eeprom_write_byte_retry(offset + 1, (uint8_t)(value >> 16));
    eeprom_write_byte_retry(offset + 2, (uint8_t)(value >> 8));
    eeprom_write_byte_retry(offset + 3, (uint8_t)value);
}

/* orig: 0x080120A8 */
void eeprom_write_u16_be(uint32_t offset, uint16_t value)
{
    eeprom_write_byte_retry(offset, (uint8_t)(value >> 8));
    eeprom_write_byte_retry(offset + 1, (uint8_t)value);
}

/* Writes the valid marker and reads it back, up to 10 times. */
static void eeprom_set_marker(uint32_t offset)
{
    for (uint8_t attempt = 0; attempt < EEPROM_WRITE_RETRIES; attempt++) {
        eeprom_write_byte(offset, EEPROM_VALID_MARKER);
        if (eeprom_read_byte(offset) == EEPROM_VALID_MARKER) {
            return;
        }
    }
}

/* orig: 0x080104D4 */
void eeprom_set_marker_a(void)
{
    eeprom_set_marker(EEPROM_MARKER_A);
}

/* orig: 0x08010F28 */
void eeprom_set_marker_b(void)
{
    eeprom_set_marker(EEPROM_MARKER_B);
}
