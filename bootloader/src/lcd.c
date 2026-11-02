/*
 * Minimal driver for the 121GW segment LCD, as used by the bootloader.
 *
 * The LCD peripheral runs at 1/4 duty, 1/3 bias, from the RTC clock (LSI).
 * A 16-word shadow copy of LCD_RAM is edited and pushed with lcd_update();
 * only COM0..COM3 (words 0..7) are used.
 */
#include "lcd.h"
#include "gpio.h"
#include "stm32l1xx.h"

/* LCD_SR_ENS/UDR/RDY/FCRSR and LCD_CR_LCDEN/MUX_SEG come from the CMSIS header. */

#define LCD_FCR_PS_DIV8      (3u << 22)
#define LCD_FCR_DIV_16       (0u << 18)
#define LCD_FCR_PON_4        (4u << 10)
#define LCD_FCR_DEAD_NONE    (0u << 7)
#define LCD_FCR_CC_LEVEL4    (4u << 4)
#define LCD_FCR_BLINK_OFF    0u
#define LCD_CR_DUTY_1_4      (3u << 2)
#define LCD_CR_BIAS_1_3      (2u << 5)
#define LCD_CR_VSEL_INTERNAL 0u

#define RCC_AHBENR_GPIO_ALL  0xFFu        /* GPIOA..GPIOH */
#define RCC_APB1ENR_LCD      (1u << 9)
#define GPIO_AF_LCD          11u

/* Shadow of LCD->RAM[0..15]: word 2*n is COMn SEG0..31, word 2*n+1 is COMn SEG32..39. */
static uint32_t lcd_buffer[16];

/*
 * 7-segment glyphs. Each byte holds two segment bits per COM row:
 * bits 7..6 -> COM0, 5..4 -> COM1, 3..2 -> COM2, 1..0 -> COM3.
 */
static const uint8_t glyph_digits[10] = {           /* orig: 0x08004EFC */
    0xBB, 0x11, 0xA7, 0x97, 0x1D, 0x9E, 0xBE, 0x1B, 0xBF, 0x9F
};
static const uint8_t glyph_upper[26] = {            /* orig: 0x08004E60 */
    0x3F, 0xBC, 0xAA, 0xB5, 0xAE, 0x2E, 0x9F, 0x3D, 0x11, 0xB1, 0x2D, 0xA8, 0xBF,
    0x3B, 0xBB, 0x2F, 0x1F, 0x2A, 0x9E, 0xAC, 0xB9, 0xB9, 0xB9, 0x1C, 0x9D, 0x86
};
static const uint8_t glyph_lower[26] = {            /* orig: 0x08004E7C */
    0x3F, 0xBC, 0xA4, 0xB5, 0xAE, 0x2E, 0x9F, 0x3C, 0x10, 0xB1, 0x2D, 0xA8, 0xBF,
    0x34, 0xB4, 0x2F, 0x1F, 0x24, 0x9E, 0xAC, 0xB0, 0xB0, 0x90, 0x1C, 0x9D, 0x86
};

/*
 * Where each digit position lives: which half of the COM row (0 = SEG0..31
 * word, 1 = SEG32..39 word) and the lower of its two segment bits.
 */
static const struct {
    uint8_t high_word;
    uint8_t bit;
} digit_position[10] = {
    { 0, 27 }, { 0, 25 }, { 0, 23 }, { 0, 21 }, { 0, 19 },
    { 1, 5 },  { 1, 3 },  { 1, 1 },  { 0, 14 }, { 0, 12 },
};

static const char *const main_messages[4] = { "-IAP-", "douun", " Good", " FAIL" };

/* Bargraph segments, lit in order as the progress count grows. */
static const struct {
    uint8_t com;
    uint8_t mask;
} progress_segment[LCD_PROGRESS_MAX + 1] = {
    { 1, 0x02 }, { 2, 0x02 }, { 3, 0x02 }, { 3, 0x04 }, { 2, 0x04 },
    { 1, 0x04 }, { 0, 0x04 }, { 0, 0x08 }, { 1, 0x08 }, { 2, 0x08 },
    { 3, 0x08 }, { 3, 0x10 }, { 2, 0x10 }, { 1, 0x10 }, { 0, 0x10 },
    { 0, 0x20 }, { 1, 0x20 }, { 2, 0x20 }, { 3, 0x20 }, { 3, 0x40 },
    { 2, 0x40 }, { 1, 0x40 }, { 0, 0x40 }, { 0, 0x80 },
    { 1, 0x80 },   /* never reached: the count is clamped to 24 */
};
#define PROGRESS_BASE_MASK 0x02u   /* COM0 bit 1, always lit */

/* orig: 0x08004134 */
static int lcd_flag(uint32_t flag)
{
    return (LCD->SR & flag) != 0;
}

/* orig: 0x080040B6 */
static void lcd_wait_fcr_sync(void)
{
    while ((LCD->SR & LCD_SR_FCRSR) == 0) {
    }
}

/* orig: 0x08003E16 - all LCD COM/SEG pins to alternate function 11
 * inlines: 0x08001D78 GPIO_StructInit */
static void lcd_gpio_init(void)
{
    static const struct {
        GPIO_TypeDef *port;
        uint16_t pins;
    } lcd_pins[] = {
        { GPIOA, 0x07CE }, { GPIOB, 0xFF23 }, { GPIOC, 0x06FF },
        { GPIOD, 0xFF00 }, { GPIOE, 0x000F },
    };

    RCC->AHBENR |= RCC_AHBENR_GPIO_ALL;

    for (unsigned p = 0; p < sizeof lcd_pins / sizeof lcd_pins[0]; p++) {
        GPIO_TypeDef *port = lcd_pins[p].port;
        gpio_init_pins(port, lcd_pins[p].pins, GPIO_MODE_AF, GPIO_SPEED_400KHZ, GPIO_PUSH_PULL,
                       GPIO_NOPULL);
        for (unsigned pin = 0; pin < 16; pin++) {
            if (lcd_pins[p].pins & (1u << pin)) {
                gpio_set_af(port, pin, GPIO_AF_LCD);
            }
        }
    }
}

/* Clear-then-set in two writes, as the original does for every LCD field. */
static void lcd_field(volatile uint32_t *reg, uint32_t keep_mask, uint32_t value)
{
    *reg &= keep_mask;
    *reg |= value;
}

/* orig: 0x08003DA0
 * inlines: 0x08004068 LCD_Init, 0x080040C0 LCD_MuxSegmentCmd,
 *          0x08004114 LCD_PulseOnDurationConfig, 0x080040E0 LCD_DeadTimeConfig,
 *          0x080040C8 LCD_ContrastConfig, 0x080040AE LCD_Cmd, 0x080040F8 LCD_BlinkConfig */
void lcd_init(void)
{
    lcd_gpio_init();
    RCC->APB1ENR |= RCC_APB1ENR_LCD;

    lcd_field(&LCD->FCR, 0xFC03FFFFu, LCD_FCR_PS_DIV8 | LCD_FCR_DIV_16);
    lcd_wait_fcr_sync();
    lcd_field(&LCD->CR, 0xFFFFFF81u, LCD_CR_DUTY_1_4 | LCD_CR_BIAS_1_3 | LCD_CR_VSEL_INTERNAL);

    LCD->CR &= ~LCD_CR_MUX_SEG;
    lcd_field(&LCD->FCR, 0xFFFFE3FFu, LCD_FCR_PON_4);
    lcd_field(&LCD->FCR, 0xFFFFFC7Fu, LCD_FCR_DEAD_NONE);
    lcd_field(&LCD->FCR, 0xFFFFFF8Fu, LCD_FCR_CC_LEVEL4);
    lcd_wait_fcr_sync();

    LCD->CR |= LCD_CR_LCDEN;
    while (!lcd_flag(LCD_SR_ENS)) {
    }
    while (!lcd_flag(LCD_SR_RDY)) {
    }
    lcd_field(&LCD->FCR, 0xFFFC1FFFu, LCD_FCR_BLINK_OFF);

    lcd_clear();
}

/* orig: 0x08003D38
 * inlines: 0x0800412C LCD_UpdateDisplayRequest */
void lcd_update(void)
{
    while (lcd_flag(LCD_SR_UDR)) {
    }
    for (unsigned i = 0; i < 8; i++) {
        LCD->RAM[i] = lcd_buffer[i];
    }
    LCD->SR = LCD_SR_UDR;   /* request update (other SR bits are read-only) */
}

/* orig: 0x08003630 */
void lcd_clear(void)
{
    while (lcd_flag(LCD_SR_UDR)) {
    }
    for (unsigned i = 0; i < 16; i++) {
        lcd_buffer[i] = 0;
    }
    lcd_update();
}

/* orig: 0x0800365E */
void lcd_all_segments_on(void)
{
    for (unsigned com = 0; com < 4; com++) {
        lcd_buffer[com * 2] = 0xFFFFFFFFu;
        lcd_buffer[com * 2 + 1] |= 0xFFu;
    }
    lcd_update();
}

/* orig: 0x08003CBA */
static uint8_t lcd_glyph(uint8_t c)
{
    if (c >= '0' && c <= '9') {
        return glyph_digits[c - '0'];
    }
    if (c >= 'A' && c <= 'Z') {
        return glyph_upper[c - 'A'];
    }
    if (c >= 'a' && c <= 'z') {
        return glyph_lower[c - 'a'];
    }
    if (c == '-') {
        return 0x04;
    }
    if (c == 0xFF) {
        return 0xBF;
    }
    return 0;   /* space and anything else: blank */
}

/*
 * orig: 0x080036CE
 * Writes one glyph at a digit position. On COM0 only the upper of the two
 * bits is cleared first (the lower one is never cleared here), matching the
 * original image.
 */
static void lcd_put_glyph(uint8_t c, uint8_t position)
{
    if (position >= 10) {
        return;
    }
    uint8_t glyph = lcd_glyph(c);
    unsigned bit = digit_position[position].bit;
    unsigned word = digit_position[position].high_word;

    for (unsigned com = 0; com < 4; com++) {
        uint32_t bits = (glyph >> (6 - com * 2)) & 3u;
        uint32_t clear = (com == 0) ? (2u << bit) : (3u << bit);
        uint32_t *w = &lcd_buffer[com * 2 + word];
        if (word) {
            /* the original edits only the low byte of the SEG32..39 words */
            uint8_t *b = (uint8_t *)w;
            *b = (uint8_t)((*b & ~clear) | (bits << bit));
        } else {
            *w = (*w & ~clear) | (bits << bit);
        }
    }
}

/* orig: 0x080035FC - always writes five characters */
static void lcd_put_text(uint8_t row, const char *text)
{
    uint8_t position = (row == LCD_ROW_MAIN) ? 5 : 0;

    for (unsigned i = 0; i < 5; i++) {
        lcd_put_glyph((uint8_t)text[i], position++);
    }
}

/* orig: 0x08003328 */
void lcd_show_main_message(uint8_t msg)
{
    lcd_buffer[4] &= ~(1u << 29);   /* COM2 SEG29 off (annunciator, role to be confirmed) */
    if (msg < 4) {
        lcd_put_text(LCD_ROW_MAIN, main_messages[msg]);
    }
    lcd_update();
}

/* orig: 0x08003388 */
void lcd_show_secondary_message(uint8_t msg)
{
    lcd_buffer[4] &= ~(1u << 29);
    if (msg == LCD_MSG_IAP) {
        lcd_put_text(LCD_ROW_SECONDARY, main_messages[LCD_MSG_IAP]);
    }
    lcd_update();
}

/* orig: 0x080033B2 - lights 'steps' bargraph segments (never clears any) */
void lcd_show_progress(uint8_t steps)
{
    if (steps > LCD_PROGRESS_MAX) {
        steps = LCD_PROGRESS_MAX;
    }
    lcd_buffer[0] |= PROGRESS_BASE_MASK;
    for (unsigned k = 0; k <= LCD_PROGRESS_MAX; k++) {
        if (steps > k) {
            lcd_buffer[progress_segment[k].com * 2] |= progress_segment[k].mask;
        }
    }
    lcd_update();
}
