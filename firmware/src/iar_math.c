/*
 * Maths functions reproduced bit-for-bit from the original IAR runtime.
 *
 * log10 is not required to be correctly rounded, so a different library
 * could give different last bits. This is IAR's algorithm (log core at
 * 0x080204C4, mode 1) with every double operation in the same order; each
 * basic operation is IEEE-exact, so the result matches the original exactly.
 */
#include "iar_math.h"

typedef union {
    double d;
    uint64_t u;
} dbits;

static double from_bits(uint64_t u)
{
    dbits v;
    v.u = u;
    return v.d;
}

static uint64_t to_bits(double d)
{
    dbits v;
    v.d = d;
    return v.u;
}

#define EXP_SHIFT 52
#define EXP_MASK  0x7FFull
#define FRAC_MASK 0x000FFFFFFFFFFFFFull
#define SIGN_BIT  0x8000000000000000ull

/*
 * orig: 0x0801BB5C (log10 entry, mode 1), 0x080204C4 (log10 body)
 * inlines: 0x08023EE8 (subnormal normalisation), 0x08023F2C (double compare flags)
 */
double iar_log10(double x)
{
    uint64_t bits = to_bits(x);
    uint32_t exponent = (uint32_t)((bits >> EXP_SHIFT) & EXP_MASK);
    uint64_t frac = bits & FRAC_MASK;
    int32_t e;

    if (exponent == EXP_MASK) {
        if (frac != 0 || !(bits & SIGN_BIT)) {
            return x;                       /* NaN or +infinity */
        }
        g_errno = IAR_EDOM;                 /* -infinity */
        return from_bits(0x7FFFFFFFFFFFFFFFull);
    }

    if (exponent == 0) {
        if (frac == 0) {                    /* +-0 */
            g_errno = IAR_ERANGE;
            return from_bits(0xFFF0000000000000ull);
        }
        /* Subnormal: shift the fraction up to the hidden-bit position,
         * counting the biased exponent down from 1 (as 0x08023EE8). */
        int32_t biased = 1;
        while ((frac & (1ull << EXP_SHIFT)) == 0) {
            frac <<= 1;
            biased--;
        }
        frac &= FRAC_MASK;
        exponent = (uint32_t)biased;
    }

    /* m = mantissa in [0.5, 1) with the original sign; e = exponent */
    e = (int32_t)exponent - 0x3FE;
    double m = from_bits((bits & SIGN_BIT) | (0x3FEull << EXP_SHIFT) | frac);
    if (bits & SIGN_BIT) {
        g_errno = IAR_EDOM;
        return from_bits(0x7FFFFFFFFFFFFFFFull);
    }

    if (m < from_bits(0x3FE6A09E667F3BCDull)) {   /* sqrt(1/2) */
        m = 2.0 * m;
        e -= 1;
    }

    double f = -1.0 + m;
    double s = f / (1.0 + m);
    double z = s * s;
    double p = from_bits(0x3FC2F8D8BF29DEF7ull) * z;
    p = p + from_bits(0x3FC3991ACCFEB3F4ull);
    p = p * z;
    p = p + from_bits(0x3FC7466FD968CB62ull);
    p = p * z;
    p = p + from_bits(0x3FCC71C4DA12C9C6ull);
    p = p * z;
    p = p + from_bits(0x3FD24924948AE94Aull);
    p = p * z;
    p = p + from_bits(0x3FD9999999975FF7ull);
    p = p * z;
    p = p + from_bits(0x3FE55555555555BCull);
    double q = p * z;
    double t = (f - q) * s;
    double ed = (double)(int16_t)e;

    double a = from_bits(0x3FDBCB7B00000000ull) * f;      /* log10(e), high part */
    double b = from_bits(0x3E5526E50E32A6ABull) * f;      /* log10(e), low part */
    double c = from_bits(0x3E7427DE7FBCC47Cull) * ed;     /* log10(2), low part */
    double d = t * from_bits(0x3FDBCB7B1526E50Eull);      /* log10(e) */
    double r = c - d;
    r = r + b;
    r = r + a;
    return from_bits(0x3FD3441300000000ull) * ed + r;     /* log10(2), high part */
}
