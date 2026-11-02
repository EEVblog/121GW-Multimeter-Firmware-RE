/*
 * Frequency-dependent corrections: AC response for functions 2 and 4 (with
 * g_flag_80b9 set) and the function 2 range 0 offset.
 */
#ifndef AC_RESPONSE_H
#define AC_RESPONSE_H

#include <stdint.h>

extern int32_t g_freq_raw;        /* 0x20007D84  frequency counter result */
extern uint8_t g_freq_decimal;    /* 0x2000803E  its decimal scaling (scale_by_decimal mode) */
extern int32_t g_ac_freq;         /* 0x20007C5C  frequency, Hz */
extern int32_t g_ac_gain;         /* 0x20007C60  gain, 1000 = unity */
extern int32_t g_ac_gain_low;     /* 0x20007C64 */
extern int32_t g_ac_gain_high;    /* 0x20007C68 */
extern int32_t g_ac_span;         /* 0x20007C6C */
extern int32_t g_ac_delta;        /* 0x20007C70 */
extern int32_t g_ac_hi;           /* 0x20007C74 */
extern int32_t g_ac_lo;           /* 0x20007C78 */
extern int32_t g_ac_hi2;          /* 0x20007C7C */
extern int32_t g_ac_lo2;          /* 0x20007C80 */
extern float g_ac_frac;           /* 0x20007C84 */
extern float g_ac_blend;          /* 0x20007C88 */

extern int32_t g_f2r0_result;     /* 0x20007C8C */
extern int32_t g_f2r0_freq;       /* 0x20007C90 */
extern int32_t g_f2r0_off_lo;     /* 0x20007C94 */
extern int32_t g_f2r0_off_hi;     /* 0x20007C98 */
extern int32_t g_f2r0_span;       /* 0x20007C9C */
extern int32_t g_f2r0_delta;      /* 0x20007CA0 */
extern int32_t g_f2r0_pct;        /* 0x20007CA4 */
extern int32_t g_f2r0_knee;       /* 0x20007CA8 */
extern int32_t g_f2r0_a;          /* 0x20007CAC */
extern int32_t g_f2r0_b;          /* 0x20007CB0 */
extern int32_t g_f2r0_c;          /* 0x20007CB4 */
extern int32_t g_f2r0_d;          /* 0x20007CB8 */
extern float g_f2r0_frac;         /* 0x20007CBC */

int32_t ac_response_correct(int32_t reading);
int32_t f2r0_freq_offset(int32_t reading, int32_t freq_raw, uint8_t freq_decimal);

#endif /* AC_RESPONSE_H */
