/*
 * HY3131 serial interface (bit-banged on port F).
 */
#include "hy3131_bus.h"
#include "board_io.h"

#define PF8 8u
#define PF8_SAMPLES 20u
#define PF8_HIGH_THRESHOLD 10u

/* orig: 0x0800C1BC */
void hy3131_clock_pulse(void)
{
    delay_loop(1);
    gpio_set(HY3131_PORT, HY3131_CK);
    delay_loop(1);
    gpio_reset(HY3131_PORT, HY3131_CK);
}

/* orig: 0x0800C16C - MSB first; turnaround == 1 adds one clock with DI low */
void hy3131_write_byte(uint8_t value, uint8_t turnaround)
{
    for (uint8_t i = 0; i < 8; i++) {
        if ((value & (0x80u >> i)) == 0) {
            gpio_reset(HY3131_PORT, HY3131_DI);
        } else {
            gpio_set(HY3131_PORT, HY3131_DI);
        }
        hy3131_clock_pulse();
    }
    if (turnaround == 1) {
        gpio_reset(HY3131_PORT, HY3131_DI);
        hy3131_clock_pulse();
    }
    gpio_set(HY3131_PORT, HY3131_DI);
}

/* orig: 0x0800C138 - MSB first, sampled while CK is high */
uint8_t hy3131_read_byte(void)
{
    uint8_t value = 0;

    gpio_reset(HY3131_PORT, HY3131_CK);
    for (uint8_t i = 0; i < 8; i++) {
        gpio_set(HY3131_PORT, HY3131_CK);
        if (gpio_read(HY3131_PORT, HY3131_DO)) {
            value |= (uint8_t)(0x80u >> i);
        }
        gpio_reset(HY3131_PORT, HY3131_CK);
    }
    return value;
}

static void hy3131_start(void)
{
    gpio_set(HY3131_PORT, HY3131_DI);
    gpio_reset(HY3131_PORT, HY3131_CK);
    gpio_reset(HY3131_PORT, HY3131_CS);
}

/*
 * orig: 0x0800C03C
 * The byte counter is 8 bits wide, as in the original, so count must be
 * below 256.
 */
void hy3131_write(const uint8_t *data, uint8_t reg, uint16_t count)
{
    hy3131_start();
    delay_loop(10);
    hy3131_write_byte((uint8_t)(reg << 1), 0);
    delay_loop(10);
    for (uint8_t i = 0; i < count; i++) {
        hy3131_write_byte(data[i], 0);
        delay_loop(10);
    }
    gpio_set(HY3131_PORT, HY3131_CS);
}

/*
 * orig: 0x0800C0C0
 * Always reads into hy3131_rx_buffer; the first argument is ignored, as in
 * the original (callers pass the buffer anyway).
 */
void hy3131_read(uint8_t *unused, uint8_t reg, uint16_t count)
{
    (void)unused;
    hy3131_start();
    delay_loop(5);
    hy3131_write_byte((uint8_t)((reg << 1) | 1u), 1);
    delay_loop(5);
    for (uint8_t i = 0; i < count; i++) {
        hy3131_rx_buffer[i] = hy3131_read_byte();
        delay_loop(2);
    }
    gpio_set(HY3131_PORT, HY3131_CS);
}

/* orig: 0x0800C28C - sample PF8 with a pull-up, then leave it analogue */
void detect_pf8_low(void)
{
    uint8_t high = 0;

    g_pf8_detected_low = 0;
    gpio_init_pins(GPIOF, 1u << PF8, GPIO_MODE_IN, GPIO_SPEED_40MHZ, GPIO_PUSH_PULL, GPIO_PULLUP);
    for (uint8_t i = 0; i < PF8_SAMPLES; i++) {
        if (gpio_read(GPIOF, PF8)) {
            high++;
        }
    }
    if (high < PF8_HIGH_THRESHOLD) {
        g_pf8_detected_low = 1;
    }
    gpio_init_pins(GPIOF, 1u << PF8, GPIO_MODE_ANALOG, GPIO_SPEED_40MHZ, GPIO_PUSH_PULL,
                   GPIO_NOPULL);
}

/* orig: 0x0801C8EC - bus idle: CS (PF1) and DI (PF2) high, CK (PF0) low */
void hy3131_bus_idle(void)
{
    GPIOF->BSRR = 1u << 1;
    GPIOF->BSRR = 1u << 2;
    GPIOF->BSRR = 1u << (0 + 16);
}
