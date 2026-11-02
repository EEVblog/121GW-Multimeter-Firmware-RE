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

/* IAR's soft-float returns the default NaN 0xFFC00000 (negative) for an
 * invalid operation such as 0/0, where libgcc returns a positive NaN. For
 * operands that are not NaN this gives IAR's result. */
static inline float iar_fdiv(float a, float b)
{
    float q = a / b;

    if (q != q) {
        union { uint32_t u; float f; } v = { 0xFFC00000u };
        q = v.f;
    }
    return q;
}

/* IAR float -> unsigned: truncates; negatives (and negative NaN) give 0,
 * values >= 2^32 (and positive NaN) give 0xFFFFFFFF. */
static inline uint32_t iar_f2u(float f)
{
    if (f != f) {
        union { float f; uint32_t u; } v = { f };
        return (v.u >> 31) ? 0u : UINT32_MAX;
    }
    if (f <= 0.0f) {
        return 0u;
    }
    if (f >= 4294967296.0f) {
        return UINT32_MAX;
    }
    return (uint32_t)f;
}

/* Cortex-M3 UDIV: division by zero gives 0. */
static inline uint32_t m3_udiv(uint32_t a, uint32_t b)
{
    return b != 0 ? a / b : 0u;
}

/* IAR 64-bit divides (__aeabi_uldivmod / __aeabi_ldivmod): division by zero
 * returns the dividend unchanged (checked in emulation). */
static inline uint64_t iar_uldiv(uint64_t a, uint64_t b)
{
    return b != 0 ? a / b : a;
}

static inline int64_t iar_ldiv(int64_t a, int64_t b)
{
    return b != 0 ? a / b : a;
}

#endif /* NUMERIC_H */
