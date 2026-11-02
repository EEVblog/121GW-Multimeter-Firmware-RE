/*
 * Display state helpers used by measurement processing.
 */
#ifndef DISPLAY_STATE_H
#define DISPLAY_STATE_H

#include <stdint.h>

extern int32_t g_disp_eac;       /* 0x20007EAC */
extern int32_t g_disp_eb0;       /* 0x20007EB0 */
extern uint8_t g_disp_flag_8062; /* 0x20008062  redraw request (bargraph?) */
extern uint8_t g_disp_flag_80bf; /* 0x200080BF */
extern uint8_t g_disp_flag_80c0; /* 0x200080C0 */
extern uint8_t g_disp_flag_80c1; /* 0x200080C1 */
extern uint8_t g_disp_flag_80ca; /* 0x200080CA */
extern uint8_t g_disp_flag_80cb; /* 0x200080CB */

void lcd_bar_flag_dual(uint8_t function, uint8_t range);
void measure_display_reset(void);

#endif /* DISPLAY_STATE_H */
