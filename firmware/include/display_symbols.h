/*
 * Display: 7-segment font and individual LCD symbol setters.
 */
#ifndef DISPLAY_SYMBOLS_H
#define DISPLAY_SYMBOLS_H

#include <stdint.h>
#include "app_globals.h"

uint8_t char_to_segments(uint8_t c);
void sub_digits_from_sel_c6(void);
void sub_digits_for_dual(void);
uint8_t dp_for_range(uint8_t range);
int32_t temp_for_display(int32_t c100);
void sym_w1_b7_update(void);
void sym_w7_b7_update(uint8_t refresh);
void sym_w0_b31_update(uint8_t refresh);
void sym_b9_for_range(uint8_t range);
void sym_b8_b11_row1(uint8_t range);
void sym_b8_b11_row2(uint8_t range);
void sym_w0_b16_update(uint8_t refresh);
uint8_t display_partial_mode(void);
void sym_w2_b31_update(void);
void sym_w5_b7_update(uint8_t refresh);
void sym_w0_b0_update(uint8_t refresh);
uint8_t autorange_shown(void);
void sym_state_804d(uint8_t state);
void sym_w6_b0_b16_update(uint8_t refresh);

#endif /* DISPLAY_SYMBOLS_H */
