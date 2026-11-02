/*
 * Thermocouple conversion and Celsius -> Fahrenheit.
 *
 * Temperatures are in hundredths of a degree.
 */
#include "temperature.h"
#include "numeric.h"

#define TC_MIN_UV      (-5891)   /* -200 C */
#define TC_LIMIT_UV    54139     /* just above 1350 C */
#define TC_FIRST_C100  (-22000)  /* table index 0 is -210 C; the formula is offset by one step */
#define TC_STEP_C100   1000

/* Type K thermocouple EMF in microvolts, -210 C to +1360 C in 10 C steps
 * (orig: 0x08025CF8). */
static const int32_t tc_type_k_uv[158] = {
    -6035, -5891, -5730, -5550, -5354, -5141, -4913, -4669, -4411, -4138,
    -3852, -3554, -3243, -2920, -2587, -2243, -1889, -1527, -1156, -778,
    -392, 0, 397, 798, 1203, 1612, 2023, 2436, 2851, 3267,
    3682, 4096, 4509, 4920, 5328, 5735, 6138, 6540, 6941, 7340,
    7739, 8138, 8539, 8940, 9343, 9747, 10153, 10561, 10970, 11382,
    11795, 12209, 12624, 13040, 13457, 13874, 14293, 14713, 15133, 15554,
    15975, 16397, 16820, 17243, 17667, 18091, 18516, 18941, 19366, 19792,
    20218, 20644, 21071, 21497, 21924, 22350, 22776, 23203, 23629, 24055,
    24480, 24905, 25330, 25755, 26179, 26602, 27025, 27447, 27869, 28289,
    28710, 29129, 29548, 29965, 30382, 30798, 31213, 31628, 32041, 32453,
    32865, 33275, 33685, 34093, 34501, 34908, 35313, 35718, 36121, 36524,
    36925, 37326, 37725, 38124, 38522, 38918, 39314, 39708, 40101, 40494,
    40885, 41276, 41665, 42053, 42440, 42826, 43211, 43595, 43978, 44359,
    44740, 45119, 45497, 45873, 46249, 46623, 46995, 47367, 47737, 48105,
    48473, 48838, 49202, 49565, 49926, 50286, 50644, 51000, 51355, 51708,
    52060, 52410, 52759, 53106, 53451, 53795, 54138, 54479,
};

/*
 * orig: 0x08013C40
 * Converts a thermocouple voltage (uV, cold junction already added) to
 * hundredths of a degree C by linear interpolation in the 10 C table.
 * Out of range gives 0 with status 2 (below -200 C) or 1 (at or above
 * 54139 uV).
 */
void thermocouple_to_celsius(int32_t microvolts)
{
    uint8_t i = 0;

    g_temp_status = 0;
    if (microvolts < TC_MIN_UV) {
        g_temp_c100 = 0;
        g_temp_status = 2;
        return;
    }
    if (microvolts >= TC_LIMIT_UV) {
        g_temp_c100 = 0;
        g_temp_status = 1;
        return;
    }
    while (tc_type_k_uv[i] < microvolts) {
        i++;
    }
    /* microvolts lies in (table[i-1], table[i]]; i >= 1 here */
    g_temp_base = (int32_t)i * TC_STEP_C100 + TC_FIRST_C100;
    int32_t fraction = ((tc_type_k_uv[i - 1] - microvolts) * TC_STEP_C100)
                       / (tc_type_k_uv[i - 1] - tc_type_k_uv[i]);
    g_temp_work = iar_f2i((float)fraction);   /* the original rounds through float */
    g_temp_work = g_temp_base + g_temp_work;
    g_temp_c100 = g_temp_work;
}

/*
 * orig: 0x08013B76
 * F = C * 1.8 + 32 in hundredths, rounded half up on the magnitude. The
 * sign is taken from bit 15 and negative values are handled as one's
 * complement (~x), as in the original.
 */
int32_t celsius_to_fahrenheit(int32_t c100)
{
    int negative = (c100 & 0x8000) != 0;
    float f;

    g_temp_work = negative ? ~c100 : c100;
    f = (float)((double)(float)g_temp_work * 1.8);
    if (!negative) {
        f = 3200.0f + f;
    } else if (f <= 3200.0f) {
        negative = 0;
        f = 3200.0f - f;
    } else {
        f = -3200.0f + f;
    }
    g_temp_work = iar_f2i(f);
    if (f - (float)g_temp_work >= 0.5f) {
        g_temp_work = g_temp_work + 1;
    }
    if (negative) {
        g_temp_work = ~g_temp_work;
    }
    return g_temp_work;
}
