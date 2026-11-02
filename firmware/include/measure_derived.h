/*
 * Derived measurement values: power/VA products, vector magnitude, simple
 * scaled reading, range thresholds and the reading hold-off timer.
 */
#ifndef MEASURE_DERIVED_H
#define MEASURE_DERIVED_H

#include <stdint.h>

extern int32_t g_derived_value;    /* 0x20007D64  product / magnitude result */
extern int32_t g_scaled_value;     /* 0x20007D68  offset- and gain-corrected reading */
extern int32_t g_vec_a;            /* 0x20007E9C */
extern int32_t g_vec_b;            /* 0x20007EA0 */
extern uint32_t g_vec_a_sq;        /* 0x20007EA4 */
extern uint32_t g_vec_b_sq;        /* 0x20007EA8 */
extern uint16_t g_holdoff;         /* 0x20007FC0 */
extern uint8_t g_scaled_ready;     /* 0x2000805E */
extern uint8_t g_flag_80b5;        /* 0x200080B5 */
extern uint8_t g_sel_80b6;         /* 0x200080B6 */
extern float g_dbm;                /* 0x20007E94 */
extern uint16_t g_func12_interval; /* 0x20007FC4 */
extern uint16_t g_func12_count;    /* 0x20007FC6 */
extern uint8_t g_flag_80a3;        /* 0x200080A3 */
extern uint8_t g_flag_8042;        /* 0x20008042 */

void holdoff_on_low_reading(void);
int32_t range_threshold(void);
void scale_reading(int32_t raw);
void product_scaled(int32_t a, int32_t b);
void vector_magnitude(void);
void dbm_from_voltage(void);
void derived_from_average(void);
void derived_from_average_positive(void);
void scaled_from_ad1(void);
void threshold_check(uint32_t value);
void func12_set_interval(void);

extern uint8_t g_flag_8049;         /* 0x20008049  copy of g_flag_8046 */

uint8_t outside_limit(int32_t value, int32_t limit);
void overload_latch(void);

extern int32_t g_minmax_max;        /* 0x20007D88 */
extern int32_t g_minmax_min;        /* 0x20007D8C */
extern int32_t g_minmax_d90;        /* 0x20007D90  cleared at start (sum/count?) */
extern int32_t g_minmax_d94;        /* 0x20007D94 */
extern int32_t g_minmax_d98;        /* 0x20007D98 */

void minmax_start(void);

#endif /* MEASURE_DERIVED_H */
