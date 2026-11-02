/*
 * Display: periodic update of both readouts, the bar graph and the symbols.
 */
#ifndef DISPLAY_UPDATE_H
#define DISPLAY_UPDATE_H

#include <stdint.h>
#include "app_globals.h"

extern uint8_t g_flag_8099;          /* 0x20008099  "show" argument of sym_function_units() */
extern uint8_t g_flag_805f;          /* 0x2000805F  redraw the main readout */
extern uint8_t g_count_805a;         /* 0x2000805A  function 9 readings to skip */
extern uint8_t g_flag_805c;          /* 0x2000805C  blank the main readout (not function 0) */
extern uint8_t g_flag_8091;          /* 0x20008091  main shows "-4-nn" */
extern uint8_t g_flag_8092;          /* 0x20008092  main shows "400-E" once */
extern uint8_t g_flag_8093;          /* 0x20008093  main shows "-5-nn" */
extern uint8_t g_flag_8094;          /* 0x20008094  main shows "5KEND" once */
extern uint8_t g_flag_8063;          /* 0x20008063  force annunciators_secondary() */
extern uint8_t g_mode_8037_shown;    /* 0x20000236  g_mode_8037 the annunciators were drawn for */
extern uint8_t g_flag_806f;          /* 0x2000806F  edit blink: visible phase */
extern uint8_t g_count_8097;         /* 0x20008097  calibration: updates before " CAL " shows */
extern uint8_t g_flag_80a8;          /* 0x200080A8  dual functions: secondary shows the burden value */
extern uint8_t g_flag_80a9;          /* 0x200080A9  dual functions: "bd" setting in mode 11 */
extern uint8_t g_flag_80a6;          /* 0x200080A6  VA functions: "bd" setting in mode 10 */
extern int16_t g_ambient_temp;       /* 0x20007FCC  internal temperature, 0.01 C */
extern uint32_t g_log_count;         /* 0x20007D38  logged samples */
extern uint16_t g_value_7f9c;        /* 0x20007F9C  shown in mode 100 when not logging */
extern uint16_t g_log_interval_edit; /* 0x20007FA6  log interval being edited */
extern uint16_t g_bat_value;         /* 0x20007FBA  shown as "BATnn" */
extern uint8_t g_rtc_edit_dd;        /* 0x200080DD  clock values being edited */
extern uint8_t g_rtc_edit_de;        /* 0x200080DE */
extern uint8_t g_rtc_edit_df;        /* 0x200080DF */
extern uint8_t g_rtc_edit_e0;        /* 0x200080E0 */
extern uint8_t g_rtc_edit_e1;        /* 0x200080E1 */

void sym_status_update_all(void);
void display_redraw_symbols(void);
void main_display_update(void);
void secondary_display_update(void);
void display_update(void);

extern uint8_t g_recall_dp;          /* 0x20007FED  decimal point of the recalled reading */

void display_recall_symbols(void);

#endif /* DISPLAY_UPDATE_H */
