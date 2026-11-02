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

extern uint16_t g_therm_adc;           /* 0x20007FCE  latest thermistor ADC reading */
extern uint16_t g_therm_avg;           /* 0x20007FD0  its 10-sample mean */
extern uint16_t g_therm_count;         /* 0x20007FD4  samples in g_therm_history (0..10) */
extern uint16_t g_therm_open_count;    /* 0x20007FD6 */
extern uint16_t g_therm_history[10];   /* 0x20007BA8  newest first once full */
extern uint8_t g_therm_fault;          /* 0x200080AC  thermistor open or out of range */
extern uint8_t g_therm_out_of_range;   /* 0x200080AF */
extern uint8_t g_therm_oor_count;      /* 0x200080B0 */
extern uint8_t g_cal_temp_request;     /* 0x20008096  calibration: take the 23.00 C offset */

void thermocouple_to_celsius(int32_t microvolts);
int32_t celsius_to_fahrenheit(int32_t c100);
int16_t thermistor_to_c100(uint16_t adc);
void cold_junction_update(uint8_t negative, int32_t deci);
void thermistor_process(uint16_t adc);

#endif /* TEMPERATURE_H */
