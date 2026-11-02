/*
 * Display: bar graph.
 *
 * The bar graph has 26 segments, numbered from the left. Consecutive
 * segments zig-zag through the COM rows of SEG1..SEG7 (shadow words 0, 2,
 * 4 and 6).
 */
#include "display_bargraph.h"
#include "buzzer.h"
#include "display_symbols.h"
#include "dual_correction.h"
#include "hy3131_ctrl.h"
#include "lcd_driver.h"
#include "measure_derived.h"

#define BARGRAPH_SEGMENTS 26u

/* orig: 0x08019408 (code) - shadow word and bit of each segment */
static const struct {
    uint8_t word;
    uint8_t bit;
} bargraph_seg[BARGRAPH_SEGMENTS] = {
    { 0, 1 }, { 2, 1 }, { 4, 1 }, { 6, 1 },
    { 6, 2 }, { 4, 2 }, { 2, 2 }, { 0, 2 },
    { 0, 3 }, { 2, 3 }, { 4, 3 }, { 6, 3 },
    { 6, 4 }, { 4, 4 }, { 2, 4 }, { 0, 4 },
    { 0, 5 }, { 2, 5 }, { 4, 5 }, { 6, 5 },
    { 6, 6 }, { 4, 6 }, { 2, 6 }, { 0, 6 },
    { 0, 7 }, { 2, 7 },
};

/*
 * orig: 0x08019408 - clear segments from `from` down to `to` (inclusive),
 * or down to segment 0 when `to` is above `from`. Does nothing for
 * from > 25.
 */
void bargraph_clear(uint8_t from, uint8_t to)
{
    uint8_t i = from;

    if (from >= BARGRAPH_SEGMENTS) {
        return;
    }
    for (;;) {
        g_lcd_shadow[bargraph_seg[i].word] &= ~(1u << bargraph_seg[i].bit);
        if (i == 0 || i == to) {
            return;
        }
        i--;
    }
}

/* Functions that show the bar graph: all but 5..8 and 12 */
static int bargraph_function(void)
{
    return (g_function < 6 || g_function > 8) && g_function != 12 && g_function != 5;
}

static void bargraph_set(uint8_t i)
{
    g_lcd_shadow[bargraph_seg[i].word] |= 1u << bargraph_seg[i].bit;
}

/*
 * orig: 0x08018FA8 - bar graph sign and scale symbols, and its length
 * (|g_scaled_value| / 400 with fine == 1, else / 2000). With an overload
 * (g_flag_8046) the bar is full: always without g_apo_long, otherwise when
 * the value is at or above g_bar_limit (unsigned compare) or not positive.
 * Function 11 is limited to 15 segments.
 */
void bargraph_compute(uint8_t fine)
{
    int32_t v;
    int32_t mag;
    uint16_t divisor;

    if (!bargraph_function()) {
        return;
    }
    if (g_scaled_value < 0) {
        g_lcd_shadow[4] |= 1u;
        g_lcd_shadow[2] &= ~1u;
    } else {
        g_lcd_shadow[4] &= ~1u;
        g_lcd_shadow[2] |= 1u;
    }
    if (fine == 1) {
        g_lcd_shadow[0] = (g_lcd_shadow[0] & ~0x1000u) | 0x400u;
        sym_range_state_update(1);
        divisor = 400;
    } else {
        g_lcd_shadow[0] = (g_lcd_shadow[0] | 0x1000u) & ~0x400u;
        sym_range_state_update(0);
        divisor = 2000;
    }
    bargraph_set(0);
    v = g_scaled_value;
    mag = (v < 0) ? (int32_t)(0u - (uint32_t)v) : v;
    g_bar_length = (uint8_t)(mag / (int32_t)divisor);
    if (g_flag_8046 != 0) {
        if (g_apo_long == 0) {
            g_bar_length = BARGRAPH_SEGMENTS - 1u;
        } else if ((uint32_t)g_scaled_value >= g_bar_limit ||
                   (int32_t)(0u - (uint32_t)g_scaled_value) >= g_scaled_value) {
            g_bar_length = BARGRAPH_SEGMENTS - 1u;
        }
    }
    if (g_function == 11 && g_bar_length >= 16) {
        g_bar_length = 15;
    }
}

/* orig: 0x08018F14 - bar graph scale: fine for function 0, ranges 3 of 1..4 and 2 of 20..21 */
void bargraph_compute_for_function(void)
{
    uint8_t f = g_function;

    if (f == 0) {
        bargraph_compute(1);
    } else if (f >= 1 && f <= 4) {
        bargraph_compute(g_range == 3);
    } else if ((f >= 9 && f <= 11) || (f >= 13 && f <= 19) || (f >= 22 && f <= 24)) {
        bargraph_compute(0);
    } else if (f == 20 || f == 21) {
        bargraph_compute(g_range == 2);
    }
}

/* orig: 0x08018B80 - compute and draw segments 1..g_bar_length */
void bargraph_draw(void)
{
    uint8_t i;

    bargraph_compute_for_function();
    for (i = 1; i < BARGRAPH_SEGMENTS; i++) {
        if (g_bar_length < i) {
            bargraph_clear(BARGRAPH_SEGMENTS - 1u, i);
        } else {
            bargraph_set(i);
        }
    }
    g_display_dirty = 1;
}

/* orig: 0x08018B14 */
void bargraph_update(void)
{
    g_scaled_ready = 0;
    if (bargraph_function()) {
        bargraph_draw();
    }
}

/* orig: 0x08019364 - bar graph off, with its sign and scale symbols */
void bargraph_off(void)
{
    g_lcd_shadow[0] &= ~0x1400u;
    g_lcd_shadow[4] &= ~0x81u;
    g_lcd_shadow[6] &= ~0x180u;
    g_lcd_shadow[2] &= ~0x801u;
    bargraph_clear(BARGRAPH_SEGMENTS - 1u, 0);
}

/* orig: 0x080192FC - empty bar graph (zero value) for bar graph functions, else off */
void bargraph_reset(void)
{
    if (bargraph_function()) {
        g_scaled_value = 0;
        g_bar_length = 0;
        bargraph_compute_for_function();
        bargraph_clear(BARGRAPH_SEGMENTS - 1u, 1);
        g_display_dirty = 1;
    } else {
        bargraph_off();
    }
}
