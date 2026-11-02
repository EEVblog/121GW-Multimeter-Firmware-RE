/*
 * Buzzer (PE7) and auto power-off timer reset.
 */
#include "buzzer.h"
#include "app_globals.h"
#include "board_io.h"

#define BUZZER_PORT GPIOE
#define BUZZER_PIN  7u

/* orig: 0x0800EEA8 - sounds only if beeping is enabled */
void buzzer_start(uint16_t ticks)
{
    g_buzzer_on = 1;
    g_buzzer_ticks = ticks;
    if (g_beep_enabled != 0) {
        gpio_set(BUZZER_PORT, BUZZER_PIN);
    }
}

/* orig: 0x0800EED0 - sounds regardless of the beep setting */
void buzzer_start_forced(uint16_t ticks)
{
    g_buzzer_on = 1;
    g_buzzer_ticks = ticks;
    gpio_set(BUZZER_PORT, BUZZER_PIN);
}

/* orig: 0x0800ED26 */
void apo_reset(void)
{
    g_apo_seconds_left = (g_minmax_mode == 0) ? APO_SECONDS_NORMAL : APO_SECONDS_LONG;
    g_apo_warn_request = 0;
    g_apo_warn_beeps = 0;
    g_apo_warning_active = 0;
}

/* orig: 0x0800EE7A */
void beep(uint8_t reset_apo, uint8_t count, uint16_t duration)
{
    if (reset_apo == 1) {
        apo_reset();
    }
    g_beep_duration = duration;
    g_beep_count = count;
    buzzer_start(duration);
}

/* orig: 0x0800EE50 */
void beep_short(void)
{
    beep(1, 1, 100);
}

/* orig: 0x0800EE6C */
void beep_triple(void)
{
    beep(1, 3, 50);
}

/* orig: 0x0800EE5E */
void beep_long(void)
{
    beep(1, 1, 200);
}
