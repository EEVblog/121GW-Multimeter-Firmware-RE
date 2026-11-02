/*
 * Segment LCD driver (STM32L1 LCD peripheral, 4 COM x 40 SEG, 1/4 duty, 1/3 bias).
 *
 * Display code edits g_lcd_shadow (a copy of LCD->RAM[0..15]; word 2n holds
 * COMn SEG0..31, word 2n+1 COMn SEG32..39) and calls lcd_update().
 */
#ifndef LCD_DRIVER_H
#define LCD_DRIVER_H

#include <stdint.h>

#define LCD_CONTRAST_MIN 10u
#define LCD_CONTRAST_MAX 17u

extern uint32_t g_lcd_shadow[16];       /* 0x20007790 */
extern uint8_t g_lcd_contrast;          /* 0x20000237  10..17, default 14 */
extern uint8_t g_display_dirty;         /* 0x2000805B  request lcd_update() */
extern int32_t g_digits[5];             /* 0x20007DB0  decimal digits, most significant first (signed) */
extern char g_digit_text[5];            /* 0x20007C30  digits as characters, leading blanks */
extern uint8_t g_flag_8050;             /* 0x20008050  role unknown; selects partial updates */
extern uint8_t g_flag_8053;             /* 0x20008053  forces one full update */
extern uint8_t g_logging_active;        /* 0x20007FE1 */

void lcd_wait_fcr_sync(void);
void lcd_update(void);
void lcd_update_partial(void);
void lcd_clear_shadow(void);
void number_to_digits(uint32_t value);
void digits_to_text(uint8_t blank_mode);
void lcd_set_main_dp(uint8_t position);
void lcd_set_sub_dp(uint8_t position);
void lcd_set_pulse_duration(uint8_t contrast);
void lcd_reapply_pulse_duration(void);
void lcd_gpio_init(void);
void lcd_init(uint8_t contrast, uint8_t clear);

#endif /* LCD_DRIVER_H */
