/*
 * Minimal driver for the 121GW segment LCD (4 COM x 40 SEG), as used by the
 * bootloader: two 5-digit 7-segment rows plus a progress bargraph.
 */
#ifndef LCD_H
#define LCD_H

#include <stdint.h>

/* Text rows written by lcd_show_text(). */
enum {
    LCD_ROW_MAIN = 0,       /* digit positions 5..9 */
    LCD_ROW_SECONDARY = 1   /* digit positions 0..4 */
};

/* Messages for the main row. */
enum {
    LCD_MSG_IAP = 0,        /* "-IAP-" */
    LCD_MSG_DOWNLOAD = 1,   /* "douun" */
    LCD_MSG_GOOD = 2,       /* " Good" */
    LCD_MSG_FAIL = 3        /* " FAIL" */
};

#define LCD_PROGRESS_MAX  24u

void lcd_init(void);
void lcd_update(void);
void lcd_clear(void);
void lcd_all_segments_on(void);
void lcd_show_main_message(uint8_t msg);
void lcd_show_secondary_message(uint8_t msg);
void lcd_show_progress(uint8_t steps);

#endif /* LCD_H */
