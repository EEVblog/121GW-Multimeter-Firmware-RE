/*
 * Analogue front-end switching: control lines set per measurement function.
 * (front_end_select(), which drives PD0/PF4/PF5/PC11/PD7, is in system_init.c.)
 */
#ifndef FRONT_END_H
#define FRONT_END_H

#include <stdint.h>

extern uint8_t g_flag_80b3;   /* 0x200080B3  alternate PE15/PF6 state for function 11 */

void front_end_pd1_set(uint8_t function, uint8_t unused);
void front_end_pe15_pf6(uint8_t function);
void front_end_pa11(uint8_t function);
void front_end_ph2(uint8_t function);
void front_end_pa12(uint8_t function);
void front_end_pe12(uint8_t function);
void front_end_pe5(uint8_t function, uint8_t range);
void front_end_configure(uint8_t function, uint8_t range);

#endif /* FRONT_END_H */
