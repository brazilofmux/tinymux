// svdrand.cpp -- Random Numbers
//
// $Id: svdrand.cpp,v 1.5 2000-04-25 18:32:39 sdennis Exp $
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

#define IA 16807
#define IM 2147483647
#define IQ 127773
#define IR 2836
#define NTAB 32
#define NDIV (1+(IM-1)/NTAB)

static long idum = 0;

void SeedRandomNumberGenerator(void)
{
    CLinearTimeAbsolute lsaNow;
    lsaNow.GetUTC();
    idum = -(long)(lsaNow.ReturnSeconds() & 0x7FFFFFFFUL);
    if (1000 < idum)
    {
        idum = -idum;
    }
    else if (-1000 < idum)
    {
        idum -= 22261048;
    }
}

static long iy=0;
static long iv[NTAB];

#if 0
long ran1(void)
{
    int j;
    long k;
    
    if (idum <= 0 || !iy)
    {
        if (-(idum) < 1) idum=1;
        else idum = -(idum);
        for (j=NTAB+7;j>=0;j--)
        {
            k=(idum)/IQ;
            idum=IA*(idum-k*IQ)-IR*k;
            if (idum < 0) idum += IM;
            if (j < NTAB) iv[j] = idum;
        }
        iy=iv[0];
    }
    k=(idum)/IQ;
    idum=IA*(idum-k*IQ)-IR*k;
    if (idum < 0) idum += IM;
    j=iy/NDIV;
    iy=iv[j];
    iv[j] = idum;
    
    return iy;
}
#endif

#define IM1 2147483563
#define IM2 2147483399
#define IA1 40014
#define IA2 40692
#define IQ1 53668
#define IQ2 52774
#define IR1 12211
#define IR2 3791
#define NDIV2 (1+(IM1-1)/NTAB)

static long idum2=123456789;

long ran2(void)
{
    int j;
    long k;
    
    if (idum <= 0)
    {
        if (-idum < 1) idum = 1;
        else idum = -idum;
        idum2 = idum;
        for ( j = NTAB + 7; j >= 0; j-- )
        {
            k = idum/IQ1;
            idum = IA1 * (idum - k*IQ1) - k*IR1;
            if ( idum < 0 ) idum += IM1;
            if ( j < NTAB ) iv[j] = idum;
        }
        iy = iv[0];
    }
    k = idum/IQ1;
    idum = IA1 * (idum - k*IQ1) - k*IR1;
    if ( idum < 0 ) idum += IM1;
    k = idum2/IQ2;
    idum2 = IA2*(idum2 - k*IQ2) - k*IR2;
    if ( idum2 < 0 ) idum2 += IM2;
    j = iy/NDIV2;
    iy = iv[j] - idum2;
    iv[j] = idum;
    if ( iy < 0 ) iy += IM1;
    return iy;
}

#define AM (1.0/IM)
#define EPS 1.2e-7
#define RNMX (1.0-EPS)

#if 0
// fran1 -- return a random floating-point number on the interval [0,1)
//
double fran1(void)
{
    double temp = AM*ran1();
    if (temp > RNMX) return RNMX;
    else return temp;
}
#endif

// fran2 -- return a random floating-point number on the interval [0,1)
//
double fran2(void)
{
    double temp = AM*ran2();
    if (temp > RNMX) return RNMX;
    else return temp;
}

double RandomFloat(double flLow, double flHigh)
{
    double fl = fran2(); // double in [0,1)
    return (fl * (flHigh-flLow)) + flLow; // double in [low,high)
}

/* RandomLong -- return a long on the interval [lLow, lHigh]
 */
long RandomLong(long lLow, long lHigh)
{
#if 0

    return lLow + ran2() % (lHigh-lLow+1);

#else

  /* In order to be perfectly anal about not introducing any further sources
   * of statistical bias, we're going to call rand2() until we get a number
   * less than the greatest representable multiple of x. We'll then return
   * n mod x.
   */
  long n;
  unsigned long x = (lHigh-lLow+1);
  long maxAcceptable = LONG_MAX - (LONG_MAX%x);

  if (x <= 0 || LONG_MAX < x-1)
  {
    return -1;
  }
  do
  {
    n = ran2();
  } while (n >= maxAcceptable);

  /* N.B. This loop happens in randomized constant time, and pretty damn
   * fast randomized constant time too, since P(LONG_MAX - n < LONG_MAX % x)
   * < 0.5 for any x, so for any X, the average number of times we should
   * have to call ran2() is less than 2.
   */
  return lLow + (n % x);

#endif
}
