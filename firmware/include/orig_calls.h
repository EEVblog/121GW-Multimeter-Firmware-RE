/*
 * Functions of the original image that have not been reconstructed yet.
 * In the hybrid test build these resolve to their original addresses; each
 * entry disappears from here once its C version exists.
 */
#ifndef ORIG_CALLS_H
#define ORIG_CALLS_H

#include <stdint.h>

void FUN_08014ae6(uint8_t code);    /* input event handler */
void FUN_08014c3c(uint8_t code);    /* short-press key handler */
void FUN_0801622c(uint8_t code);    /* long-press key handler */

/* HY3131 result handlers (measurement processing) */
void hy3131_on_ad1_ready(void);      /* 0x080061E4 */
void hy3131_on_rms_ready(void);      /* 0x08008208 */
void hy3131_on_peak_hold(void);      /* 0x0800A7FC */
void hy3131_on_counter_ready(void);  /* 0x0800AA00 */
void FUN_0800b540(void);             /* polled in function 12, ranges 0..4 */

#endif /* ORIG_CALLS_H */
