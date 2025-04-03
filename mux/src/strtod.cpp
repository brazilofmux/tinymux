#include "autoconf.h"
#include "config.h"
#include "externs.h"

#if   defined(HAVE_FPU_CONTROL_H)
#include <fpu_control.h>
#elif defined(IEEEFP_H_USEABLE)
#include <ieeefp.h>
#elif defined(HAVE_FENV_H)
#include <fenv.h>
#endif

#if defined(WORDS_BIGENDIAN)
#define IEEE_MC68k
#elif defined(WORDS_LITTLEENDIAN)
#define IEEE_8087
#else
#error Must be either Big or Little Endian.
#endif

#if 0
#define Long INT32
typedef UINT32 ULong;

#ifndef Omit_Private_Memory
#ifndef PRIVATE_MEM
#define PRIVATE_MEM 2304
#endif
#define PRIVATE_mem ((PRIVATE_MEM+sizeof(double)-1)/sizeof(double))
static double private_mem[PRIVATE_mem], *pmem_next = private_mem;
#endif
#endif

#undef IEEE_Arith
#undef Avoid_Underflow
#ifdef IEEE_MC68k
#define IEEE_Arith
#endif
#ifdef IEEE_8087
#define IEEE_Arith
#endif

#ifdef IEEE_Arith
#ifndef NO_INFNAN_CHECK
#undef INFNAN_CHECK
#define INFNAN_CHECK
#endif
#else
#undef INFNAN_CHECK
#define NO_STRTOD_BIGCOMP
#endif

#include "dtoa.c"

#if defined(HAVE_FPU_CONTROL_H) \
 && defined(_FPU_GETCW) \
 && defined(_FPU_SETCW)

fpu_control_t maskoff = 0
#if defined(_FPU_EXTENDED)
    | _FPU_EXTENDED
#endif
#if defined(_FPU_SINGLE)
    | _FPU_SINGLE
#endif
    ;

fpu_control_t maskon = 0
#if defined(_FPU_DOUBLE)
    | _FPU_DOUBLE
#endif
    ;

fpu_control_t origcw;

void mux_FPInit(void)
{
    _FPU_GETCW(origcw);
}

void mux_FPSet(void)
{
    // Set double-precision.
    //
    fpu_control_t newcw;
    newcw = (origcw & ~maskoff) | maskon;
    _FPU_SETCW(newcw);
}

void mux_FPRestore(void)
{
    _FPU_SETCW(origcw);
}

#elif defined(IEEEFP_H_USEABLE)

fp_rnd_t   orig_rnd;
fp_prec_t orig_prec;

void mux_FPInit(void)
{
    orig_rnd  = fpgetround();
    orig_prec = fpgetprec();
}

void mux_FPSet(void)
{
    // Set double-precision.
    //
    fpsetprec(FP_PD);
}

void mux_FPRestore(void)
{
    fpsetprec(orig_prec);
}

#elif defined(WIN32) && !defined(_WIN64)

#if (_MSC_VER >= 1400)
static unsigned int cw;

void mux_FPInit(void)
{
    _controlfp_s(&cw, 0, 0);
}

void mux_FPSet(void)
{
    // Set double-precision.
    //
    _controlfp_s(&cw, _PC_53, _MCW_PC);
}

void mux_FPRestore(void)
{
    _controlfp_s(&cw, _CW_DEFAULT, MCW_PC);
}
#else // _MSC_VER
static unsigned origcw;

void mux_FPInit(void)
{
    origcw = _controlfp(0, 0);
}

void mux_FPSet(void)
{
    // Set double-precision.
    //
    _controlfp(_PC_53, _MCW_PC);
}

void mux_FPRestore(void)
{
    const unsigned int maskall = 0xFFFFFFFF;
    _controlfp(origcw, maskall);
}
#endif // _MSC_VER

#elif defined(HAVE_FENV_H) \
   && defined(HAVE_FESETPREC) \
   && defined(HAVE_FEGETPREC) \
   && defined(FE_DBLPREC)

int origcw;

void mux_FPInit(void)
{
    origcw = fegetprec();
}

void mux_FPSet(void)
{
    // Set double-precision.
    //
    fesetprec(FE_DBLPREC);
}

void mux_FPRestore(void)
{
    fesetprec(origcw);
}

#else

#if !defined(_WIN64) && !(defined(__APPLE__) && defined(__MACH__) && !defined(__i386__))
#warning "No method of floating-point control was found, using dummy functions"
#endif

void mux_FPInit(void)
{
}

void mux_FPSet(void)
{
}

void mux_FPRestore(void)
{
}

#endif

void FLOAT_Initialize(void)
{
    mux_FPInit();
    mux_FPSet();
}

double mux_strtod(const UTF8* s00, UTF8** se)
{
    return dtoa_strtod((const char*)s00, (char**)se);
}

double mux_ulp(double d)
{
    U x;
    dval(&x) = d;
    return ulp(&x);
}

UTF8* mux_dtoa(double d, int mode, int ndigits, int* decpt, int* sign, UTF8** rve)
{
    return (UTF8 *)dtoa(d, mode, ndigits, decpt, sign, (char **)rve);
}
