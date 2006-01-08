// funmath.cpp -- MUX math function handlers.
//
// $Id: funmath.cpp,v 1.8 2006-01-08 17:03:42 sdennis Exp $
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <float.h>
#include <limits.h>
#include <math.h>

#include "functions.h"
#include "funmath.h"
#include "sha1.h"

#ifdef HAVE_IEEE_FP_FORMAT

static const char *mux_FPStrings[] = { "+Inf", "-Inf", "Ind", "NaN", "0", "0", "0", "0" };

#define MUX_FPGROUP_PASS  0x00 // Pass-through to printf
#define MUX_FPGROUP_ZERO  0x10 // Force to be zero.
#define MUX_FPGROUP_PINF  0x20 // "+Inf"
#define MUX_FPGROUP_NINF  0x30 // "-Inf"
#define MUX_FPGROUP_IND   0x40 // "Ind"
#define MUX_FPGROUP_NAN   0x50 // "NaN"
#define MUX_FPGROUP(x) ((x) & 0xF0)

// mux_fpclass returns an integer that is one of the following:
//
#define MUX_FPCLASS_PINF  (MUX_FPGROUP_PINF|0) // Positive infinity (+INF)
#define MUX_FPCLASS_NINF  (MUX_FPGROUP_NINF|1) // Negative infinity (-INF)
#define MUX_FPCLASS_QNAN  (MUX_FPGROUP_IND |2) // Quiet NAN (Indefinite)
#define MUX_FPCLASS_SNAN  (MUX_FPGROUP_NAN |3) // Signaling NAN
#define MUX_FPCLASS_ND    (MUX_FPGROUP_ZERO|4) // Negative denormalized
#define MUX_FPCLASS_NZ    (MUX_FPGROUP_ZERO|5) // Negative zero (-0)
#define MUX_FPCLASS_PZ    (MUX_FPGROUP_ZERO|6) // Positive zero (+0)
#define MUX_FPCLASS_PD    (MUX_FPGROUP_ZERO|7) // Positive denormalized
#define MUX_FPCLASS_PN    (MUX_FPGROUP_PASS|8) // Positive normalized non-zero
#define MUX_FPCLASS_NN    (MUX_FPGROUP_PASS|9) // Negative normalized non-zero
#define MUX_FPCLASS(x)    ((x) & 0x0F)

#ifdef WIN32
#define IEEE_MASK_SIGN     0x8000000000000000ui64
#define IEEE_MASK_EXPONENT 0x7FF0000000000000ui64
#define IEEE_MASK_MANTISSA 0x000FFFFFFFFFFFFFui64
#define IEEE_MASK_QNAN     0x0008000000000000ui64
#else
#define IEEE_MASK_SIGN     0x8000000000000000ull
#define IEEE_MASK_EXPONENT 0x7FF0000000000000ull
#define IEEE_MASK_MANTISSA 0x000FFFFFFFFFFFFFull
#define IEEE_MASK_QNAN     0x0008000000000000ull
#endif

#define ARBITRARY_NUMBER 1
#define IEEE_MAKE_TABLESIZE 5
typedef union
{
    INT64  i64;
    double d;
} SpecialFloatUnion;

// We return a Quiet NAN when a Signalling NAN is requested because
// any operation on a Signalling NAN will result in a Quiet NAN anyway.
// MUX doesn't catch SIGFPE, but if it did, a Signalling NAN would
// generate a SIGFPE.
//
static SpecialFloatUnion SpecialFloatTable[IEEE_MAKE_TABLESIZE] =
{
    { 0 }, // Unused.
    { IEEE_MASK_EXPONENT | IEEE_MASK_QNAN | ARBITRARY_NUMBER },
    { IEEE_MASK_EXPONENT | IEEE_MASK_QNAN | ARBITRARY_NUMBER },
    { IEEE_MASK_EXPONENT },
    { IEEE_MASK_EXPONENT | IEEE_MASK_SIGN }
};

double MakeSpecialFloat(int iWhich)
{
    return SpecialFloatTable[iWhich].d;
}

static int mux_fpclass(double result)
{
    UINT64 i64;

    *((double *)&i64) = result;

    if ((i64 & IEEE_MASK_EXPONENT) == 0)
    {
        if (i64 & IEEE_MASK_MANTISSA)
        {
            if (i64 & IEEE_MASK_SIGN) return MUX_FPCLASS_ND;
            else                      return MUX_FPCLASS_PD;
        }
        else
        {
            if (i64 & IEEE_MASK_SIGN) return MUX_FPCLASS_NZ;
            else                      return MUX_FPCLASS_PZ;
        }
    }
    else if ((i64 & IEEE_MASK_EXPONENT) == IEEE_MASK_EXPONENT)
    {
        if (i64 & IEEE_MASK_MANTISSA)
        {
            if (i64 & IEEE_MASK_QNAN) return MUX_FPCLASS_QNAN;
            else                      return MUX_FPCLASS_SNAN;
        }
        else
        {
            if (i64 & IEEE_MASK_SIGN) return MUX_FPCLASS_NINF;
            else                      return MUX_FPCLASS_PINF;
        }
    }
    else
    {
        if (i64 & IEEE_MASK_SIGN)     return MUX_FPCLASS_NN;
        else                          return MUX_FPCLASS_PN;
    }
}
#endif // HAVE_IEEE_FP_FORMAT

static double AddWithError(double& err, double a, double b)
{
    double sum = a+b;
    err = b-(sum-a);
    return sum;
}

// Typically, we are within 1ulp of an exact answer, find the shortest answer
// within that 1 ulp (that is, within 0, +ulp, and -ulp).
//
static double NearestPretty(double R)
{
    char *rve = NULL;
    int decpt;
    int bNegative;
    const int mode = 0;

    double ulpR = ulp(R);
    double R0 = R-ulpR;
    double R1 = R+ulpR;

    // R.
    //
    char *p = mux_dtoa(R, mode, 50, &decpt, &bNegative, &rve);
    int nDigits = rve - p;

    // R-ulp(R)
    //
    p = mux_dtoa(R0, mode, 50, &decpt, &bNegative, &rve);
    if (rve - p < nDigits)
    {
        nDigits = rve - p;
        R  = R0;
    }

    // R+ulp(R)
    //
    p = mux_dtoa(R1, mode, 50, &decpt, &bNegative, &rve);
    if (rve - p < nDigits)
    {
        nDigits = rve - p;
        R = R1;
    }
    return R;
}

// Compare for decreasing order by absolute value.
//
static int DCL_CDECL f_comp_abs(const void *s1, const void *s2)
{
    double a = fabs(*(double *)s1);
    double b = fabs(*(double *)s2);

    if (a > b)
    {
        return -1;
    }
    else if (a < b)
    {
        return 1;
    }
    return 0;
}

// Double compensation method. Extended by Priest from Knuth and Kahan.
//
// Error of sum is less than 2*epsilon or 1 ulp except for very large n.
// Return the result that yields the shortest number of base-10 digits.
//
static double AddDoubles(int n, double pd[])
{
    qsort(pd, n, sizeof(double), f_comp_abs);
    double sum = 0.0;
    if (0 < n)
    {
        sum = pd[0];
        double sum_err = 0.0;
        int i;
        for (i = 1; i < n; i++)
        {
            double addend_err;
            double addend = AddWithError(addend_err, sum_err, pd[i]);
            double sum1_err;
            double sum1 = AddWithError(sum1_err, sum, addend);
            sum = AddWithError(sum_err, sum1, addend_err + sum1_err);
        }
    }
    return NearestPretty(sum);
}

/* ---------------------------------------------------------------------------
 * fval: copy the floating point value into a buffer and make it presentable
 */
static void fval(char *buff, char **bufc, double result)
{
    // Get double val into buffer.
    //
#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = mux_fpclass(result);
    if (MUX_FPGROUP(fpc) == MUX_FPGROUP_PASS)
    {
#endif // HAVE_IEEE_FP_FORMAT
        double rIntegerPart;
        double rFractionalPart = modf(result, &rIntegerPart);
        if (  0.0 == rFractionalPart
           && LONG_MIN <= rIntegerPart
           && rIntegerPart <= LONG_MAX)
        {
            long i = (long)rIntegerPart;
            safe_ltoa(i, buff, bufc);
        }
        else
        {
            safe_str(mux_ftoa(result, false, 0), buff, bufc);
        }
#ifdef HAVE_IEEE_FP_FORMAT
    }
    else
    {
        safe_str(mux_FPStrings[MUX_FPCLASS(fpc)], buff, bufc);
    }
#endif // HAVE_IEEE_FP_FORMAT
}

static const long nMaximums[10] =
{
    0, 9, 99, 999, 9999, 99999, 999999, 9999999, 99999999, 999999999
};

static double g_aDoubles[(LBUF_SIZE+1)/2];

FUNCTION(fun_add)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int i;
    for (i = 0; i < nfargs; i++)
    {
        int nDigits;
        long nMaxValue = 0;
        if (  !is_integer(fargs[i], &nDigits)
           || nDigits > 9
           || (nMaxValue += nMaximums[nDigits]) > 999999999L)
        {
            // Do it the slow way.
            //
            for (int j = 0; j < nfargs; j++)
            {
                g_aDoubles[j] = mux_atof(fargs[j]);
            }
            fval(buff, bufc, AddDoubles(nfargs, g_aDoubles));
            return;
        }
    }

    // We can do it the fast way.
    //
    long sum = 0;
    for (i = 0; i < nfargs; i++)
    {
        sum += mux_atol(fargs[i]);
    }
    safe_ltoa(sum, buff, bufc);
}

FUNCTION(fun_ladd)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int n = 0;
    if (0 < nfargs)
    {
        SEP sep;
        if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
        {
            return;
        }

        char *cp = trim_space_sep(fargs[0], &sep);
        while (cp)
        {
            char *curr = split_token(&cp, &sep);
            g_aDoubles[n++] = mux_atof(curr);
        }
    }
    fval(buff, bufc, AddDoubles(n, g_aDoubles));
}

/////////////////////////////////////////////////////////////////
// Function : iadd(Arg[0], Arg[1],..,Arg[n])
//
// Written by : Chris Rouse (Seraphim) 04/04/2000
/////////////////////////////////////////////////////////////////

FUNCTION(fun_iadd)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    INT64 sum = 0;
    for (int i = 0; i < nfargs; i++)
    {
        sum += mux_atoi64(fargs[i]);
    }
    safe_i64toa(sum, buff, bufc);
}

FUNCTION(fun_sub)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int nDigits;
    if (  is_integer(fargs[0], &nDigits)
       && nDigits <= 9
       && is_integer(fargs[1], &nDigits)
       && nDigits <= 9)
    {
        int iResult;
        long a = mux_atol(fargs[0]);
        long b = mux_atol(fargs[1]);
        iResult = a - b;
        safe_ltoa(iResult, buff, bufc);
    }
    else
    {
        g_aDoubles[0] = mux_atof(fargs[0]);
        g_aDoubles[1] = -mux_atof(fargs[1]);
        fval(buff, bufc, AddDoubles(2, g_aDoubles));
    }
}

/////////////////////////////////////////////////////////////////
// Function : isub(Arg[0], Arg[1])
//
// Written by : Chris Rouse (Seraphim) 04/04/2000
/////////////////////////////////////////////////////////////////

FUNCTION(fun_isub)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    INT64 a = mux_atoi64(fargs[0]);
    INT64 b = mux_atoi64(fargs[1]);
    INT64 diff = a - b;
    safe_i64toa(diff, buff, bufc);
}

FUNCTION(fun_mul)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double prod = 1.0;
    for (int i = 0; i < nfargs; i++)
    {
        prod *= mux_atof(fargs[i]);
    }
    fval(buff, bufc, NearestPretty(prod));
}

/////////////////////////////////////////////////////////////////
// Function : imul(Arg[0], Arg[1], ... , Arg[n])
//
// Written by : Chris Rouse (Seraphim) 04/04/2000
/////////////////////////////////////////////////////////////////

FUNCTION(fun_imul)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    INT64 prod = 1;
    for (int i = 0; i < nfargs; i++)
    {
        prod *= mux_atoi64(fargs[i]);
    }
    safe_i64toa(prod, buff, bufc);
}

FUNCTION(fun_gt)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool bResult = false;
    int nDigits;
    if (  is_integer(fargs[0], &nDigits)
       && nDigits <= 9
       && is_integer(fargs[1], &nDigits)
       && nDigits <= 9)
    {
        long a = mux_atol(fargs[0]);
        long b = mux_atol(fargs[1]);
        bResult = (a > b);
    }
    else
    {
        double a = mux_atof(fargs[0]);
        double b = mux_atof(fargs[1]);
        bResult = (a > b);
    }
    safe_bool(bResult, buff, bufc);
}

FUNCTION(fun_gte)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool bResult = false;
    int nDigits;
    if (  is_integer(fargs[0], &nDigits)
       && nDigits <= 9
       && is_integer(fargs[1], &nDigits)
       && nDigits <= 9)
    {
        long a = mux_atol(fargs[0]);
        long b = mux_atol(fargs[1]);
        bResult = (a >= b);
    }
    else
    {
        double a = mux_atof(fargs[0]);
        double b = mux_atof(fargs[1]);
        bResult = (a >= b);
    }
    safe_bool(bResult, buff, bufc);
}

FUNCTION(fun_lt)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool bResult = false;
    int nDigits;
    if (  is_integer(fargs[0], &nDigits)
       && nDigits <= 9
       && is_integer(fargs[1], &nDigits)
       && nDigits <= 9)
    {
        long a = mux_atol(fargs[0]);
        long b = mux_atol(fargs[1]);
        bResult = (a < b);
    }
    else
    {
        double a = mux_atof(fargs[0]);
        double b = mux_atof(fargs[1]);
        bResult = (a < b);
    }
    safe_bool(bResult, buff, bufc);
}

FUNCTION(fun_lte)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool bResult = false;
    int nDigits;
    if (  is_integer(fargs[0], &nDigits)
       && nDigits <= 9
       && is_integer(fargs[1], &nDigits)
       && nDigits <= 9)
    {
        long a = mux_atol(fargs[0]);
        long b = mux_atol(fargs[1]);
        bResult = (a <= b);
    }
    else
    {
        double a = mux_atof(fargs[0]);
        double b = mux_atof(fargs[1]);
        bResult = (a <= b);
    }
    safe_bool(bResult, buff, bufc);
}

FUNCTION(fun_eq)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool bResult = true;
    int nDigits;
    if (  is_integer(fargs[0], &nDigits)
       && nDigits <= 9
       && is_integer(fargs[1], &nDigits)
       && nDigits <= 9)
    {
        long a = mux_atol(fargs[0]);
        long b = mux_atol(fargs[1]);
        bResult = (a == b);
    }
    else
    {
        if (strcmp(fargs[0], fargs[1]) != 0)
        {
            double a = mux_atof(fargs[0]);
            double b = mux_atof(fargs[1]);
            bResult = (a == b);
        }
    }
    safe_bool(bResult, buff, bufc);
}

FUNCTION(fun_neq)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool bResult = false;
    int nDigits;
    if (  is_integer(fargs[0], &nDigits)
       && nDigits <= 9
       && is_integer(fargs[1], &nDigits)
       && nDigits <= 9)
    {
        long a = mux_atol(fargs[0]);
        long b = mux_atol(fargs[1]);
        bResult = (a != b);
    }
    else
    {
        if (strcmp(fargs[0], fargs[1]) != 0)
        {
            double a = mux_atof(fargs[0]);
            double b = mux_atof(fargs[1]);
            bResult = (a != b);
        }
    }
    safe_bool(bResult, buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_max, fun_min: Return maximum (minimum) value.
 */

FUNCTION(fun_max)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double maximum = 0.0;
    for (int i = 0; i < nfargs; i++)
    {
        double tval = mux_atof(fargs[i]);
        if (  i == 0
           || tval > maximum)
        {
            maximum = tval;
        }
    }
    fval(buff, bufc, maximum);
}

FUNCTION(fun_min)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double minimum = 0.0;
    for (int i = 0; i < nfargs; i++)
    {
        double tval = mux_atof(fargs[i]);
        if (  i == 0
           || tval < minimum)
        {
            minimum = tval;
        }
    }
    fval(buff, bufc, minimum);
}

/* ---------------------------------------------------------------------------
 * fun_sign: Returns -1, 0, or 1 based on the the sign of its argument.
 */

FUNCTION(fun_sign)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double num = mux_atof(fargs[0]);
    if (num < 0)
    {
        safe_str("-1", buff, bufc);
    }
    else
    {
        safe_bool(num > 0, buff, bufc);
    }
}

// fun_isign: Returns -1, 0, or 1 based on the the sign of its argument.
//
FUNCTION(fun_isign)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    INT64 num = mux_atoi64(fargs[0]);

    if (num < 0)
    {
        safe_str("-1", buff, bufc);
    }
    else
    {
        safe_bool(num > 0, buff, bufc);
    }
}

// shl() and shr() borrowed from PennMUSH 1.50
//
FUNCTION(fun_shl)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (  is_integer(fargs[0], NULL)
       && is_integer(fargs[1], NULL))
    {
        INT64 a = mux_atoi64(fargs[0]);
        long  b = mux_atol(fargs[1]);
        safe_i64toa(a << b, buff, bufc);
    }
    else
    {
        safe_str("#-1 ARGUMENTS MUST BE INTEGERS", buff, bufc);
    }
}

FUNCTION(fun_shr)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (  is_integer(fargs[0], NULL)
       && is_integer(fargs[1], NULL))
    {
        INT64 a = mux_atoi64(fargs[0]);
        long  b = mux_atol(fargs[1]);
        safe_i64toa(a >> b, buff, bufc);
    }
    else
    {
        safe_str("#-1 ARGUMENTS MUST BE INTEGERS", buff, bufc);
    }
}

FUNCTION(fun_inc)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs == 1)
    {
        safe_i64toa(mux_atoi64(fargs[0]) + 1, buff, bufc);
    }
    else
    {
        safe_chr('1', buff, bufc);
    }
}

FUNCTION(fun_dec)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs == 1)
    {
        safe_i64toa(mux_atoi64(fargs[0]) - 1, buff, bufc);
    }
    else
    {
        safe_str("-1", buff, bufc);
    }
}

FUNCTION(fun_trunc)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double rArg = mux_atof(fargs[0]);
    double rIntegerPart;

    mux_FPRestore();
    (void)modf(rArg, &rIntegerPart);
    mux_FPSet();

#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = mux_fpclass(rIntegerPart);
    if (MUX_FPGROUP(fpc) == MUX_FPGROUP_PASS)
    {
#endif // HAVE_IEEE_FP_FORMAT
        safe_tprintf_str(buff, bufc, "%.0f", rIntegerPart);
#ifdef HAVE_IEEE_FP_FORMAT
    }
    else
    {
        safe_str(mux_FPStrings[MUX_FPCLASS(fpc)], buff, bufc);
    }
#endif // HAVE_IEEE_FP_FORMAT
}

FUNCTION(fun_fdiv)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double bot = mux_atof(fargs[1]);
    double top = mux_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if (bot == 0.0)
    {
        if (top > 0.0)
        {
            safe_str("+Inf", buff, bufc);
        }
        else if (top < 0.0)
        {
            safe_str("-Inf", buff, bufc);
        }
        else
        {
            safe_str("Ind", buff, bufc);
        }
    }
    else
    {
        fval(buff, bufc, top/bot);
    }
#else
    fval(buff, bufc, top/bot);
#endif
}

FUNCTION(fun_idiv)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    INT64 bot, top;

    bot = mux_atoi64(fargs[1]);
    if (bot == 0)
    {
        safe_str("#-1 DIVIDE BY ZERO", buff, bufc);
    }
    else
    {
        top = mux_atoi64(fargs[0]);
        top = i64Division(top, bot);
        safe_i64toa(top, buff, bufc);
    }
}

FUNCTION(fun_floordiv)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    INT64 bot, top;

    bot = mux_atoi64(fargs[1]);
    if (bot == 0)
    {
        safe_str("#-1 DIVIDE BY ZERO", buff, bufc);
    }
    else
    {
        top = mux_atoi64(fargs[0]);
        top = i64FloorDivision(top, bot);
        safe_i64toa(top, buff, bufc);
    }
}

FUNCTION(fun_mod)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    INT64 bot, top;

    bot = mux_atoi64(fargs[1]);
    if (bot == 0)
    {
        bot = 1;
    }
    top = mux_atoi64(fargs[0]);
    top = i64Mod(top, bot);
    safe_i64toa(top, buff, bufc);
}

FUNCTION(fun_remainder)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    INT64 bot, top;

    bot = mux_atoi64(fargs[1]);
    if (bot == 0)
    {
        bot = 1;
    }
    top = mux_atoi64(fargs[0]);
    top = i64Remainder(top, bot);
    safe_i64toa(top, buff, bufc);
}

/* ---------------------------------------------------------------------------
 * fun_abs: Returns the absolute value of its argument.
 */

FUNCTION(fun_abs)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double num = mux_atof(fargs[0]);
    if (0.0 == num)
    {
        safe_chr('0', buff, bufc);
    }
    else if (num < 0.0)
    {
        fval(buff, bufc, -num);
    }
    else
    {
        fval(buff, bufc, num);
    }
}

// fun_iabs: Returns the absolute value of its argument.
//
FUNCTION(fun_iabs)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    INT64 num = mux_atoi64(fargs[0]);

    if (num == 0)
    {
        safe_chr('0', buff, bufc);
    }
    else if (num < 0)
    {
        safe_i64toa(-num, buff, bufc);
    }
    else
    {
        safe_i64toa(num, buff, bufc);
    }
}

FUNCTION(fun_dist2d)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double a, b, d;
    double sum;

    a = mux_atof(fargs[0]);
    b = mux_atof(fargs[2]);
    d = a - b;
    sum  = d * d;
    a = mux_atof(fargs[1]);
    b = mux_atof(fargs[3]);
    d = a - b;
    sum += d * d;

    mux_FPRestore();
    double result = sqrt(sum);
    mux_FPSet();

    fval(buff, bufc, result);
}

FUNCTION(fun_dist3d)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double a, b, d;
    double sum;

    a = mux_atof(fargs[0]);
    b = mux_atof(fargs[3]);
    d = a - b;
    sum  = d * d;
    a = mux_atof(fargs[1]);
    b = mux_atof(fargs[4]);
    d = a - b;
    sum += d * d;
    a = mux_atof(fargs[2]);
    b = mux_atof(fargs[5]);
    d = a - b;
    sum += d * d;

    mux_FPRestore();
    double result = sqrt(sum);
    mux_FPSet();

    fval(buff, bufc, result);
}

//------------------------------------------------------------------------
// Vector functions: VADD, VSUB, VMUL, VCROSS, VMAG, VUNIT, VDIM
// Vectors are space-separated numbers.
//
#define VADD_F   0
#define VSUB_F   1
#define VMUL_F   2
#define VDOT_F   3
#define VCROSS_F 4

static void handle_vectors
(
    char *vecarg1, char *vecarg2, char *buff, char **bufc, SEP *psep,
    SEP *posep, int flag
)
{
    char *v1[(LBUF_SIZE+1)/2], *v2[(LBUF_SIZE+1)/2];
    double scalar;
    int n, m, i;

    // Split the list up, or return if the list is empty.
    //
    if (!vecarg1 || !*vecarg1 || !vecarg2 || !*vecarg2)
    {
        return;
    }
    n = list2arr(v1, (LBUF_SIZE+1)/2, vecarg1, psep);
    m = list2arr(v2, (LBUF_SIZE+1)/2, vecarg2, psep);

    // vmul() and vadd() accepts a scalar in the first or second arg,
    // but everything else has to be same-dimensional.
    //
    if (  n != m
       && !(  (  flag == VMUL_F
              || flag == VADD_F
              || flag == VSUB_F)
           && (  n == 1
              || m == 1)))
    {
        safe_str("#-1 VECTORS MUST BE SAME DIMENSIONS", buff, bufc);
        return;
    }

    switch (flag)
    {
    case VADD_F:

        // If n or m is 1, this is scalar addition.
        // otherwise, add element-wise.
        //
        if (n == 1)
        {
            scalar = mux_atof(v1[0]);
            for (i = 0; i < m; i++)
            {
                if (i != 0)
                {
                    print_sep(posep, buff, bufc);
                }
                fval(buff, bufc, mux_atof(v2[i]) + scalar);
            }
            n = m;
        }
        else if (m == 1)
        {
            scalar = mux_atof(v2[0]);
            for (i = 0; i < n; i++)
            {
                if (i != 0)
                {
                    print_sep(posep, buff, bufc);
                }
                fval(buff, bufc, mux_atof(v1[i]) + scalar);
            }
        }
        else
        {
            for (i = 0; i < n; i++)
            {
                if (i != 0)
                {
                    print_sep(posep, buff, bufc);
                }
                double a = mux_atof(v1[i]);
                double b = mux_atof(v2[i]);
                fval(buff, bufc, a + b);
            }
        }
        break;

    case VSUB_F:

        if (n == 1)
        {
            // This is a scalar minus a vector.
            //
            scalar = mux_atof(v1[0]);
            for (i = 0; i < m; i++)
            {
                if (i != 0)
                {
                    print_sep(posep, buff, bufc);
                }
                fval(buff, bufc, scalar - mux_atof(v2[i]));
            }
        }
        else if (m == 1)
        {
            // This is a vector minus a scalar.
            //
            scalar = mux_atof(v2[0]);
            for (i = 0; i < n; i++)
            {
                if (i != 0)
                {
                    print_sep(posep, buff, bufc);
                }
                fval(buff, bufc, mux_atof(v1[i]) - scalar);
            }
        }
        else
        {
            // This is a vector minus a vector.
            //
            for (i = 0; i < n; i++)
            {
                if (i != 0)
                {
                    print_sep(posep, buff, bufc);
                }
                double a = mux_atof(v1[i]);
                double b = mux_atof(v2[i]);
                fval(buff, bufc, a - b);
            }
        }
        break;

    case VMUL_F:

        // If n or m is 1, this is scalar multiplication.
        // otherwise, multiply elementwise.
        //
        if (n == 1)
        {
            scalar = mux_atof(v1[0]);
            for (i = 0; i < m; i++)
            {
                if (i != 0)
                {
                    print_sep(posep, buff, bufc);
                }
                fval(buff, bufc, mux_atof(v2[i]) * scalar);
            }
        }
        else if (m == 1)
        {
            scalar = mux_atof(v2[0]);
            for (i = 0; i < n; i++)
            {
                if (i != 0)
                {
                    print_sep(posep, buff, bufc);
                }
                fval(buff, bufc, mux_atof(v1[i]) * scalar);
            }
        }
        else
        {
            // Vector element-wise product.
            //
            for (i = 0; i < n; i++)
            {
                if (i != 0)
                {
                    print_sep(posep, buff, bufc);
                }
                double a = mux_atof(v1[i]);
                double b = mux_atof(v2[i]);
                fval(buff, bufc, a * b);
            }
        }
        break;

    case VDOT_F:

        scalar = 0.0;
        for (i = 0; i < n; i++)
        {
            double a = mux_atof(v1[i]);
            double b = mux_atof(v2[i]);
            scalar +=  a * b;
        }
        fval(buff, bufc, scalar);
        break;

    case VCROSS_F:

        // cross product: (a,b,c) x (d,e,f) = (bf - ce, cd - af, ae - bd)
        //
        // Or in other words:
        //
        //      | a  b  c |
        //  det | d  e  f | = i(bf-ce) + j(cd-af) + k(ae-bd)
        //      | i  j  k |
        //
        // where i, j, and k are unit vectors in the x, y, and z
        // cartisian coordinate space and are understood when expressed
        // in vector form.
        //
        if (n != 3)
        {
            safe_str("#-1 VECTORS MUST BE DIMENSION OF 3", buff, bufc);
        }
        else
        {
            double a[2][3];
            for (i = 0; i < 3; i++)
            {
                a[0][i] = mux_atof(v1[i]);
                a[1][i] = mux_atof(v2[i]);
            }
            fval(buff, bufc, (a[0][1] * a[1][2]) - (a[0][2] * a[1][1]));
            print_sep(posep, buff, bufc);
            fval(buff, bufc, (a[0][2] * a[1][0]) - (a[0][0] * a[1][2]));
            print_sep(posep, buff, bufc);
            fval(buff, bufc, (a[0][0] * a[1][1]) - (a[0][1] * a[1][0]));
        }
        break;

    default:

        // If we reached this, we're in trouble.
        //
        safe_str("#-1 UNIMPLEMENTED", buff, bufc);
    }
}

FUNCTION(fun_vadd)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }
    handle_vectors(fargs[0], fargs[1], buff, bufc, &sep, &osep, VADD_F);
}

FUNCTION(fun_vsub)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }
    handle_vectors(fargs[0], fargs[1], buff, bufc, &sep, &osep, VSUB_F);
}

FUNCTION(fun_vmul)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }
    handle_vectors(fargs[0], fargs[1], buff, bufc, &sep, &osep, VMUL_F);
}

FUNCTION(fun_vdot)
{
    // dot product: (a,b,c) . (d,e,f) = ad + be + cf
    //
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }
    handle_vectors(fargs[0], fargs[1], buff, bufc, &sep, &osep, VDOT_F);
}

FUNCTION(fun_vcross)
{
    // cross product: (a,b,c) x (d,e,f) = (bf - ce, cd - af, ae - bd)
    //
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }
    handle_vectors(fargs[0], fargs[1], buff, bufc, &sep, &osep, VCROSS_F);
}

FUNCTION(fun_vmag)
{
    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    char *v1[LBUF_SIZE];
    int n, i;
    double tmp, res = 0.0;

    // Split the list up, or return if the list is empty.
    //
    if (!fargs[0] || !*fargs[0])
    {
        return;
    }
    n = list2arr(v1, LBUF_SIZE, fargs[0], &sep);

    // Calculate the magnitude.
    //
    for (i = 0; i < n; i++)
    {
        tmp = mux_atof(v1[i]);
        res += tmp * tmp;
    }

    if (res > 0)
    {
        mux_FPRestore();
        double result = sqrt(res);
        mux_FPSet();

        fval(buff, bufc, result);
    }
    else
    {
        safe_chr('0', buff, bufc);
    }
}

FUNCTION(fun_vunit)
{
    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    char *v1[LBUF_SIZE];
    int n, i;
    double tmp, res = 0.0;

    // Split the list up, or return if the list is empty.
    //
    if (!fargs[0] || !*fargs[0])
    {
        return;
    }
    n = list2arr(v1, LBUF_SIZE, fargs[0], &sep);

    // Calculate the magnitude.
    //
    for (i = 0; i < n; i++)
    {
        tmp = mux_atof(v1[i]);
        res += tmp * tmp;
    }

    if (res <= 0)
    {
        safe_str("#-1 CAN'T MAKE UNIT VECTOR FROM ZERO-LENGTH VECTOR",
            buff, bufc);
        return;
    }
    for (i = 0; i < n; i++)
    {
        if (i != 0)
        {
            print_sep(&sep, buff, bufc);
        }

        mux_FPRestore();
        double result = sqrt(res);
        mux_FPSet();

        fval(buff, bufc, mux_atof(v1[i]) / result);
    }
}

FUNCTION(fun_floor)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    mux_FPRestore();
    double r = floor(mux_atof(fargs[0]));
    mux_FPSet();

#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = mux_fpclass(r);
    if (MUX_FPGROUP(fpc) == MUX_FPGROUP_PASS)
    {
#endif // HAVE_IEEE_FP_FORMAT
        safe_tprintf_str(buff, bufc, "%.0f", r);
#ifdef HAVE_IEEE_FP_FORMAT
    }
    else
    {
        safe_str(mux_FPStrings[MUX_FPCLASS(fpc)], buff, bufc);
    }
#endif // HAVE_IEEE_FP_FORMAT
}

FUNCTION(fun_ceil)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    mux_FPRestore();
    double r = ceil(mux_atof(fargs[0]));
    mux_FPSet();

#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = mux_fpclass(r);
    if (MUX_FPGROUP(fpc) == MUX_FPGROUP_PASS)
    {
#endif // HAVE_IEEE_FP_FORMAT
        safe_tprintf_str(buff, bufc, "%.0f", r);
#ifdef HAVE_IEEE_FP_FORMAT
    }
    else
    {
        safe_str(mux_FPStrings[MUX_FPCLASS(fpc)], buff, bufc);
    }
#endif // HAVE_IEEE_FP_FORMAT
}

FUNCTION(fun_round)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double r = mux_atof(fargs[0]);
#ifdef HAVE_IEEE_FP_FORMAT
    int fpc = mux_fpclass(r);
    if (  MUX_FPGROUP(fpc) == MUX_FPGROUP_PASS
       || MUX_FPGROUP(fpc) == MUX_FPGROUP_ZERO)
    {
        if (MUX_FPGROUP(fpc) == MUX_FPGROUP_ZERO)
        {
            r = 0.0;
        }
#endif // HAVE_IEEE_FP_FORMAT
        int frac = mux_atol(fargs[1]);
        safe_str(mux_ftoa(r, true, frac), buff, bufc);
#ifdef HAVE_IEEE_FP_FORMAT
    }
    else
    {
        safe_str(mux_FPStrings[MUX_FPCLASS(fpc)], buff, bufc);
    }
#endif // HAVE_IEEE_FP_FORMAT
}

FUNCTION(fun_pi)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str("3.141592653589793", buff, bufc);
}

FUNCTION(fun_e)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str("2.718281828459045", buff, bufc);
}

static double ConvertRDG2R(double d, const char *szUnits)
{
    switch (mux_tolower(szUnits[0]))
    {
    case 'd':
        // Degrees to Radians.
        //
        d *= 0.017453292519943295;
        break;

    case 'g':
        // Gradians to Radians.
        //
        d *= 0.015707963267948967;
        break;
    }
    return d;
}

static double ConvertR2RDG(double d, const char *szUnits)
{
    switch (mux_tolower(szUnits[0]))
    {
    case 'd':
        // Radians to Degrees.
        //
        d *= 57.29577951308232;
        break;

    case 'g':
        // Radians to Gradians.
        //
        d *= 63.66197723675813;
        break;
    }
    return d;
}

FUNCTION(fun_ctu)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val = mux_atof(fargs[0]);
    val = ConvertRDG2R(val, fargs[1]);
    val = ConvertR2RDG(val, fargs[2]);
    fval(buff, bufc, val);
}

FUNCTION(fun_sin)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double d = mux_atof(fargs[0]);
    if (nfargs == 2)
    {
        d = ConvertRDG2R(d, fargs[1]);
    }

    mux_FPRestore();
    d = sin(d);
    mux_FPSet();

    fval(buff, bufc, d);
}

FUNCTION(fun_cos)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double d = mux_atof(fargs[0]);
    if (nfargs == 2)
    {
        d = ConvertRDG2R(d, fargs[1]);
    }

    mux_FPRestore();
    d = cos(d);
    mux_FPSet();

    fval(buff, bufc, d);
}

FUNCTION(fun_tan)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double d = mux_atof(fargs[0]);
    if (nfargs == 2)
    {
        d = ConvertRDG2R(d, fargs[1]);
    }

    mux_FPRestore();
    d = tan(d);
    mux_FPSet();

    fval(buff, bufc, d);
}

FUNCTION(fun_asin)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val = mux_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if ((val < -1.0) || (val > 1.0))
    {
        safe_str("Ind", buff, bufc);
        return;
    }
#endif
    mux_FPRestore();
    val = asin(val);
    mux_FPSet();

    if (nfargs == 2)
    {
        val = ConvertR2RDG(val, fargs[1]);
    }
    fval(buff, bufc, val);
}

FUNCTION(fun_acos)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val = mux_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if ((val < -1.0) || (val > 1.0))
    {
        safe_str("Ind", buff, bufc);
        return;
    }
#endif
    mux_FPRestore();
    val = acos(val);
    mux_FPSet();

    if (nfargs == 2)
    {
        val = ConvertR2RDG(val, fargs[1]);
    }
    fval(buff, bufc, val);
}

FUNCTION(fun_atan)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val = mux_atof(fargs[0]);

    mux_FPRestore();
    val = atan(val);
    mux_FPSet();

    if (nfargs == 2)
    {
        val = ConvertR2RDG(val, fargs[1]);
    }
    fval(buff, bufc, val);
}

FUNCTION(fun_exp)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val = mux_atof(fargs[0]);

    mux_FPRestore();
    val = exp(val);
    mux_FPSet();

    fval(buff, bufc, val);
}

FUNCTION(fun_power)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val, val1, val2;

    val1 = mux_atof(fargs[0]);
    val2 = mux_atof(fargs[1]);
#ifndef HAVE_IEEE_FP_SNAN
    if (val1 < 0.0)
    {
        safe_str("Ind", buff, bufc);
    }
    else
    {
        mux_FPRestore();
        val = pow(val1, val2);
        mux_FPSet();
    }
#else
    mux_FPRestore();
    val = pow(val1, val2);
    mux_FPSet();
#endif
    fval(buff, bufc, val);
}

FUNCTION(fun_fmod)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val, val1, val2;

    val1 = mux_atof(fargs[0]);
    val2 = mux_atof(fargs[1]);
#ifndef HAVE_IEEE_FP_SNAN
    if (val1 == 0.0)
    {
        safe_str("Ind", buff, bufc);
    }
    else
    {
        mux_FPRestore();
        val = fmod(val1, val2);
        mux_FPSet();
    }
#else
    mux_FPRestore();
    val = fmod(val1, val2);
    mux_FPSet();
#endif
    fval(buff, bufc, val);
}

FUNCTION(fun_ln)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val;

    val = mux_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if (val < 0.0)
    {
        safe_str("Ind", buff, bufc);
    }
    else if (val == 0.0)
    {
        safe_str("-Inf", buff, bufc);
    }
    else
    {
        mux_FPRestore();
        val = log(val);
        mux_FPSet();
    }
#else
    mux_FPRestore();
    val = log(val);
    mux_FPSet();
#endif
    fval(buff, bufc, val);
}

FUNCTION(fun_log)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val;

    val = mux_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if (val < 0.0)
    {
        safe_str("Ind", buff, bufc);
    }
    else if (val == 0.0)
    {
        safe_str("-Inf", buff, bufc);
    }
    else
    {
        mux_FPRestore();
        val = log10(val);
        mux_FPSet();
    }
#else
    mux_FPRestore();
    val = log10(val);
    mux_FPSet();
#endif
    fval(buff, bufc, val);
}

FUNCTION(fun_sqrt)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val;

    val = mux_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if (val < 0.0)
    {
        safe_str("Ind", buff, bufc);
    }
    else if (val == 0.0)
    {
        safe_chr('0', buff, bufc);
    }
    else
    {
        mux_FPRestore();
        val = sqrt(val);
        mux_FPSet();
    }
#else
    mux_FPRestore();
    val = sqrt(val);
    mux_FPSet();
#endif
    fval(buff, bufc, val);
}

/* ---------------------------------------------------------------------------
 * isnum: is the argument a number?
 */

FUNCTION(fun_isnum)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_bool(is_real(fargs[0]), buff, bufc);
}

/* ---------------------------------------------------------------------------
 * israt: is the argument an rational?
 */

FUNCTION(fun_israt)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_bool(is_rational(fargs[0]), buff, bufc);
}

/* ---------------------------------------------------------------------------
 * isint: is the argument an integer?
 */

FUNCTION(fun_isint)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_bool(is_integer(fargs[0], NULL), buff, bufc);
}

FUNCTION(fun_and)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool val = true;
    for (int i = 0; i < nfargs && val; i++)
    {
        val = isTRUE(mux_atol(fargs[i]));
    }
    safe_bool(val, buff, bufc);
}

FUNCTION(fun_or)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool val = false;
    for (int i = 0; i < nfargs && !val; i++)
    {
        val = isTRUE(mux_atol(fargs[i]));
    }
    safe_bool(val, buff, bufc);
}

FUNCTION(fun_andbool)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool val = true;
    for (int i = 0; i < nfargs && val; i++)
    {
        val = xlate(fargs[i]);
    }
    safe_bool(val, buff, bufc);
}

FUNCTION(fun_orbool)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool val = false;
    for (int i = 0; i < nfargs && !val; i++)
    {
        val = xlate(fargs[i]);
    }
    safe_bool(val, buff, bufc);
}

FUNCTION(fun_cand)
{
    bool val = true;
    char *temp = alloc_lbuf("fun_cand");
    for (int i = 0; i < nfargs && val && !MuxAlarm.bAlarmed; i++)
    {
        char *bp = temp;
        char *str = fargs[i];
        mux_exec(temp, &bp, executor, caller, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        *bp = '\0';
        val = isTRUE(mux_atol(temp));
    }
    free_lbuf(temp);
    safe_bool(val, buff, bufc);
}

FUNCTION(fun_cor)
{
    bool val = false;
    char *temp = alloc_lbuf("fun_cor");
    for (int i = 0; i < nfargs && !val && !MuxAlarm.bAlarmed; i++)
    {
        char *bp = temp;
        char *str = fargs[i];
        mux_exec(temp, &bp, executor, caller, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        *bp = '\0';
        val = isTRUE(mux_atol(temp));
    }
    free_lbuf(temp);
    safe_bool(val, buff, bufc);
}

FUNCTION(fun_candbool)
{
    bool val = true;
    char *temp = alloc_lbuf("fun_candbool");
    for (int i = 0; i < nfargs && val && !MuxAlarm.bAlarmed; i++)
    {
        char *bp = temp;
        char *str = fargs[i];
        mux_exec(temp, &bp, executor, caller, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        *bp = '\0';
        val = xlate(temp);
    }
    free_lbuf(temp);
    safe_bool(val, buff, bufc);
}

FUNCTION(fun_corbool)
{
    bool val = false;
    char *temp = alloc_lbuf("fun_corbool");
    for (int i = 0; i < nfargs && !val && !MuxAlarm.bAlarmed; i++)
    {
        char *bp = temp;
        char *str = fargs[i];
        mux_exec(temp, &bp, executor, caller, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        *bp = '\0';
        val = xlate(temp);
    }
    free_lbuf(temp);
    safe_bool(val, buff, bufc);
}

FUNCTION(fun_xor)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool val = false;
    for (int i = 0; i < nfargs; i++)
    {
        int tval = mux_atol(fargs[i]);
        val = (val && !tval) || (!val && tval);
    }
    safe_bool(val, buff, bufc);
}

FUNCTION(fun_not)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_bool(!xlate(fargs[0]), buff, bufc);
}

FUNCTION(fun_t)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (  nfargs <= 0
       || fargs[0][0] == '\0')
    {
        safe_chr('0', buff, bufc);
    }
    else
    {
        safe_bool(xlate(fargs[0]), buff, bufc);
    }
}
static const char *bigones[] =
{
    "",
    "thousand",
    "million",
    "billion",
    "trillion"
};

static const char *singles[] =
{
    "",
    "one",
    "two",
    "three",
    "four",
    "five",
    "six",
    "seven",
    "eight",
    "nine"
};

static const char *teens[] =
{
    "ten",
    "eleven",
    "twelve",
    "thirteen",
    "fourteen",
    "fifteen",
    "sixteen",
    "seventeen",
    "eighteen",
    "nineteen"
};

static const char *tens[] =
{
    "",
    "",
    "twenty",
    "thirty",
    "forty",
    "fifty",
    "sixty",
    "seventy",
    "eighty",
    "ninety"
};

static const char *th_prefix[] =
{
    "",
    "ten",
    "hundred"
};

class CSpellNum
{
public:
    void SpellNum(const char *p, char *buff_arg, char **bufc_arg);

private:
    void TwoDigits(const char *p);
    void ThreeDigits(const char *p, int iBigOne);
    void ManyDigits(int n, const char *p, bool bHundreds);
    void FractionalDigits(int n, const char *p);

    void StartWord(void);
    void AddWord(const char *p);

    char *buff;
    char **bufc;
    bool bNeedSpace;
};

void CSpellNum::StartWord(void)
{
    if (bNeedSpace)
    {
        safe_chr(' ', buff, bufc);
    }
    bNeedSpace = true;
}

void CSpellNum::AddWord(const char *p)
{
    safe_str(p, buff, bufc);
}

// Handle two-character sequences.
//
void CSpellNum::TwoDigits(const char *p)
{
    int n0 = p[0] - '0';
    int n1 = p[1] - '0';

    if (n0 == 0)
    {
        if (n1 != 0)
        {
            StartWord();
            AddWord(singles[n1]);
        }
        return;
    }
    else if (n0 == 1)
    {
        StartWord();
        AddWord(teens[n1]);
        return;
    }
    if (n1 == 0)
    {
        StartWord();
        AddWord(tens[n0]);
    }
    else
    {
        StartWord();
        AddWord(tens[n0]);
        AddWord("-");
        AddWord(singles[n1]);
    }
}

// Handle three-character sequences.
//
void CSpellNum::ThreeDigits(const char *p, int iBigOne)
{
    if (  p[0] == '0'
       && p[1] == '0'
       && p[2] == '0')
    {
        return;
    }

    // Handle hundreds.
    //
    if (p[0] != '0')
    {
        StartWord();
        AddWord(singles[p[0]-'0']);
        StartWord();
        AddWord("hundred");
    }
    TwoDigits(p+1);
    if (iBigOne > 0)
    {
        StartWord();
        AddWord(bigones[iBigOne]);
    }
}

// Handle a series of patterns of three.
//
void CSpellNum::ManyDigits(int n, const char *p, bool bHundreds)
{
    // Handle special Hundreds cases.
    //
    if (  bHundreds
       && n == 4
       && p[1] != '0')
    {
        TwoDigits(p);
        StartWord();
        AddWord("hundred");
        TwoDigits(p+2);
        return;
    }

    // Handle normal cases.
    //
    int ndiv = ((n + 2) / 3) - 1;
    int nrem = n % 3;
    char buf[3];
    if (nrem == 0)
    {
        nrem = 3;
    }

    int j = nrem;
    for (int i = 2; 0 <= i; i--)
    {
        if (j)
        {
            j--;
            buf[i] = p[j];
        }
        else
        {
            buf[i] = '0';
        }
    }
    ThreeDigits(buf, ndiv);
    p += nrem;
    while (ndiv-- > 0)
    {
        ThreeDigits(p, ndiv);
        p += 3;
    }
}

// Handle precision ending for part to the right of the decimal place.
//
void CSpellNum::FractionalDigits(int n, const char *p)
{
    ManyDigits(n, p, false);
    if (  0 < n
       && n < 15)
    {
        int d = n / 3;
        int r = n % 3;
        StartWord();
        if (r != 0)
        {
            AddWord(th_prefix[r]);
            if (d != 0)
            {
                AddWord("-");
            }
        }
        AddWord(bigones[d]);
        AddWord("th");
        INT64 i64 = mux_atoi64(p);
        if (i64 != 1)
        {
            AddWord("s");
        }
    }
}

void CSpellNum::SpellNum(const char *number, char *buff_arg, char **bufc_arg)
{
    buff = buff_arg;
    bufc = bufc_arg;
    bNeedSpace = false;

    // Trim Spaces from beginning.
    //
    while (mux_isspace(*number))
    {
        number++;
    }

    if (*number == '-')
    {
        StartWord();
        AddWord("negative");
        number++;
    }

    // Trim Zeroes from Beginning.
    //
    while (*number == '0')
    {
        number++;
    }

    const char *pA = number;
    while (mux_isdigit(*number))
    {
        number++;
    }
    size_t nA = number - pA;

    const char *pB  = NULL;
    size_t nB = 0;
    if (*number == '.')
    {
        number++;
        pB = number;
        while (mux_isdigit(*number))
        {
            number++;
        }
        nB = number - pB;
    }

    // Skip trailing spaces.
    //
    while (mux_isspace(*number))
    {
        number++;
    }

    if (  *number
       || nA >= 16
       || nB >= 15)
    {
        safe_str("#-1 ARGUMENT MUST BE A NUMBER", buff, bufc);
        return;
    }

    if (nA == 0)
    {
        if (nB == 0)
        {
            StartWord();
            AddWord("zero");
        }
    }
    else
    {
        ManyDigits(nA, pA, true);
        if (nB)
        {
            StartWord();
            AddWord("and");
        }
    }
    if (nB)
    {
        FractionalDigits(nB, pB);
    }
}

FUNCTION(fun_spellnum)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    CSpellNum sn;
    sn.SpellNum(fargs[0], buff, bufc);
}

FUNCTION(fun_roman)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const char *number = fargs[0];

    // Trim Spaces from beginning.
    //
    while (mux_isspace(*number))
    {
        number++;
    }

    // Trim Zeroes from Beginning.
    //
    while (*number == '0')
    {
        number++;
    }

    const char *pA = number;
    while (mux_isdigit(*number))
    {
        number++;
    }
    size_t nA = number - pA;

    // Skip trailing spaces.
    //
    while (mux_isspace(*number))
    {
        number++;
    }

    // Validate that argument is numeric with a value between 1 and 3999.
    //
    if (*number || nA < 1)
    {
        safe_str("#-1 ARGUMENT MUST BE A POSITIVE NUMBER", buff, bufc);
        return;
    }
    else if (  nA > 4
            || (  nA == 1
               && pA[0] == '0')
            || (  nA == 4
               && '3' < pA[0]))
    {
        safe_range(buff, bufc);
        return;
    }

    // I:1, V:5, X:10, L:50, C:100, D:500, M:1000
    //
    // Ones:      _ I II III IV V VI VII VIII IX
    // Tens:      _ X XX XXX XL L LX LXX LXXX XC
    // Hundreds:  _ C CC CCC CD D DC DCC DCCC CM
    // Thousands: _ M MM MMM
    //
    static const char aLetters[4][3] =
    {
        { 'I', 'V', 'X' },
        { 'X', 'L', 'C' },
        { 'C', 'D', 'M' },
        { 'M', ' ', ' ' }
    };

    static const char *aCode[10] =
    {
        "",
        "1",
        "11",
        "111",
        "12",
        "2",
        "21",
        "211",
        "2111",
        "13"
    };

    while (nA--)
    {
        const char *pCode = aCode[*pA - '0'];
        const char *pLetters = aLetters[nA];

        while (*pCode)
        {
            safe_chr(pLetters[*pCode - '1'], buff, bufc);
            pCode++;
        }
        pA++;
    }
}

/*-------------------------------------------------------------------------
 * List-based numeric functions.
 */

FUNCTION(fun_land)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool bValue = true;
    if (0 < nfargs)
    {
        SEP sep;
        if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
        {
            return;
        }

        char *cp = trim_space_sep(fargs[0], &sep);
        while (cp && bValue)
        {
            char *curr = split_token(&cp, &sep);
            bValue = isTRUE(mux_atol(curr));
        }
    }
    safe_bool(bValue, buff, bufc);
}

FUNCTION(fun_lor)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool bValue = false;
    if (0 < nfargs)
    {
        SEP sep;
        if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
        {
            return;
        }

        char *cp = trim_space_sep(fargs[0], &sep);
        while (cp && !bValue)
        {
            char *curr = split_token(&cp, &sep);
            bValue = isTRUE(mux_atol(curr));
        }
    }
    safe_bool(bValue, buff, bufc);
}

FUNCTION(fun_band)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UINT64 val = UINT64_MAX_VALUE;
    for (int i = 0; i < nfargs; i++)
    {
        if (is_integer(fargs[i], NULL))
        {
            val &= mux_atoi64(fargs[i]);
        }
        else
        {
            safe_str("#-1 ARGUMENTS MUST BE INTEGERS", buff, bufc);
            return;
        }
    }
    safe_i64toa(val, buff, bufc);
}

FUNCTION(fun_bor)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UINT64 val = 0;
    for (int i = 0; i < nfargs; i++)
    {
        if (is_integer(fargs[i], NULL))
        {
            val |= mux_atoi64(fargs[i]);
        }
        else
        {
            safe_str("#-1 ARGUMENTS MUST BE INTEGERS", buff, bufc);
            return;
        }
    }
    safe_i64toa(val, buff, bufc);
}

FUNCTION(fun_bnand)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (  is_integer(fargs[0], NULL)
       && is_integer(fargs[1], NULL))
    {
        INT64 a = mux_atoi64(fargs[0]);
        INT64 b = mux_atoi64(fargs[1]);
        safe_i64toa(a & ~(b), buff, bufc);
    }
    else
    {
        safe_str("#-1 ARGUMENTS MUST BE INTEGERS", buff, bufc);
    }
}

FUNCTION(fun_bxor)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UINT64 val = 0;
    for (int i = 0; i < nfargs; i++)
    {
        if (is_integer(fargs[i], NULL))
        {
            val ^= mux_atoi64(fargs[i]);
        }
        else
        {
            safe_str("#-1 ARGUMENTS MUST BE INTEGERS", buff, bufc);
            return;
        }
    }
    safe_i64toa(val, buff, bufc);
}

FUNCTION(fun_crc32)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UINT32 ulCRC32 = 0;
    for (int i = 0; i < nfargs; i++)
    {
        int n = strlen(fargs[i]);
        ulCRC32 = CRC32_ProcessBuffer(ulCRC32, fargs[i], n);
    }
    safe_i64toa(ulCRC32, buff, bufc);
}

FUNCTION(fun_sha1)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int i;
    SHA1_CONTEXT shac;
    SHA1_Init(&shac);
    for (i = 0; i < nfargs; i++)
    {
        SHA1_Compute(&shac, strlen(fargs[i]), fargs[i]);
    }
    SHA1_Final(&shac);
    for (i = 0; i <= 4; i++)
    {
        char buf[9];
        mux_sprintf(buf, sizeof(buf), "%08X", shac.H[i]);
        safe_str(buf, buff, bufc);
    }
}
