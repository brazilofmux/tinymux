// svdrand.cpp -- Random Numbers, CLinearTimeAbsolute, and CLinearTimeDelta
// modules
//
// $Id: svdrand.cpp,v 1.1 2000-04-11 07:14:47 sdennis Exp $
//
// Random Numbers based on algorithms presented in "Numerical Recipes in C",
// Cambridge Press, 1992.
// 
// Date/Time code based on algorithms presented in "Calendrical Calculations",
// Cambridge Press, 1998.
//
// RandomLong() and do_convtime() were derived from existing game server code.
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
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

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

// fran1 -- return a random floating-point number on the interval [0,1)
//
#define AM (1.0/IM)
#define EPS 1.2e-7
#define RNMX (1.0-EPS)
double fran1(void)
{
    double temp = AM*ran1();
    if (temp > RNMX) return RNMX;
    else return temp;
}

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

int iMod(int x, int y)
{
    if (x < 0)
    {
        return ((x+1) % y) + y - 1;
    }
    else
    {
        return x % y;
    }
}

#if 0
INT64 i64Mod(INT64 x, INT64 y)
{
    if (x < 0)
    {
        return ((x+1) % y) + y - 1;
    }
    else
    {
        return x % y;
    }
}

int iModAdjusted(int x, int y)
{
    return iMod(x - 1, y) + 1;
}

INT64 i64ModAdjusted(INT64 x, INT64 y)
{
    return i64Mod(x - 1, y) + 1;
}
#endif

int DCL_CDECL iFloorDivision(int x, int y)
{
    if (x < 0)
    {
        return (x - y + 1) / y;
    }
    else
    {
        return x / y;
    }
}

INT64 DCL_CDECL i64FloorDivision(INT64 x, INT64 y)
{
    if (x < 0)
    {
        return (x - y + 1) / y;
    }
    else
    {
        return x / y;
    }
}

int DCL_CDECL iFloorDivisionMod(int x, int y, int *piMod)
{
    if (x < 0)
    {
        *piMod = ((x+1) % y) + y - 1;
        return (x - y + 1) / y;
    }
    else
    {
        *piMod = x % y;
        return x / y;
    }
}

INT64 DCL_CDECL i64FloorDivisionMod(INT64 x, INT64 y, INT64 *piMod)
{
    if (x < 0)
    {
        *piMod = ((x+1) % y) + y - 1;
        return (x - y + 1) / y;
    }
    else
    {
        *piMod = x % y;
        return x / y;
    }
}

#if 0
int iCeilingDivision(int x, int y)
{
    if (x < 0)
    {
        return x / y;
    }
    else
    {
        return (x + y - 1) / y;
    }
}

INT64 i64CeilingDivision(INT64 x, INT64 y)
{
    if (x < 0)
    {
        return x / y;
    }
    else
    {
        return (x + y - 1) / y;
    }
}
#endif

#define FACTOR_NANOSECONDS_PER_100NS 100
#define FACTOR_100NS_PER_MICROSECOND 10
#define FACTOR_100NS_PER_MILLISECOND 10000
#ifdef WIN32
#define FACTOR_100NS_PER_SECOND      10000000i64
#define EPOCH_OFFSET 116444736000000000i64
#else // WIN32
#define FACTOR_100NS_PER_SECOND      10000000ULL
#define EPOCH_OFFSET 116444736000000000ull
#endif // WIN32
const INT64 FACTOR_100NS_PER_MINUTE = FACTOR_100NS_PER_SECOND*60;
const INT64 FACTOR_100NS_PER_HOUR   = FACTOR_100NS_PER_MINUTE*60;
const INT64 FACTOR_100NS_PER_DAY = FACTOR_100NS_PER_HOUR*24;

int CLinearTimeAbsolute::m_nCount = 0;
char CLinearTimeAbsolute::m_Buffer[204];

void GetUTCLinearTime(INT64 *plt);
void GetLocalFieldedTime(FIELDEDTIME *ft);

int operator<(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb)
{
    return lta.m_tAbsolute < ltb.m_tAbsolute;
}

int operator>(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb)
{
    return lta.m_tAbsolute > ltb.m_tAbsolute;
}

int operator==(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb)
{
    return lta.m_tAbsolute == ltb.m_tAbsolute;
}

int operator<=(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb)
{
    return lta.m_tAbsolute <= ltb.m_tAbsolute;
}

CLinearTimeAbsolute::CLinearTimeAbsolute(const CLinearTimeAbsolute& ltaOrigin, const CLinearTimeDelta& ltdOffset)
{
    m_tAbsolute = ltaOrigin.m_tAbsolute + ltdOffset.m_tDelta;
}

char *CLinearTimeAbsolute::ReturnSecondsString(void)
{
    INT64 lt = i64FloorDivision(m_tAbsolute - EPOCH_OFFSET, FACTOR_100NS_PER_SECOND);
    Tiny_i64toa(lt, m_Buffer);
    return m_Buffer;
}

CLinearTimeAbsolute::CLinearTimeAbsolute(const CLinearTimeAbsolute &lta)
{
    m_tAbsolute = lta.m_tAbsolute;
}

CLinearTimeAbsolute::CLinearTimeAbsolute(void)
{
    m_tAbsolute = 0;
}

CLinearTimeDelta::CLinearTimeDelta(void)
{
    m_tDelta = 0;
}

void CLinearTimeDelta::ReturnTimeValueStruct(struct timeval *tv)
{
    INT64 Leftover;
    tv->tv_sec = (long)i64FloorDivisionMod(m_tDelta, FACTOR_100NS_PER_SECOND, &Leftover);
    tv->tv_usec = (long)i64FloorDivision(Leftover, FACTOR_100NS_PER_MICROSECOND);
}

// Time string format is usually 24 characters long, in format
// Ddd Mmm DD HH:MM:SS YYYY
//
// However, the year may be larger than 4 characters.
//

char *DayOfWeekString[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
const char daystab[] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
const char *monthtab[] =
{
    "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

void CLinearTimeDelta::SetMilliseconds(unsigned long arg_dwMilliseconds)
{
    m_tDelta = arg_dwMilliseconds * FACTOR_100NS_PER_MILLISECOND;
}

long CLinearTimeDelta::ReturnMilliseconds(void)
{
    return (long)i64FloorDivision(m_tDelta, FACTOR_100NS_PER_MILLISECOND);
}

void CLinearTimeDelta::SetSeconds(INT64 arg_tSeconds)
{
    m_tDelta = arg_tSeconds;
    m_tDelta *= FACTOR_100NS_PER_SECOND;
}

void CLinearTimeAbsolute::SetSeconds(INT64 arg_tSeconds)
{
    m_tAbsolute = arg_tSeconds;
    m_tAbsolute *= FACTOR_100NS_PER_SECOND;

    // Epoch difference between (00:00:00 UTC, January 1, 1970) and
    // (00:00:00 UTC, January 1, 1601).
    //
    // TODO: Verify that I haven't included an extra 8 hours in here
    // because I'm in PST zone.
    //
    m_tAbsolute += EPOCH_OFFSET;
}

INT64 CLinearTimeAbsolute::ReturnSeconds(void)
{
    // INT64 is in hundreds of nanoseconds.
    // And the Epoch is 0:00 1/1/1601 instead of 0:00 1/1/1970
    //
    return i64FloorDivision(m_tAbsolute - EPOCH_OFFSET, FACTOR_100NS_PER_SECOND);
}

BOOL isLeapYear(unsigned long iYear)
{
    if (iMod(iYear, 4) != 0)
    {
        // Not a leap year.
        //
        return FALSE;
    }
    unsigned long wMod = iMod(iYear, 400);
    if ((wMod == 100) || (wMod == 200) || (wMod == 300))
    {
        // Not a leap year.
        //
        return FALSE;
    }
    return TRUE;
}

BOOL isValidDate(int iYear, int iMonth, int iDay)
{
    if (iYear < -27256 || 30826 < iYear)
    {
        return FALSE;
    }
    if (iMonth < 1 || 12 < iMonth)
    {
        return FALSE;
    }
    if (iDay < 1 || daystab[iMonth-1] < iDay)
    {
        return FALSE;
    }
    if (iMonth == 2 && iDay == 29 && !isLeapYear(iYear))
    {
        return FALSE;
    }
    return TRUE;
}

int FixedFromGregorian(int iYear, int iMonth, int iDay)
{
    iYear = iYear - 1;
    int iFixedDay = 365 * iYear;
    iFixedDay += iFloorDivision(iYear, 4);
    iFixedDay -= iFloorDivision(iYear, 100);
    iFixedDay += iFloorDivision(iYear, 400);
    iFixedDay += iFloorDivision(367 * iMonth - 362, 12);
    iFixedDay += iDay;

    if (iMonth > 2)
    {
        if (isLeapYear(iYear+1))
        {
            iFixedDay -= 1;
        }
        else
        {
            iFixedDay -= 2;
        }
    }

    // At this point, iFixedDay has an epoch of 1 R.D.
    //
    return iFixedDay;
}

int FixedFromGregorian_Adjusted(int iYear, int iMonth, int iDay)
{
    int iFixedDay = FixedFromGregorian(iYear, iMonth, iDay);

    // At this point, iFixedDay has an epoch of 1 R.D.
    // We need an Epoch of (00:00:00 UTC, January 1, 1601)
    // 
    return iFixedDay - 584389;
}


// Epoch of iFixedDay should be 1 R.D.
//
void GregorianFromFixed(int iFixedDay, int &iYear, int &iMonth,  int &iDayOfYear, int &iDayOfMonth, int &iDayOfWeek)
{
    int d0 = iFixedDay - 1;
    int d1, n400 = iFloorDivisionMod(d0, 146097, &d1);
    int d2, n100 = iFloorDivisionMod(d1,  36524, &d2);
    int d3, n4   = iFloorDivisionMod(d2,   1461, &d3);
    int d4, n1   = iFloorDivisionMod(d3,    365, &d4);
    d4 = d4 + 1;

    iYear = 400*n400 + 100*n100 + 4*n4 + n1;

    if (n100 != 4 && n1 != 4)
    {
        iYear = iYear + 1;
    }

    static int cache_iYear = 99999;
    static int cache_iJan1st = 0;
    static int cache_iMar1st = 0;
    int iFixedDayOfJanuary1st;
    int iFixedDayOfMarch1st;
    if (iYear == cache_iYear)
    {
        iFixedDayOfJanuary1st = cache_iJan1st;
        iFixedDayOfMarch1st = cache_iMar1st;
    }
    else
    {
        cache_iYear = iYear;
        cache_iJan1st = iFixedDayOfJanuary1st = FixedFromGregorian(iYear, 1, 1);
        cache_iMar1st = iFixedDayOfMarch1st = FixedFromGregorian(iYear, 3, 1);
    }
        

    int iPriorDays = iFixedDay - iFixedDayOfJanuary1st;
    int iCorrection;
    if (iFixedDay < iFixedDayOfMarch1st)
    {
        iCorrection = 0;
    }
    else if (isLeapYear(iYear))
    {
        iCorrection = 1;
    }
    else
    {
        iCorrection = 2;
    }
    
    iMonth = (12*(iPriorDays+iCorrection)+373)/367;
    iDayOfMonth = iFixedDay - FixedFromGregorian(iYear, iMonth, 1) + 1;
    iDayOfYear = iPriorDays + 1;

    // Calculate the Day of week using the linear progression of days.
    //
    iDayOfWeek = iMod(iFixedDay, 7);
}

void GregorianFromFixed_Adjusted(int iFixedDay, int &iYear, int &iMonth, int &iDayOfYear, int &iDayOfMonth, int &iDayOfWeek)
{
    // We need to convert the Epoch to 1 R.D. from
    // (00:00:00 UTC, January 1, 1601)
    //
    GregorianFromFixed(iFixedDay + 584389, iYear, iMonth, iDayOfYear, iDayOfMonth, iDayOfWeek);
}

/*
 * converts time string to a struct tm. Returns 1 on success, 0 on fail.
 * * Time string format is always 24 characters long, in format
 * * Ddd Mmm DD HH:MM:SS YYYY
 */

#define get_substr(buf, p) { \
    p = (char *)strchr(buf, ' '); \
    if (p) { \
        *p++ = '\0'; \
        while (*p == ' ') p++; \
    } \
}

int do_convtime(const char *str, FIELDEDTIME *ft)
{
    char *buf, *p, *q;
    int i;
    
    if (!str || !ft)
        return 0;

    while (*str == ' ')
        str++;

    // Make a temp copy of arg.
    //
    buf = p = alloc_sbuf("do_convtime");
    safe_sb_str(str, buf, &p);
    *p = '\0';

    // Day-of-week or month.
    //
    get_substr(buf, p);
    if (!p || strlen(buf) != 3)
    {
        free_sbuf(buf);
        return 0;
    }
    for (i = 0; (i < 12) && string_compare(monthtab[i], p); i++) ;
    if (i == 12)
    {
        // Month
        //
        get_substr(p, q);
        if (!q || strlen(p) != 3)
        {
            free_sbuf(buf);
            return 0;
        }
        for (i = 0; (i < 12) && string_compare(monthtab[i], p); i++) ;
        if (i == 12)
        {
            free_sbuf(buf);
            return 0;
        }
        p = q;
    }
    ft->iMonth = i + 1; // January = 1, February = 2, etc.
    
    // Day of month.
    //
    get_substr(p, q);
    if (!q || (ft->iDayOfMonth = (unsigned short)Tiny_atol(p)) < 1 || ft->iDayOfMonth > daystab[i])
    {
        free_sbuf(buf);
        return 0;
    }

    // Hours
    //
    p = (char *)strchr(q, ':');
    if (!p)
    {
        free_sbuf(buf);
        return 0;
    }
    *p++ = '\0';
    ft->iHour = (unsigned short)Tiny_atol(q);
    if (ft->iHour > 23)
    {
        free_sbuf(buf);
        return 0;
    }
    if (ft->iHour == 0)
    {
        while (Tiny_IsSpace[(unsigned char)*q])
            q++;

        if (*q != '0')
        {
            free_sbuf(buf);
            return 0;
        }
    }

    // Minutes
    //
    q = (char *)strchr(p, ':');
    if (!q)
    {
        free_sbuf(buf);
        return 0;
    }
    *q++ = '\0';
    if ((ft->iMinute = (unsigned short)Tiny_atol(p)) > 59)
    {
        free_sbuf(buf);
        return 0;
    }
    if (ft->iMinute == 0)
    {
        while (Tiny_IsSpace[(unsigned char)*p])
            p++;

        if (*p != '0')
        {
            free_sbuf(buf);
            return 0;
        }
    }

    // Seconds
    //
    get_substr(q, p);
    if (!p || (ft->iSecond = (unsigned short)Tiny_atol(q)) > 59)
    {
        free_sbuf(buf);
        return 0;
    }
    if (ft->iSecond == 0)
    {
        while (Tiny_IsSpace[(unsigned char)*q])
            q++;

        if (*q != '0')
        {
            free_sbuf(buf);
            return 0;
        }
    }

    // Milliseconds, Microseconds and Nanoseconds
    //
    ft->iMillisecond = 0;
    ft->iMicrosecond = 0;
    ft->iNanosecond = 0;

    // DayOfYear and DayOfWeek
    //
    ft->iDayOfYear = 0;
    ft->iDayOfWeek = 0;

    // Year
    //
    get_substr(p, q);
    ft->iYear = (short)Tiny_atol(p);
    if (ft->iYear == 0)
    {
        while (Tiny_IsSpace[(unsigned char)*p])
            p++;

        if (*p != '0')
        {
            free_sbuf(buf);
            return 0;
        }
    }
    free_sbuf(buf);
    return isValidDate(ft->iYear, ft->iMonth, ft->iDayOfMonth);
}

CLinearTimeDelta::CLinearTimeDelta(CLinearTimeAbsolute t0, CLinearTimeAbsolute t1)
{
    m_tDelta = t1.m_tAbsolute - t0.m_tAbsolute;
}

long CLinearTimeDelta::ReturnDays(void)
{
    return (long)i64FloorDivision(m_tDelta, FACTOR_100NS_PER_DAY);
}

long CLinearTimeDelta::ReturnSeconds(void)
{
    return (long)i64FloorDivision(m_tDelta, FACTOR_100NS_PER_SECOND);
}

BOOL CLinearTimeAbsolute::ReturnFields(FIELDEDTIME *arg_tStruct)
{
    return LinearTimeToFieldedTime(m_tAbsolute, arg_tStruct);
}

BOOL CLinearTimeAbsolute::SetString(const char *arg_tBuffer)
{
    FIELDEDTIME ft;
    GetLocalFieldedTime(&ft);
    if (do_convtime(arg_tBuffer, &ft))
    {
        if (FieldedTimeToLinearTime(&ft, &m_tAbsolute))
        {
            return TRUE;
        }
    }
    m_tAbsolute = 0;
    return FALSE;
}

CLinearTimeDelta operator-(const CLinearTimeAbsolute& ltaA, const CLinearTimeAbsolute& ltaB)
{
    CLinearTimeDelta ltd;
    ltd.m_tDelta = ltaA.m_tAbsolute - ltaB.m_tAbsolute;
    return ltd;
}

CLinearTimeAbsolute operator+(const CLinearTimeAbsolute& ltaA, const CLinearTimeDelta& ltdB)
{
    CLinearTimeAbsolute lta;
    lta.m_tAbsolute = ltaA.m_tAbsolute + ltdB.m_tDelta;
    return lta;
}

void CLinearTimeAbsolute::operator=(const CLinearTimeAbsolute& lta)
{
    m_tAbsolute = lta.m_tAbsolute;
}

CLinearTimeDelta operator*(const CLinearTimeDelta& ltd, int Scale)
{
    CLinearTimeDelta ltdResult;
    ltdResult.m_tDelta = ltd.m_tDelta * Scale;
    return ltdResult;
}

int operator/(const CLinearTimeDelta& ltdA, const CLinearTimeDelta& ltdB)
{
    int iResult = (int)(ltdA.m_tDelta / ltdB.m_tDelta);
    return iResult;
}

int operator<(const CLinearTimeDelta& ltdA, const CLinearTimeDelta& ltdB)
{
    return ltdA.m_tDelta < ltdB.m_tDelta;
}

int operator>(const CLinearTimeDelta& ltdA, const CLinearTimeDelta& ltdB)
{
    return ltdA.m_tDelta > ltdB.m_tDelta;
}

void CLinearTimeAbsolute::operator-=(const CLinearTimeDelta& ltd)
{
    m_tAbsolute -= ltd.m_tDelta;
}

void CLinearTimeAbsolute::operator+=(const CLinearTimeDelta& ltd)
{
    m_tAbsolute += ltd.m_tDelta;
}

BOOL CLinearTimeAbsolute::SetFields(FIELDEDTIME *arg_tStruct)
{
    m_tAbsolute = 0;
    return FieldedTimeToLinearTime(arg_tStruct, &m_tAbsolute);
}

void CLinearTimeAbsolute::ReturnUniqueString(char *buffer)
{
    FIELDEDTIME ft;
    if (LinearTimeToFieldedTime(m_tAbsolute, &ft))
    {
        sprintf(buffer, "%04d%02d%02d-%02d%02d%02d", ft.iYear, ft.iMonth,
                ft.iDayOfMonth, ft.iHour, ft.iMinute, ft.iSecond);
    }
    else
    {
        sprintf(buffer, "%03d", m_nCount++);
    }
}

char *CLinearTimeAbsolute::ReturnDateString(void)
{
    FIELDEDTIME ft;
    if (LinearTimeToFieldedTime(m_tAbsolute, &ft))
    {
        sprintf(m_Buffer, "%s %s %02d %02d:%02d:%02d %04d", DayOfWeekString[ft.iDayOfWeek],
            monthtab[ft.iMonth-1], ft.iDayOfMonth, ft.iHour, ft.iMinute, ft.iSecond, ft.iYear);
    }
    else
    {
        m_Buffer[0] = 0;
    }
    return m_Buffer;
}

void CLinearTimeAbsolute::GetUTC(void)
{
    GetUTCLinearTime(&m_tAbsolute);
}

void CLinearTimeAbsolute::GetLocal(void)
{
    FIELDEDTIME ft;
    GetLocalFieldedTime(&ft);
    FieldedTimeToLinearTime(&ft, &m_tAbsolute);
}

BOOL FieldedTimeToLinearTime(FIELDEDTIME *ft, INT64 *plt)
{
    if (!isValidDate(ft->iYear, ft->iMonth, ft->iDayOfMonth))
    {
        *plt = 0;
        return FALSE;
    }

    int iFixedDays = FixedFromGregorian_Adjusted(ft->iYear, ft->iMonth, ft->iDayOfMonth);
    INT64 lt;

    lt  = iFixedDays * FACTOR_100NS_PER_DAY;
    lt += ft->iHour * FACTOR_100NS_PER_HOUR;
    lt += ft->iMinute * FACTOR_100NS_PER_MINUTE;
    lt += ft->iSecond * FACTOR_100NS_PER_SECOND;
    lt += ft->iMicrosecond * FACTOR_100NS_PER_MICROSECOND;
    lt += ft->iMillisecond * FACTOR_100NS_PER_MILLISECOND;
    lt += ft->iNanosecond / FACTOR_NANOSECONDS_PER_100NS;
    
    *plt = lt;
    return TRUE;
}

BOOL LinearTimeToFieldedTime(INT64 lt, FIELDEDTIME *ft)
{
    INT64 ns100;
    int iYear, iMonth, iDayOfYear, iDayOfMonth, iDayOfWeek;

    memset(ft, 0, sizeof(FIELDEDTIME));
    int d0 = (int)i64FloorDivisionMod(lt, FACTOR_100NS_PER_DAY, &ns100);
    GregorianFromFixed_Adjusted(d0, iYear, iMonth, iDayOfYear, iDayOfMonth, iDayOfWeek);
    if (!isValidDate(iYear, iMonth, iDayOfMonth))
    {
        return FALSE;
    }

    ft->iYear = iYear;
    ft->iMonth = iMonth;
    ft->iDayOfYear = iDayOfYear;
    ft->iDayOfMonth = iDayOfMonth;
    ft->iDayOfWeek = iDayOfWeek;

    ft->iHour = (int)(ns100 / FACTOR_100NS_PER_HOUR);
    ns100 = ns100 % FACTOR_100NS_PER_HOUR;
    ft->iMinute = (int)(ns100 / FACTOR_100NS_PER_MINUTE);
    ns100 = ns100 % FACTOR_100NS_PER_MINUTE;
    ft->iSecond = (int)(ns100 / FACTOR_100NS_PER_SECOND);
    ns100 = ns100 % FACTOR_100NS_PER_SECOND;

    ft->iMillisecond = (int)(ns100 % FACTOR_100NS_PER_MILLISECOND);
    ns100 = ns100 % FACTOR_100NS_PER_MILLISECOND;
    ft->iMicrosecond = (int)(ns100 % FACTOR_100NS_PER_MICROSECOND);
    ns100 = ns100 % FACTOR_100NS_PER_MICROSECOND;
    ft->iNanosecond = (int)(ns100 * FACTOR_NANOSECONDS_PER_100NS);

    return TRUE;
}

void CLinearTimeAbsolute::SetSecondsString(char *arg_szSeconds)
{
    INT64 lt = Tiny_atoi64(arg_szSeconds);
    m_tAbsolute = (lt * FACTOR_100NS_PER_SECOND) + EPOCH_OFFSET;
}

// OS Dependent Routines:
//
#ifdef WIN32

#if 0
void GetUTCFieldedTime(FIELDEDTIME *ft)
{
    SYSTEMTIME st;
    GetSystemTime(&st);

    ft->iYear = st.wYear;
    ft->iMonth = st.wMonth;
    ft->iDayOfMonth = st.wDay;
    ft->iDayOfWeek = st.wDayOfWeek;
    ft->iDayOfYear = 0;
    ft->iHour = st.wHour;
    ft->iMinute = st.wMinute;
    ft->iSecond = st.wSecond;
    ft->iMillisecond = st.wMilliseconds;
    ft->iMicrosecond = 0;
    ft->iNanosecond = 0;
}
#endif

void GetUTCLinearTime(INT64 *plt)
{
    SYSTEMTIME st;
    GetSystemTime(&st);

    FIELDEDTIME ft;
    ft.iYear = st.wYear;
    ft.iMonth = st.wMonth;
    ft.iDayOfMonth = st.wDay;
    ft.iDayOfWeek = st.wDayOfWeek;
    ft.iDayOfYear = 0;
    ft.iHour = st.wHour;
    ft.iMinute = st.wMinute;
    ft.iSecond = st.wSecond;
    ft.iMillisecond = st.wMilliseconds;
    ft.iMicrosecond = 0;
    ft.iNanosecond = 0;

    FieldedTimeToLinearTime(&ft, plt);
}

void GetLocalFieldedTime(FIELDEDTIME *ft)
{
    SYSTEMTIME st;
    GetLocalTime(&st);

    ft->iYear = st.wYear;
    ft->iMonth = st.wMonth;
    ft->iDayOfMonth = st.wDay;
    ft->iDayOfWeek = st.wDayOfWeek;
    ft->iDayOfYear = 0;
    ft->iHour = st.wHour;
    ft->iMinute = st.wMinute;
    ft->iSecond = st.wSecond;
    ft->iMillisecond = st.wMilliseconds;
    ft->iMicrosecond = 0;
    ft->iNanosecond = 0;
}

#else // WIN32

#if 0
void GetUTCFieldedTime(FIELDEDTIME *ft)
{
    struct timeval tv;
    struct timezone tz;
    tz.tz_minuteswest = 0;
    tz.tz_dsttime = 0;
    gettimeofday(&tv, &tz);
    time_t seconds = tv.tv_sec;
    struct tm *ptm = gmtime(&seconds);

    ft->iYear = ptm->tm_year + 1900;
    ft->iMonth = ptm->tm_mon + 1;
    ft->iDayOfMonth = ptm->tm_mday;
    ft->iDayOfWeek = ptm->tm_wday;
    ft->iDayOfYear = 0;
    ft->iHour = ptm->tm_hour;
    ft->iMinute = ptm->tm_min;
    ft->iSecond = ptm->tm_sec;
    ft->iMillisecond = (tv.tv_usec/1000);
    ft->iMicrosecond = (tv.tv_usec%1000);
    ft->iNanosecond = 0;
}
#endif

void GetUTCLinearTime(INT64 *plt)
{
    struct timeval tv;
    struct timezone tz;
    tz.tz_minuteswest = 0;
    tz.tz_dsttime = 0;
    gettimeofday(&tv, &tz);

    *plt = (((INT64)tv.tv_sec) * FACTOR_100NS_PER_SECOND)
         + (tv.tv_usec * FACTOR_100NS_PER_MICROSECOND)
         + EPOCH_OFFSET;
}

void GetLocalFieldedTime(FIELDEDTIME *ft)
{
    struct timeval tv;
    struct timezone tz;
    tz.tz_minuteswest = 0;
    tz.tz_dsttime = 0;
    gettimeofday(&tv, &tz);
    time_t seconds = tv.tv_sec;
    struct tm *ptm = localtime(&seconds);

    ft->iYear = ptm->tm_year + 1900;
    ft->iMonth = ptm->tm_mon + 1;
    ft->iDayOfMonth = ptm->tm_mday;
    ft->iDayOfWeek = ptm->tm_wday;
    ft->iDayOfYear = 0;
    ft->iHour = ptm->tm_hour;
    ft->iMinute = ptm->tm_min;
    ft->iSecond = ptm->tm_sec;
    ft->iMillisecond = (tv.tv_usec/1000);
    ft->iMicrosecond = (tv.tv_usec%1000);
    ft->iNanosecond = 0;
}
#endif // WIN32
