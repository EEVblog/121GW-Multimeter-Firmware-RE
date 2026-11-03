/*
 * Trim-based correction of AC readings (functions 0, 2, 4, 16, 18, 20).
 *
 * The four calibration trims of the current record give offsets at two
 * reference levels; frequency tables (percent of the trim at each
 * frequency point) scale them for the measured frequency, and the result
 * is interpolated in level. Tables are copied from the original image.
 */
#include "trim_correction.h"
#include "calibration.h"
#include "ac_response.h"
#include "hy3131_ctrl.h"
#include "measure_scale.h"
#include "numeric.h"
#include "range_select.h"
#include "system_init.h"

#define TRIM_FREQ_MIN     51
#define TRIM_HIGH_BAND    20001    /* selections 2/3: separate tables from here up */
#define TRIM_LOW_LEVEL    5001     /* below: the low-level row */
#define TRIM_FULL_SCALE   50000.0f

/* orig: 0x0802900C / 0x08029030 / 0x08029054 / 0x080290F0 - frequency points, Hz */
static const int32_t trim_freq_a[9] = { 50, 400, 1000, 10000, 30000, 50000, 70000, 90000, 100000 };     /* selections 0, 1 */
static const int32_t trim_freq_lo[9] = { 50, 400, 1000, 3000, 5000, 7000, 10000, 15000, 20000 };    /* selections 2, 3 up to 20 kHz */
static const int32_t trim_freq_hi[9] = { 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000 };    /* selections 2, 3 above 20 kHz */
static const int32_t trim_freq_b[7] = { 50, 400, 1000, 3000, 5000, 7000, 10000 };     /* selections 4..7 */

/* orig: 0x08028A18 - selections 0 and 1: [selection][level row][point], percent */
static const int32_t trim_pct_a[2][2][9] = {
    {
        { 0, 3, 0, -2, 1, 18, 47, 81, 100 },
        { 0, -5, -1, 7, 40, 70, 90, 98, 100 }
    },
    {
        { 0, 1, -1, -2, -1, 15, 45, 82, 100 },
        { 0, -2, -1, 3, 19, 40, 65, 88, 100 }
    },
};

/* orig: 0x08028E08 - selections 4..7: [selection - 4][level row][point], percent */
static const int32_t trim_pct_b[4][2][7] = {
    {
        { 0, 3, 10, 52, 77, 90, 100 },
        { 0, 2, 9, 52, 77, 90, 100 }
    },
    {
        { 0, 2, 9, 51, 77, 90, 100 },
        { 0, 2, 9, 51, 77, 90, 100 }
    },
    {
        { 0, -10, 3, 20, 35, 55, 100 },
        { 0, -20, -8, 2, 16, 43, 100 }
    },
    {
        { 0, -10, 3, 20, 40, 55, 100 },
        { 0, -15, -4, 7, 20, 47, 100 }
    },
};

/* orig: 0x08028AA8 - selections 2 and 3: [table][band][trim][point], percent.
 * Table 0: selection 2 with the flag set, 1: selection 2 without, 2: selection 3. */
static const int32_t trim_pct_ac[3][2][2][9] = {
    { {
        { 0, 1, 18, 80, 95, 105, 109, 105, 100 },
        { 0, -1, 14, 60, 83, 92, 97, 99, 100 }
      }, {
        { 0, 5, 13, 23, 36, 50, 66, 83, 100 },
        { 0, 4, 10, 18, 29, 43, 60, 78, 100 }
      } },
    { {
        { 0, -10, 14, 80, 110, 120, 125, 115, 100 },
        { 0, -3, 12, 60, 83, 92, 98, 100, 100 }
      }, {
        { 0, 5, 13, 23, 36, 50, 66, 83, 100 },
        { 0, 5, 12, 22, 33, 47, 63, 80, 100 }
      } },
    { {
        { 0, -5, 15, 90, 100, 110, 107, 105, 100 },
        { 0, -5, 10, 57, 80, 89, 96, 99, 100 }
      }, {
        { 0, 5, 13, 23, 36, 50, 66, 83, 100 },
        { 0, 4, 8, 16, 27, 41, 58, 77, 100 }
      } },
};

/* (int)((double)(steps * fraction) + 0.5) */
static int32_t round_half_up(float v)
{
    return iar_d2i((double)v + 0.5);
}

/* Index of the first point >= freq (the last point if none is). */
static int find_point(const int32_t *points, int n, int32_t freq)
{
    for (int i = 0; i < n; i++) {
        if (points[i] >= freq) {
            return i;
        }
    }
    return n - 1;
}

static int32_t wrap_add(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a + (uint32_t)b);
}

static int32_t wrap_mul(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

/*
 * orig: 0x080129EE - selections 2 and 3
 * Up to 20 kHz the offsets are trim2/trim3 scaled by the frequency tables;
 * above, trim2 + (trim0 - trim2) * pct and trim3 + (trim1 - trim3) * pct.
 * The level interpolation runs from the low offset at trim2 + 5000 counts
 * to the high offset at 50000.
 */
int32_t trim_correction_ac(int32_t reading, int32_t trim0, int32_t trim1, int32_t trim2,
                           int32_t trim3, uint8_t selection, int32_t freq, uint8_t flag)
{
    const int32_t *points = (freq < TRIM_HIGH_BAND) ? trim_freq_lo : trim_freq_hi;
    int band = (freq < TRIM_HIGH_BAND) ? 0 : 1;
    int i;

    g_trac_d31 = trim1 - trim3;
    g_trac_d20 = trim0 - trim2;
    i = find_point(points, 9, freq);
    g_trac_a = points[i];
    g_trac_b = points[i - 1];
    g_trac_span = g_trac_a - g_trac_b;
    g_trac_delta = freq - g_trac_b;
    g_trac_frac = (float)g_trac_delta / (float)g_trac_span;

    if (selection == 2 || selection == 3) {
        int table = (selection == 3) ? 2 : (flag != 0 ? 0 : 1);
        const int32_t (*pct)[9] = trim_pct_ac[table][band];
        g_trac_a = pct[0][i];
        g_trac_b = pct[0][i - 1];
        g_trac_c = pct[1][i];
        g_trac_d = pct[1][i - 1];
    }
    g_trac_span = g_trac_b;
    g_trac_delta = g_trac_a - g_trac_b;
    g_trac_pct2 = round_half_up((float)g_trac_delta * g_trac_frac) + g_trac_span;
    g_trac_span = g_trac_d;
    g_trac_delta = g_trac_c - g_trac_d;
    g_trac_pct3 = round_half_up((float)g_trac_delta * g_trac_frac) + g_trac_span;

    if (freq >= TRIM_HIGH_BAND) {
        g_trac_off_lo = wrap_add(trim2, wrap_mul(g_trac_d20, g_trac_pct2) / 100);
        g_trac_off_hi = wrap_add(trim3, wrap_mul(g_trac_d31, g_trac_pct3) / 100);
    } else {
        g_trac_off_lo = wrap_mul(g_trac_pct2, trim2) / 100;
        g_trac_off_hi = wrap_mul(g_trac_pct3, trim3) / 100;
    }

    g_trac_knee = wrap_add(trim2, 5000);
    if (g_trac_knee < reading) {
        g_trac_level = iar_f2i(((float)reading - (float)g_trac_knee)
                               / (TRIM_FULL_SCALE - (float)g_trac_knee) * 100.0f);
        g_trac_diff = g_trac_off_hi - g_trac_off_lo;
        g_trac_out = g_trac_diff - wrap_mul(g_trac_diff, g_trac_level) / 100;
        g_trac_out = g_trac_off_hi - g_trac_out;
    } else {
        /* 0/0 when trim2 is -5000 and the reading 0: IAR's negative NaN */
        g_trac_level = iar_f2i(iar_fdiv((float)g_trac_knee - (float)reading, (float)g_trac_knee)
                               * 100.0f);
        g_trac_diff = g_trac_off_lo;
        g_trac_out = g_trac_off_lo - wrap_mul(g_trac_off_lo, g_trac_level) / 100;
    }
    g_trac_result = g_trac_out;
    return g_trac_result;
}

/*
 * orig: 0x080124F4
 * Correction (counts) for a reading at the measured frequency. Selections
 * 2 and 3 use trim_correction_ac(); 0/1 and 4..7 interpolate a percentage
 * gain from the tables (row by level), scale trim0/trim1 by it, and
 * interpolate between them in level. Returns 0 for readings of 55000 and
 * above (6600 for selection 5, 60000 for selection 4), for selections 8 and
 * up, and for frequencies up to 50 Hz.
 */
int32_t trim_correction(int32_t reading, int32_t trim0, int32_t trim1, int32_t trim2, int32_t trim3,
                        uint8_t selection, int32_t freq_raw, uint8_t freq_decimal, uint8_t flag)
{
    const int32_t *points;
    const int32_t *pct = 0;
    int n;
    int i;
    int row;

    g_trc_result = 0;
    g_trc_extra = 0;
    if (selection == 5 && reading >= 6600) {
        return g_trc_result;
    }
    if (selection == 4 && reading >= 60000) {
        return g_trc_result;
    }
    if (reading >= 55000 || selection >= 8) {
        return g_trc_result;
    }
    g_trc_freq = scale_by_decimal(freq_decimal, freq_raw);
    if (g_trc_freq < TRIM_FREQ_MIN) {
        return g_trc_result;
    }
    if (selection == 2 || selection == 3) {
        g_trc_result = trim_correction_ac(reading, trim0, trim1, trim2, trim3, selection, g_trc_freq, flag);
        return g_trc_result;
    }

    n = (selection >= 4) ? 7 : 9;
    points = (selection < 2) ? trim_freq_a : trim_freq_b;
    i = find_point(points, n, g_trc_freq);
    g_trc_t0 = trim0;
    g_trc_t1 = trim1;
    g_trc_a = points[i];
    g_trc_b = points[i - 1];
    g_trc_span = g_trc_a - g_trc_b;
    g_trc_delta = g_trc_freq - g_trc_b;
    g_trc_frac = (float)g_trc_delta / (float)g_trc_span;

    row = (reading >= TRIM_LOW_LEVEL) ? 1 : 0;
    if (selection < 2) {
        pct = trim_pct_a[selection][row];
    } else {
        pct = trim_pct_b[selection - 4][row];
    }
    g_trc_a = pct[i];
    g_trc_b = pct[i - 1];
    g_trc_span = g_trc_b;
    g_trc_delta = g_trc_a - g_trc_b;
    g_trc_gain = round_half_up((float)g_trc_delta * g_trc_frac) + g_trc_span;
    g_trc_off0 = iar_f2i((float)g_trc_gain * (float)g_trc_t0 / 100.0f);
    g_trc_off1 = iar_f2i((float)g_trc_gain * (float)g_trc_t1 / 100.0f);

    if (selection == 5) {
        g_trc_knee = wrap_add(g_trc_off0, 600);
        g_trc_span2 = wrap_add(g_trc_off1, 5400);
        g_trc_base = 600;
    } else {
        g_trc_knee = wrap_add(g_trc_off0, 5000);
        g_trc_span2 = wrap_add(g_trc_off1, 45000);
        g_trc_base = 5000;
    }
    if (g_trc_knee < reading) {
        g_trc_a = g_trc_off1;
        g_trc_b = g_trc_off1 - g_trc_off0;
        g_trc_level = (float)((double)((float)(reading - g_trc_base) / (float)g_trc_span2) + -1.0);
        g_trc_level = (float)g_trc_b * g_trc_level;
    } else {
        g_trc_a = 0;
        /* 0/0 when the knee and the reading are 0: IAR's negative NaN */
        g_trc_level = (float)g_trc_off0 * iar_fdiv((float)reading, (float)g_trc_knee);
    }
    g_trc_result = wrap_add(g_trc_extra, wrap_add(iar_f2i(g_trc_level), g_trc_a));
    return g_trc_result;
}

/*
 * orig: 0x080075FA
 * trim_correction() with the current trims and frequency. For function 2
 * AC+DC, and function 0 with g_flag_8098, the frequency of that mode's own
 * counter is used (and copied into g_freq_raw/g_freq_decimal).
 */
int32_t trim_correction_for(uint8_t selection, int32_t reading)
{
    if (g_function == 16 || g_function == 18 || g_function == 20
        || g_function == 2 || g_function == 4) {
        if (g_function == 2 && g_flag_80ba != 0) {
            g_freq_raw = g_freq_raw_acdc;
            g_freq_decimal = g_freq_decimal_acdc;
        }
    } else if (g_function == 0 && g_flag_8098 != 0) {
        g_freq_raw = g_freq_raw_f0;
        g_freq_decimal = g_freq_decimal_f0;
    }
    return trim_correction(reading, g_cal_trim[0], g_cal_trim[1], g_cal_trim[2], g_cal_trim[3],
                           selection, g_freq_raw, g_freq_decimal, g_flag_8087);
}

/*
 * orig: 0x0800742C
 * Removes the trim correction from a reading of functions 2, 4, 16, 18, 20
 * (selection: function 4 range, function 2 range + 2, 6/7/8 for 16/18/20),
 * plus the function 2 range 0 frequency offset when g_flag_8087 is set.
 * Not applied in calibration, with g_flag_80a5, or for functions 2/4 with
 * g_flag_80b9 (the frequency-response correction is used there instead).
 */
int32_t trim_correct_reading(uint8_t function, int32_t value)
{
    uint8_t selection = 0;

    if (g_flag_80a5 != 0) {
        return value;
    }
    if ((function == 2 || function == 4) && g_flag_80b9 == 1) {
        return value;
    }
    if (g_flag_808a != 0) {
        return value;
    }
    if (!(function == 16 || function == 18 || function == 20 || function == 2 || function == 4)) {
        return value;
    }
    switch (function) {
    case 4:  selection = g_range; break;
    case 2:  selection = (uint8_t)(g_range + 2); break;
    case 16: selection = 6; break;
    case 18: selection = 7; break;
    default: selection = 8; break;   /* function 20: no table, correction 0 */
    }
    g_trim_correction = trim_correction_for(selection, value);
    value = (int32_t)((uint32_t)value - (uint32_t)g_trim_correction);
    if (g_flag_8087 != 0 && function == 2 && g_range == 0) {
        g_trim_correction = f2r0_freq_offset(value, g_freq_raw, g_freq_decimal);
        value = (int32_t)((uint32_t)value - (uint32_t)g_trim_correction);
    }
    return value;
}
