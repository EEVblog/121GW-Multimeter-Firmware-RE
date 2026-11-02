/*
 * ADC task: thermistor and battery readings.
 */
#ifndef BATTERY_H
#define BATTERY_H

#include <stdint.h>
#include "app_globals.h"

extern uint8_t g_adc_phase;            /* 0x20008086  0 thermistor, 1 battery */
extern uint16_t g_adc_raw;             /* 0x20007FD2  latest conversion */
extern uint16_t g_bat_count;           /* 0x20007FBC  samples in g_bat_history (0..20) */
extern uint32_t g_bat_history[20];     /* 0x200076F0  newest first once full */
extern uint32_t g_bat_sorted[20];      /* 0x20007740  history sorted, highest first */
extern uint8_t g_bat_low_now;          /* 0x200080F9 */
extern uint16_t g_bat_low_count;       /* 0x20007FDC */

uint16_t adc_read(uint8_t battery);
void bat_sort_history(void);
uint32_t bat_filter(uint32_t sample);
uint16_t bat_adc_to_value(uint32_t adc);
void battery_measure(void);
void thermistor_measure(void);
void adc_task(void);

#endif /* BATTERY_H */
