/*
 * Derived measurement values.
 */
#include "measure_derived.h"
#include "hy3131_ctrl.h"
#include "measure_rms.h"
#include "measure_scale.h"
#include "system_init.h"
#include "iar_math.h"

#define HOLDOFF_TICKS   2000u
#define VECTOR_LIMIT    60000
#define VECTOR_OVERFLOW 100000

/*
 * orig: 0x08006C7A
 * With a new reading below a function-dependent level (and the hold-off
 * counter above 2), restarts the hold-off at 2000.
 */
void holdoff_on_low_reading(void)
{
    int32_t level = 0;

    if (g_new_reading == 0) {
        return;
    }
    if ((g_function == 2 || g_function == 4) && g_flag_80b9 == 1) {
        return;
    }
    if (g_holdoff <= 2) {
        return;
    }
    if (g_function == 2) {
        level = (g_range == 3) ? 100 : 1000;
    } else if (g_function == 4 || g_function == 16 || g_function == 18 || g_function == 20) {
        level = 1000;
    }
    if (level != 0 && g_derived_value < level) {
        g_holdoff = HOLDOFF_TICKS;
    }
}

/* orig: 0x08007940 */
int32_t range_threshold(void)
{
    if (g_flag_80b5 == 0) {
        if (g_sel_80b6 < 2) {
            return 3000;
        }
        if (g_sel_80b6 < 4) {
            return 30000;
        }
        return 3000;
    }
    switch (g_sel_80b6) {
    case 0: return 3500;
    case 1: return 2500;
    case 2: return 30500;
    case 3: return 29500;
    default: return 3000;
    }
}

/* orig: 0x08008468 */
void scale_reading(int32_t raw)
{
    g_scaled_value = subtract_zero_offset(raw);
    g_scaled_value = apply_gain(g_scaled_value);
    if ((g_function == 2 || g_function == 4) && g_flag_80b9 == 1) {
        g_scaled_value /= 10;
    }
    g_scaled_ready = 1;
}

/*
 * orig: 0x0800A4AC
 * |a| * |b| as a signed 64-bit product divided by 1e5 (functions 13, 14,
 * 22, 23, or ranges 0-1) or 1e4. Negation wraps as in the original, so
 * INT32_MIN stays negative.
 */
void product_scaled(int32_t a, int32_t b)
{
    int32_t divisor;

    if (a < 0) {
        a = (int32_t)(0u - (uint32_t)a);
    }
    if (b < 0) {
        b = (int32_t)(0u - (uint32_t)b);
    }
    if (g_function == 14 || g_function == 23 || g_function == 13 || g_function == 22
        || g_range < 2) {
        divisor = 100000;
    } else {
        divisor = 10000;
    }
    g_derived_value = (int32_t)(((int64_t)a * (int64_t)b) / divisor);
}

/*
 * orig: 0x0800A6A8
 * sqrt(a^2 + b^2) for |a|, |b| <= 60000, else 100000. The squares and their
 * sum wrap at 32 bits, as in the original; isqrt_u32() gives exactly the
 * original's (int)sqrt((double)sum).
 */
void vector_magnitude(void)
{
    if (g_vec_a < 0) {
        g_vec_a = (int32_t)(0u - (uint32_t)g_vec_a);   /* wraps: INT32_MIN stays negative */
    }
    if (g_vec_b < 0) {
        g_vec_b = (int32_t)(0u - (uint32_t)g_vec_b);
    }
    if (g_vec_a <= VECTOR_LIMIT && g_vec_b <= VECTOR_LIMIT) {
        g_vec_a_sq = (uint32_t)g_vec_a * (uint32_t)g_vec_a;
        g_vec_b_sq = (uint32_t)g_vec_b * (uint32_t)g_vec_b;
        g_derived_value = (int32_t)isqrt_u32(g_vec_b_sq + g_vec_a_sq);
    } else {
        g_derived_value = VECTOR_OVERFLOW;
    }
}

/*
 * orig: 0x0800A74C
 * dBm into 600 ohm: 10 * log10(v^2 / 0.6), with v = g_derived_value scaled
 * by the range (10000, 1000, 100, 10 for ranges 0..3). For other ranges the
 * original uses an uninitialised register as v; 0 is used here.
 */
void dbm_from_voltage(void)
{
    float v = 0.0f;

    switch (g_range) {
    case 0: v = (float)g_derived_value / 10000.0f; break;
    case 1: v = (float)g_derived_value / 1000.0f; break;
    case 2: v = (float)g_derived_value / 100.0f; break;
    case 3: v = (float)g_derived_value / 10.0f; break;
    default: break;
    }
    float power = (float)((double)(v * v) / 0.6);
    g_dbm = (float)(iar_log10((double)power) * 10.0);
}
