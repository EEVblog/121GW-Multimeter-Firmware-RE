/*
 * Segment LCD driver (STM32L1 LCD peripheral).
 */
#include "lcd_driver.h"
#include "board_io.h"
#include "stm32l1xx.h"

#define FCR_SYNC_TIMEOUT      0x8000u
#define RCC_AHBENR_GPIO_ALL   0xFFu
#define RCC_APB1ENR_LCD       (1u << 9)
#define GPIO_AF_LCD           11u

#define LCD_FCR_PS_DIV8       (3u << 22)
#define LCD_FCR_DIV_16        (0u << 18)
#define LCD_CR_DUTY_1_4       (3u << 2)
#define LCD_CR_BIAS_1_3       (2u << 5)
#define LCD_FCR_DEAD_NONE     0u
#define LCD_FCR_CC_LEVEL4     (4u << 4)
#define LCD_FCR_BLINKF_DIV64  0x4000u   /* blink frequency field; blinking itself off */
#define LCD_FCR_BLINK_OFF     0u

/* Pulse-on duration (FCR.PON) for each contrast setting 10..17. */
static const uint16_t pulse_on_for_contrast[8] = {
    0x0000, 0x0400, 0x0800, 0x0C00, 0x1000, 0x1400, 0x1800, 0x1C00
};

/* Clear-then-set in two writes, as the ST library does. */
static void lcd_field(volatile uint32_t *reg, uint32_t keep_mask, uint32_t value)
{
    *reg &= keep_mask;
    *reg |= value;
}

/* orig: 0x08020402 - wait for FCR synchronisation, with a timeout */
void lcd_wait_fcr_sync(void)
{
    uint32_t n = 0;
    uint32_t sr;

    do {
        sr = LCD->SR;
        n++;
        if (n == FCR_SYNC_TIMEOUT) {
            return;
        }
    } while ((sr & LCD_SR_FCRSR) == 0);
}

/* orig: 0x0802048A (LCD_UpdateDisplayRequest) */
void lcd_request_update(void)
{
    LCD->SR = LCD_SR_UDR;
}

/* orig: 0x08020492 (LCD_GetFlagStatus) */
uint8_t lcd_flag_set(uint32_t flag)
{
    return (LCD->SR & flag) != 0;
}

/*
 * orig: 0x0801B304
 * Full update, unless g_flag_8050 is set while not logging and no full update
 * was forced, in which case only part of the display is refreshed. The flags
 * are combined bitwise, as in the original.
 */
void lcd_update(void)
{
    if ((uint8_t)(g_flag_8050 & (g_logging_active ^ 1u) & (g_flag_8053 ^ 1u)) == 0) {
        g_flag_8053 = 0;
        while (LCD->SR & LCD_SR_UDR) {
        }
        for (unsigned i = 0; i < 8; i++) {
            LCD->RAM[i] = g_lcd_shadow[i];
        }
        lcd_request_update();
    } else {
        lcd_update_partial();
    }
}

/*
 * orig: 0x0801B396
 * Refreshes only selected SEG0..31 bits of each COM row (the rest of the
 * display keeps its previous content). Unlike lcd_update() it does not wait
 * for a previous update to finish.
 */
void lcd_update_partial(void)
{
    lcd_field(&LCD->RAM[0], 0xFFFFEB01u, g_lcd_shadow[0] & 0x14FEu);
    lcd_field(&LCD->RAM[2], 0xFFFFF700u, g_lcd_shadow[2] & 0x08FFu);
    lcd_field(&LCD->RAM[4], 0xFFFFFF00u, (uint8_t)g_lcd_shadow[4]);
    lcd_field(&LCD->RAM[6], 0xFFFFFE01u, g_lcd_shadow[6] & 0x01FEu);
    lcd_request_update();
}

/* orig: 0x0801A81C */
void lcd_clear_shadow(void)
{
    while (LCD->SR & LCD_SR_UDR) {
    }
    for (uint8_t i = 0; i < 16; i++) {
        g_lcd_shadow[i] = 0;
    }
    g_display_dirty = 1;
}

/* orig: 0x0801B090 - five decimal digits, most significant first */
void number_to_digits(uint32_t value)
{
    g_digits[0] = (int32_t)(value / 10000);
    g_digits[1] = (int32_t)((value % 10000) / 1000);
    g_digits[2] = (int32_t)((value % 1000) / 100);
    g_digits[3] = (int32_t)((value % 100) / 10);
    g_digits[4] = (int32_t)(value % 10);
}

/*
 * orig: 0x0801B130
 * Converts g_digits to characters in g_digit_text, blanking leading zeros:
 * blank_mode 0 blanks up to four, 1 up to three, 2 up to two, 3 up to one,
 * other values none. Digits are signed, as in the original: values above
 * 255 become 0, values 10..255 pass through unchanged, and anything below
 * 10 (including negative values) has '0' added.
 */
void digits_to_text(uint8_t blank_mode)
{
    unsigned blankable;

    for (unsigned i = 0; i < 5; i++) {
        if (g_digits[i] > 0xFF) {
            g_digits[i] = 0;
        }
    }

    switch (blank_mode) {
    case 0:  blankable = 4; break;
    case 1:  blankable = 3; break;
    case 2:  blankable = 2; break;
    case 3:  blankable = 1; break;
    default: blankable = 0; break;
    }
    for (unsigned i = 0; i < blankable && g_digits[i] == 0; i++) {
        g_digits[i] = ' ';
    }

    for (unsigned i = 0; i < 5; i++) {
        if (g_digits[i] < 10) {
            g_digits[i] += '0';
        }
    }
    for (unsigned i = 0; i < 5; i++) {
        g_digit_text[i] = (char)g_digits[i];
    }
}

/* orig: 0x08017FB4 - secondary display (digits 0..4) decimal point, position 1..4 (0 = none) */
void lcd_set_secondary_dp(uint8_t position)
{
    g_lcd_shadow[0] &= 0xF55FFFFFu;
    switch (position) {
    case 1: g_lcd_shadow[0] |= 0x00200000u; break;
    case 2: g_lcd_shadow[0] |= 0x00800000u; break;
    case 3: g_lcd_shadow[0] |= 0x02000000u; break;
    case 4: g_lcd_shadow[0] |= 0x08000000u; break;
    default: break;
    }
}

/*
 * orig: 0x0801717E - main display (digits 5..9) decimal point, position 1..4.
 * Note: the mask on word 1 also clears bits 12..31 (SEG44+ do not exist, so
 * this has no visible effect).
 */
void lcd_set_main_dp(uint8_t position)
{
    g_lcd_shadow[0] &= ~0x00004000u;
    g_lcd_shadow[1] &= 0x00000FD5u;
    switch (position) {
    case 1: g_lcd_shadow[0] |= 0x00004000u; break;
    case 2: g_lcd_shadow[1] |= 0x00000002u; break;
    case 3: g_lcd_shadow[1] |= 0x00000008u; break;
    case 4: g_lcd_shadow[1] |= 0x00000020u; break;
    default: break;
    }
}

static void lcd_pulse_on_config(uint32_t pon)
{
    lcd_field(&LCD->FCR, 0xFFFFE3FFu, pon);
}

/* orig: 0x0801B708 - other contrast values leave FCR unchanged; inlines: 0x08020472 (LCD_PulseOnDurationConfig) */
void lcd_set_pulse_duration(uint8_t contrast)
{
    if (contrast >= LCD_CONTRAST_MIN && contrast <= LCD_CONTRAST_MAX) {
        lcd_pulse_on_config(pulse_on_for_contrast[contrast - LCD_CONTRAST_MIN]);
    }
}

/*
 * orig: 0x0801B77A
 * Writes PON = FCR & (PON value for the current contrast), as the original
 * does. For a contrast outside 10..17 the original passes an uninitialised
 * register; 0 is used here (valid settings never reach that case).
 */
void lcd_reapply_pulse_duration(void)
{
    uint32_t pon = 0;
    uint8_t c = g_lcd_contrast;

    if (c > LCD_CONTRAST_MIN && c <= LCD_CONTRAST_MAX) {
        pon = LCD->FCR & pulse_on_for_contrast[c - LCD_CONTRAST_MIN];
    } else if (c == LCD_CONTRAST_MIN) {
        (void)LCD->FCR;
    }
    lcd_pulse_on_config(pon);
}

/* orig: 0x0801B4D6 - COM/SEG pins to alternate function 11 */
void lcd_gpio_init(void)
{
    static const struct {
        GPIO_TypeDef *port;
        uint16_t pins;
    } lcd_pins[] = {
        { GPIOA, 0x87CE }, { GPIOB, 0xFF3B }, { GPIOC, 0x06FF },
        { GPIOD, 0xFF00 }, { GPIOE, 0x000F },
    };

    RCC->AHBENR |= RCC_AHBENR_GPIO_ALL;
    for (unsigned p = 0; p < sizeof lcd_pins / sizeof lcd_pins[0]; p++) {
        GPIO_TypeDef *port = lcd_pins[p].port;
        gpio_init_pins(port, lcd_pins[p].pins, GPIO_MODE_AF, GPIO_SPEED_400KHZ, GPIO_PUSH_PULL,
                       GPIO_NOPULL);
        for (unsigned pin = 0; pin < 16; pin++) {
            if (lcd_pins[p].pins & (1u << pin)) {
                volatile uint32_t *afr = &port->AFR[pin >> 3];
                unsigned shift = (pin & 7) * 4;
                *afr &= ~(0xFu << shift);
                *afr |= GPIO_AF_LCD << shift;
            }
        }
    }
}

/*
 * orig: 0x0801B450; inlines: 0x080203B4 (LCD_Init), 0x0802041E (LCD_MuxSegmentCmd),
 * 0x08020426 (LCD_ContrastConfig), 0x0802043E (LCD_DeadTimeConfig), 0x08020456 (LCD_BlinkConfig)
 */
void lcd_init(uint8_t contrast, uint8_t clear)
{
    lcd_gpio_init();
    RCC->APB1ENR |= RCC_APB1ENR_LCD;

    lcd_field(&LCD->FCR, 0xFC03FFFFu, LCD_FCR_PS_DIV8 | LCD_FCR_DIV_16);
    lcd_wait_fcr_sync();
    lcd_field(&LCD->CR, 0xFFFFFF81u, LCD_CR_DUTY_1_4 | LCD_CR_BIAS_1_3);

    LCD->CR &= ~LCD_CR_MUX_SEG;
    lcd_set_pulse_duration(contrast);
    lcd_field(&LCD->FCR, 0xFFFFFC7Fu, LCD_FCR_DEAD_NONE);
    lcd_field(&LCD->FCR, 0xFFFFFF8Fu, LCD_FCR_CC_LEVEL4);
    lcd_wait_fcr_sync();

    LCD->CR |= LCD_CR_LCDEN;
    while ((LCD->SR & LCD_SR_ENS) == 0) {
    }
    while ((LCD->SR & LCD_SR_RDY) == 0) {
    }
    lcd_field(&LCD->FCR, 0xFFFC1FFFu, LCD_FCR_BLINKF_DIV64 | LCD_FCR_BLINK_OFF);

    if (clear) {
        lcd_clear_shadow();
    }
    lcd_update();
}
