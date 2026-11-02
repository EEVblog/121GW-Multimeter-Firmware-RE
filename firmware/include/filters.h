/*
 * Reading filters.
 */
#ifndef FILTERS_H
#define FILTERS_H

#include <stdint.h>

#define MOVING_AVERAGE_LEN   10
#define MOVING_AVERAGE_B_LEN 20   /* function 9 range 6 averages 20 readings */

extern int32_t g_smooth3_out;     /* 0x20007E70 */
extern int32_t g_smooth3_sum;     /* 0x20007E74 */
extern int32_t g_smooth3_count;   /* 0x20007E78  0..3 */
extern int32_t g_mavg_buf[MOVING_AVERAGE_LEN];   /* 0x200070A8  newest first */
extern int32_t g_mavg_out;        /* 0x20007CD8 */
extern uint8_t g_mavg_count;      /* 0x20008084 */
extern int32_t g_mavg_b_buf[MOVING_AVERAGE_B_LEN]; /* 0x20007238 (400 bytes free in the original) */
extern int32_t g_mavg_b_out;      /* 0x20007DF8 */
extern uint8_t g_mavg_b_count;    /* 0x20008085 */

int32_t smooth3(int32_t value);
int32_t moving_average(int32_t value, int32_t window, uint16_t max_count);
int32_t moving_average_b(int32_t value, int32_t window, uint16_t max_count);

#endif /* FILTERS_H */
