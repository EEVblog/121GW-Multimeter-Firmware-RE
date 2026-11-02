/*
 * HY3131 AD1 (high-resolution ADC) results and per-function dispatch.
 */
#ifndef MEASURE_AD1_H
#define MEASURE_AD1_H

#include <stdint.h>

extern int32_t g_ad1_value;        /* 0x20007D74  latest AD1 reading (signed) */
extern uint32_t g_ad1_raw24;       /* 0x20007DC8  raw 24-bit register value */
extern int32_t g_ad1_value_copy;   /* 0x20007DCC */
extern int32_t g_ad1_sum;          /* 0x20007DD0 */
extern int32_t g_ad1_average;      /* 0x20007DD4 */
extern uint8_t g_ad1_count;        /* 0x20008080 */
extern uint8_t g_ad1_average_ready;/* 0x20008081 */

int32_t ad1_read_value(void);
uint8_t ad1_average(int32_t value);
void ad1_process_func0(void);
void hy3131_on_ad1_ready(void);

#endif /* MEASURE_AD1_H */
