/*
 * Two-dimensional correction of the reference value for the dual-readout
 * functions 13..15 and 24.
 */
#ifndef DUAL_CORRECTION_H
#define DUAL_CORRECTION_H

#include <stdint.h>

extern int64_t g_dual_sums_22[2];  /* 0x20007C10  calibration sums, functions 22..24 */
extern int64_t g_dual_sums_13[2];  /* 0x20007C20  calibration sums, functions 13..15 */
extern uint8_t g_flag_8046;        /* 0x20008046  overload on either reading */
extern uint8_t g_flag_804c;        /* 0x2000804C */
extern uint8_t g_flag_8060;        /* 0x20008060 */
extern int32_t g_level_7d80;      /* 0x20007D80  second reading (level) for the correction */
extern uint8_t g_flag_80cc;       /* 0x200080CC */
extern int32_t g_grid_c00;        /* 0x20007EB8 */
extern int32_t g_grid_c01;        /* 0x20007EBC */
extern int32_t g_grid_c10;        /* 0x20007EC0 */
extern int32_t g_grid_c11;        /* 0x20007EC4 */
extern int32_t g_dc_ref_off;      /* 0x20007CDC */
extern int32_t g_dc_ref_span;     /* 0x20007CE0 */
extern int32_t g_dc_ref_pct;      /* 0x20007CE4 */
extern int32_t g_dc_d0;           /* 0x20007CE8 */
extern int32_t g_dc_d1;           /* 0x20007CEC */
extern int32_t g_dc_v0;           /* 0x20007CF0 */
extern int32_t g_dc_v1;           /* 0x20007CF4 */
extern int32_t g_dc_level_off;    /* 0x20007CF8 */
extern int32_t g_dc_level_span;   /* 0x20007CFC */
extern int32_t g_dc_level_pct;    /* 0x20007D00 */
extern int32_t g_dc_value;        /* 0x20007D04 */

void dual_grid_corners(uint8_t level, uint8_t ref, uint8_t negative);
void dual_correction(void);
void ad1_process_func22(void);
void ad1_process_func13(void);

#endif /* DUAL_CORRECTION_H */
