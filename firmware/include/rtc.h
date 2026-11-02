/*
 * NJU6350 real-time clock (3-wire, bit-banged).
 *
 *   PC13 CE (active high), PB6 CLK, PB7 DATA (bidirectional).
 *
 * A transfer is a 4-bit address then 8 data bits, both LSB first. Writes use
 * address + 1. Values are BCD on the chip.
 */
#ifndef RTC_H
#define RTC_H

#include <stdint.h>

/* Date/time staged for rtc_write_datetime(), in binary (roles from the
 * register numbers written; to be confirmed against the log format). */
extern uint8_t g_rtc_set_reg2;   /* 0x200080D7 */
extern uint8_t g_rtc_set_reg4;   /* 0x200080D8 */
extern uint8_t g_rtc_set_reg6;   /* 0x200080D9 */
extern uint8_t g_rtc_set_reg10;  /* 0x200080DA */
extern uint8_t g_rtc_set_reg12;  /* 0x200080DB */
extern uint8_t g_rtc_set_reg14;  /* 0x200080DC */

void rtc_select(void);
void rtc_deselect(void);
void rtc_data_pin_output(void);
void rtc_data_pin_input(void);
void rtc_write_address4(uint8_t address);
void rtc_write_byte(uint8_t value);
uint8_t rtc_read_byte(void);
uint8_t bin_to_bcd(uint8_t value);
void rtc_write_register(uint8_t address, uint8_t value);
uint8_t rtc_read_register(uint8_t address);
void rtc_write_datetime(void);

#endif /* RTC_H */
