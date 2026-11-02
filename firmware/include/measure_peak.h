/*
 * HY3131 peak-hold results.
 */
#ifndef MEASURE_PEAK_H
#define MEASURE_PEAK_H

#include <stdint.h>

extern uint32_t g_peak_raw_min;  /* 0x20007DEC  HY3131 minimum peak register */
extern uint32_t g_peak_raw_max;  /* 0x20007DF0  HY3131 maximum peak register */
extern uint32_t g_peak_min_raw;  /* 0x20007DF4  raw register at the held minimum */
extern uint32_t g_peak_max;      /* 0x20007E8C  held maximum (scaled) */
extern uint32_t g_peak_min;      /* 0x20007E90  held minimum (inverted raw) */
extern uint8_t g_peak_select;    /* 0x200080B8  0 = maximum, else minimum */
extern uint8_t g_peak_is_min;    /* 0x20008045 */

void hy3131_on_peak_hold(void);

#endif /* MEASURE_PEAK_H */
