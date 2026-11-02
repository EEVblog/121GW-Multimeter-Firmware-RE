/*
 * HY3131 measurement control: per-function register set-up, interrupt
 * enables, frequency-counter restart, and the interrupt service routine.
 *
 * Function index (g_function) and range (g_range) select a 20-byte image of
 * registers 0x20..0x33 from hy3131_tables.c.
 */
#ifndef HY3131_CTRL_H
#define HY3131_CTRL_H

#include <stdint.h>

/* HY3131 registers */
#define HY_REG_INTF  0x1Eu
#define HY_REG_INTE  0x1Fu
#define HY_REG_R20   0x20u
#define HY_REG_CTA   0x1Bu
#define HY_REG_R29   0x29u

/* INTE bits */
#define HY_IRQ_CT    0x01u   /* frequency counter */
#define HY_IRQ_AD1   0x04u   /* high-resolution ADC */
#define HY_IRQ_RMS   0x10u   /* RMS converter */

extern uint32_t g_counter_preset;      /* 0x2000001C  default 0xE000 */
extern uint8_t g_hy_scratch;           /* 0x20007DC4 */
extern uint8_t g_flag_7fe0;            /* 0x20007FE0 */
extern uint8_t g_function;             /* 0x2000803C  current measurement function index */
extern uint8_t g_range;                /* 0x2000803D  current range within the function */
extern uint8_t g_hy_r20;               /* 0x2000807D  last value of HY3131 R20 */
extern uint8_t g_hy_r29;               /* 0x2000807E  last value of HY3131 R29 */
extern uint8_t g_hy_irq_pending;       /* 0x2000807F  INTF & INTE at the last service */
extern uint8_t g_flag_8098;            /* 0x20008098 */
extern uint8_t g_flag_80b7;            /* 0x200080B7 */
extern uint8_t g_flag_80ba;            /* 0x200080BA */
extern uint8_t g_flag_80bd;            /* 0x200080BD */
extern uint8_t g_flag_80c4;            /* 0x200080C4 */
extern uint8_t g_sel_80c6;             /* 0x200080C6  row select when g_flag_80c4 == 0 */
extern uint8_t g_sel_80c7;             /* 0x200080C7  row select when g_flag_80c4 != 0 */

void hy3131_write_mode_registers(uint8_t function, uint8_t range);
void hy3131_set_irq_enables(uint8_t function);
void hy3131_restart_counter(void);
void hy3131_configure_mode(uint8_t function, uint8_t range);
void hy3131_service(void);
void hy3131_poll(void);

#endif /* HY3131_CTRL_H */
