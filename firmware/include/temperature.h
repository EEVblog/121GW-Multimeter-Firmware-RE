/*
 * Thermocouple conversion and Celsius -> Fahrenheit.
 */
#ifndef TEMPERATURE_H
#define TEMPERATURE_H

#include <stdint.h>

extern int32_t g_temp_c100;     /* 0x20007E80  temperature, 0.01 C */
extern int32_t g_temp_base;     /* 0x20007E84  10 C step below the reading, 0.01 C */
extern int32_t g_temp_work;     /* 0x20007E88  scratch shared by both conversions */
extern uint8_t g_temp_status;   /* 0x200080AD  0 ok, 1 over range, 2 under range */

void thermocouple_to_celsius(int32_t microvolts);
int32_t celsius_to_fahrenheit(int32_t c100);

#endif /* TEMPERATURE_H */
