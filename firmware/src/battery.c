/*
 * ADC task: alternately the thermistor (channel 4) and the battery
 * (channel 5), one conversion per call.
 *
 * The battery value is a 20-sample mean with the 5 highest and 5 lowest
 * samples dropped once the history is full, shown as "BATnn"; below 41 the
 * low-battery symbol follows after 10 consecutive readings.
 */
#include "battery.h"
#include "display_state.h"
#include "display_symbols.h"
#include "display_update.h"
#include "measure_funcs.h"
#include "numeric.h"
#include "power.h"
#include "temperature.h"
#include "stm32l1xx.h"

#define ADC_CH_THERMISTOR   4u
#define ADC_CH_BATTERY      5u
#define ADC_SAMPLE_384      7u        /* longest sample time */
#define RCC_APB2ENR_ADC1    (1u << 9)
#define BAT_HISTORY         20u
#define BAT_ADC_MIN         0x3FC
#define BAT_ADC_MAX         0xC30
#define BAT_LOW_LIMIT       41u

/* battery ADC reading per 0.5 step of the result, descending (orig: 0x08028C98; entry 0 is a sentinel) */
static const uint32_t bat_adc_table[18] = {
    100000, 0xC30, 0xBA4, 0xB18, 0xA8C, 0xA00, 0x974, 0x8E8, 0x85C,
    0x7D0, 0x744, 0x6B8, 0x62C, 0x5A0, 0x514, 0x488, 0x3FC, 0,
};

/*
 * orig: 0x080137B4 - one conversion of channel 5 (battery == 1) or 4
 * (thermistor); returns the 12-bit result
 * inlines: 0x0801FF9E (RCC_APB2PeriphClockCmd), 0x08020104 (ADC_Cmd),
 * 0x080202FA (ADC_GetFlagStatus), 0x0802012C (ADC_RegularChannelConfig, rank 1),
 * 0x080202EA (ADC_SoftwareStartConv), 0x080202F4 (ADC_GetConversionValue)
 */
uint16_t adc_read(uint8_t battery)
{
    uint32_t channel = (battery == 1) ? ADC_CH_BATTERY : ADC_CH_THERMISTOR;

    RCC->APB2ENR |= RCC_APB2ENR_ADC1;
    ADC1->CR2 |= 1u;
    while ((ADC1->SR & ADC_SR_ADONS) == 0) {
    }
    ADC1->SMPR3 = (ADC1->SMPR3 & ~(7u << (channel * 3u))) | (ADC_SAMPLE_384 << (channel * 3u));
    ADC1->SQR5 = (ADC1->SQR5 & ~0x1Fu) | channel;
    ADC1->CR2 |= ADC_CR2_SWSTART;
    while ((ADC1->SR & ADC_SR_EOC) == 0) {
    }
    return (uint16_t)ADC1->DR;
}

/* orig: 0x0801368A - copy of the history sorted from highest to lowest */
void bat_sort_history(void)
{
    uint32_t sorted[BAT_HISTORY];
    uint32_t pos = 0;

    for (uint8_t i = 0; i < BAT_HISTORY; i++) {
        g_bat_sorted[i] = g_bat_history[i];
    }
    for (uint8_t n = 0; n < BAT_HISTORY; n++) {
        uint32_t max = 0;

        for (uint8_t i = 0; i < BAT_HISTORY; i++) {
            if (max <= g_bat_sorted[i]) {
                pos = i;
                max = g_bat_sorted[pos];
            }
        }
        g_bat_sorted[pos] = 0;
        sorted[n] = max;
    }
    for (uint8_t i = 0; i < BAT_HISTORY; i++) {
        g_bat_sorted[i] = sorted[i];
    }
}

/* orig: 0x080135B0 - add a sample; mean of all samples, or the middle 10 of 20 */
uint32_t bat_filter(uint32_t sample)
{
    uint32_t sum = 0;

    if (g_bat_count == BAT_HISTORY) {
        for (uint32_t i = 1; i < g_bat_count; i++) {
            g_bat_history[g_bat_count - i] = g_bat_history[g_bat_count - 1 - i];
        }
        g_bat_history[0] = sample;
        bat_sort_history();
        for (uint32_t i = 5; i < g_bat_count - 5u; i++) {
            sum += g_bat_sorted[i];
        }
        sum = m3_udiv(sum, g_bat_count - 10u);
    } else {
        g_bat_history[g_bat_count] = sample;
        for (uint32_t i = 0; i <= g_bat_count; i++) {
            sum += g_bat_history[i];
        }
        sum = m3_udiv(sum, g_bat_count + 1u);
    }
    g_bat_count++;
    if (g_bat_count > BAT_HISTORY) {
        g_bat_count = BAT_HISTORY;
    }
    return sum;
}

/* orig: 0x08013728 - battery ADC reading (0x3FC..0xC30) to 50 per table step, 1050 at the top */
uint16_t bat_adc_to_value(uint32_t adc)
{
    const uint32_t *t = &bat_adc_table[1];
    uint8_t i = 0;
    int32_t fraction;

    while (adc < t[i]) {
        i++;
    }
    fraction = iar_f2i(iar_fdiv((float)((adc - t[i]) * 50u), (float)(t[i - 1] - t[i])));
    return (uint16_t)(1050 - i * 50 + fraction);
}

/* orig: 0x080134DA - battery: read, filter, low-battery symbol after 10 readings below 41 */
void battery_measure(void)
{
    uint16_t adc;

    g_adc_raw = adc_read(1);
    adc_off();
    adc = g_adc_raw;
    if (g_adc_raw < BAT_ADC_MIN) {
        adc = BAT_ADC_MIN;
    }
    if (g_adc_raw > BAT_ADC_MAX) {
        adc = BAT_ADC_MAX;
    }
    g_bat_value = (uint16_t)(bat_filter(bat_adc_to_value(adc)) / 10u);
    g_bat_low_now = g_bat_value < BAT_LOW_LIMIT;
    if (g_mode_8037 == 1) {
        g_disp_flag_8062 = 1;
    }
    if (g_bat_low_now == g_flag_803b) {
        g_bat_low_count = 0;
    } else {
        g_bat_low_count++;
        if (g_bat_low_count > 10) {
            g_bat_low_count = 0;
            g_flag_803b = g_bat_low_now;
            sym_w7_b7_update(1);
        }
    }
}

/* orig: 0x0801381E - thermistor: read and process (readings of 0x8000 and up ignored) */
void thermistor_measure(void)
{
    g_adc_raw = adc_read(0);
    adc_off();
    if (g_adc_raw < 0x8000u) {
        thermistor_process(g_adc_raw);
        if (g_mode_8037 == 0) {
            g_disp_flag_8062 = 1;
        }
    }
}

/* orig: 0x0801349C */
void adc_task(void)
{
    g_adc_phase++;
    if (g_adc_phase > 1) {
        g_adc_phase = 0;
    }
    if (g_adc_phase == 0) {
        thermistor_measure();
    } else if (g_adc_phase == 1) {
        battery_measure();
    }
}
