// svdrand.cpp -- Random Numbers
//
// $Id: svdrand.cpp,v 1.7 2000-04-27 23:32:20 sdennis Exp $
//
// Random Numbers based on algorithms presented in "Numerical Recipes in C",
// Cambridge Press, 1992.
// 
// RandomLong() was derived from existing game server code.
//
// MUX 2.0
// Copyright (C) 1998 through 2000 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved. Permission is given to
// use this code for building and hosting text-based game servers.
// Permission is given to use this code for other non-commercial
// purposes. To use this code for commercial purposes other than
// building/hosting text-based game servers, contact the author at
// Stephen Dennis <sdennis@svdltd.com> for another license.
//
#include "autoconf.h"
#include "config.h"
#include "timeutil.h"
#include "svdrand.h"
#include "svdhash.h"

#define NTAB 32
static unsigned long iv[NTAB];
static unsigned long iy = 0;
static unsigned long idum = 123456789;
static unsigned long idum2 = 123456789;
static BOOL bSeeded = FALSE;

#define IM1 4294967291UL
#define IM2 4294967279UL
#define IA1 40014UL
#define IA2 40692UL
#define IQ1 107336UL
#define IQ2 105548UL
#define IR1 24587UL
#define IR2 8063UL
#define NDIV2 (1+(IM1-1)/NTAB)

// Schrage's algorithm algorithm for multiplying two unsigned 32-bit
// integers modulo a unsigned 32-bit constant without using
// intermediates larger than an unsigned 32-bit variable.
//
// Given:
//
//  r < q, q = m/a, r = m%a, so that m = aq + r
//
// We calculate:
//
//  (a * z) % m
//
unsigned long ModulusMultiply
(
    unsigned long z,
    unsigned long a,
    unsigned long m,
    unsigned long q,
    unsigned long r
)
{
    unsigned long k = z/q;
    z = a*(z - k*q);
    if (z <= k*r)
    {
        z += m;
    }
    z -= r*k;
    return z;
}

void SeedRandomNumberGenerator(void)
{
    if (bSeeded) return;
    bSeeded = TRUE;

    // Determine the initial seed, idum.
    //
    CLinearTimeAbsolute lsaNow;
    lsaNow.GetUTC();
    idum = (unsigned long)(lsaNow.ReturnSeconds() & 0xFFFFFFFFUL);
    if (idum <= 1000)
    {
        idum += 22261048;
    }

    // ASSERT: 1000 < idum

    // Fill in the shuffle array.
    //
    int j;
    idum2 = idum;
    for (j = 0; j < 8; j++)
    {
        idum = ModulusMultiply(idum, IA1, IM1, IQ1, IR1);
    }
    for (j = NTAB-1; j >= 0; j--)
    {
        idum = ModulusMultiply(idum, IA1, IM1, IQ1, IR1);
        iv[j] = idum;
    }
    iy = iv[0];
}

long ran2(void)
{
    idum  = ModulusMultiply(idum,  IA1, IM1, IQ1, IR1);
    idum2 = ModulusMultiply(idum2, IA2, IM2, IQ2, IR2);
    int j = iy/NDIV2;
    iy = iv[j];
    if (iy < idum2)
    {
        iy += IM1;
    }
    iy -= idum2;
    iv[j] = idum;
    return CRC32_ProcessInteger(iy);
}

// RandomLong -- return a long on the interval [lLow, lHigh]
//
long RandomLong(long lLow, long lHigh)
{
    // Validate parameters
    //
    if (lHigh < lLow)
    {
        return -1;
    }
    else if (lHigh == lLow)
    {
        return lLow;
    }

    unsigned long x = lHigh-lLow;
    if (LONG_MAX < x)
    {
        return -1;
    }
    x++;

    // We can now look for an random number on the interval [0,x-1].
    //
    static unsigned long maxLeftover = 0;
    static unsigned long n = 0;

    if (maxLeftover < x)
    {
        maxLeftover = ULONG_MAX;
        n = ran2();
    }

    // In order to be perfectly conservative about not introducing any
    // further sources of statistical bias, we're going to call rand2()
    // until we get a number less than the greatest representable
    // multiple of x. We'll then return n mod x.
    //
    // N.B. This loop happens in randomized constant time, and pretty
    // damn fast randomized constant time too, since
    //
    //      P(ULONG_MAX - n < ULONG_MAX % x) < 0.5, for any x.
    //
    // So even for the least desireable x, the average number of times
    // we will call ran2() is less than 2.
    //
    unsigned long nLimit = maxLeftover - (maxLeftover%x);
    while (n >= nLimit)
    {
        n = ran2();
        if (maxLeftover != ULONG_MAX)
        {
            maxLeftover = ULONG_MAX;
            nLimit = ULONG_MAX - (ULONG_MAX%x);
        }
    }

    // Save useful, leftover bits. x -always- divides evenly into
    // (nLimit-1). And, the probability of the final n on the
    // interval [0,maxLeftover-1] is evenly distributed.
    //
    maxLeftover = (nLimit-1) / x;
    long nAnswer = lLow + (n%x);
    n /= x;
    return nAnswer;
}
