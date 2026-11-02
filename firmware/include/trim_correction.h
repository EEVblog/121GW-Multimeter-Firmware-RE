/*
 * Trim-based correction of AC readings (functions 0, 2, 4, 16, 18, 20).
 */
#ifndef TRIM_CORRECTION_H
#define TRIM_CORRECTION_H

#include <stdint.h>

extern int32_t g_freq_raw_acdc;      /* 0x20007E60  frequency counter, function 2 AC+DC */
extern int32_t g_freq_raw_f0;        /* 0x20007E64  frequency counter, function 0 */
extern uint8_t g_freq_decimal_acdc;  /* 0x200080A0 */
extern uint8_t g_freq_decimal_f0;    /* 0x200080A1 */
extern uint8_t g_flag_8087;          /* 0x20008087 */
extern int32_t g_trim_correction;    /* 0x20007E5C  last correction applied */

/* working values (kept as globals, as in the original) */
extern int32_t g_trc_result;    /* 0x20007F0C */
extern int32_t g_trc_freq;      /* 0x20007F10 */
extern int32_t g_trc_gain;      /* 0x20007F14 */
extern int32_t g_trc_span;      /* 0x20007F18 */
extern int32_t g_trc_delta;     /* 0x20007F1C */
extern int32_t g_trc_off0;      /* 0x20007F20 */
extern int32_t g_trc_off1;      /* 0x20007F24 */
extern int32_t g_trc_knee;      /* 0x20007F28 */
extern int32_t g_trc_span2;     /* 0x20007F2C */
extern int32_t g_trc_a;         /* 0x20007F30 */
extern int32_t g_trc_b;         /* 0x20007F34 */
extern int32_t g_trc_base;      /* 0x20007F38 */
extern int32_t g_trc_t0;        /* 0x20007F3C */
extern int32_t g_trc_t1;        /* 0x20007F40 */
extern int32_t g_trc_extra;     /* 0x20007F44 */
extern float g_trc_frac;        /* 0x20007F48 */
extern float g_trc_level;       /* 0x20007F4C */
extern int32_t g_trac_result;   /* 0x20007F50 */
extern int32_t g_trac_d31;      /* 0x20007F54 */
extern int32_t g_trac_d20;      /* 0x20007F58 */
extern int32_t g_trac_pct2;     /* 0x20007F5C */
extern int32_t g_trac_pct3;     /* 0x20007F60 */
extern int32_t g_trac_span;     /* 0x20007F64 */
extern int32_t g_trac_delta;    /* 0x20007F68 */
extern int32_t g_trac_level;    /* 0x20007F6C */
extern int32_t g_trac_knee;     /* 0x20007F70 */
extern int32_t g_trac_a;        /* 0x20007F74 */
extern int32_t g_trac_b;        /* 0x20007F78 */
extern int32_t g_trac_c;        /* 0x20007F7C */
extern int32_t g_trac_d;        /* 0x20007F80 */
extern int32_t g_trac_off_lo;   /* 0x20007F84 */
extern int32_t g_trac_off_hi;   /* 0x20007F88 */
extern int32_t g_trac_out;      /* 0x20007F8C */
extern int32_t g_trac_diff;     /* 0x20007F90 */
extern float g_trac_frac;       /* 0x20007F94 */

int32_t trim_correction_ac(int32_t reading, int32_t trim0, int32_t trim1, int32_t trim2,
                           int32_t trim3, uint8_t selection, int32_t freq, uint8_t flag);
int32_t trim_correction(int32_t reading, int32_t trim0, int32_t trim1, int32_t trim2, int32_t trim3,
                        uint8_t selection, int32_t freq_raw, uint8_t freq_decimal, uint8_t flag);
int32_t trim_correction_for(uint8_t selection, int32_t reading);
int32_t trim_correct_reading(uint8_t function, int32_t value);

#endif /* TRIM_CORRECTION_H */
