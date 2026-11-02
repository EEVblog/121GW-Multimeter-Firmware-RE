/*
 * Frequency-response correction for AC voltage/current (functions 2 and 4
 * with g_flag_80b9 set).
 */
#include "ac_response.h"
#include "hy3131_ctrl.h"
#include "system_init.h"
#include "measure_scale.h"
#include "numeric.h"

#define AC_POINTS     216
#define AC_FREQ_MIN   20
#define AC_FREQ_MAX   6000
#define AC_LOW_LEVEL  100000   /* below: low-level gain table */
#define AC_HIGH_LEVEL 500000   /* above: high-level gain table; between: blended */

/*
 * orig: 0x08025018..0x08025A3B. Frequency points (Hz), then the high-level
 * and low-level gains (1000 = unity) at each point. The tables are kept
 * together, with the word before them: at exactly 20 Hz the original
 * interpolates from the entry before each table, which is the previous
 * table's last entry (or that word for the frequencies).
 */
static const int32_t ac_tab[1 + 3 * AC_POINTS] = {
    1073877018,   /* flash word before the frequency table */
    /* frequency, Hz (0x0802501C) */
    20, 30, 40, 50, 60, 70, 80, 90, 100, 110, 120, 130,
    140, 150, 160, 170, 180, 190, 200, 210, 220, 230, 240, 250,
    260, 270, 280, 290, 300, 310, 320, 330, 340, 350, 360, 370,
    380, 390, 400, 410, 420, 430, 440, 450, 460, 470, 480, 490,
    500, 510, 520, 530, 540, 550, 560, 570, 580, 590, 600, 610,
    620, 630, 640, 650, 660, 670, 680, 690, 700, 710, 720, 730,
    740, 750, 760, 770, 780, 790, 800, 810, 820, 830, 840, 850,
    860, 870, 880, 890, 900, 910, 920, 930, 940, 950, 960, 970,
    980, 990, 1000, 1010, 1020, 1030, 1040, 1050, 1060, 1070, 1080, 1090,
    1100, 1110, 1120, 1130, 1140, 1150, 1160, 1170, 1180, 1190, 1200, 1210,
    1220, 1230, 1240, 1250, 1260, 1270, 1280, 1290, 1300, 1310, 1320, 1330,
    1340, 1350, 1360, 1370, 1380, 1390, 1400, 1410, 1420, 1430, 1440, 1450,
    1460, 1470, 1480, 1490, 1500, 1510, 1520, 1530, 1540, 1550, 1560, 1570,
    1580, 1590, 1600, 1610, 1620, 1630, 1640, 1650, 1660, 1670, 1680, 1690,
    1700, 1710, 1720, 1730, 1740, 1750, 1760, 1770, 1780, 1790, 1800, 1810,
    1820, 1830, 1840, 1850, 1860, 1870, 1880, 1890, 1900, 1910, 1920, 1930,
    1940, 1950, 1960, 1970, 1980, 1990, 2000, 2200, 2400, 2600, 2800, 3000,
    3200, 3400, 3600, 3800, 4000, 4200, 4400, 4600, 4800, 5000, 5500, 6000,
    /* high-level gain (0x0802537C) */
    1015, 1025, 1038, 1053, 1073, 1096, 1122, 1150, 1181, 1214, 1249, 1285,
    1323, 1362, 1403, 1445, 1488, 1532, 1577, 1622, 1669, 1716, 1763, 1812,
    1860, 1910, 1959, 2010, 2060, 2111, 2162, 2214, 2266, 2317, 2369, 2422,
    2474, 2527, 2579, 2631, 2683, 2736, 2788, 2840, 2892, 2943, 2994, 3044,
    3094, 3143, 3191, 3239, 3286, 3332, 3377, 3421, 3464, 3505, 3545, 3583,
    3620, 3655, 3688, 3720, 3749, 3776, 3801, 3825, 3846, 3864, 3664, 3895,
    3906, 3916, 3923, 3928, 3929, 3931, 3928, 3924, 3918, 3909, 3899, 3886,
    3873, 3857, 3840, 3820, 3800, 3779, 3755, 3732, 3706, 3680, 3653, 3625,
    3597, 3566, 3538, 3507, 3477, 3446, 3415, 3383, 3351, 3321, 3287, 3256,
    3225, 3193, 3161, 3130, 3096, 3067, 3036, 3006, 2975, 2945, 2914, 2886,
    2856, 2828, 2798, 2771, 2741, 2715, 2688, 2661, 2634, 2608, 2584, 2558,
    2533, 2508, 2484, 2459, 2436, 2413, 2390, 2368, 2345, 2324, 2302, 2280,
    2260, 2239, 2219, 2201, 2180, 2160, 2142, 2123, 2105, 2088, 2069, 2051,
    2034, 2017, 2000, 1984, 1968, 1952, 1936, 1920, 1905, 1891, 1876, 1861,
    1846, 1832, 1818, 1804, 1791, 1777, 1764, 1751, 1739, 1727, 1714, 1703,
    1690, 1678, 1666, 1654, 1644, 1632, 1622, 1610, 1600, 1590, 1580, 1569,
    1558, 1549, 1538, 1528, 1519, 1509, 1499, 1333, 1200, 1092, 998, 918,
    855, 785, 729, 678, 643, 583, 565, 547, 497, 466, 296, 0,
    /* low-level gain (0x080256DC) */
    1015, 1025, 1038, 1042, 1059, 1079, 1102, 1127, 1154, 1183, 1214, 1247,
    1281, 1317, 1355, 1393, 1433, 1474, 1517, 1559, 1602, 1647, 1693, 1738,
    1785, 1832, 1880, 1928, 1977, 2026, 2075, 2125, 2175, 2225, 2275, 2327,
    2377, 2428, 2480, 2531, 2582, 2634, 2685, 2735, 2786, 2836, 2886, 2935,
    2984, 3032, 3079, 3128, 3174, 3220, 3265, 3307, 3348, 3391, 3431, 3469,
    3505, 3540, 3572, 3605, 3632, 3657, 3685, 3712, 3732, 3750, 3768, 3783,
    3793, 3806, 3812, 3820, 3824, 3825, 3824, 3822, 3815, 3810, 3798, 3787,
    3776, 3760, 3746, 3726, 3709, 3689, 3667, 3645, 3622, 3598, 3573, 3547,
    3520, 3492, 3464, 3433, 3406, 3377, 3348, 3317, 3284, 3258, 3226, 3196,
    3166, 3135, 3106, 3077, 3048, 3016, 2989, 2957, 2929, 2900, 2873, 2845,
    2815, 2788, 2760, 2735, 2708, 2683, 2658, 2631, 2606, 2581, 2557, 2533,
    2516, 2491, 2465, 2439, 2420, 2396, 2372, 2353, 2331, 2309, 2289, 2269,
    2250, 2229, 2208, 2189, 2172, 2153, 2135, 2118, 2101, 2082, 2066, 2047,
    2031, 2014, 1997, 1982, 1967, 1952, 1934, 1921, 1907, 1892, 1877, 1865,
    1850, 1837, 1823, 1810, 1798, 1786, 1771, 1760, 1749, 1736, 1723, 1695,
    1701, 1689, 1678, 1666, 1656, 1645, 1635, 1626, 1616, 1604, 1596, 1588,
    1575, 1568, 1556, 1549, 1540, 1530, 1519, 1366, 1244, 1140, 1054, 985,
    928, 862, 813, 760, 729, 661, 649, 636, 587, 562, 381, 0,
};

static const int32_t *const ac_freq = &ac_tab[1];
static const int32_t *const ac_gain_high = &ac_tab[1 + AC_POINTS];
static const int32_t *const ac_gain_low = &ac_tab[1 + 2 * AC_POINTS];

/* (int)((double)(steps * fraction) + 0.5) */
static int32_t round_half_up(float v)
{
    return iar_d2i((double)v + 0.5);
}

/* Gain at the current frequency from one gain table (uses g_ac_frac). */
static int32_t gain_from(const int32_t *gain, int i)
{
    g_ac_hi = gain[i];
    g_ac_lo = gain[i - 1];
    g_ac_span = g_ac_lo;
    g_ac_delta = g_ac_hi - g_ac_lo;
    return round_half_up((float)g_ac_delta * g_ac_frac) + g_ac_span;
}

/*
 * orig: 0x08006D40
 * Scales a reading by the gain for the measured frequency (interpolated
 * between table points, and between the low- and high-level tables for
 * readings of 100000..500000). Frequencies below 20 Hz leave the reading
 * unchanged; above 6000 Hz give 0.
 */
int32_t ac_response_correct(int32_t reading)
{
    int i;

    if (!((g_function == 2 || g_function == 4) && g_flag_80b9 == 1)) {
        return reading;
    }
    g_ac_freq = scale_by_decimal(g_freq_decimal, g_freq_raw);
    if (g_ac_freq < AC_FREQ_MIN) {
        return reading;
    }
    if (g_ac_freq > AC_FREQ_MAX) {
        return 0;
    }
    for (i = 0; i < AC_POINTS; i++) {
        if (ac_freq[i] >= g_ac_freq) {
            break;
        }
    }
    if (i == AC_POINTS) {
        i = AC_POINTS - 1;   /* not reached: the last point is 6000 Hz */
    }
    g_ac_hi = ac_freq[i];
    g_ac_lo = ac_freq[i - 1];
    g_ac_span = g_ac_hi - g_ac_lo;
    g_ac_delta = g_ac_freq - g_ac_lo;
    g_ac_frac = (float)g_ac_delta / (float)g_ac_span;

    if (reading < AC_LOW_LEVEL) {
        g_ac_gain = gain_from(ac_gain_low, i);
    } else if (reading > AC_HIGH_LEVEL) {
        g_ac_gain = gain_from(ac_gain_high, i);
    } else {
        g_ac_hi = ac_gain_low[i];
        g_ac_lo = ac_gain_low[i - 1];
        g_ac_hi2 = ac_gain_high[i];
        g_ac_lo2 = ac_gain_high[i - 1];
        g_ac_span = g_ac_lo;
        g_ac_delta = g_ac_hi - g_ac_lo;
        g_ac_gain_low = round_half_up((float)g_ac_delta * g_ac_frac) + g_ac_span;
        g_ac_span = g_ac_lo2;
        g_ac_delta = g_ac_hi2 - g_ac_lo2;
        g_ac_gain_high = round_half_up((float)g_ac_delta * g_ac_frac) + g_ac_span;
        g_ac_blend = (float)(g_ac_gain_high - g_ac_gain_low)
                     * (((float)reading + -100000.0f) / 400000.0f);
        g_ac_gain = iar_f2i(g_ac_blend) + g_ac_gain_low;
    }
    return (int32_t)((uint32_t)g_ac_gain * (uint32_t)reading) / 1000 / 10;
}

/* ---- function 2 range 0: frequency-dependent offset ---- */

#define F2R0_POINTS     17
#define F2R0_FREQ_MIN   51
#define F2R0_LIMIT      57000
#define F2R0_LOW_LEVEL  5001
#define F2R0_FULL_SCALE 50000.0f

/* orig: 0x08028C58 frequency points (Hz); the first is below the minimum, so
 * index 0 is never interpolated from. */
static const int32_t f2r0_freq[F2R0_POINTS] = { 50, 400, 1000, 2000, 3000, 5000, 7000, 10000, 20000, 30000, 40000, 50000, 60000, 70000, 80000, 90000, 100000 };
/* orig: 0x08028648 offset at low level (for readings up to 5000) */
static const int32_t f2r0_off_low[F2R0_POINTS] = { 0, 7, 36, 92, 152, 171, 190, 206, 199, 199, 199, 195, 231, 237, 237, 264, 269 };
/* orig: 0x0802868C offset at full scale */
static const int32_t f2r0_off_high[F2R0_POINTS] = { 0, 41, 294, 680, 1126, 1552, 1716, 1788, 1867, 1884, 1912, 1931, 1949, 1962, 1970, 1981, 1979 };

/*
 * orig: 0x080070CC
 * Offset to subtract from a function 2 range 0 reading, interpolated in
 * frequency between table points (beyond 100 kHz the last segment is
 * extrapolated) and then in level. Readings of 57000 and above, and
 * frequencies up to 50 Hz, give 0.
 */
int32_t f2r0_freq_offset(int32_t reading, int32_t freq_raw, uint8_t freq_decimal)
{
    int i;

    g_f2r0_result = 0;
    if (reading >= F2R0_LIMIT) {
        return g_f2r0_result;
    }
    g_f2r0_freq = scale_by_decimal(freq_decimal, freq_raw);
    if (g_f2r0_freq < F2R0_FREQ_MIN) {
        return g_f2r0_result;
    }
    for (i = 0; i < F2R0_POINTS; i++) {
        if (f2r0_freq[i] >= g_f2r0_freq) {
            break;
        }
    }
    if (i == F2R0_POINTS) {
        i = F2R0_POINTS - 1;
    }
    g_f2r0_a = f2r0_freq[i];
    g_f2r0_b = f2r0_freq[i - 1];
    g_f2r0_span = g_f2r0_a - g_f2r0_b;
    g_f2r0_delta = g_f2r0_freq - g_f2r0_b;
    g_f2r0_frac = (float)g_f2r0_delta / (float)g_f2r0_span;
    if (reading < F2R0_LOW_LEVEL) {
        g_f2r0_a = 0;
        g_f2r0_b = 0;
        g_f2r0_c = f2r0_off_low[i];
        g_f2r0_d = f2r0_off_low[i - 1];
    } else {
        g_f2r0_a = f2r0_off_low[i];
        g_f2r0_b = f2r0_off_low[i - 1];
        g_f2r0_c = f2r0_off_high[i];
        g_f2r0_d = f2r0_off_high[i - 1];
    }
    g_f2r0_span = g_f2r0_b;
    g_f2r0_delta = g_f2r0_a - g_f2r0_b;
    g_f2r0_off_lo = round_half_up((float)g_f2r0_delta * g_f2r0_frac) + g_f2r0_span;
    g_f2r0_span = g_f2r0_d;
    g_f2r0_delta = g_f2r0_c - g_f2r0_d;
    g_f2r0_off_hi = round_half_up((float)g_f2r0_delta * g_f2r0_frac) + g_f2r0_span;

    /* level: 0 % at g_f2r0_knee (low offset + 5000), 100 % at 0 and at 50000 */
    g_f2r0_knee = g_f2r0_off_lo + 5000;
    if (g_f2r0_knee < reading) {
        g_f2r0_pct = iar_f2i(((float)reading - (float)g_f2r0_knee)
                             / (F2R0_FULL_SCALE - (float)g_f2r0_knee) * 100.0f);
    } else {
        g_f2r0_pct = iar_f2i(((float)g_f2r0_knee - (float)reading) / (float)g_f2r0_knee * 100.0f);
    }
    g_f2r0_a = g_f2r0_off_hi - g_f2r0_off_lo;
    g_f2r0_b = g_f2r0_a - (int32_t)((uint32_t)g_f2r0_pct * (uint32_t)g_f2r0_a) / 100;
    g_f2r0_c = g_f2r0_off_hi - g_f2r0_b;
    g_f2r0_result = g_f2r0_c;
    return g_f2r0_result;
}
