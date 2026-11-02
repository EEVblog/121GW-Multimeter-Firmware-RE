/*
 * Commands received from the BLE module, and the setup-edit time-out.
 */
#ifndef BLE_COMMAND_H
#define BLE_COMMAND_H

#include <stdint.h>
#include "app_globals.h"
#include "stm32l1xx.h"

extern uint8_t g_ble_rx[20];          /* 0x20007B80  bytes received */
extern uint8_t g_ble_cmd[20];         /* 0x20007B94  command being decoded */
extern uint8_t g_ble_rx_len;          /* 0x2000802F */
extern uint8_t g_ble_rx_ready;        /* 0x20008030 */
extern uint8_t g_clock_set_by_ble;    /* 0x20008033 */
extern uint8_t g_flag_8034;           /* 0x20008034  cleared on a bad clock checksum */
extern uint8_t g_edit_idle_ticks;     /* 0x20008070 */

void usart_send_byte(USART_TypeDef *usart, uint8_t byte);
void ble_echo_command(void);
uint8_t ble_hex_byte(uint8_t pos);
void ble_handle_command(void);
void setting_edit_timeout(void);

void usart_enable(USART_TypeDef *usart, uint8_t enable);

extern uint8_t g_ble_tx_index;        /* 0x20008031  packet byte being sent */
extern uint8_t g_ble_rx_index;        /* 0x2000802D */
extern uint8_t g_ble_rx_expected;     /* 0x2000802E  command length */

void USART2_IRQHandler(void);

#endif /* BLE_COMMAND_H */
