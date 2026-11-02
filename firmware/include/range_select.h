/*
 * Function/range change: HY3131 reconfiguration, calibration reload,
 * secondary display decimal point and display mode.
 */
#ifndef RANGE_SELECT_H
#define RANGE_SELECT_H

#include <stdint.h>

extern uint8_t g_sub_dp;       /* 0x2000803F  secondary display decimal point */
extern uint8_t g_flag_80a5;    /* 0x200080A5 */
extern uint8_t g_flag_80c3;    /* 0x200080C3  set on every function/range change */
extern uint8_t g_flag_80c5;    /* 0x200080C5 */
extern uint16_t g_counter_fc2; /* 0x20007FC2 */

void range_configure(uint8_t load_cal);
void range_select(uint8_t load_cal);
uint8_t sub_dp_for_range(uint8_t function, uint8_t range);
void sub_dp_update(void);
void mode_238_select(void);
void range_change(void);
void reconfigure_borrow_f2(uint8_t borrow);
void reconfigure_borrow_f1(uint8_t borrow);

#endif /* RANGE_SELECT_H */
