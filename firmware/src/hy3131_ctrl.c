/*
 * HY3131 measurement control.
 */
#include "hy3131_ctrl.h"
#include "measure_peak.h"
#include "measure_counter.h"
#include "hy3131_bus.h"
#include "hy3131_tables.h"
#include "board_io.h"
#include "system_init.h"
#include "measure_rms.h"
#include "measure_ad1.h"
#include "orig_calls.h"

#define HY_CFG_REGS        20u     /* registers 0x20..0x33 */
#define HY_SERVICE_REGS    0x34u   /* registers 0x00..0x33 */
#define R20_COUNTER_ON     0x02u
#define R20_COUNTER_MASK   0xF9u
#define R20_COUNTER_MASK_FREQ 0xFDu
#define R29_PEAK_HOLD      0x04u
#define COUNTER_STOP_DELAY 10000u

static const uint8_t *row(const uint8_t *table, uint8_t index)
{
    return table + (uint32_t)index * HY3131_CFG_ROW;
}

/* Row 0 or 2 depending on g_sel_80c7 (functions 15 and 24). */
static uint8_t sel_80c7_0_or_2(void)
{
    return g_sel_80c7 != 0 ? 2u : 0u;
}

/*
 * Picks the register image for a function/range. Returns NULL for function
 * indices above 0x18, for which the original writes an uninitialised buffer.
 */
static const uint8_t *mode_image(uint8_t f, uint8_t range)
{
    switch (f) {
    case 0:
        return g_flag_8098 ? HY3131_CFG_08028A04 : HY3131_CFG_080288DC;
    case 1:
        return row(HY3131_CFG_080288A0, range);
    case 2:
        if (g_flag_80ba == 0) {
            if (g_flag_80b9 == 0 && g_flag_80b7 != 0) {
                return row(HY3131_CFG_08028904, range);
            }
            return row(HY3131_CFG_080289C8, range);
        }
        return row(g_flag_80bd == 0 ? HY3131_CFG_080289C8 : HY3131_CFG_080288A0, range);
    case 3:
        return row(HY3131_CFG_08028F1C, range);
    case 4:
        if (g_flag_80b9 == 0 && g_flag_80b7 != 0) {
            return row(HY3131_CFG_08028F6C, range);
        }
        return row(HY3131_CFG_08028F44, range);
    case 5:
        return HY3131_CFG_08028F1C;
    case 6: case 7: case 8:
        return HY3131_CFG_08028FBC;
    case 9:
        return row(HY3131_CFG_080285BC, range);
    case 10:
        return HY3131_CFG_08029150;
    case 11:
        return row(HY3131_CFG_08028F94, range);
    case 12:
        return row(g_pf8_detected_low == 0 ? HY3131_CFG_08028748 : HY3131_CFG_080286D0, range);
    case 13: case 14:
        return g_flag_80c4 == 0 ? row(HY3131_CFG_080289C8, g_sel_80c6)
                                : row(HY3131_CFG_08028D1C, g_sel_80c7);
    case 15:
        return g_flag_80c4 == 0 ? row(HY3131_CFG_080289C8, g_sel_80c6)
                                : row(HY3131_CFG_08028D1C, sel_80c7_0_or_2());
    case 16: case 18: case 20:
        return row(HY3131_CFG_08028D1C, range);
    case 17: case 19: case 21:
        return row(HY3131_CFG_08028D94, range);
    case 22: case 23:
        return g_flag_80c4 == 0 ? row(HY3131_CFG_080288A0, g_sel_80c6)
                                : row(HY3131_CFG_08028D58, g_sel_80c7);
    case 24:
        return g_flag_80c4 == 0 ? row(HY3131_CFG_080288A0, g_sel_80c6)
                                : row(HY3131_CFG_08028D58, sel_80c7_0_or_2());
    default:
        return 0;
    }
}

/* orig: 0x0800B994 - writes registers 0x20..0x33 for a function/range */
void hy3131_write_mode_registers(uint8_t function, uint8_t range)
{
    uint8_t regs[HY_CFG_REGS];
    const uint8_t *image = mode_image(function, range);

    for (unsigned i = 0; i < HY_CFG_REGS; i++) {
        regs[i] = image ? image[i] : 0u;   /* original: uninitialised for invalid functions */
    }
    if (function == 0 && g_flag_8098) {
        regs[10] |= 0x20;                         /* R2A */
        regs[14] = (uint8_t)((regs[14] & 0x7F) | 0x20);  /* R2E */
    }
    if (function == 9 && range == 6 && g_flag_7fe0 != 0) {
        regs[10] &= 0xF7;                         /* R2A */
    }
    g_hy_r20 = regs[0];
    hy3131_write(regs, HY_REG_R20, HY_CFG_REGS);
}

/* orig: 0x0800BEAC - clears INTF and sets INTE for a function */
void hy3131_set_irq_enables(uint8_t function)
{
    uint8_t regs[2];

    regs[0] = 0;   /* INTF */
    switch (function) {
    case 0:
        regs[1] = g_flag_8098 ? (HY_IRQ_AD1 | HY_IRQ_CT) : HY_IRQ_AD1;
        break;
    case 2:
        if (g_flag_80ba != 0 && g_flag_80bd != 0) {
            regs[1] = HY_IRQ_AD1;
            break;
        }
        /* fall through */
    case 4:
        regs[1] = (g_flag_80b7 == 1) ? (HY_IRQ_RMS | HY_IRQ_CT) : (HY_IRQ_AD1 | HY_IRQ_CT);
        break;
    case 6: case 7: case 8: case 12:
        regs[1] = HY_IRQ_CT;
        break;
    case 13: case 14: case 15: case 16: case 18: case 20:
        regs[1] = HY_IRQ_AD1 | HY_IRQ_CT;
        break;
    default:
        /* 1, 3, 5, 9, 10, 11, 17, 19, 21..24. The original leaves INTE
         * uninitialised for indices above 24; AD1 is used here. */
        regs[1] = HY_IRQ_AD1;
        break;
    }
    hy3131_write(regs, HY_REG_INTF, 2);
}

/*
 * orig: 0x0800BF8C
 * Stops the counter (R20), presets CTA from g_counter_preset, reads it
 * back, and restarts the counter.
 */
void hy3131_restart_counter(void)
{
    uint8_t buf[3];

    if (g_function == 6 || g_function == 7 || g_function == 8) {
        buf[0] = g_hy_r20 & R20_COUNTER_MASK_FREQ;
    } else {
        buf[0] = g_hy_r20 & R20_COUNTER_MASK;
    }
    hy3131_write(buf, HY_REG_R20, 1);
    delay_loop(COUNTER_STOP_DELAY);

    buf[0] = 0;
    buf[1] = (uint8_t)g_counter_preset;
    buf[2] = (uint8_t)(g_counter_preset >> 8);
    hy3131_write(buf, HY_REG_CTA, 3);
    hy3131_read(hy3131_rx_buffer, HY_REG_CTA, 3);

    buf[0] = g_hy_r20 | R20_COUNTER_ON;
    hy3131_write(buf, HY_REG_R20, 1);
}

/* orig: 0x0800B974 */
void hy3131_configure_mode(uint8_t function, uint8_t range)
{
    hy3131_write_mode_registers(function, range);
    hy3131_set_irq_enables(function);
    hy3131_restart_counter();
}

/*
 * orig: 0x0800614E
 * Reads registers 0x00..0x33, clears INTF and dispatches the results.
 */
void hy3131_service(void)
{
    hy3131_read(hy3131_rx_buffer, 0x00, HY_SERVICE_REGS);
    g_hy_scratch = 0;
    hy3131_write(&g_hy_scratch, HY_REG_INTF, 1);

    g_hy_irq_pending = hy3131_rx_buffer[HY_REG_INTF] & hy3131_rx_buffer[HY_REG_INTE];
    g_hy_r20 = hy3131_rx_buffer[HY_REG_R20];
    g_hy_r29 = hy3131_rx_buffer[HY_REG_R29];

    if (g_hy_irq_pending & HY_IRQ_AD1) {
        hy3131_on_ad1_ready();
    }
    if (g_hy_irq_pending & HY_IRQ_RMS) {
        hy3131_on_rms_ready();
    }
    if ((g_hy_r29 & R29_PEAK_HOLD) && g_flag_80ba == 0) {
        hy3131_on_peak_hold();
    }
    if (g_hy_irq_pending & HY_IRQ_CT) {
        hy3131_on_counter_ready();
    }
}

/* orig: 0x08006124 - called from the main loop */
void hy3131_poll(void)
{
    if (gpio_read(HY3131_PORT, HY3131_DO)) {
        hy3131_service();
    }
    if (g_function == 12 && g_range < 5) {
        func12_timeout_poll();
    }
}
