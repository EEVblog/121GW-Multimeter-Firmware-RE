/*
 * Per-function processing of AD1 results (called from hy3131_on_ad1_ready).
 */
#ifndef MEASURE_FUNCS_H
#define MEASURE_FUNCS_H

#include <stdint.h>

extern int32_t g_f11_last;       /* 0x20007CD4  last reading that beeped (function 11) */
extern uint8_t g_beep_holdoff;   /* 0x200080B4 */
extern int32_t g_f9r6_result;    /* 0x20007CC0 */
extern float g_f9r6_frac;        /* 0x20007CC4 */
extern float g_f9r6_pct;         /* 0x20007CC8 */
extern int32_t g_f9r6_x_lo;      /* 0x20007CCC */
extern int32_t g_f9r6_x_hi;      /* 0x20007CD0 */
extern int32_t g_tc_cold_junction_uv; /* 0x20007E7C  added to the thermocouple voltage */
extern uint8_t g_temp_unit_c;    /* 0x200080AB  0 = Fahrenheit */
extern uint8_t g_acdc_settle;    /* 0x200080BE  readings since the last phase switch */
extern uint8_t g_acdc_have_b;    /* 0x200080BB */
extern uint8_t g_acdc_have_a;    /* 0x200080BC */
extern uint8_t g_mode_8037;      /* 0x20008037 */
extern uint8_t g_setting_239;    /* 0x20000239 */
extern int32_t g_ref_7d7c;       /* 0x20007D7C */
extern int32_t g_va_aux;         /* 0x20007E6C  smoothed reading of the borrowed phase */
extern uint8_t g_flag_80a7;      /* 0x200080A7  g_va_aux > 5000 */
extern int32_t g_avg16_result;   /* 0x20007E08  16-sample mean (calibration reference) */

int32_t f9r5_correction(int32_t reading);
int32_t f9r6_correction(int32_t reading, int32_t scale);
void ad1_process_func9(void);
void ad1_process_func10(void);
void ad1_process_func3(void);
void ad1_process_func11(void);
void ad1_process_func5(void);
void acdc_capture(uint8_t next);
void acdc_settle(uint8_t phase);
void func2_reading_done(void);
void ad1_process_func1(void);
int32_t sign_offset(int32_t ref, int32_t reading);
void ad1_process_func17(void);
void ad1_process_func17_va(void);
void ad1_process_dual_ac(void);
void ad1_process_func4(void);
void ad1_process_func2(void);
void ad1_process_func16(void);
void ad1_process_func16_va(void);

#endif /* MEASURE_FUNCS_H */
