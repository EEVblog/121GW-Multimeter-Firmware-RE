/*
 * Maths functions reproduced bit-for-bit from the original IAR runtime.
 */
#ifndef IAR_MATH_H
#define IAR_MATH_H

#include <stdint.h>

#define IAR_EDOM   33
#define IAR_ERANGE 34

extern int32_t g_errno;   /* 0x20000228  IAR errno */

double iar_log10(double x);

#endif /* IAR_MATH_H */
