/*
 * Numeric conversions with the exact behaviour of the original IAR runtime.
 *
 * IAR's float/double -> int conversions truncate towards zero and saturate:
 * values >= 2^31 give 0x7FFFFFFF, values <= -2^31 give 0x80000000, and NaN
 * saturates according to its sign bit. (C leaves out-of-range conversions
 * undefined, and libgcc returns 0 for NaN.)
 */
#ifndef NUMERIC_H
#define NUMERIC_H

#include <stdint.h>

static inline int32_t iar_d2i(double d)
{
    if (d != d) {   /* NaN */
        union { double d; uint64_t u; } v = { d };
        return (v.u >> 63) ? INT32_MIN : INT32_MAX;
    }
    if (d >= 2147483648.0) {
        return INT32_MAX;
    }
    if (d <= -2147483648.0) {
        return INT32_MIN;
    }
    return (int32_t)d;
}

static inline int32_t iar_f2i(float f)
{
    if (f != f) {
        union { float f; uint32_t u; } v = { f };
        return (v.u >> 31) ? INT32_MIN : INT32_MAX;
    }
    if (f >= 2147483648.0f) {
        return INT32_MAX;
    }
    if (f <= -2147483648.0f) {
        return INT32_MIN;
    }
    return (int32_t)f;
}

#endif /* NUMERIC_H */
