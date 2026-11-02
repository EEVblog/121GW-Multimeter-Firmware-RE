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

#endif /* ORIG_CALLS_H */
