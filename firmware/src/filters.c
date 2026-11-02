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
 * orig: 0x08008344
 * Mean of the last max_count readings (64-bit sum). A reading more than
 * `window` away from the previous output restarts the average.
 */
int32_t moving_average(int32_t value, int32_t window, uint16_t max_count)
{
    int32_t step = (int32_t)((uint32_t)value - (uint32_t)g_mavg_out);
    int64_t sum = 0;

    if (step > window || (int64_t)step + window < 0) {
        g_mavg_count = 0;
    }
    if (g_mavg_count == 0) {
        g_mavg_buf[g_mavg_count] = value;
        g_mavg_count++;
        g_mavg_out = value;
        return g_mavg_out;
    }
    g_mavg_count++;
    if (max_count < g_mavg_count) {
        g_mavg_count = (uint8_t)max_count;
    }
    for (int16_t i = 1; i < g_mavg_count; i++) {
        g_mavg_buf[g_mavg_count - i] = g_mavg_buf[g_mavg_count - 1 - i];
    }
    g_mavg_buf[0] = value;
    for (int16_t i = 0; i < g_mavg_count; i++) {
        sum += g_mavg_buf[i];
    }
    g_mavg_out = (int32_t)(sum / g_mavg_count);
    return g_mavg_out;
}
