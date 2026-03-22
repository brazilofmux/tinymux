/* Stub math.h for RV64 freestanding build. */
#ifndef _RV64_MATH_H
#define _RV64_MATH_H

double floor(double x);
double modf(double x, double *iptr);

/* sqrt via RV64D FSQRT.D instruction. */
static inline double sqrt(double x) {
    double r;
    __asm__ ("fsqrt.d %0, %1" : "=f"(r) : "f"(x));
    return r;
}

#define HUGE_VAL __builtin_huge_val()

#endif

