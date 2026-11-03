/*
 * Calibration records in data EEPROM.
 *
 * Each function/range has a 16-byte record at
 *   cal_base[function] + range * 16
 * holding big-endian int32 values:
 *   +0  zero offset
 *   +4  gain * 1e6 (function 12: * 1e5, or * 1e4 for range 4 when PF8 reads low)
 *   +8  second offset
 *   +12 value * 10000 (via float, as in the original)
 * Special records: 0x0104 (function 6 gain), 0x02D0/0x02D4 (functions 2/4
 * when g_flag_80b7 is set), 0x0178 (index 150), 0x02E0 (index 200, 16-bit).
 * Functions 13..15 and 22..24 store two gains/offsets per record.
 * Four signed 16-bit trims sit at record + 8 (functions 16/18: range 1;
 * function 0: function 2 range 3). Functions 16..21 have an extra offset at
 * 0x4D0 + 8 * (function - 16): a 0xA5 marker byte then a big-endian int32.
 */
#ifndef CALIBRATION_H
#define CALIBRATION_H

#include <stdint.h>

#define CAL_INDEX_OFFSET        0u
#define CAL_INDEX_GAIN          1u
#define CAL_INDEX_OFFSET2       2u
#define CAL_INDEX_SCALED        3u
#define CAL_INDEX_REC_178       150u
#define CAL_INDEX_REC_2E0       200u

extern uint32_t g_cal_address;      /* 0x20007D5C  last record address written */
extern int32_t g_cal_e14;           /* 0x20007E14  stored at 0x178, clamped to +-3000 */
extern int32_t g_cal_func6_gain;    /* 0x20007E18  stored at 0x104 */
extern int32_t g_cal_dual_offset_a; /* 0x20007E24 */
extern int32_t g_cal_dual_offset_b; /* 0x20007E28 */
extern int16_t g_cal_fbe;           /* 0x20007FBE  stored at 0x2E0, clamped to +-1000 */
extern int32_t g_cal_ref_e68;       /* 0x20007E68  reference reading used for function 12 */
extern uint8_t g_cal_flag_808e;     /* 0x2000808E */
extern uint8_t g_cal_done_808f;     /* 0x2000808F */
extern int64_t g_cal_sum64;         /* 0x20007C38  signed 64-bit sample sum */
extern uint8_t g_setting_238;       /* 0x20000238  readings still to skip after a function/range change (mode_238_select()); 5 after the index-150 calibration */
extern int32_t g_cal_extra_offset;  /* 0x20007E0C  functions 16..21 range 0, subtracted from the reading */
extern int32_t g_cal_trim[4];       /* 0x20007E4C  16-bit trims at record + 8 */
extern uint8_t g_flag_808a;         /* 0x2000808A */
extern uint8_t g_cal_trim_request[4]; /* 0x2000809C  set to capture trim 1..4 */

uint32_t cal_address_for_range(uint32_t unused);
void cal_clamp_e14(void);
void cal_store_dual(uint8_t index);
void cal_store_value(uint8_t index, int32_t value);
void calibrate_zero_and_gain(int32_t sample, int32_t reference);
int32_t cal_zero_stage(int32_t sample);
int32_t cal_gain_stage(int32_t sample);
uint16_t cal_trim_address(uint8_t function, uint8_t range);
uint16_t cal_extra_address(uint32_t function);
void cal_load_extra(uint8_t function, uint8_t range);
void cal_load_dual(void);
void cal_trim_clamp(uint8_t index);
void cal_load_trims(void);
void cal_store_trims(uint8_t function, uint8_t range, uint8_t which);
void cal_load(uint8_t function, uint8_t range);
void cal_set_trim(uint8_t which, int32_t measured);
void cal_trim_requests(void);

void cal_setting_239_reset(void);

#endif /* CALIBRATION_H */
