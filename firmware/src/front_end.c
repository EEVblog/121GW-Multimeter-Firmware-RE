/*
 * Analogue front-end switching per measurement function.
 */
#include "front_end.h"
#include "board_io.h"
#include "hy3131_ctrl.h"
#include "system_init.h"

/* orig: 0x0800FE00 - always sets PD1 (the arguments are unused) */
void front_end_pd1_set(uint8_t function, uint8_t unused)
{
    (void)function;
    (void)unused;
    gpio_set(GPIOD, 1);
}

/* orig: 0x0800FD30 */
void front_end_pe15_pf6(uint8_t function)
{
    if (function == 11 && g_flag_80b3 != 0) {
        gpio_reset(GPIOE, 15);
        gpio_set(GPIOF, 6);
        return;
    }
    if (function == 4) {
        gpio_reset(GPIOE, 15);
    } else {
        gpio_set(GPIOE, 15);
    }
    gpio_reset(GPIOF, 6);
}

/* orig: 0x0800FD80 */
void front_end_pa11(uint8_t function)
{
    if (function == 14 || function == 15) {
        gpio_reset(GPIOA, 11);
    } else {
        gpio_set(GPIOA, 11);
    }
}

/* orig: 0x0800FDB0 */
void front_end_ph2(uint8_t function)
{
    if (function == 5) {
        gpio_set(GPIOH, 2);
    } else {
        gpio_reset(GPIOH, 2);
    }
}

/* orig: 0x0800FDD0 */
void front_end_pa12(uint8_t function)
{
    if (function == 6 || function == 7 || function == 8) {
        gpio_reset(GPIOA, 12);
    } else {
        gpio_set(GPIOA, 12);
    }
}

/* orig: 0x0800FE14 - set for functions 13..24 */
void front_end_pe12(uint8_t function)
{
    if (function >= 13 && function <= 24) {
        gpio_set(GPIOE, 12);
    } else {
        gpio_reset(GPIOE, 12);
    }
}

/* orig: 0x0800FE74 */
void front_end_pe5(uint8_t function, uint8_t range)
{
    int low;

    switch (function) {
    case 16: case 17: case 18: case 19: case 20: case 21:
        low = range != 0;
        break;
    case 13: case 14: case 15: case 22: case 23: case 24:
        low = g_sel_80c7 != 0;
        break;
    default:
        low = 0;
        break;
    }
    if (low) {
        gpio_reset(GPIOE, 5);
    } else {
        gpio_set(GPIOE, 5);
    }
}

/* orig: 0x0800FBE8 */
void front_end_configure(uint8_t function, uint8_t range)
{
    front_end_pd1_set(function, g_flag_8098);
    front_end_select(function);
    g_flag_80b3 = 0;
    front_end_pe15_pf6(function);
    front_end_pa12(function);
    front_end_pa11(function);
    front_end_ph2(function);
    front_end_pe12(function);
    front_end_pe5(function, range);
}
