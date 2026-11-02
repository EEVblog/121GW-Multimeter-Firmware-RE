/*
 * Shared measurement scaling: zero offset, gain, 16-sample averaging, and the
 * per-range gain divisors used by calibration.
 */
#ifndef MEASURE_SCALE_H
#define MEASURE_SCALE_H

#include <stdint.h>

extern int32_t g_zero_offset;       /* 0x20007E00 */
extern int32_t g_average16;         /* 0x20007E04  last 16-sample mean */
extern float g_gain;                /* 0x20007E10  counts per display unit */
extern int32_t g_avg_sum;           /* 0x20007E1C */
extern float g_span;                /* 0x20007E20  measured calibration span */
extern float g_gain_e2c;            /* 0x20007E2C  gain for functions 13-15/22-24 (flag 80C4 clear) */
extern float g_gain_e30;            /* 0x20007E30  gain for functions 13-15/22-24 (flag 80C4 set) */
extern uint8_t g_new_reading;       /* 0x2000805D  a new value is ready for display */
extern uint8_t g_avg_flag_808c;     /* 0x2000808C */
extern uint8_t g_avg_done_808d;     /* 0x2000808D */
extern uint8_t g_avg_count;         /* 0x20008095  counts down from 16 */
extern uint8_t g_flag_8090;         /* 0x20008090  route readings through FUN_0800f470 */

int32_t subtract_zero_offset(int32_t value);
float select_gain(void);
int32_t apply_gain(int32_t raw);
int32_t average16(int32_t value);
void gain_set_divisor(uint32_t unused, float divisor);
void gain_set_50000(uint32_t unused);
void gain_set_30000(uint32_t unused);
void gain_set_10000(uint32_t unused);
void gain_set_6000(uint32_t unused);
void gain_set_ac_peak(void);
void gain_select_for_range(uint8_t alternate);
int32_t remove_offset(int32_t value);
int32_t offset_stage(int32_t value);
int32_t gain_stage(int32_t value);
int32_t scale_by_decimal(uint8_t mode, int32_t value);
uint8_t sel_code(void);

#endif /* MEASURE_SCALE_H */
