/*
 * Low-level board I/O helpers for the application.
 */
#include "board_io.h"

/*
 * orig: 0x0800ECF6
 * Busy-wait used for bit-banged bus timing. The nop keeps the loop from being
 * optimised away and gives it the same body as the original
 * (nop, add, compare, branch).
 */
void delay_loop(uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        __asm volatile("nop");
    }
}

/*
 * orig: 0x0801BD48 (GPIO_Init as built into the application)
 * Unlike the bootloader's copy, each field is updated in a single write.
 */
void gpio_init_pins(GPIO_TypeDef *port, uint16_t pins, uint32_t mode, uint32_t speed,
                    uint32_t otype, uint32_t pupd)
{
    for (unsigned pin = 0; pin < 16; pin++) {
        uint32_t bit = 1u << pin;
        if ((pins & bit) == 0) {
            continue;
        }
        unsigned shift = pin * 2;

        port->MODER = (port->MODER & ~(3u << shift)) | (mode << shift);
        if (mode == GPIO_MODE_OUT || mode == GPIO_MODE_AF) {
            port->OSPEEDR = (port->OSPEEDR & ~(3u << shift)) | (speed << shift);
            port->OTYPER = (uint16_t)((port->OTYPER & ~bit) | (otype << pin));
        }
        port->PUPDR = (port->PUPDR & ~(3u << shift)) | (pupd << shift);
    }
}
