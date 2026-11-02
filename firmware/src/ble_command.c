/*
 * Commands received from the BLE module (USART2) and the setup-edit time-out.
 *
 * A received command (g_ble_rx, g_ble_rx_len bytes) is copied to g_ble_cmd
 * and decoded:
 *   0xF8 + 7 ASCII-hex bytes  set the clock (registers 2, 4, 6, 10, 12, 14),
 *                             the 7th byte is the XOR of the other six; only
 *                             the first such command after start-up is applied
 *   0xF4 + 2 ASCII-hex bytes  the same key code twice: inject a key event
 * Accepted commands are echoed back.
 */
#include "ble_command.h"
#include "display_state.h"
#include "display_text.h"
#include "display_update.h"
#include "display_symbols.h"
#include "rtc.h"
#include "settings.h"
#include "measure_funcs.h"
#include "hy3131_ctrl.h"
#include "stm32l1xx.h"

#define CMD_SET_CLOCK  0xF8u
#define CMD_KEY        0xF4u
#define USART_TXE_FLAG   0x80u
#define EDIT_TIMEOUT   240u

/*
 * orig: 0x0800D170 - wait for the transmitter, then send one byte
 * inlines: 0x0801CC5E (USART_GetFlagStatus), 0x0801CBF8 (USART_SendData)
 */
void usart_send_byte(USART_TypeDef *usart, uint8_t byte)
{
    while ((usart->SR & USART_TXE_FLAG) == 0) {
    }
    usart->DR = byte & 0x1FFu;
}

/* orig: 0x0800C308 */
void ble_echo_command(void)
{
    for (uint8_t i = 0; i < g_ble_rx_len; i++) {
        usart_send_byte(USART2, g_ble_cmd[i]);
    }
}

static uint8_t hex_digit(uint8_t c)
{
    if (c >= '0' && c <= '9') {
        return (uint8_t)(c - '0');
    }
    if (c > 0x40 && c < 0x47) {   /* 'A'..'F' */
        return (uint8_t)(c - 0x37);
    }
    return 0;
}

/* orig: 0x0800C470 - two ASCII-hex characters at g_ble_cmd[pos] as a byte (upper case only) */
uint8_t ble_hex_byte(uint8_t pos)
{
    return (uint8_t)((hex_digit(g_ble_cmd[pos]) << 4) + hex_digit(g_ble_cmd[pos + 1]));
}

/* orig: 0x0800C332 */
void ble_handle_command(void)
{
    g_ble_rx_ready = 0;
    for (uint8_t i = 0; i < g_ble_rx_len; i++) {
        g_ble_cmd[i] = g_ble_rx[i];
    }
    if (g_ble_cmd[0] == CMD_SET_CLOCK) {
        uint8_t v[6];
        uint8_t check = 0;

        for (unsigned i = 0; i < 6; i++) {
            v[i] = ble_hex_byte((uint8_t)(1 + 2 * i));
            check ^= v[i];
        }
        if (check == ble_hex_byte(13)) {
            ble_echo_command();
            if (g_clock_set_by_ble == 0) {
                g_rtc_set_reg2 = v[0];
                g_rtc_set_reg4 = v[1];
                g_rtc_set_reg6 = v[2];
                g_rtc_set_reg10 = v[3];
                g_rtc_set_reg12 = v[4];
                g_rtc_set_reg14 = v[5];
                rtc_write_datetime();
                g_clock_set_by_ble = 1;
            }
        } else {
            g_flag_8034 = 0;
        }
    } else if (g_ble_cmd[0] == CMD_KEY) {
        uint8_t a = ble_hex_byte(1);
        uint8_t b = ble_hex_byte(3);

        if (a == b) {
            ble_echo_command();
            g_input_event_pending = 1;
            g_input_event_code = a;
        }
    }
}

/*
 * orig: 0x0800D190 - after 240 calls without a key, leave setting editing:
 * restore the edited value from its source and redraw the secondary readout.
 */
void setting_edit_timeout(void)
{
    g_edit_idle_ticks++;
    if (g_edit_idle_ticks <= EDIT_TIMEOUT) {
        return;
    }
    g_flag_806e = 0;
    if (g_mode_8037 == 8) {
        g_edit_value = g_serial_number;
    } else if (g_mode_8037 == 5) {
        g_rtc_edit_dd = g_rtc_set_reg2;
    } else if (g_mode_8037 == 6) {
        g_rtc_edit_de = g_rtc_set_reg4;
        g_rtc_edit_df = g_rtc_set_reg6;
        g_flag_80d6 = 0;
    } else if (g_mode_8037 == 7) {
        g_rtc_edit_e0 = g_rtc_set_reg10;
        g_rtc_edit_e1 = g_rtc_set_reg12;
        g_flag_80d6 = 0;
    } else if (g_mode_8037 == 2) {
        settings_load_apo();
    } else if (g_mode_8037 == 3) {
        settings_load_beep();
    } else if (g_mode_8037 == 0) {
        settings_load_temp_unit();
    } else if (g_function == 10 && g_mode_8037 == 10) {
        settings_load_sel_80b6();
    }
    g_disp_flag_8062 = 1;
}
