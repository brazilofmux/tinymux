/*! \file funmath.cpp
 * \brief MUX math function handlers.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#ifdef HAVE_FLOAT_H
#include <float.h>
#endif // HAVE_FLOAT_H
#include <math.h>

#include "functions.h"
#include "funmath.h"
#include "mathutil.h"

#ifdef UNIX_DIGEST
#include <openssl/sha.h>
#include <openssl/evp.h>
#else
#include "sha1.h"
#endif

static const long nMaximums[10] =
{
    0, 9, 99, 999, 9999, 99999, 999999, 9999999, 99999999, 999999999
};

static double g_aDoubles[MAX_WORDS];

FUNCTION(fun_add)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int nArgs = nfargs;
    if (MAX_WORDS < nArgs)
    {
        nArgs = MAX_WORDS;
    }

    int i;
    for (i = 0; i < nArgs; i++)
    {
        int nDigits;
        long nMaxValue = 0;
        if (  !is_integer(fargs[i], &nDigits)
           || nDigits > 9
           || (nMaxValue += nMaximums[nDigits]) > 999999999L)
        {
            // Do it the slow way.
            //
            for (int j = 0; j < nArgs; j++)
            {
                g_aDoubles[j] = mux_atof(fargs[j]);
            }

            fval(buff, bufc, AddDoubles(nArgs, g_aDoubles));
            return;
        }
    }

    // We can do it the fast way.
    //
    long sum = 0;
    for (i = 0; i < nArgs; i++)
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
    UNUSED_PARAMETER(eval);
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

        UTF8 *cp = trim_space_sep(fargs[0], sep);
        while (  cp
              && n < MAX_WORDS)
        {
            UTF8 *curr = split_token(&cp, sep);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
        if (strcmp((char *)fargs[0], (char *)fargs[1]) != 0)
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
    UNUSED_PARAMETER(eval);
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
        if (strcmp((char *)fargs[0], (char *)fargs[1]) != 0)
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
    UNUSED_PARAMETER(eval);
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

FUNCTION(fun_lmax)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double maximum = 0.0;
    if (0 < nfargs)
    {
        SEP sep;
        if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
        {
            return;
        }

        int n = 0;
        UTF8 *cp = trim_space_sep(fargs[0], sep);
        while (nullptr != cp)
        {
            UTF8 *curr = split_token(&cp, sep);
            double tval = mux_atof(curr);
            if (  n++ == 0
               || tval > maximum)
            {
                maximum = tval;
            }
        }
    }
    fval(buff, bufc, maximum);
}

FUNCTION(fun_min)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
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

FUNCTION(fun_lmin)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double minimum = 0.0;
    if (0 < nfargs)
    {
        SEP sep;
        if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
        {
            return;
        }

        int n = 0;
        UTF8 *cp = trim_space_sep(fargs[0], sep);
        while (nullptr != cp)
        {
            UTF8 *curr = split_token(&cp, sep);
            double tval = mux_atof(curr);
            if (  n++ == 0
               || tval < minimum)
            {
                minimum = tval;
            }
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double num = mux_atof(fargs[0]);
    if (num < 0)
    {
        safe_str(T("-1"), buff, bufc);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    INT64 num = mux_atoi64(fargs[0]);

    if (num < 0)
    {
        safe_str(T("-1"), buff, bufc);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (  is_integer(fargs[0], nullptr)
       && is_integer(fargs[1], nullptr))
    {
        long  b = mux_atol(fargs[1]);
        if (0 <= b)
        {
            INT64 a = mux_atoi64(fargs[0]);
            safe_i64toa(a << b, buff, bufc);
        }
        else
        {
            safe_str(T("#-1 SECOND ARGUMENT MUST BE A POSITIVE NUMBER"), buff, bufc);
        }
    }
    else
    {
        safe_str(T("#-1 ARGUMENTS MUST BE INTEGERS"), buff, bufc);
    }
}

FUNCTION(fun_shr)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (  is_integer(fargs[0], nullptr)
       && is_integer(fargs[1], nullptr))
    {
        long  b = mux_atol(fargs[1]);
        if (0 <= b)
        {
            INT64 a = mux_atoi64(fargs[0]);
            safe_i64toa(a >> b, buff, bufc);
        }
        else
        {
            safe_str(T("#-1 SECOND ARGUMENT MUST BE A POSITIVE NUMBER"), buff, bufc);
        }
    }
    else
    {
        safe_str(T("#-1 ARGUMENTS MUST BE INTEGERS"), buff, bufc);
    }
}

FUNCTION(fun_inc)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs == 1)
    {
        safe_i64toa(mux_atoi64(fargs[0]) - 1, buff, bufc);
    }
    else
    {
        safe_str(T("-1"), buff, bufc);
    }
}

FUNCTION(fun_trunc)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
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
        fval(buff, bufc, rIntegerPart);
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
    UNUSED_PARAMETER(eval);
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
            safe_str(T("+Inf"), buff, bufc);
        }
        else if (top < 0.0)
        {
            safe_str(T("-Inf"), buff, bufc);
        }
        else
        {
            safe_str(T("Ind"), buff, bufc);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    INT64 bot, top;

    bot = mux_atoi64(fargs[1]);
    if (bot == 0)
    {
        safe_str(T("#-1 DIVIDE BY ZERO"), buff, bufc);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    INT64 bot, top;

    bot = mux_atoi64(fargs[1]);
    if (bot == 0)
    {
        safe_str(T("#-1 DIVIDE BY ZERO"), buff, bufc);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    __in UTF8 *vecarg1, __in UTF8 *vecarg2, __inout UTF8 *buff, __deref_inout UTF8 **bufc,
    __in const SEP &sep, __in const SEP &osep, int flag
)
{
    // Return if the list is empty.
    //
    if (!vecarg1 || !*vecarg1 || !vecarg2 || !*vecarg2)
    {
        return;
    }

    UTF8 **v1 = new UTF8 *[(LBUF_SIZE+1)/2];
    ISOUTOFMEMORY(v1);
    UTF8 **v2 = new UTF8 *[(LBUF_SIZE+1)/2];
    ISOUTOFMEMORY(v2);

    // Split the list up, or return if the list is empty.
    //
    int n = list2arr(v1, (LBUF_SIZE+1)/2, vecarg1, sep);
    int m = list2arr(v2, (LBUF_SIZE+1)/2, vecarg2, sep);

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
        safe_str(T("#-1 VECTORS MUST BE SAME DIMENSIONS"), buff, bufc);
        delete [] v1;
        delete [] v2;
        return;
    }

    double scalar;
    int i;

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
                    print_sep(osep, buff, bufc);
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
                    print_sep(osep, buff, bufc);
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
                    print_sep(osep, buff, bufc);
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
                    print_sep(osep, buff, bufc);
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
                    print_sep(osep, buff, bufc);
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
                    print_sep(osep, buff, bufc);
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
                    print_sep(osep, buff, bufc);
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
                    print_sep(osep, buff, bufc);
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
                    print_sep(osep, buff, bufc);
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
            safe_str(T("#-1 VECTORS MUST BE DIMENSION OF 3"), buff, bufc);
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
            print_sep(osep, buff, bufc);
            fval(buff, bufc, (a[0][2] * a[1][0]) - (a[0][0] * a[1][2]));
            print_sep(osep, buff, bufc);
            fval(buff, bufc, (a[0][0] * a[1][1]) - (a[0][1] * a[1][0]));
        }
        break;

    default:

        // If we reached this, we're in trouble.
        //
        safe_str(T("#-1 UNIMPLEMENTED"), buff, bufc);
    }
    delete [] v1;
    delete [] v2;
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
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VADD_F);
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
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VSUB_F);
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
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VMUL_F);
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
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VDOT_F);
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
    handle_vectors(fargs[0], fargs[1], buff, bufc, sep, osep, VCROSS_F);
}

FUNCTION(fun_vmag)
{
    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    // Split the list up, or return if the list is empty.
    //
    if (!fargs[0] || !*fargs[0])
    {
        return;
    }

    UTF8 **v1 = nullptr;
    try
    {
        v1 = new UTF8 *[LBUF_SIZE/2];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr != v1)
    {
        int n = list2arr(v1, LBUF_SIZE/2, fargs[0], sep);

        // Calculate the magnitude.
        //
        double res = 0.0;
        for (int i = 0; i < n; i++)
        {
            double tmp = mux_atof(v1[i]);
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
        delete [] v1;
    }
}

FUNCTION(fun_vunit)
{
    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    // Split the list up, or return if the list is empty.
    //
    if (!fargs[0] || !*fargs[0])
    {
        return;
    }

    UTF8 **v1 = nullptr;
    try
    {
        v1 = new UTF8 *[LBUF_SIZE/2];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr != v1)
    {
        int n = list2arr(v1, LBUF_SIZE/2, fargs[0], sep);

        // Calculate the magnitude.
        //
        int i;
        double res = 0.0;
        for (i = 0; i < n; i++)
        {
            double tmp = mux_atof(v1[i]);
            res += tmp * tmp;
        }

        if (res <= 0)
        {
            safe_str(T("#-1 CANNOT MAKE UNIT VECTOR FROM ZERO-LENGTH VECTOR"),
                buff, bufc);
            delete [] v1;
            return;
        }

        for (i = 0; i < n; i++)
        {
            if (0 != i)
            {
                print_sep(sep, buff, bufc);
            }

            mux_FPRestore();
            double result = sqrt(res);
            mux_FPSet();

            fval(buff, bufc, mux_atof(v1[i]) / result);
        }
        delete [] v1;
    }
}

FUNCTION(fun_floor)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
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
        fval(buff, bufc, r);
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
    UNUSED_PARAMETER(eval);
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
        fval(buff, bufc, r);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str(T("3.141592653589793"), buff, bufc);
}

FUNCTION(fun_e)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str(T("2.718281828459045"), buff, bufc);
}

static double ConvertRDG2R(double d, const UTF8 *szUnits)
{
    switch (szUnits[0])
    {
    case 'd':
    case 'D':
        // Degrees to Radians.
        //
        d *= 0.017453292519943295;
        break;

    case 'g':
    case 'G':
        // Gradians to Radians.
        //
        d *= 0.015707963267948967;
        break;
    }
    return d;
}

static double ConvertR2RDG(double d, const UTF8 *szUnits)
{
    switch (szUnits[0])
    {
    case 'd':
    case 'D':
        // Radians to Degrees.
        //
        d *= 57.29577951308232;
        break;

    case 'g':
    case 'G':
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val = mux_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if ((val < -1.0) || (val > 1.0))
    {
        safe_str(T("Ind"), buff, bufc);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val = mux_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if ((val < -1.0) || (val > 1.0))
    {
        safe_str(T("Ind"), buff, bufc);
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
    UNUSED_PARAMETER(eval);
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

FUNCTION(fun_atan2)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val1 = mux_atof(fargs[0]);
    double val2 = mux_atof(fargs[1]);

    mux_FPRestore();
    val1 = atan2(val1, val2);
    mux_FPSet();

    if (3 == nfargs)
    {
        val1 = ConvertR2RDG(val1, fargs[2]);
    }
    fval(buff, bufc, val1);
}

FUNCTION(fun_exp)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val, val1, val2;

    val1 = mux_atof(fargs[0]);
    val2 = mux_atof(fargs[1]);
#ifndef HAVE_IEEE_FP_SNAN
    if (val1 < 0.0)
    {
        safe_str(T("Ind"), buff, bufc);
    }
    else
    {
        mux_FPRestore();
        val = pow(val1, val2);
        mux_FPSet();
        fval(buff, bufc, val);
    }
#else
    mux_FPRestore();
    val = pow(val1, val2);
    mux_FPSet();
    fval(buff, bufc, val);
#endif
}

FUNCTION(fun_fmod)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val, val1, val2;

    val1 = mux_atof(fargs[0]);
    val2 = mux_atof(fargs[1]);
#ifndef HAVE_IEEE_FP_SNAN
    if (val1 == 0.0)
    {
        safe_str(T("Ind"), buff, bufc);
    }
    else
    {
        mux_FPRestore();
        val = fmod(val1, val2);
        mux_FPSet();
        fval(buff, bufc, val);
    }
#else
    mux_FPRestore();
    val = fmod(val1, val2);
    mux_FPSet();
    fval(buff, bufc, val);
#endif
}

FUNCTION(fun_ln)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val;

    val = mux_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if (val < 0.0)
    {
        safe_str(T("Ind"), buff, bufc);
    }
    else if (val == 0.0)
    {
        safe_str(T("-Inf"), buff, bufc);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    typedef enum
    {
#ifdef HAVE_LOG2
        kBinary,
#endif
        kNatural,
        kCommon,
        kOther
    } logarithm_base;

    logarithm_base kBase;

    double val = mux_atof(fargs[0]);

    double base;
    if (2 == nfargs)
    {
        int nDigits;
        if (  is_integer(fargs[1], &nDigits)
           && nDigits <= 2)
        {
            int iBase = mux_atol(fargs[1]);
            if (10 == iBase)
            {
                kBase = kCommon;
            }
#ifdef HAVE_LOG2
            else if (2 == iBase)
            {
                kBase = kBinary;
            }
#endif
            else
            {
                kBase = kOther;
                base = mux_atof(fargs[1]);
            }
        }
        else if (  'e' == fargs[1][0]
                && '\0' == fargs[1][1])
        {
            kBase = kNatural;
        }
        else
        {
            kBase = kOther;
            base = mux_atof(fargs[1]);
        }
    }
    else
    {
        kBase = kCommon;
    }

    if (  kOther == kBase
       && base <= 1)
    {
        safe_str(T("#-1 BASE OUT OF RANGE"), buff, bufc);
        return;
    }

#ifndef HAVE_IEEE_FP_SNAN
    if (val < 0.0)
    {
        safe_str(T("Ind"), buff, bufc);
        return;
    }
    else if (0.0 == val)
    {
        safe_str(T("-Inf"), buff, bufc);
        return;
    }
    else
#endif
    {
        mux_FPRestore();
        if (kCommon == kBase)
        {
            val = log10(val);
        }
        else if (kNatural == kBase)
        {
            val = log(val);
        }
#ifdef HAVE_LOG2
        else if (kBinary == kBase)
        {
            val = log2(val);
        }
#endif
        else
        {
            val = log(val)/log(base);
        }
        mux_FPSet();
    }
    fval(buff, bufc, val);
}

FUNCTION(fun_sqrt)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val;

    val = mux_atof(fargs[0]);
#ifndef HAVE_IEEE_FP_SNAN
    if (val < 0.0)
    {
        safe_str(T("Ind"), buff, bufc);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_bool(is_integer(fargs[0], nullptr), buff, bufc);
}

FUNCTION(fun_and)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UTF8 *temp = alloc_lbuf("fun_cand");
    for (int i = 0; i < nfargs && val && !alarm_clock.alarmed; i++)
    {
        UTF8 *bp = temp;
        mux_exec(fargs[i], LBUF_SIZE-1, temp, &bp, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *bp = '\0';
        val = isTRUE(mux_atol(temp));
    }
    free_lbuf(temp);
    safe_bool(val, buff, bufc);
}

FUNCTION(fun_cor)
{
    bool val = false;
    UTF8 *temp = alloc_lbuf("fun_cor");
    for (int i = 0; i < nfargs && !val && !alarm_clock.alarmed; i++)
    {
        UTF8 *bp = temp;
        mux_exec(fargs[i], LBUF_SIZE-1, temp, &bp, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *bp = '\0';
        val = isTRUE(mux_atol(temp));
    }
    free_lbuf(temp);
    safe_bool(val, buff, bufc);
}

FUNCTION(fun_candbool)
{
    bool val = true;
    UTF8 *temp = alloc_lbuf("fun_candbool");
    for (int i = 0; i < nfargs && val && !alarm_clock.alarmed; i++)
    {
        UTF8 *bp = temp;
        mux_exec(fargs[i], LBUF_SIZE-1, temp, &bp, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *bp = '\0';
        val = xlate(temp);
    }
    free_lbuf(temp);
    safe_bool(val, buff, bufc);
}

FUNCTION(fun_corbool)
{
    bool val = false;
    UTF8 *temp = alloc_lbuf("fun_corbool");
    for (int i = 0; i < nfargs && !val && !alarm_clock.alarmed; i++)
    {
        UTF8 *bp = temp;
        mux_exec(fargs[i], LBUF_SIZE-1, temp, &bp, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
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
static const UTF8 *bigones[] =
{
    T(""),
    T("thousand"),
    T("million"),
    T("billion"),
    T("trillion")
};

static const UTF8 *singles[] =
{
    T(""),
    T("one"),
    T("two"),
    T("three"),
    T("four"),
    T("five"),
    T("six"),
    T("seven"),
    T("eight"),
    T("nine")
};

static const UTF8 *teens[] =
{
    T("ten"),
    T("eleven"),
    T("twelve"),
    T("thirteen"),
    T("fourteen"),
    T("fifteen"),
    T("sixteen"),
    T("seventeen"),
    T("eighteen"),
    T("nineteen")
};

static const UTF8 *tens[] =
{
    T(""),
    T(""),
    T("twenty"),
    T("thirty"),
    T("forty"),
    T("fifty"),
    T("sixty"),
    T("seventy"),
    T("eighty"),
    T("ninety")
};

static const UTF8 *th_prefix[] =
{
    T(""),
    T("ten"),
    T("hundred")
};

class CSpellNum
{
public:
    void SpellNum(const UTF8 *p, UTF8 *buff_arg, UTF8 **bufc_arg);

private:
    void TwoDigits(const UTF8 *p);
    void ThreeDigits(const UTF8 *p, size_t iBigOne);
    void ManyDigits(size_t n, const UTF8 *p, bool bHundreds);
    void FractionalDigits(size_t n, const UTF8 *p);

    void StartWord(void);
    void AddWord(const UTF8 *p);

    UTF8 *buff;
    UTF8 **bufc;
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

void CSpellNum::AddWord(const UTF8 *p)
{
    safe_str(p, buff, bufc);
}

// Handle two-character sequences.
//
void CSpellNum::TwoDigits(const UTF8 *p)
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
        AddWord(T("-"));
        AddWord(singles[n1]);
    }
}

// Handle three-character sequences.
//
void CSpellNum::ThreeDigits(const UTF8 *p, size_t iBigOne)
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
        AddWord(T("hundred"));
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
void CSpellNum::ManyDigits(size_t n, const UTF8 *p, bool bHundreds)
{
    // Handle special Hundreds cases.
    //
    if (  bHundreds
       && n == 4
       && p[1] != '0')
    {
        TwoDigits(p);
        StartWord();
        AddWord(T("hundred"));
        TwoDigits(p+2);
        return;
    }

    // Handle normal cases.
    //
    size_t ndiv = ((n + 2) / 3) - 1;
    size_t nrem = n % 3;
    UTF8 buf[3];
    if (nrem == 0)
    {
        nrem = 3;
    }

    size_t j = nrem;
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
void CSpellNum::FractionalDigits(size_t n, const UTF8 *p)
{
    ManyDigits(n, p, false);
    if (  0 < n
       && n < 15)
    {
        size_t d = n / 3;
        size_t r = n % 3;
        StartWord();
        if (r != 0)
        {
            AddWord(th_prefix[r]);
            if (d != 0)
            {
                AddWord(T("-"));
            }
        }
        AddWord(bigones[d]);
        AddWord(T("th"));
        INT64 i64 = mux_atoi64(p);
        if (i64 != 1)
        {
            AddWord(T("s"));
        }
    }
}

void CSpellNum::SpellNum(const UTF8 *number, UTF8 *buff_arg, UTF8 **bufc_arg)
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
        AddWord(T("negative"));
        number++;
    }

    // Trim Zeroes from Beginning.
    //
    while (*number == '0')
    {
        number++;
    }

    const UTF8 *pA = number;
    while (mux_isdigit(*number))
    {
        number++;
    }
    size_t nA = number - pA;

    const UTF8 *pB  = nullptr;
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
        safe_str(T("#-1 ARGUMENT MUST BE A NUMBER"), buff, bufc);
        return;
    }

    if (nA == 0)
    {
        if (nB == 0)
        {
            StartWord();
            AddWord(T("zero"));
        }
    }
    else
    {
        ManyDigits(nA, pA, true);
        if (nB)
        {
            StartWord();
            AddWord(T("and"));
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
    UNUSED_PARAMETER(eval);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const UTF8 *number = fargs[0];

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

    const UTF8 *pA = number;
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
        safe_str(T("#-1 ARGUMENT MUST BE A POSITIVE NUMBER"), buff, bufc);
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
    static const UTF8 aLetters[4][3] =
    {
        { 'I', 'V', 'X' },
        { 'X', 'L', 'C' },
        { 'C', 'D', 'M' },
        { 'M', ' ', ' ' }
    };

    static const UTF8 *aCode[10] =
    {
        T(""),
        T("1"),
        T("11"),
        T("111"),
        T("12"),
        T("2"),
        T("21"),
        T("211"),
        T("2111"),
        T("13")
    };

    while (nA--)
    {
        const UTF8 *pCode = aCode[*pA - '0'];
        const UTF8 *pLetters = aLetters[nA];

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
    UNUSED_PARAMETER(eval);
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

        UTF8 *cp = trim_space_sep(fargs[0], sep);
        while (cp && bValue)
        {
            UTF8 *curr = split_token(&cp, sep);
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
    UNUSED_PARAMETER(eval);
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

        UTF8 *cp = trim_space_sep(fargs[0], sep);
        while (cp && !bValue)
        {
            UTF8 *curr = split_token(&cp, sep);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UINT64 val = UINT64_MAX_VALUE;
    for (int i = 0; i < nfargs; i++)
    {
        if (is_integer(fargs[i], nullptr))
        {
            val &= mux_atoi64(fargs[i]);
        }
        else
        {
            safe_str(T("#-1 ARGUMENTS MUST BE INTEGERS"), buff, bufc);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UINT64 val = 0;
    for (int i = 0; i < nfargs; i++)
    {
        if (is_integer(fargs[i], nullptr))
        {
            val |= mux_atoi64(fargs[i]);
        }
        else
        {
            safe_str(T("#-1 ARGUMENTS MUST BE INTEGERS"), buff, bufc);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (  is_integer(fargs[0], nullptr)
       && is_integer(fargs[1], nullptr))
    {
        INT64 a = mux_atoi64(fargs[0]);
        INT64 b = mux_atoi64(fargs[1]);
        safe_i64toa(a & ~(b), buff, bufc);
    }
    else
    {
        safe_str(T("#-1 ARGUMENTS MUST BE INTEGERS"), buff, bufc);
    }
}

FUNCTION(fun_bxor)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UINT64 val = 0;
    for (int i = 0; i < nfargs; i++)
    {
        if (is_integer(fargs[i], nullptr))
        {
            val ^= mux_atoi64(fargs[i]);
        }
        else
        {
            safe_str(T("#-1 ARGUMENTS MUST BE INTEGERS"), buff, bufc);
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
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UINT32 ulCRC32 = 0;
    for (int i = 0; i < nfargs; i++)
    {
        size_t n = strlen((char *)fargs[i]);
        ulCRC32 = CRC32_ProcessBuffer(ulCRC32, fargs[i], n);
    }
    safe_i64toa(ulCRC32, buff, bufc);
}

void safe_hex(UINT8 md[], size_t len, bool bUpper, __in UTF8 *buff, __deref_inout UTF8 **bufc)
{
    UTF8 *buf = nullptr;
    try
    {
        buf = new UTF8[(len * 2) + 1];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == buf)
    {
        return;
    }

    int bufoffset = 0;
    const UTF8 *Digits16 = bUpper ? Digits16U : Digits16L;
    for (size_t i = 0; i < len; i++)
    {
        UINT8 c = md[i];
        buf[bufoffset++] = Digits16[(c >> 4) & 0x0F];
        buf[bufoffset++] = Digits16[(c     ) & 0x0F];
    }
    buf[bufoffset] = '\0';
    safe_str(buf, buff, bufc);
    delete [] buf;
}

void sha1_helper(int nfargs, __in UTF8 *fargs[], __in UTF8 *buff, __deref_inout UTF8 **bufc)
{
    SHA_CTX shac;
    SHA1_Init(&shac);
    for (int i = 0; i < nfargs; ++i)
    {
        SHA1_Update(&shac, fargs[i], strlen((const char *)fargs[i]));
    }
    UINT8 md[SHA_DIGEST_LENGTH];
    SHA1_Final(md, &shac);
    safe_hex(md, SHA_DIGEST_LENGTH, true, buff, bufc);
}

FUNCTION(fun_sha1)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    sha1_helper(nfargs, fargs, buff, bufc);
}

FUNCTION(fun_digest)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

#ifdef UNIX_DIGEST
    EVP_MD_CTX *ctx;
#if HAVE_EVP_MD_CTX_NEW
    ctx = EVP_MD_CTX_new();
#elif HAVE_EVP_MD_CTX_CREATE
    ctx = EVP_MD_CTX_create();
#else
#error Need EVP_MD_CTX_new() or EVP_MD_CTX_create().
#endif

    const EVP_MD *mp = EVP_get_digestbyname((const char *)fargs[0]);
    if (nullptr == mp)
    {
        safe_str(T("#-1 UNSUPPORTED DIGEST TYPE"), buff, bufc);
        return;
    }

    EVP_DigestInit(ctx, mp);

    int i;
    for (i = 1; i < nfargs; i++)
    {
        EVP_DigestUpdate(ctx, fargs[i], strlen((const char *)fargs[i]));
    }

    unsigned int len = 0;
    UINT8 md[EVP_MAX_MD_SIZE];
    EVP_DigestFinal(ctx, md, &len);
#if HAVE_EVP_MD_CTX_NEW
    EVP_MD_CTX_free(ctx);
#elif HAVE_EVP_MD_CTX_CREATE
    EVP_MD_CTX_destroy(ctx);
#else
#error Need EVP_MD_CTX_new() or EVP_MD_CTX_create().
#endif
    safe_hex(md, len, true, buff, bufc);
#else
    if (mux_stricmp(fargs[0], T("sha1")) == 0)
    {
        sha1_helper(nfargs-1, fargs+1, buff, bufc);
    }
    else
    {
        safe_str(T("#-1 UNSUPPORTED DIGEST TYPE"), buff, bufc);
    }
#endif // UNIX_DIGEST
}
