/*
 * Calibration records in data EEPROM.
 */
#include "calibration.h"
#include "measure_funcs.h"
#include "eeprom.h"
#include "hy3131_bus.h"
#include "hy3131_ctrl.h"
#include "board_io.h"
#include "buzzer.h"
#include "measure_derived.h"
#include "range_select.h"
#include "hy3131_tables.h"
#include "measure_scale.h"
#include "numeric.h"

#define CAL_RECORD_SIZE    16u
#define CAL_ADDR_FUNC6     0x0104u
#define CAL_ADDR_178       0x0178u
#define CAL_ADDR_AC_PEAK   0x02D0u
#define CAL_ADDR_2E0       0x02E0u

/* Record base for a function (26-entry little-endian u16 table in flash). */
static uint32_t cal_base(uint8_t function)
{
    const uint8_t *t = HY3131_CFG_08028EE8 + (uint32_t)function * 2;
    return (uint32_t)t[0] | ((uint32_t)t[1] << 8);
}

static int ac_peak_variant(void)
{
    return (g_function == 2 || g_function == 4) && g_flag_80b7 == 1;
}

/* orig: 0x0801159C - 16-bit result; the argument is unused */
uint32_t cal_address_for_range(uint32_t unused)
{
    (void)unused;
    return (uint16_t)(cal_base(g_function) + (uint32_t)g_range * CAL_RECORD_SIZE);
}

/* orig: 0x08011924 */
void cal_clamp_e14(void)
{
    if (g_cal_e14 > 3000 || g_cal_e14 < -3000) {
        g_cal_e14 = 0;
    }
}

#if APP_FIX_BUGS
/*
 * A gain the record can hold: positive and below the int32 limit once
 * scaled. A botched gain step (wrong input, reversed leads) gives zero or a
 * negative gain; stored, a negative one reloads as a huge positive gain
 * (the record is read as unsigned). Such a gain is refused with a triple
 * beep and the old record kept. The caller has already put the refused gain
 * in the working gain (gain_select_for_range()), so the stored record is
 * loaded again and readings go on with it.
 */
static int cal_gain_storable(int32_t scaled)
{
    if (scaled <= 0 || scaled == INT32_MAX) {
        beep_triple();
        cal_load(g_function, g_range);
        return 0;
    }
    return 1;
}
#endif

/* orig: 0x08011DCE - functions 13..15 and 22..24 */
void cal_store_dual(uint8_t index)
{
#if APP_FIX_BUGS
    if (index == CAL_INDEX_GAIN
        && (!cal_gain_storable(iar_f2i(1000000.0f * g_gain_e2c))
            || !cal_gain_storable(iar_f2i(1000000.0f * g_gain_e30)))) {
        return;
    }
#endif
    if (index == CAL_INDEX_OFFSET) {
        g_cal_address = cal_address_for_range(0);
        eeprom_write_u32_be(g_cal_address, (uint32_t)g_cal_dual_offset_a);
        g_cal_address = cal_address_for_range(1) + 8;
        eeprom_write_u32_be(g_cal_address, (uint32_t)g_cal_dual_offset_b);
    } else if (index == CAL_INDEX_GAIN) {
        g_cal_address = cal_address_for_range(0) + 4;
        eeprom_write_u32_be(g_cal_address, (uint32_t)iar_f2i(1000000.0f * g_gain_e2c));
        g_cal_address = cal_address_for_range(1) + 12;
        eeprom_write_u32_be(g_cal_address, (uint32_t)iar_f2i(1000000.0f * g_gain_e30));
    }
}

/* orig: 0x08011B40 */
void cal_store_value(uint8_t index, int32_t value)
{
    uint32_t record = cal_base(g_function) + (uint32_t)g_range * CAL_RECORD_SIZE;

    if (index == CAL_INDEX_REC_2E0) {
        g_cal_address = CAL_ADDR_2E0;
        if (g_cal_fbe > 1000 || g_cal_fbe < -1000) {
            g_cal_fbe = 0;
        }
        eeprom_write_u16_be(CAL_ADDR_2E0, (uint16_t)g_cal_fbe);
        return;
    }
    if (index == CAL_INDEX_REC_178) {
        cal_clamp_e14();
        eeprom_write_u32_be(CAL_ADDR_178, (uint32_t)g_cal_e14);
        return;
    }
    switch (g_function) {
    case 13: case 14: case 15: case 22: case 23: case 24:
        cal_store_dual(index);
        return;
    default:
        break;
    }

    switch (index) {
    case CAL_INDEX_OFFSET:
        g_cal_address = ac_peak_variant() ? CAL_ADDR_AC_PEAK : record;
        eeprom_write_u32_be(g_cal_address, (uint32_t)value);
        break;
    case CAL_INDEX_GAIN:
        if (g_function == 6) {
            g_cal_address = CAL_ADDR_FUNC6;
            eeprom_write_u32_be(CAL_ADDR_FUNC6, (uint32_t)g_cal_func6_gain);
            break;
        }
        g_cal_address = ac_peak_variant() ? CAL_ADDR_AC_PEAK + 4 : record + 4;
        {
            float scale = 1000000.0f;
            if (g_function == 12) {
                scale = (g_range == 4 && g_pf8_detected_low != 0) ? 10000.0f : 100000.0f;
            }
#if APP_FIX_BUGS
            if (!cal_gain_storable(iar_f2i(scale * g_gain))) {
                break;
            }
#endif
            eeprom_write_u32_be(g_cal_address, (uint32_t)iar_f2i(scale * g_gain));
        }
        break;
    case CAL_INDEX_OFFSET2:
        g_cal_address = record + 8;
        eeprom_write_u32_be(g_cal_address, (uint32_t)value);
        break;
    case CAL_INDEX_SCALED:
        g_cal_address = record + 12;
        /* int * 10000 (wrapping), rounded through float, as the original */
        eeprom_write_u32_be(g_cal_address,
                            (uint32_t)iar_f2i((float)(int32_t)((uint32_t)value * 10000u)));
        break;
    default:
        break;
    }
}

/*
 * orig: 0x0800F7B8
 * Accumulates 16 samples (shares the counter and sum with average16()).
 * When complete:
 *   zero offset = reference - (mean - reference) / 9
 *   function 12: gain = (g_cal_ref_e68 - offset) / 10000
 *   otherwise:   span = mean - offset, gain = span / divisor for the range
 * and both are stored in EEPROM. Calibrating function 2 range 0 or function
 * 9 range 6 also sets an EEPROM valid marker.
 */
void calibrate_zero_and_gain(int32_t sample, int32_t reference)
{
    int32_t sum = sample;

    if (g_avg_count != 16) {
        sum = sample + g_avg_sum;
    }
    g_avg_sum = sum;
    g_avg_count--;

    if (g_avg_count == 0) {
        g_cal_flag_808e = 0;
        g_cal_done_808f = 1;
        int32_t mean = g_avg_sum / 16;
        g_zero_offset = reference - (mean - reference) / 9;
        cal_store_value(CAL_INDEX_OFFSET, g_zero_offset);

        if (g_function == 12) {
            g_gain = (float)(g_cal_ref_e68 - g_zero_offset) / 10000.0f;
        } else {
            g_span = (float)(mean - g_zero_offset);
            gain_select_for_range(0);
        }
        cal_store_value(CAL_INDEX_GAIN, iar_f2i(g_gain));   /* value unused for the gain */

        if (g_function == 2 && g_range == 0) {
            eeprom_set_marker_a();
        }
        if (g_function == 9 && g_range == 6) {
            eeprom_set_marker_b();
            g_flag_7fe0 = 1;
        }
    }
    g_new_reading = 1;
}

/* Samples averaged for calibration: 16, or 8/4 for function 9 ranges 5/6. */
static int32_t cal_sample_count(void)
{
    if (g_function == 9 && g_range == 5) {
        return 8;
    }
    if (g_function == 9 && g_range == 6) {
        return 4;
    }
    return 16;
}

/*
 * orig: 0x0800EF44
 * Zero calibration: averages the samples into the zero offset and stores it.
 * For function 9 range 5 the first 8 samples are discarded. Returns 0.
 */
int32_t cal_zero_stage(int32_t sample)
{
    int32_t sum = sample;

    if (g_avg_count != 16) {
        if (g_function == 9 && g_range == 5 && g_avg_count >= 9) {
            sum = 0;
            g_avg_sum = 0;
        } else {
            sum = sample + g_avg_sum;
        }
    }
    g_avg_sum = sum;
    g_avg_count--;
    if (g_avg_count == 0) {
        g_avg_flag_808c = 0;
        g_avg_done_808d = 1;
        g_new_reading = 1;
        g_zero_offset = g_avg_sum / cal_sample_count();
        cal_store_value(CAL_INDEX_OFFSET, g_zero_offset);
    }
    return 0;
}

static void cal_accumulate64(int32_t sample, int discard_early)
{
    if (discard_early && g_avg_count >= 9) {
        g_cal_sum64 = 0;
    } else {
        g_cal_sum64 += sample;
    }
}

/*
 * orig: 0x0800F470
 * Gain calibration. While samples are collected the displayed value is
 * raw / 60.80732; when complete the span gives the gain, which is stored,
 * and the value is raw / gain. With g_flag_8090 set it instead calibrates
 * the index-150 record from the mean of the last 8 samples.
 */
int32_t cal_gain_stage(int32_t sample)
{
    if (g_flag_8090 == 0) {
        cal_accumulate64(sample, g_function == 9 && g_range >= 5);
        g_avg_count--;
        if (g_avg_count != 0) {
            return iar_d2i((double)(float)sample / 60.80732);
        }
        g_cal_flag_808e = 0;
        g_cal_done_808f = 1;
        g_new_reading = 1;
        g_span = (float)(int32_t)(g_cal_sum64 / cal_sample_count());
        gain_select_for_range(0);
        cal_store_value(CAL_INDEX_GAIN, iar_f2i(g_gain));
        return iar_f2i((float)sample / g_gain);
    }

    cal_accumulate64(sample, 1);
    g_avg_count--;
    if (g_avg_count == 0) {
        g_flag_8090 = 0;
        g_cal_e14 = iar_f2i((float)(int32_t)(g_cal_sum64 / 8) / g_gain) - 30000;
        cal_store_value(CAL_INDEX_REC_178, g_cal_e14);
        g_setting_238 = 5;
    }
    return 0;
}

/* ---- loading records ---- */

#define CAL_FUNC6_GAIN_MIN     40000000
#define CAL_FUNC6_GAIN_MAX     60000000
#define CAL_FUNC6_GAIN_DEFAULT 49152050
#define CAL_TRIM_OFFSET        8u      /* trims live at record + 8 */
#define CAL_FUNC_AC_PEAK_REC   25u     /* cal_base entry for the AC-peak record */

static int dual_gain_function(uint8_t function)
{
    return function == 13 || function == 14 || function == 15
        || function == 22 || function == 23 || function == 24;
}

/*
 * orig: 0x080115CC
 * Address of the four 16-bit trims of a function/range record. Functions
 * 16 and 18 always use range 1.
 */
uint16_t cal_trim_address(uint8_t function, uint8_t range)
{
    if (function == 16 || function == 18) {
        range = 1;
    }
    return (uint16_t)((uint16_t)(cal_base(function) + CAL_TRIM_OFFSET) + range * 16u);
}

/*
 * orig: 0x0801154E
 * Address of the extra offset record for functions 16..21; any other
 * argument is returned unchanged (low 16 bits).
 */
uint16_t cal_extra_address(uint32_t function)
{
    switch ((uint8_t)function) {
    case 16: return 0x4D0;
    case 17: return 0x4D8;
    case 18: return 0x4E0;
    case 19: return 0x4E8;
    case 20: return 0x4F0;
    case 21: return 0x4F8;
    default: return (uint16_t)function;
    }
}

/*
 * orig: 0x08011448
 * Functions 16..21, range 0: loads the extra offset when its record
 * carries the valid marker, otherwise 0.
 */
void cal_load_extra(uint8_t function, uint8_t range)
{
    g_cal_extra_offset = 0;
    if (function >= 16 && function <= 21 && range == 0) {
        uint16_t address = cal_extra_address(function);
        if (eeprom_read_byte(address) == EEPROM_VALID_MARKER) {
            g_cal_extra_offset = (int32_t)eeprom_read_u32_be((uint16_t)(address + 1));
        }
    }
}

/* orig: 0x08011950 - both offset/gain pairs of a dual-gain function */
void cal_load_dual(void)
{
    g_cal_address = cal_address_for_range(0);
    g_cal_dual_offset_a = (int32_t)eeprom_read_u32_be(g_cal_address);
    g_gain_e2c = (float)eeprom_read_u32_be(g_cal_address + 4);
    g_gain_e2c = g_gain_e2c / 1000000.0f;
    g_cal_address = cal_address_for_range(1);
    g_cal_address = g_cal_address + 8;
    g_cal_dual_offset_b = (int32_t)eeprom_read_u32_be(g_cal_address);
    g_gain_e30 = (float)eeprom_read_u32_be(g_cal_address + 4);
    g_gain_e30 = g_gain_e30 / 1000000.0f;
}

/* orig: 0x080119F4 - trims 0 and 1 are limited to +-20000, 2 and 3 to +-10000 */
void cal_trim_clamp(uint8_t index)
{
    int32_t limit;

    if (index > 3) {
        return;
    }
    limit = (index < 2) ? 20000 : 10000;
    if (g_cal_trim[index] > limit || g_cal_trim[index] < -limit) {
        g_cal_trim[index] = 0;
    }
}

/* orig: 0x08011AA4 - function 0 uses the function 2, range 3 record */
void cal_load_trims(void)
{
    if (g_function == 0) {
        g_cal_address = cal_trim_address(2, 3);
    } else {
        g_cal_address = cal_trim_address(g_function, g_range);
    }
    for (uint8_t i = 0; i < 4; i++) {
        g_cal_trim[i] = eeprom_read_s16_be(g_cal_address + 2u * i);
        cal_trim_clamp(i);
    }
}

/* orig: 0x08011E88 - which = 1..4 stores one trim, 100 stores all four */
void cal_store_trims(uint8_t function, uint8_t range, uint8_t which)
{
    g_cal_address = cal_trim_address(function, range);
    for (uint8_t i = 0; i < 4; i++) {
        if (which == i + 1 || which == 100) {
            cal_trim_clamp(i);
            eeprom_write_u16_be(g_cal_address + 2u * i, (uint16_t)g_cal_trim[i]);
        }
    }
}

/*
 * orig: 0x0801167C
 * Loads the calibration for a function/range into the working offset and
 * gain (or the special records for index 200, function 6 and the dual-gain
 * functions).
 */
void cal_load(uint8_t function, uint8_t range)
{
    if (function == CAL_INDEX_REC_2E0) {
        g_cal_address = CAL_ADDR_2E0;
        g_cal_fbe = (int16_t)eeprom_read_u16_be(g_cal_address);
        if (g_cal_fbe > 1000 || g_cal_fbe < -1000) {
            g_cal_fbe = 0;
        }
        return;
    }
    if (g_flag_808a == 0 && function == 9 && g_range == 6) {
        g_cal_e14 = (int32_t)eeprom_read_u32_be(CAL_ADDR_178);
        cal_clamp_e14();
    }
    if (dual_gain_function(g_function)) {
        cal_load_dual();
        return;
    }
    if (function == 6) {
        g_cal_address = cal_base(6);
        g_cal_func6_gain = (int32_t)eeprom_read_u32_be(g_cal_address + 4);
        if (g_cal_func6_gain > CAL_FUNC6_GAIN_MAX || g_cal_func6_gain < CAL_FUNC6_GAIN_MIN) {
            g_cal_func6_gain = CAL_FUNC6_GAIN_DEFAULT;
        }
        return;
    }

    if ((function == 2 || function == 4) && g_flag_80b7 == 1) {
        g_cal_address = cal_base(CAL_FUNC_AC_PEAK_REC);
    } else if (function == 0) {
        g_cal_address = cal_base(g_flag_8098 != 0 ? 2 : 1);
        g_cal_address = g_cal_address + 0x30;
    } else if (function == 2) {
        g_cal_address = cal_base((g_flag_80ba != 0 && g_flag_80bd != 0) ? 1 : 2);
        g_cal_address = g_cal_address + range * 16u;
    } else if (function == 10) {
        g_cal_address = cal_base(9);
        g_cal_address = g_cal_address + 0x10;
    } else {
        g_cal_address = cal_base(function);
        g_cal_address = g_cal_address + range * 16u;
    }

    g_zero_offset = (int32_t)eeprom_read_u32_be(g_cal_address);
    g_gain = (float)eeprom_read_u32_be(g_cal_address + 4);
    if (function == 12) {
        if (g_range == 4 && g_pf8_detected_low != 0) {
            g_gain = g_gain / 10000.0f;
        } else {
            g_gain = g_gain / 100000.0f;
        }
    } else {
        g_gain = g_gain / 1000000.0f;
    }
    delay_loop(1);
}

/*
 * orig: 0x0800F8E4
 * Trim calibration: stores (measured - nominal) as trim `which` (1..4)
 * and writes it to EEPROM. Nominals are 5000 for trims 1 and 3 and 50000
 * for trims 2 and 4; 600 and 6000 for function 2 range 3. Other values
 * of `which` change nothing (100 rewrites all four).
 */
void cal_set_trim(uint8_t which, int32_t measured)
{
    int low = (g_function == 2 && g_range == 3);

    switch (which) {
    case 1:
    case 3:
        g_cal_trim[which - 1] = (int32_t)((uint32_t)measured - (low ? 600u : 5000u));
        break;
    case 2:
    case 4:
        g_cal_trim[which - 1] = (int32_t)((uint32_t)measured - (low ? 6000u : 50000u));
        break;
    default:
        break;
    }
    cal_store_trims(g_function, g_range, which);
}

/*
 * orig: 0x08007532
 * In calibration mode (functions 2, 4, 16, 18, 20, with g_flag_80a5 clear)
 * takes the current reading as each requested trim (flags 0x2000809C..9F),
 * with a short beep per trim.
 */
void cal_trim_requests(void)
{
    if (g_flag_80a5 != 0 || g_flag_808a == 0) {
        return;
    }
    if (!(g_function == 16 || g_function == 18 || g_function == 20
          || g_function == 2 || g_function == 4)) {
        return;
    }
    for (uint8_t i = 0; i < 4; i++) {
        if (g_cal_trim_request[i] != 0) {
            g_cal_trim_request[i] = 0;
            cal_set_trim(i + 1, g_derived_value);
            beep_short();
        }
    }
}

/* orig: 0x0800662C (sets 5 whatever g_flag_8098 is) */
void cal_setting_239_reset(void)
{
    g_setting_239 = 5;
}
