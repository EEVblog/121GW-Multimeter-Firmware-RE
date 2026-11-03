/*
 * Measurement modes behind the keys: HOLD, REL, MIN/MAX, the 0x20008050
 * mode, MEM store/recall display, and the start of a calibration step.
 *
 * MIN/MAX (g_minmax_mode) cycles 1 MAX, 2 MIN, 3 AVG, 4 held value.
 */
#ifndef MODES_H
#define MODES_H

#include <stdint.h>
#include "app_globals.h"

extern int32_t g_hold_value;            /* 0x20007D6C  reading frozen by HOLD */
extern int32_t g_minmax_last;           /* 0x20007D9C  last reading taken by MIN/MAX */
extern int32_t g_rel_reference;         /* 0x20007DA8  REL reference */
extern uint8_t g_rel_capture;           /* 0x20008059  take the next reading as the REL reference */
extern uint8_t g_manual_range_saved;    /* 0x20008044  g_manual_range before HOLD / MIN/MAX */
extern uint8_t g_flag_8051;             /* 0x20008051 */
extern uint8_t g_flag_8052;             /* 0x20008052 */
extern uint8_t g_flag_8055;             /* 0x20008055 */
extern uint8_t g_range_8056;            /* 0x20008056  range when auto-hold mode started */
extern uint16_t g_count_7fb2;           /* 0x20007FB2 */
extern uint8_t g_log_due;               /* 0x20007FE2 */
extern int32_t g_value_7d44;            /* 0x20007D44  set to 60 when MEM is full */
extern uint8_t g_flag_8074;             /* 0x20008074  functions 6-8 */
extern const int32_t g_bar_limit_table[25][7];   /* 0x08025A3C */

void bar_limit_update(void);
int32_t minmax_value(int32_t value_if_none);
void minmax_update(void);
void rel_apply(void);
void autohold_mode_set(uint8_t on, uint8_t clear_modes);
void menu_exit(void);
void flag_80b3_toggle(void);
void minmax_show(void);
void minmax_key(void);
void mem_log_store(void);
void mem_recall_show(uint16_t n);
void cal_trims_reset(void);
uint8_t cal_freq_check(uint8_t second);
uint8_t cal_step_samples(void);
uint8_t cal_start(uint8_t which);
void mode_key(void);

extern int32_t g_autohold_value;        /* 0x20007DA0  reading held by auto-hold (1000000: overload) */
extern int32_t g_autohold_candidate;    /* 0x20007DA4 */
extern int32_t g_autohold_captured;     /* 0x20007DAC */
extern uint8_t g_autohold_armed;        /* 0x20008057 */
extern uint8_t g_autohold_force;        /* 0x20008054  capture the next stable reading even if close to the held one */
extern uint8_t g_preset_phase;          /* 0x2000809B */
extern uint8_t g_lowz_count;            /* 0x2000023A  (initialised RAM) */

uint8_t range_step_blocked(uint8_t up);
void autohold_update(void);
void voltage_warnings(void);
void counter_preset_alternate(void);
void lowz_detect(void);
void measure_postprocess(void);

extern uint8_t g_jack_prev;             /* 0x200080EA  last socket sample (bit 0 PE10, bit 1 PE11) */
extern uint8_t g_jack_debounce;         /* 0x2000023B  (initialised RAM) */
extern uint8_t g_jack_amps;             /* 0x20008075  plug in the mA/uA socket (code 1) */

void input_jack_check(void);
void secondary_mode_next(void);

#endif /* MODES_H */
