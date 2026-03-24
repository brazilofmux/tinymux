/* Stub math.h for RV64 freestanding build.
 *
 * These are stub declarations only.  The DBT intercepts calls to
 * these symbols and replaces them with native host libm intrinsics.
 * The RV64 bodies are never executed.
 */
#ifndef _RV64_MATH_H
#define _RV64_MATH_H

/* double → double */
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double exp(double x);
double log(double x);
double log10(double x);
double ceil(double x);
double floor(double x);
double fabs(double x);

/* (double, double) → double */
double pow(double x, double y);
double atan2(double y, double x);
double fmod(double x, double y);

/* modf has a pointer arg — handled separately if needed. */
double modf(double x, double *iptr);

/* sqrt via RV64D FSQRT.D instruction (native, not intrinsic). */
static inline double sqrt(double x) {
    double r;
    __asm__ ("fsqrt.d %0, %1" : "=f"(r) : "f"(x));
    return r;
}

#define HUGE_VAL __builtin_huge_val()

#endif

