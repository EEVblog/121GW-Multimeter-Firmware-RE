/*
 * Per-function processing of AD1 results (called from hy3131_on_ad1_ready).
 */
#include "measure_funcs.h"
#include "buzzer.h"
#include "calibration.h"
#include "filters.h"
#include "front_end.h"
#include "hy3131_bus.h"
#include "measure_ad1.h"
#include "display_state.h"
#include "measure_derived.h"
#include "measure_scale.h"
#include "hy3131_ctrl.h"
#include "numeric.h"
#include "ac_response.h"
#include "trim_correction.h"
#include "system_init.h"
#include "range_select.h"
#include "temperature.h"

#define F11_BUZZ_LEVEL     1000
#define F11_CHANGE_STEP    1000
#define F11_HIGH_LEVEL     14000
#define F11_HIGH_OFFSET    500
#define F11_HOLDOFF        100

/*
 * Function 9 range 6 correction curve: x points (thousands of counts) and
 * percentages of g_cal_e14. A reading of 0 selects segment 0, which takes
 * the word before each table (the original reads past the start); those
 * words are kept here so the result is identical.
 */
static const int32_t f9r6_x_tab[14] = {
    -405941767,                                     /* flash word before the table */
    0, 5, 10, 15, 20, 25, 30, 35, 40, 45, 50, 55, 60
};
static const int32_t f9r6_pct_tab[14] = {
    60,                                             /* = last x point (tables are adjacent) */
    0, 37, 65, 85, 100, 100, 100, 87, 70, 40, 0, -37, -65
};
#define F9R6_X(i)   (f9r6_x_tab[(i) + 1])
#define F9R6_PCT(i) (f9r6_pct_tab[(i) + 1])
#define F9R6_POINTS 13
#define F9R6_LIMIT  56000

/*
 * orig: 0x08007B70
 * Function 9 range 5 correction (counts): rises by 1 per 1000 counts from
 * 1000 to 15 at 15000, flat to 35999, then falls back to 0 at 50000.
 */
int32_t f9r5_correction(int32_t reading)
{
    if (reading >= 50000 || reading < 1000) {
        return 0;
    }
    if (reading < 15000) {
        return reading / 1000;
    }
    if (reading < 36000) {
        return 15;
    }
    return 15 - (reading - 35000) / 1000;
}

/*
 * orig: 0x08007D1E
 * Function 9 range 6 correction: g_cal_e14 * curve(reading) / 100, the
 * curve interpolated linearly between 5000-count points (in float).
 */
int32_t f9r6_correction(int32_t reading, int32_t scale)
{
    uint8_t i;

    g_f9r6_result = 0;
    if (reading >= F9R6_LIMIT) {
        g_f9r6_result = 0;
        return g_f9r6_result;
    }
    for (i = 0; i < F9R6_POINTS; i++) {
        if (F9R6_X(i) * 1000 >= reading) {
            break;
        }
    }
    /* reading < 56000 <= 60 * 1000, so the loop always breaks */
    g_f9r6_x_lo = F9R6_X(i - 1);
    g_f9r6_x_hi = F9R6_X(i);
    g_f9r6_frac = (float)(int32_t)((uint32_t)reading - (uint32_t)g_f9r6_x_lo * 1000u)
                  / (float)(int32_t)(((uint32_t)g_f9r6_x_hi - (uint32_t)g_f9r6_x_lo) * 1000u);
    g_f9r6_pct = (float)(F9R6_PCT(i) - F9R6_PCT(i - 1));
    g_f9r6_pct = (float)F9R6_PCT(i - 1) + g_f9r6_frac * g_f9r6_pct;
    g_f9r6_result = iar_f2i((float)scale * g_f9r6_pct / 100.0f);
    return g_f9r6_result;
}

/*
 * orig: 0x080079C4 - function 9
 * Negative readings are clipped to 0 except in range 0. The moving average
 * depends on the range; outside calibration ranges 5 and 6 get their
 * correction curves subtracted.
 */
void ad1_process_func9(void)
{
    if (g_ad1_average_ready != 0) {
        int32_t window;
        uint16_t depth;

        derived_from_average();
        if (g_derived_value < 0 && g_range != 0) {
            g_derived_value = 0;
        }
        switch (g_range) {
        case 6: window = 100; depth = 20; break;
        case 5: window = 10;  depth = 5;  break;
        case 0: window = 100; depth = 10; break;
        case 2: window = 10;  depth = 10; break;
        default: window = 20; depth = 10; break;
        }
        g_derived_value = moving_average_b(g_derived_value, window, depth);
        if (g_flag_7fe0 == 0) {
            if (g_flag_808a == 0 && g_function == 9 && g_range == 5) {
                g_derived_value = g_derived_value - f9r5_correction(g_derived_value);
            }
            if (g_flag_808a == 0 && g_function == 9 && g_range == 6) {
                g_derived_value = g_derived_value - f9r6_correction(g_derived_value, g_cal_e14);
            }
        }
        g_new_reading = 1;
    }
    scaled_from_ad1();
    if (g_scaled_value < 0) {
        g_scaled_value = 0;
    }
}

/* orig: 0x080078B8 - function 10 */
void ad1_process_func10(void)
{
    ad1_process_func9();
    threshold_check((uint32_t)g_scaled_value);
}

/* orig: 0x080077F0 - function 3 */
void ad1_process_func3(void)
{
    if (g_ad1_average_ready != 0) {
        derived_from_average();
        g_derived_value = moving_average_b(g_derived_value, 5, 10);
        g_new_reading = 1;
    }
    scaled_from_ad1();
}

/*
 * orig: 0x08007E84 - function 11
 * With g_flag_80b3 the readings are divided by 10. A reading within
 * +-999 keeps the buzzer on; otherwise a change of more than 1000 from the
 * last remembered reading gives a triple beep (once, until g_beep_holdoff
 * is cleared elsewhere). Above 14000 with PF8 low and g_flag_80b3 set
 * (outside calibration) the reading gets +500.
 */
void ad1_process_func11(void)
{
    if (g_ad1_average_ready != 0) {
        derived_from_average();
        if (g_flag_80b3 != 0) {
            g_derived_value = g_derived_value / 10;
        }
        g_derived_value = moving_average_b(g_derived_value, 2, 2);
        if ((uint8_t)(g_flag_808a | (g_pf8_detected_low ^ 1u) | (g_flag_80b3 ^ 1u)) == 0
            && g_derived_value > F11_HIGH_LEVEL) {
            g_derived_value = g_derived_value + F11_HIGH_OFFSET;
        }
        if (g_derived_value < F11_BUZZ_LEVEL && g_derived_value > -F11_BUZZ_LEVEL) {
            buzzer_start_forced(500);
            g_f11_last = g_derived_value;
        } else if (((int32_t)((uint32_t)g_f11_last + F11_CHANGE_STEP) < g_derived_value
                    || g_derived_value < (int32_t)((uint32_t)g_f11_last - F11_CHANGE_STEP))
                   && g_beep_holdoff == 0) {
            g_f11_last = g_derived_value;
            g_beep_holdoff = F11_HOLDOFF;
            beep_triple();
        }
        g_new_reading = 1;
    }
    scaled_from_ad1();
    if (g_flag_80b3 != 0) {
        g_scaled_value = g_scaled_value / 10;
    }
}

/*
 * orig: 0x08007840 - function 5 (temperature)
 * Thermocouple voltage plus cold-junction offset -> 0.01 C (or F), then
 * to 0.1 degree. The rounding looks at the last digit of the already
 * divided value (not the discarded one), as in the original.
 */
void ad1_process_func5(void)
{
    int32_t t;

    ad1_process_func3();
    if (g_new_reading == 0) {
        g_scaled_ready = 0;
        return;
    }
    thermocouple_to_celsius(g_tc_cold_junction_uv + g_derived_value);
    t = g_temp_c100;
    if (g_temp_unit_c == 0) {
        t = celsius_to_fahrenheit(g_temp_c100);
    }
    t = t / 10;
    if (t % 10 >= 5) {
        t = t + 1;
    }
    if (g_new_reading != 0) {
        g_derived_value = t;
    }
    g_scaled_value = t;
}

/*
 * orig: 0x0800A608
 * Function 2 with g_flag_80ba (AC+DC): stores the reading of the phase
 * just measured and switches phase (g_flag_80bd = next). Completing the
 * pair (next = 0) gives sqrt(a^2 + b^2), averaged, as the new reading.
 * The HY3131 and calibration are then set up for the next phase.
 */
void acdc_capture(uint8_t next)
{
    g_acdc_settle = 0;
    g_flag_80bd = next;
    if (next == 1) {
        g_vec_a = g_derived_value;
        g_acdc_have_a = 1;
    } else {
        g_vec_b = g_derived_value;
        g_acdc_have_b = 1;
    }
    if (next == 0) {
        vector_magnitude();
        g_derived_value = moving_average_b(g_derived_value, 20, 10);
        g_new_reading = 1;
        g_scaled_ready = 1;
        g_scaled_value = g_derived_value;
    }
    hy3131_configure_mode(g_function, g_range);
    cal_load(g_function, g_range);
}

/* orig: 0x0800A5BC - phase 0 captures after 5 readings, phase 1 after 3 */
void acdc_settle(uint8_t phase)
{
    if (phase == 0) {
        g_acdc_settle++;
        if (g_acdc_settle >= 5) {
            acdc_capture(1);
        }
    } else {
        g_acdc_settle++;
        if (g_acdc_settle >= 3) {
            acdc_capture(0);
        }
    }
}

/* orig: 0x080076BC - function 2 reading complete: AC+DC phase step and dBm */
void func2_reading_done(void)
{
    if (g_function == 2 && g_flag_80ba != 0) {
        if (g_flag_80bd == 0) {
            acdc_settle(g_flag_80bd);
        }
    } else {
        g_new_reading = 1;
    }
    dbm_from_voltage();
    if (g_function == 2 && g_mode_8037 == 11) {
        g_disp_flag_8062 = 1;
    }
}

/* orig: 0x08007714 - function 1 (also function 2 phases, and others for the scaled value) */
void ad1_process_func1(void)
{
    if (g_ad1_average_ready != 0) {
        derived_from_average();
        g_derived_value = moving_average_b(g_derived_value, 2, 2);
        if (!(g_function == 2 && g_flag_80ba != 0)) {
            g_new_reading = 1;
        }
        if (g_function == 2 && g_flag_80ba != 0 && g_flag_80bd == 1) {
            acdc_settle(g_flag_80bd);
        }
    }
    if (g_function == 2 && g_flag_80ba != 0) {
        return;   /* AC+DC: the scaled value comes from acdc_capture() */
    }
    switch (g_function) {
    case 16: case 17: case 18: case 19: case 20: case 21:
        return;
    case 0:
        if (g_setting_239 != 5) {
            return;
        }
        break;
    default:
        break;
    }
    scaled_from_ad1();
}

/*
 * Offsets for functions 22..24 by range (0..3) and sign quadrant
 * [ref >= 0, reading >= 0], [ref < 0, reading >= 0], [ref >= 0, reading < 0],
 * [ref < 0, reading < 0].
 */
static const int16_t sign_offset_tab[3][4][4] = {
    { { 200, 200, -200, -300 }, { 2100, 2100, -2100, -2100 },
      { 130, 130, -170, -170 }, { 2110, 2110, -2130, -2140 } },
    { { 130, 132, -71, -70 }, { 1583, 1605, -1548, -1529 },
      { 82, 83, -120, -120 }, { 1537, 1566, -1606, -1578 } },
    { { 128, 131, -65, -65 }, { 1559, 1581, -1517, -1499 },
      { 5, 12, -16, -18 }, { 156, 160, -161, -159 } },
};

#define SIGN_OFFSET_DEADBAND 2500

/*
 * orig: 0x080087F8
 * Functions 22..24 outside calibration, with PF8 high and g_flag_7fe0 set:
 * an offset chosen by range and the signs of `ref` and `reading`, for
 * |reading| > 2500 (else 0). Ranges above 3 give 0.
 */
int32_t sign_offset(int32_t ref, int32_t reading)
{
    if (g_flag_808a != 0) {
        return 0;
    }
    if (!(g_function == 22 || g_function == 23 || g_function == 24)) {
        return 0;
    }
    if ((uint8_t)(g_pf8_detected_low | (g_flag_7fe0 ^ 1u)) != 0) {
        return 0;
    }
    if (reading <= SIGN_OFFSET_DEADBAND && reading >= -SIGN_OFFSET_DEADBAND) {
        return 0;
    }
    if (g_range > 3) {
        return 0;
    }
    return sign_offset_tab[g_function - 22][g_range][(ref < 0) | ((reading < 0) << 1)];
}

/*
 * orig: 0x08008138 - functions 17, 19, 21 (main phase) and 22..24
 * Range 0 also removes the extra offset loaded for functions 16..21.
 */
void ad1_process_func17(void)
{
    if (g_ad1_average_ready != 0) {
        derived_from_average();
        g_derived_value = moving_average_b(g_derived_value, 10, 10);
        g_derived_value = sign_offset(g_ref_7d7c, g_derived_value) + g_derived_value;
        if (g_range == 0) {
            g_derived_value = g_derived_value - g_cal_extra_offset;
        }
        g_new_reading = 1;
    }
    scaled_from_ad1();
    if ((g_function == 16 || g_function == 18 || g_function == 20
         || g_function == 17 || g_function == 19 || g_function == 21) && g_mode_8037 == 10) {
        g_disp_flag_8062 = 1;
    }
}

/*
 * VA functions alternate between the main measurement and a borrowed
 * function (g_flag_80c4). The first `readings` new readings of a phase are
 * discarded; the next one switches phase. Leaving the borrowed phase
 * smooths its reading into g_va_aux, and g_flag_80a7 records whether that
 * exceeds 5000.
 */
static void va_phase_step(uint8_t readings, void (*reconfigure)(uint8_t borrow))
{
    if (g_new_reading == 0) {
        return;
    }
    g_flag_80c5++;
    if (g_flag_80c5 <= readings) {
        g_scaled_ready = 0;
        g_new_reading = 0;
        return;
    }
    if (g_flag_80c4 != 0) {
        g_va_aux = smooth3(g_va_aux);
        g_flag_80a7 = 0;
        if (g_va_aux > 5000) {
            g_flag_80a7 = 1;
        }
        g_scaled_ready = 0;
        g_new_reading = 0;
        if (g_mode_8037 == 11) {
            g_disp_flag_8062 = 1;
        }
    }
    g_flag_80c5 = 0;
    g_flag_80c4 ^= 1u;
    reconfigure(g_flag_80c4 != 0);
}

/*
 * orig: 0x08007FA8 - functions 17, 19, 21
 * With g_flag_80a5 set (outside calibration) alternates between the main
 * measurement and function 1 (borrowed), switching on every 4th reading.
 */
void ad1_process_func17_va(void)
{
    if ((uint8_t)(g_flag_808a | (g_flag_80a5 ^ 1u)) != 0) {
        ad1_process_func17();
        return;
    }
    if (g_flag_80c4 != 0) {
        ad1_process_func1();
        if (g_ad1_average_ready != 0) {
            g_va_aux = g_derived_value;
        }
    } else {
        ad1_process_func17();
    }
    va_phase_step(3, reconfigure_borrow_f1);
}

static int dual_gain_function(void)
{
    return g_function == 13 || g_function == 14 || g_function == 15;
}

static int va_function(void)
{
    return g_function == 16 || g_function == 18 || g_function == 20
        || g_function == 17 || g_function == 19 || g_function == 21;
}

/* Zero offset (clipped at 0), gain, optional frequency response, trims. */
static int32_t corrected_average(int use_ac_response)
{
    int32_t v = subtract_zero_offset(g_ad1_average);

    if (v < 0) {
        v = 0;
    }
    g_derived_value = v;
    g_derived_value = apply_gain(g_derived_value);
    if (use_ac_response) {
        g_derived_value = ac_response_correct(g_derived_value);
    }
    g_derived_value = trim_correct_reading(g_function, g_derived_value);
    return g_derived_value;
}

/*
 * The averaging / calibration branch shared by functions 2, 4 and 16..21:
 * returns 1 when it produced a new corrected reading in g_derived_value.
 */
static int average_or_calibrate(int use_ac_response)
{
    if (g_avg_flag_808c != 0) {
        g_avg16_result = average16(g_ad1_average);
        return 0;
    }
    if (g_cal_flag_808e != 0 && !dual_gain_function()) {
        calibrate_zero_and_gain(g_ad1_average, g_avg16_result);
        return 0;
    }
    corrected_average(use_ac_response);
    return 1;
}

/* orig: 0x08006A9E - functions 13..15 within the function 2/4 handlers */
void ad1_process_dual_ac(void)
{
    derived_from_average();
    if (g_derived_value < 0) {
        g_derived_value = 0;
    }
    g_derived_value = moving_average(g_derived_value, 20, 10);
    func2_reading_done();
}

/* orig: 0x080069B4 - function 4 */
void ad1_process_func4(void)
{
    if (g_ad1_average_ready != 0 && g_flag_80b7 == 0) {
        if (dual_gain_function()) {
            ad1_process_dual_ac();
        } else if (g_avg_flag_808c != 0) {
            g_avg16_result = average16(g_ad1_average);
        } else if (g_cal_flag_808e != 0) {
            calibrate_zero_and_gain(g_ad1_average, g_avg16_result);
        } else {
            corrected_average(1);
            g_derived_value = moving_average(g_derived_value, 20, 10);
            cal_trim_requests();
            g_new_reading = 1;
        }
    }
    scale_reading(g_ad1_value);
    g_scaled_value = trim_correct_reading(g_function, g_scaled_value);
}

/* orig: 0x08006AEC - function 2 (AC with the RMS converter off) */
void ad1_process_func2(void)
{
    if (g_ad1_average_ready != 0 && g_flag_80b7 == 0) {
        if (dual_gain_function()) {
            ad1_process_dual_ac();
        } else if (average_or_calibrate(1)) {
            g_derived_value = moving_average(g_derived_value, 20, 10);
            cal_trim_requests();
            func2_reading_done();
        }
        holdoff_on_low_reading();
    }
    if (dual_gain_function() || va_function()) {
        return;
    }
    if (g_function == 2 && g_flag_80ba != 0) {
        return;
    }
    if (g_function == 0 && g_setting_239 != 5) {
        return;
    }
    scale_reading(g_ad1_value);
    g_scaled_value = trim_correct_reading(g_function, g_scaled_value);
    if ((g_function == 2 || g_function == 4) && g_flag_80b9 == 1) {
        g_scaled_value = ac_response_correct((int32_t)((uint32_t)g_scaled_value * 10u));
    }
}

/* orig: 0x080067D6 - functions 16, 18, 20 (main phase) */
void ad1_process_func16(void)
{
    if (g_ad1_average_ready != 0) {
        if (dual_gain_function()) {
            derived_from_average_positive();
        } else {
            average_or_calibrate(0);
            if (g_range == 0) {
                g_derived_value = (int32_t)((uint32_t)g_derived_value - (uint32_t)g_cal_extra_offset);
                if (g_derived_value < 0) {
                    g_derived_value = 0;
                }
            }
        }
        g_derived_value = moving_average(g_derived_value, 20, 10);
        cal_trim_requests();
        if ((g_function == 16 || g_function == 18 || g_function == 20)
            && g_range == 0 && g_derived_value < 31) {
            g_derived_value = 0;
        }
        g_new_reading = 1;
        holdoff_on_low_reading();
    }
    if (!dual_gain_function()) {
        scale_reading(g_ad1_value);
        g_scaled_value = trim_correct_reading(g_function, g_scaled_value);
    }
    if (va_function() && g_mode_8037 == 10) {
        g_disp_flag_8062 = 1;
    }
}

/*
 * orig: 0x0800664A - functions 16, 18, 20
 * As ad1_process_func17_va() but borrowing function 2, switching on every
 * 7th reading.
 */
void ad1_process_func16_va(void)
{
    if ((uint8_t)(g_flag_808a | (g_flag_80a5 ^ 1u)) != 0) {
        ad1_process_func16();
        return;
    }
    if (g_flag_80c4 != 0) {
        ad1_process_func2();
        if (g_ad1_average_ready != 0) {
            g_va_aux = g_derived_value;
        }
    } else {
        ad1_process_func16();
    }
    va_phase_step(6, reconfigure_borrow_f2);
}
