/*
 * NJU6350 real-time clock (3-wire, bit-banged).
 */
#include "rtc.h"
#include "board_io.h"

#define RTC_CE_PORT   GPIOC
#define RTC_CE        13u
#define RTC_BUS_PORT  GPIOB
#define RTC_CLK       6u
#define RTC_DATA      7u
#define RTC_DELAY     5u

/* orig: 0x08010308 */
void rtc_select(void)
{
    gpio_reset(RTC_BUS_PORT, RTC_CLK);
    gpio_reset(RTC_BUS_PORT, RTC_DATA);
    gpio_set(RTC_CE_PORT, RTC_CE);
}

/* orig: 0x0801031E */
void rtc_deselect(void)
{
    gpio_reset(RTC_BUS_PORT, RTC_CLK);
    gpio_reset(RTC_BUS_PORT, RTC_DATA);
    gpio_reset(RTC_CE_PORT, RTC_CE);
}

/* orig: 0x0801033C */
void rtc_data_pin_output(void)
{
    gpio_init_pins(RTC_BUS_PORT, 1u << RTC_DATA, GPIO_MODE_OUT, GPIO_SPEED_2MHZ, GPIO_PUSH_PULL,
                   GPIO_NOPULL);
}

/* orig: 0x08010378 */
void rtc_data_pin_input(void)
{
    gpio_init_pins(RTC_BUS_PORT, 1u << RTC_DATA, GPIO_MODE_IN, GPIO_SPEED_40MHZ, GPIO_PUSH_PULL,
                   GPIO_PULLUP);
}

static void rtc_data(int bit)
{
    if (bit) {
        gpio_set(RTC_BUS_PORT, RTC_DATA);
    } else {
        gpio_reset(RTC_BUS_PORT, RTC_DATA);
    }
}

/* orig: 0x08010204 - 4 bits, LSB first, data set up while CLK is low */
void rtc_write_address4(uint8_t address)
{
    for (uint8_t i = 0; i < 4; i++) {
        rtc_data((address >> i) & 1u);
        gpio_set(RTC_BUS_PORT, RTC_CLK);
        delay_loop(RTC_DELAY);
        gpio_reset(RTC_BUS_PORT, RTC_CLK);
        delay_loop(RTC_DELAY);
    }
}

/* orig: 0x080102B8 - 8 bits, LSB first */
void rtc_write_byte(uint8_t value)
{
    for (uint8_t i = 0; i < 8; i++) {
        gpio_reset(RTC_BUS_PORT, RTC_CLK);
        delay_loop(RTC_DELAY);
        rtc_data((value >> i) & 1u);
        gpio_set(RTC_BUS_PORT, RTC_CLK);
        delay_loop(RTC_DELAY);
    }
}

/* orig: 0x08010254 - 8 bits, LSB first, sampled while CLK is high */
uint8_t rtc_read_byte(void)
{
    uint8_t value = 0;

    delay_loop(RTC_DELAY);
    rtc_data_pin_input();
    delay_loop(RTC_DELAY);
    for (uint8_t i = 0; i < 8; i++) {
        gpio_set(RTC_BUS_PORT, RTC_CLK);
        delay_loop(RTC_DELAY);
        if (gpio_read(RTC_BUS_PORT, RTC_DATA)) {
            value |= 0x80u;
        }
        gpio_reset(RTC_BUS_PORT, RTC_CLK);
        delay_loop(RTC_DELAY);
        if (i < 7) {
            value >>= 1;
        }
    }
    rtc_data_pin_output();
    delay_loop(RTC_DELAY);
    return value;
}

/* orig: 0x0801013E */
uint8_t bin_to_bcd(uint8_t value)
{
    return (uint8_t)((value % 10) + (value / 10) * 16);
}

/* orig: 0x080101AC */
void rtc_write_register(uint8_t address, uint8_t value)
{
    uint8_t bcd = bin_to_bcd(value);

    rtc_select();
    delay_loop(RTC_DELAY);
    rtc_write_address4((uint8_t)(address + 1));
    rtc_write_byte(bcd);
    delay_loop(RTC_DELAY);
    rtc_deselect();
}

/* orig: 0x080101E2 - returns the raw (BCD) register value */
uint8_t rtc_read_register(uint8_t address)
{
    rtc_select();
    rtc_write_address4(address);
    uint8_t value = rtc_read_byte();
    rtc_deselect();
    return value;
}

/* orig: 0x0801016C */
void rtc_write_datetime(void)
{
    rtc_write_register(2, g_rtc_set_reg2);
    rtc_write_register(4, g_rtc_set_reg4);
    rtc_write_register(6, g_rtc_set_reg6);
    rtc_write_register(10, g_rtc_set_reg10);
    rtc_write_register(12, g_rtc_set_reg12);
    rtc_write_register(14, g_rtc_set_reg14);
}

/* orig: 0x08010118 - packed BCD byte to binary */
uint8_t bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)((bcd >> 4) * 10u + (bcd & 0x0Fu));
}

/*
 * orig: 0x080103D8 - days in month 1..12 of year 2000 + yy (Gregorian leap
 * years). Other months read past the original's 12-byte stack copy of the
 * table; the rebuild returns 0 for them.
 */
uint8_t rtc_days_in_month(uint8_t yy, uint8_t month)
{
    static const uint8_t days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };   /* orig: 0x08029190 */
    uint32_t year = (uint32_t)yy + 2000u;

    if (month < 1 || month > 12) {
        return 0;
    }
    if (month == 2 && ((year % 4u == 0 && year % 100u != 0) || year % 400u == 0)) {
        return 29;
    }
    return days[month - 1];
}

/* orig: 0x080100C4 - on each new RTC second: request the 1 s task and toggle g_flag_80e7 */
void rtc_poll_second(void)
{
    uint8_t sec = rtc_read_register(0x0E);

    if (g_rtc_last_second != sec) {
        g_task_flag_1s = 1;
        g_rtc_last_second = sec;
        g_flag_80e7 = (g_flag_80e7 == 1) ? 0 : 1;
    }
}
