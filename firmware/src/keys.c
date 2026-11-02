/*
 * Front-panel keys and rotary switch.
 */
#include "keys.h"
#include "app_globals.h"
#include "orig_calls.h"
#include "stm32l1xx.h"

#define KEY_STABLE_SAMPLES     2u    /* identical samples before a change counts */
#define KEY_LONG_PRESS_TICKS   0x32u /* hold count that reports a long press */
#define KEY_LONG_PRESS_DONE    0x33u
#define KEY_LOCKOUT_AFTER_LONG 10u
#define KEY_LOCKOUT_AFTER_SHORT 10u
#define KEY_LOCKOUT_IMMEDIATE  5u
#define REPEAT_DELAY_NORMAL    0x32u
#define REPEAT_DELAY_MENU      3u
#define REPEAT_STEP_TICKS      0x1Eu
#define REPEAT_FAST_TICKS      10u
#define REPEAT_FAST_STAGE      6u

static int pg_low(unsigned pin)
{
    return (GPIOG->IDR & (1u << pin)) == 0;
}

/* orig: 0x08016BCA - PG8..PG15, one IDR read per key */
void keys_read_raw(void)
{
    g_keys_raw = (uint8_t)pg_low(8);
    for (unsigned n = 1; n < 8; n++) {
        if (pg_low(8 + n)) {
            g_keys_raw |= (uint8_t)(1u << n);
        }
    }
}

/* orig: 0x08014A14 - PG7..PG0 into bits 0..7 */
void rotary_read_raw(void)
{
    g_rotary_raw = (uint8_t)pg_low(7);
    for (unsigned n = 1; n < 8; n++) {
        if (pg_low(7 - n)) {
            g_rotary_raw |= (uint8_t)(1u << n);
        }
    }
}

/* orig: 0x08016B4C - single keys, the PG11+PG13 combination, and PG14/PG15 */
uint8_t key_can_long_press(uint8_t code)
{
    return code == KEY_PG8 || code == KEY_PG9 || code == KEY_PG10 || code == KEY_PG11
        || code == KEY_PG12 || code == KEY_COMBO_UPDATE || code == KEY_PG14
        || code == KEY_PG15;
}

/* orig: 0x08016B7C - any single key */
uint8_t key_is_single_key(uint8_t code)
{
    return code == KEY_PG8 || code == KEY_PG9 || code == KEY_PG10 || code == KEY_PG11
        || code == KEY_PG12 || code == KEY_PG13 || code == KEY_PG14 || code == KEY_PG15;
}

/* orig: 0x08016BB8 */
uint8_t key_is_update_combo(uint8_t code)
{
    return code == KEY_COMBO_UPDATE;
}

static void post_short_press(uint8_t code)
{
    g_key_short_pending = 1;
    g_key_short_code = code;
}

/*
 * orig: 0x08016A64
 * held == 0 resets the repeat state. While held: first repeat after 50 ticks
 * (3 in the menu), then five repeats every 30 ticks, then every 10 ticks.
 */
void key_autorepeat(uint8_t held)
{
    if (!held) {
        g_key_repeat_stage = 0;
        g_key_repeat_ticks = 0;
        return;
    }
    if (g_key_repeat_stage == 0) {
        uint8_t delay = g_menu_active ? REPEAT_DELAY_MENU : REPEAT_DELAY_NORMAL;
        g_key_repeat_ticks++;
        if (g_key_repeat_ticks > delay) {
            g_key_repeat_stage = 1;
            g_key_repeat_ticks = 0;
            post_short_press(g_keys_raw);
        }
        return;
    }

    uint32_t step = (uint32_t)g_key_repeat_stage - 1u;
    if (step < 5) {
        g_key_repeat_ticks++;
        if (g_key_repeat_ticks > REPEAT_STEP_TICKS) {
            g_key_repeat_stage++;
            g_key_repeat_ticks = 0;
            post_short_press(g_keys_raw);
        }
    } else if (step == 5) {
        g_key_repeat_ticks++;
        if (g_key_repeat_ticks > REPEAT_FAST_TICKS) {
            g_key_repeat_stage = REPEAT_FAST_STAGE;
            g_key_repeat_ticks = 0;
            post_short_press(g_keys_raw);
        }
    }
}

static int is_repeat_key(uint8_t code)
{
    return code == KEY_PG14 || code == KEY_PG15;
}

/* orig: 0x0801688C - called every scan tick */
void key_scan(void)
{
    if (g_key_lockout_ticks != 0) {
        g_key_lockout_ticks--;
    }

    keys_read_raw();

    if (g_keys_raw != g_key_prev_sample) {
        g_key_prev_sample = g_keys_raw;
        g_key_stable_count = 0;
        return;
    }
    g_key_stable_count++;
    if (g_key_stable_count < KEY_STABLE_SAMPLES) {
        return;
    }

    if (g_keys_raw == 0) {
        /* all released: report a press that was waiting for release */
        g_key_hold_ticks = 0;
        g_key_stable_count = KEY_STABLE_SAMPLES;
        if (g_key_release_armed == 1) {
            g_key_release_armed = 0;
            g_key_lockout_ticks = KEY_LOCKOUT_AFTER_SHORT;
            post_short_press(g_key_armed_code);
        }
        return;
    }

    if (!key_is_single_key(g_keys_raw) && !key_is_update_combo(g_keys_raw)) {
        return;
    }

    if (g_key_hold_ticks == 0) {
        if (g_key_lockout_ticks != 0) {
            return;
        }
        /* new press */
        g_key_stable_count = KEY_STABLE_SAMPLES;
        g_key_hold_ticks = 1;
        key_autorepeat(0);
        if (g_cfg_022e == 0 || g_flag_80ce != 0) {
            if (g_menu_active == 0 || !is_repeat_key(g_keys_raw)) {
                g_key_release_armed = 1;
                g_key_armed_code = g_keys_raw;
            }
        } else {
            g_key_release_armed = 0;
            g_key_lockout_ticks = KEY_LOCKOUT_IMMEDIATE;
            post_short_press(g_keys_raw);
        }
        return;
    }

    /* still held */
    g_key_hold_ticks++;
    if (g_key_hold_ticks == KEY_LONG_PRESS_TICKS) {
        g_key_hold_ticks = KEY_LONG_PRESS_DONE;
        g_key_release_armed = 0;
        if (key_can_long_press(g_keys_raw)) {
            g_key_long_pending = 1;
            g_key_long_code = g_keys_raw;
            g_key_lockout_ticks = KEY_LOCKOUT_AFTER_LONG;
        }
    } else if ((g_menu_active != 0 || g_flag_806e != 0) && is_repeat_key(g_keys_raw)) {
        g_key_hold_ticks = 1;
        key_autorepeat(1);
    }
}

/* orig: 0x0801682C */
void keys_dispatch_events(void)
{
    if (g_key_short_pending) {
        g_key_short_pending = 0;
        g_flag_801b = 0;
        g_flag_801d = 0;
        FUN_08014c3c(g_key_short_code);
    }
    if (g_key_long_pending) {
        g_key_long_pending = 0;
        g_flag_801b = 0;
        g_flag_801d = 0;
        FUN_0801622c(g_key_long_code);
    }
}

/* orig: 0x0801680C */
void keys_task(void)
{
    key_scan();
    keys_dispatch_events();
    if (g_input_event_pending) {
        FUN_08014ae6(g_input_event_code);
    }
}
