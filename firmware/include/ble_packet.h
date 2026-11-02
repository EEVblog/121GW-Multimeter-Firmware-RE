/*
 * BLE reading packet.
 */
#ifndef BLE_PACKET_H
#define BLE_PACKET_H

#include <stdint.h>
#include "app_globals.h"

extern uint8_t g_ble_packet[19];          /* 0x20007AE0 */
extern uint8_t g_ble_checksum;            /* 0x20008032 */
extern uint16_t g_ble_history_index;      /* 0x20007FB0  0..999 */
extern int32_t g_ble_history_main[1000];  /* 0x20003950 */
extern int32_t g_ble_history_a[1000];     /* 0x200048F0  first dual reading */
extern int32_t g_ble_history_b[1000];     /* 0x20005890  second dual reading */
extern uint8_t g_flag_8047;               /* 0x20008047  frequency overload */
extern uint8_t g_flag_8048;               /* 0x20008048  main reading overload */

void ble_put_value(uint8_t pos, int32_t value);
void ble_serial_fields(void);
void ble_field_temperature(uint8_t pos);
void ble_field_battery(uint8_t pos);
void ble_field_apo(uint8_t pos);
void ble_field_contrast(uint8_t pos);
void ble_field_year(uint8_t pos);
void ble_field_date(uint8_t pos);
void ble_field_time(uint8_t pos);
void ble_field_none(uint8_t pos);
void ble_field_bd(uint8_t pos, uint8_t on);
void ble_field_frequency(uint8_t pos, uint8_t dp, uint8_t overload, int32_t value);
void ble_field_dual_a(uint8_t pos, uint8_t dp, uint8_t record);
void ble_field_dual_b(uint8_t pos, uint8_t dp, uint8_t record);
void ble_field_burden(void);
void ble_field_mode12(uint8_t pos);
void ble_field_mode10(uint8_t pos);
void ble_field_mode11(uint8_t pos);
void ble_build_packet(void);
void ble_send_reading(void);

#endif /* BLE_PACKET_H */
