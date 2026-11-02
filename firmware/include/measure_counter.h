/*
 * HY3131 counter results: frequency, period, duty cycle and the
 * counter-based functions (6, 7, 8, 12).
 */
#ifndef MEASURE_COUNTER_H
#define MEASURE_COUNTER_H

#include <stdint.h>

extern uint32_t g_ctr_rate;        /* 0x20007E34  counts per ms of the reference */
extern uint32_t g_ctr_preset_span; /* 0x20007E38  0x1000000 - preset * 256 */
extern uint32_t g_ctr_c;           /* 0x20007E3C  CTC */
extern uint32_t g_ctr_b;           /* 0x20007E40  CTB */
extern uint32_t g_ctr_a;           /* 0x20007E44  CTA */
extern uint32_t g_ctr_ref;         /* 0x20007E48  reference count */
extern uint64_t g_ctr_value6;      /* 0x20007C40  function 6 value */
extern uint64_t g_ctr_value7;      /* 0x20007C48  function 7 value */
extern int32_t g_avg16_unsigned;   /* 0x20007DFC */
extern uint8_t g_flag_8047;        /* 0x20008047  secondary frequency overload */
extern int32_t g_f12_avg;          /* 0x20007D08  function 12 16-sample mean */
extern uint16_t g_func12_count_copy; /* 0x20007FC8 */
extern uint8_t g_f12_skip;         /* 0x200080A4 */
extern uint8_t g_flag_807c;        /* 0x2000807C */
extern uint8_t g_flag_804a;        /* 0x2000804A */
extern uint8_t g_flag_804b;        /* 0x2000804B */
extern uint8_t g_flag_80a2;        /* 0x200080A2 */

void counter_capture(void);
void counter_rate_per_ms(void);
void counter_process_func8(void);
void counter_calibrate_func6(void);
void counter_value_func6(void);
void counter_value_func7(void);
void counter_set_range_func7(uint8_t selector);
int32_t average16_unsigned(uint32_t sample);
void calibrate_counter(uint32_t sample, int32_t reference);
void freq_scale_store(uint8_t dest, uint8_t decade);
void freq_process(uint8_t dest, uint8_t range);
void counter_process_func7(void);
void counter_process_func12(void);
void hy3131_on_counter_ready(void);
void func12_timeout_poll(void);

#endif /* MEASURE_COUNTER_H */
