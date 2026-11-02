/*
 * Buzzer (PE7) and auto power-off timer reset.
 *
 * The buzzer is switched on here and timed out elsewhere (g_buzzer_ticks
 * counts down in the timer interrupt; g_beep_count repeats).
 */
#ifndef BUZZER_H
#define BUZZER_H

#include <stdint.h>

#define APO_SECONDS_NORMAL 1800u   /* 30 min */
#define APO_SECONDS_LONG   3600u   /* 60 min */

extern uint16_t g_buzzer_ticks;        /* 0x20007FB4 */
extern uint16_t g_beep_duration;       /* 0x20007FB6 */
extern uint8_t g_apo_long;             /* 0x2000804E  select the 60-minute auto power-off */
extern uint8_t g_beep_enabled;         /* 0x20008078 */
extern uint8_t g_buzzer_on;            /* 0x20008079 */
extern uint8_t g_beep_count;           /* 0x2000807A */
extern uint8_t g_apo_warning_active;   /* 0x200080E6  less than 60 s left */
extern uint8_t g_apo_warn_request;     /* 0x200080E8 */
extern uint8_t g_apo_warn_beeps;       /* 0x200080E9 */

void buzzer_start(uint16_t ticks);
void buzzer_start_forced(uint16_t ticks);
void apo_reset(void);
void beep(uint8_t reset_apo, uint8_t count, uint16_t duration);
void beep_short(void);
void beep_triple(void);

#endif /* BUZZER_H */
