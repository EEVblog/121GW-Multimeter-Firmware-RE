/*
 * HY3131 RMS converter results.
 */
#ifndef MEASURE_RMS_H
#define MEASURE_RMS_H

#include <stdint.h>

extern uint32_t g_rms_value;        /* 0x20007D78  latest value: isqrt(RMS >> 8) * 8 */
extern uint32_t g_rms_raw_low;      /* 0x20007DD8  RMS<7:0> */
extern uint32_t g_rms_raw;          /* 0x20007DDC  RMS<39:8> */
extern uint32_t g_rms_value_copy;   /* 0x20007DE0 */
extern uint32_t g_rms_sum;          /* 0x20007DE4 */
extern uint32_t g_rms_average;      /* 0x20007DE8  mean of the last 4 values */
extern uint8_t g_rms_average_ready; /* 0x20008082 */
extern uint8_t g_rms_count;         /* 0x20008083 */

uint32_t isqrt_u32(uint32_t n);
int32_t rms_read_value(void);
uint8_t rms_average(uint32_t value);
void rms_post_func0(void);
void rms_post_func2(void);
void rms_post_func4(void);
void rms_post_func16(void);
void hy3131_on_rms_ready(void);

#endif /* MEASURE_RMS_H */
