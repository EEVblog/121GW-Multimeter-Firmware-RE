/*
 * Secondary-display unit annunciators.
 */
#ifndef ANNUNCIATORS_H
#define ANNUNCIATORS_H

#include <stdint.h>

extern uint8_t g_flag_80c2;   /* 0x200080C2  selects the dual-readout symbol set */

void annunciator_temp_unit(void);
void annunciator_dual(void);
void annunciator_sub_clear(void);
void annunciators_secondary(void);

#endif /* ANNUNCIATORS_H */
