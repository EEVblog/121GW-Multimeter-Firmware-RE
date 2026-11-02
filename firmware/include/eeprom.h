/*
 * Data EEPROM access (calibration and settings, 0x08080000).
 */
#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>

#define EEPROM_BASE          0x08080000u
#define EEPROM_WRITE_RETRIES 10u
#define EEPROM_VALID_MARKER  0xA5u
#define EEPROM_MARKER_A      0x1Fu   /* written after calibrating function 2, range 0 */
#define EEPROM_MARKER_B      0x26u   /* written after calibrating function 9, range 6 */

uint8_t eeprom_read_byte_raw(uint32_t offset);
uint8_t eeprom_read_byte(uint32_t offset);
uint8_t eeprom_program_byte_raw(volatile uint8_t *address, uint8_t value);
void data_eeprom_lock(void);
uint8_t eeprom_program_byte(uint32_t offset, uint8_t value);
void eeprom_write_byte_retry(uint32_t offset, uint8_t value);
void eeprom_write_byte(uint32_t offset, uint8_t value);
void eeprom_write_u32_be(uint32_t offset, uint32_t value);
void eeprom_write_u16_be(uint32_t offset, uint16_t value);
void eeprom_set_marker_a(void);
void eeprom_set_marker_b(void);

#endif /* EEPROM_H */
