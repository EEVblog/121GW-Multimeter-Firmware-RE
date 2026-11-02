/*
 * Application global state.
 *
 * Every global is listed with its address in the original v2.05 image. In
 * the hybrid test build these declarations resolve to those addresses (see
 * tools/hybrid_syms.py and ghidra/app_names.tsv), so rebuilt functions share
 * RAM with the original firmware. Names marked "role unknown" keep their
 * address in the name until their purpose is established.
 */
#ifndef APP_GLOBALS_H
#define APP_GLOBALS_H

#include <stdint.h>

/* ---- settings (initialised data, 0x20000000..0x2000023D) ---------------- */
extern uint16_t g_log_interval_s;       /* 0x2000022C  default 1 */
extern volatile uint16_t g_cfg_022e;    /* 0x2000022E  1 kHz countdown (TIM3): start-up screen and its key actions */
extern uint16_t g_apo_seconds_left;     /* 0x20000230  default 1800 (30 min) */

/* ---- keys and rotary switch ---------------------------------------------- */
extern volatile uint8_t g_rotary_raw;   /* 0x20008066 */
extern uint8_t g_key_short_pending;     /* 0x20008067 */
extern uint8_t g_key_long_pending;      /* 0x20008068 */
extern uint8_t g_input_event_pending;   /* 0x20008069  set elsewhere; role unknown */
extern volatile uint8_t g_keys_raw;     /* 0x2000806A */
extern uint8_t g_key_short_code;        /* 0x2000806B */
extern uint8_t g_key_long_code;         /* 0x2000806C */
extern uint8_t g_input_event_code;      /* 0x2000806D */
extern uint8_t g_key_hold_ticks;        /* 0x20008072 */
extern uint8_t g_key_lockout_ticks;     /* 0x20008073 */
extern uint8_t g_key_release_armed;     /* 0x200080EE */
extern uint8_t g_key_stable_count;      /* 0x200080EF */
extern uint8_t g_key_prev_sample;       /* 0x200080F0 */
extern uint8_t g_key_armed_code;        /* 0x200080F1 */
extern uint8_t g_key_repeat_stage;      /* 0x200080F2 */
extern uint8_t g_key_repeat_ticks;      /* 0x200080F3 */

/* ---- flags whose role is not yet established ----------------------------- */
extern uint8_t g_menu_active;           /* 0x20007FE4  tentative: setup menu open */
extern uint8_t g_flag_801b;             /* 0x2000801B  cleared on key events */
extern uint8_t g_flag_801d;             /* 0x2000801D  cleared on key events */
extern uint8_t g_flag_806e;             /* 0x2000806E  enables auto-repeat */
extern uint8_t g_flag_80ce;             /* 0x200080CE */
extern uint8_t g_flag_7fe3;             /* 0x20007FE3 */
extern uint8_t g_flag_7fe7;             /* 0x20007FE7  menu copy of g_flag_80b9 */
extern uint8_t g_flag_7fe8;             /* 0x20007FE8  menu copy of g_flag_80b7 */
extern uint8_t g_ble_enabled;             /* 0x2000802B */
extern uint8_t g_apo_disabled;          /* 0x20008039  auto power-off disabled (setup) */
extern uint8_t g_flag_803b;             /* 0x2000803B */
extern uint8_t g_dp_position;           /* 0x20008040  decimal point of the number being drawn */
extern uint8_t g_flag_804d;             /* 0x2000804D  symbol state 0..2 */
extern uint8_t g_hold_active;           /* 0x2000804F  HOLD */
extern uint8_t g_rel_active;            /* 0x20008058  REL */
extern uint8_t g_flag_80b1;             /* 0x200080B1 */
extern uint8_t g_flag_8061;             /* 0x20008061  set by main loop, cleared by sym_range_marks_update() */
extern uint8_t g_flag_7fe9;             /* 0x20007FE9  menu copy of g_flag_80ba */
extern uint8_t g_function_7feb;         /* 0x20007FEB  menu copy of g_function */

#endif /* APP_GLOBALS_H */
