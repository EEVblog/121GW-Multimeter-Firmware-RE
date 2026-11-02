/*
 * GPIO configuration helpers (bootloader).
 *
 * Each field is cleared and then set in two separate writes, in the same
 * order as the original image (MODER, then OSPEEDR/OTYPER for outputs and
 * alternate functions, then PUPDR), so the pin goes through the same states.
 */
#include "gpio.h"

/* orig: 0x08001CD0 */
void gpio_init_pins(GPIO_TypeDef *port, uint16_t pins, uint32_t mode, uint32_t speed,
                    uint32_t otype, uint32_t pupd)
{
    for (unsigned pin = 0; pin < 16; pin++) {
        uint32_t bit = 1u << pin;
        if ((pins & bit) == 0) {
            continue;
        }
        unsigned shift = pin * 2;

        port->MODER &= ~(3u << shift);
        port->MODER |= mode << shift;
        if (mode == GPIO_MODE_OUT || mode == GPIO_MODE_AF) {
            port->OSPEEDR &= ~(3u << shift);
            port->OSPEEDR |= speed << shift;
            port->OTYPER &= ~bit;
            port->OTYPER |= otype << pin;
        }
        port->PUPDR &= ~(3u << shift);
        port->PUPDR |= pupd << shift;
    }
}

/* orig: 0x08001D90 */
void gpio_set_af(GPIO_TypeDef *port, unsigned pin, uint32_t af)
{
    volatile uint32_t *afr = &port->AFR[pin >> 3];
    unsigned shift = (pin & 7) * 4;

    *afr &= ~(0xFu << shift);
    *afr |= af << shift;
}
