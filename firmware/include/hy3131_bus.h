/*
 * HY3131 serial interface (bit-banged on port F).
 *
 *   PF0 CK, PF1 CS (active low), PF2 DI (to HY3131), PF3 DO (from HY3131).
 *
 * A transfer starts with (register << 1) | R/W, MSB first. Data bytes follow
 * with the register address auto-incrementing. Reads need one extra clock
 * after the address byte. While CS is high, DO is the HY3131 interrupt
 * output (high = interrupt pending).
 */
#ifndef HY3131_BUS_H
#define HY3131_BUS_H

#include <stdint.h>

#define HY3131_PORT  GPIOF
#define HY3131_CK    0u
#define HY3131_CS    1u
#define HY3131_DI    2u
#define HY3131_DO    3u

/* Destination of every hy3131_read(): register contents from 'reg' upward. */
extern uint8_t hy3131_rx_buffer[];      /* 0x200077D0 */
/* Set at start-up when PF8 reads mostly low (role to be confirmed). */
extern uint8_t g_pf8_detected_low;      /* 0x20007FDF */

void hy3131_clock_pulse(void);
void hy3131_write_byte(uint8_t value, uint8_t turnaround);
uint8_t hy3131_read_byte(void);
void hy3131_write(const uint8_t *data, uint8_t reg, uint16_t count);
void hy3131_read(uint8_t *unused, uint8_t reg, uint16_t count);
void detect_pf8_low(void);

void hy3131_bus_idle(void);

#endif /* HY3131_BUS_H */
