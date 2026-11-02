/*
 * Functions of the original image that have not been reconstructed yet.
 * In the hybrid test build these resolve to their original addresses; each
 * entry disappears from here once its C version exists.
 */
#ifndef ORIG_CALLS_H
#define ORIG_CALLS_H

#include <stdint.h>

void FUN_08014c3c(uint8_t code);    /* short-press key handler */
void FUN_0801622c(uint8_t code);    /* long-press key handler */

/* HY3131 result handlers (measurement processing) */

#endif /* ORIG_CALLS_H */
