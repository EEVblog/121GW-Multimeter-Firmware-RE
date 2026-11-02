/*
 * Function/range change: HY3131 reconfiguration, calibration reload,
 * main display decimal point and display mode.
 */
#ifndef RANGE_SELECT_H
#define RANGE_SELECT_H

#include <stdint.h>

extern uint8_t g_main_dp;      /* 0x2000803F  main display decimal point */
extern uint8_t g_flag_80a5;    /* 0x200080A5 */
extern uint8_t g_flag_80c3;    /* 0x200080C3  set on every function/range change */
extern uint8_t g_flag_80c5;    /* 0x200080C5 */
extern uint16_t g_counter_fc2; /* 0x20007FC2 */
extern uint8_t g_flag_80c8;    /* 0x200080C8  set when auto-range moves up */
extern int32_t g_disp_eb4;     /* 0x20007EB4  unscaled second reading (functions 13, 22) */

void range_configure(uint8_t load_cal);
void range_select(uint8_t load_cal);
uint8_t main_dp_for_range(uint8_t function, uint8_t range);
void main_dp_update(void);
void mode_238_select(void);
void range_change(void);
void reconfigure_borrow_f2(uint8_t borrow);
void reconfigure_borrow_f1(uint8_t borrow);
void autorange_primary(int32_t value);
void autorange_secondary(int32_t value);

uint8_t range_fixed_function(uint8_t ignore_range);

extern const uint8_t g_range_count[26];   /* 0x20000000  ranges per function (initialised RAM) */

void dual_rows_for_range(uint8_t function, uint8_t range);
void range_key(void);

#endif /* RANGE_SELECT_H */
