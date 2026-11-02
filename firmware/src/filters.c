/*
 * Reading filters.
 */
#include "filters.h"

#define SMOOTH3_STEP  10
#define SMOOTH3_DEPTH 3

/*
 * orig: 0x0800B888
 * Running mean of up to 3 readings; restarts when a reading differs from
 * the last output by more than 10 counts. When full, the oldest share is
 * approximated by the last output.
 */
int32_t smooth3(int32_t value)
{
    int32_t step = (int32_t)((uint32_t)value - (uint32_t)g_smooth3_out);

    if (step > SMOOTH3_STEP || step < -SMOOTH3_STEP) {
        g_smooth3_count = 0;
    }
    if (g_smooth3_count == 0) {
        g_smooth3_count = 1;
        g_smooth3_sum = value;
        g_smooth3_out = value;
        return value;
    }
    g_smooth3_count++;
    if (g_smooth3_count > SMOOTH3_DEPTH) {
        g_smooth3_count = SMOOTH3_DEPTH;
        g_smooth3_sum = (int32_t)((uint32_t)value + ((uint32_t)g_smooth3_sum - (uint32_t)g_smooth3_out));
    } else {
        g_smooth3_sum = (int32_t)((uint32_t)value + (uint32_t)g_smooth3_sum);
    }
    g_smooth3_out = g_smooth3_sum / g_smooth3_count;
    return g_smooth3_out;
}

/*
 * Mean of the last max_count readings (64-bit sum), newest first in buf.
 * A reading more than `window` away from the previous output restarts it.
 */
static int32_t mavg_step(int32_t *buf, int32_t *out, uint8_t *count,
                         int32_t value, int32_t window, uint16_t max_count)
{
    int32_t step = (int32_t)((uint32_t)value - (uint32_t)*out);
    int64_t sum = 0;

    if (step > window || (int64_t)step + window < 0) {
        *count = 0;
    }
    if (*count == 0) {
        buf[*count] = value;
        (*count)++;
        *out = value;
        return *out;
    }
    (*count)++;
    if (max_count < *count) {
        *count = (uint8_t)max_count;
    }
    for (int16_t i = 1; i < *count; i++) {
        buf[*count - i] = buf[*count - 1 - i];
    }
    buf[0] = value;
    for (int16_t i = 0; i < *count; i++) {
        sum += buf[i];
    }
    *out = (int32_t)(sum / *count);
    return *out;
}

/* orig: 0x08008344 */
int32_t moving_average(int32_t value, int32_t window, uint16_t max_count)
{
    return mavg_step(g_mavg_buf, &g_mavg_out, &g_mavg_count, value, window, max_count);
}

/*
 * orig: 0x0800B754
 * Second instance with its own history; window 0 or max_count 0 passes
 * the reading through untouched.
 */
int32_t moving_average_b(int32_t value, int32_t window, uint16_t max_count)
{
    if (window == 0 || max_count == 0) {
        return value;
    }
    return mavg_step(g_mavg_b_buf, &g_mavg_b_out, &g_mavg_b_count, value, window, max_count);
}
