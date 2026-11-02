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
void hy3131_on_peak_hold(void);      /* 0x0800A7FC */
void hy3131_on_counter_ready(void);  /* 0x0800AA00 */
void FUN_0800b540(void);             /* polled in function 12, ranges 0..4 */

/* AD1 per-function processing */
void FUN_08006aec(void);
void FUN_08007714(void);
void FUN_080084e0(void);   /* functions 22-24 */
void FUN_08008b8c(void);   /* functions 13-15 */
void FUN_080077f0(void);   /* function 3 */
void FUN_080069b4(void);   /* function 4 */
void FUN_08007840(void);   /* function 5 */
void FUN_080079c4(void);   /* function 9 */
void FUN_080078b8(void);   /* function 10 */
void FUN_08007e84(void);   /* function 11 */
void FUN_0800664a(void);   /* functions 16, 18, 20 */
void FUN_08007fa8(void);   /* functions 17, 19, 21 */

#endif /* ORIG_CALLS_H */
