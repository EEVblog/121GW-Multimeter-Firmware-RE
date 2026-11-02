/*
 * Low-level board I/O helpers for the application: busy-wait delay and
 * GPIO pin configuration.
 */
#ifndef BOARD_IO_H
#define BOARD_IO_H

#include <stdint.h>
#include "stm32l1xx.h"

#define GPIO_MODE_IN      0u
#define GPIO_MODE_OUT     1u
#define GPIO_MODE_AF      2u
#define GPIO_MODE_ANALOG  3u
#define GPIO_SPEED_400KHZ 0u
#define GPIO_SPEED_2MHZ   1u
#define GPIO_SPEED_10MHZ  2u
#define GPIO_SPEED_40MHZ  3u
#define GPIO_PUSH_PULL    0u
#define GPIO_OPEN_DRAIN   1u
#define GPIO_NOPULL       0u
#define GPIO_PULLUP       1u
#define GPIO_PULLDOWN     2u

void delay_loop(uint32_t count);
void gpio_init_pins(GPIO_TypeDef *port, uint16_t pins, uint32_t mode, uint32_t speed,
                    uint32_t otype, uint32_t pupd);

static inline void gpio_set(GPIO_TypeDef *port, unsigned pin)
{
    port->BSRR = 1u << pin;
}

static inline void gpio_reset(GPIO_TypeDef *port, unsigned pin)
{
    port->BSRR = 1u << (pin + 16);
}

static inline int gpio_read(GPIO_TypeDef *port, unsigned pin)
{
    return (port->IDR & (1u << pin)) != 0;
}

#endif /* BOARD_IO_H */
