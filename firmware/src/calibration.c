/*
 * Calibration records in data EEPROM.
 */
#include "calibration.h"
#include "eeprom.h"
#include "hy3131_bus.h"
#include "hy3131_ctrl.h"
#include "hy3131_tables.h"
#include "measure_scale.h"
#include "numeric.h"

#define CAL_RECORD_SIZE    16u
#define CAL_ADDR_FUNC6     0x0104u
#define CAL_ADDR_178       0x0178u
#define CAL_ADDR_AC_PEAK   0x02D0u
#define CAL_ADDR_2E0       0x02E0u

/* Record base for a function (25-entry little-endian u16 table in flash). */
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

/* orig: 0x08011DCE - functions 13..15 and 22..24 */
void cal_store_dual(uint8_t index)
{
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
