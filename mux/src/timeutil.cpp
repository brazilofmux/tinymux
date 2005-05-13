// timeutil.cpp -- CLinearTimeAbsolute and CLinearTimeDelta modules.
//
// $Id: timeutil.cpp,v 1.40 2005-05-13 02:12:07 sdennis Exp $
//
// MUX 2.4
// Copyright (C) 1998 through 2004 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.
//
// Date/Time code based on algorithms presented in "Calendrical Calculations",
// Cambridge Press, 1998.
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

// for tzset() and localtime()
//
#include <time.h>

#include "timeutil.h"
#include "stringutil.h"

CMuxAlarm MuxAlarm;

#ifdef SMALLEST_INT_GTE_NEG_QUOTIENT

// The following functions provide a consistent division/modulus function
// regardless of how the platform chooses to provide this function.
//
// Confused yet? Here's an example:
//
// SMALLEST_INT_GTE_NEG_QUOTIENT indicates that this platform computes
// division and modulus like so:
//
//   -9/5 ==> -1 and -9%5 ==> -4
//   and (-9/5)*5 + (-9%5) ==> -1*5 + -4 ==> -5 + -4 ==> -9
//
// The iMod() function uses this to provide LARGEST_INT_LTE_NEG_QUOTIENT
// behavior (required by much math). This behavior computes division and
// modulus like so:
//
//   -9/5 ==> -2 and -9%5 ==> 1
//   and (-9/5)*5 + (-9%5) ==> -2*5 + 1 ==> -10 + 1 ==> -9
//

// Provide LLEQ modulus on a SGEQ platform.
//
int iMod(int x, int y)
{
    if (y < 0)
    {
        if (x <= 0)
        {
            return x % y;
        }
        else
        {
            return ((x-1) % y) + y + 1;
        }
    }
    else
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
}

INT64 i64Mod(INT64 x, INT64 y)
{
    if (y < 0)
    {
        if (x <= 0)
        {
            return x % y;
        }
        else
        {
            return ((x-1) % y) + y + 1;
        }
    }
    else
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
}

// Provide SGEQ modulus on a SGEQ platform.
//
DCL_INLINE int iRemainder(int x, int y)
{
    return x % y;
}

// Provide SGEQ division on a SGEQ platform.
//
DCL_INLINE int iDivision(int x, int y)
{
    return x / y;
}

// Provide LLEQ division on a SGEQ platform.
//
int iFloorDivision(int x, int y)
{
    if (y < 0)
    {
        if (x <= 0)
        {
            return x / y;
        }
        else
        {
            return (x - y - 1) / y;
        }
    }
    else
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
}

INT64 i64FloorDivision(INT64 x, INT64 y)
{
    if (y < 0)
    {
        if (x <= 0)
        {
            return x / y;
        }
        else
        {
            return (x - y - 1) / y;
        }
    }
    else
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
}

int iFloorDivisionMod(int x, int y, int *piMod)
{
    if (y < 0)
    {
        if (x <= 0)
        {
            *piMod = x % y;
            return x / y;
        }
        else
        {
            *piMod = ((x-1) % y) + y + 1;
            return (x - y - 1) / y;
        }
    }
    else
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
}

INT64 i64FloorDivisionMod(INT64 x, INT64 y, INT64 *piMod)
{
    if (y < 0)
    {
        if (x <= 0)
        {
            *piMod = x % y;
            return x / y;
        }
        else
        {
            *piMod = ((x-1) % y) + y + 1;
            return (x - y - 1) / y;
        }
    }
    else
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
#endif // 0

#else // LARGEST_INT_LTE_NEG_QUOTIENT

// Provide LLEQ modulus on a LLEQ platform.
//
int DCL_INLINE iMod(int x, int y)
{
    return x % y;
}

// Provide a SGEQ modulus on a LLEQ platform.
//
int iRemainder(int x, int y)
{
    if (y < 0)
    {
        if (x <= 0)
        {
            return x % y;
        }
        else
        {
            return ((x+1) % y) - y - 1;
        }
    }
    else
    {
        if (x < 0)
        {
            return ((x-1) % y) - y + 1;
        }
        else
        {
            return x % y;
        }
    }
}

INT64 i64Remainder(INT64 x, INT64 y)
{
    if (y < 0)
    {
        if (x <= 0)
        {
            return x % y;
        }
        else
        {
            return ((x+1) % y) - y - 1;
        }
    }
    else
    {
        if (x < 0)
        {
            return ((x-1) % y) - y + 1;
        }
        else
        {
            return x % y;
        }
    }
}

// Provide SGEQ division on a LLEQ platform.
//
int iDivision(int x, int y)
{
    if (y < 0)
    {
        if (x <= 0)
        {
            return x / y;
        }
        else
        {
            return (x + y + 1) / y;
        }
    }
    else
    {
        if (x < 0)
        {
            return (x + y - 1) / y;
        }
        else
        {
            return x / y;
        }
    }
}

INT64 i64Division(INT64 x, INT64 y)
{
    if (y < 0)
    {
        if (x <= 0)
        {
            return x / y;
        }
        else
        {
            return (x + y + 1) / y;
        }
    }
    else
    {
        if (x < 0)
        {
            return (x + y - 1) / y;
        }
        else
        {
            return x / y;
        }
    }
}

// Provide a LLEQ division on a LLEQ platform.
//
int DCL_INLINE iFloorDivision(int x, int y)
{
    return x / y;
}

INT64 DCL_INLINE i64FloorDivisionMod(INT64 x, INT64 y, INT64 *piMod)
{
    *piMod = x % y;
    return x / y;
}
#endif // LARGEST_INT_LTE_NEG_QUOTIENT

int iModAdjusted(int x, int y)
{
    return iMod(x - 1, y) + 1;
}
#if 0
INT64 i64ModAdjusted(INT64 x, INT64 y)
{
    return i64Mod(x - 1, y) + 1;
}
#endif // 0

#ifdef WIN32
const INT64 FACTOR_100NS_PER_SECOND = 10000000i64;
#else // WIN32
const INT64 FACTOR_100NS_PER_SECOND = 10000000ull;
#endif // WIN32
const INT64 FACTOR_100NS_PER_MINUTE = FACTOR_100NS_PER_SECOND*60;
const INT64 FACTOR_100NS_PER_HOUR   = FACTOR_100NS_PER_MINUTE*60;
const INT64 FACTOR_100NS_PER_DAY = FACTOR_100NS_PER_HOUR*24;
const INT64 FACTOR_100NS_PER_WEEK = FACTOR_100NS_PER_DAY*7;

int CLinearTimeAbsolute::m_nCount = 0;
char CLinearTimeAbsolute::m_Buffer[204];
char CLinearTimeDelta::m_Buffer[204];

void GetUTCLinearTime(INT64 *plt);
void GetLocalFieldedTime(FIELDEDTIME *ft);

bool operator<(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb)
{
    return lta.m_tAbsolute < ltb.m_tAbsolute;
}

bool operator>(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb)
{
    return lta.m_tAbsolute > ltb.m_tAbsolute;
}

bool operator==(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb)
{
    return lta.m_tAbsolute == ltb.m_tAbsolute;
}

bool operator==(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb)
{
    return lta.m_tDelta == ltb.m_tDelta;
}

bool operator!=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb)
{
    return lta.m_tDelta != ltb.m_tDelta;
}

bool operator<=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb)
{
    return lta.m_tDelta <= ltb.m_tDelta;
}

bool operator<=(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb)
{
    return lta.m_tAbsolute <= ltb.m_tAbsolute;
}

CLinearTimeAbsolute operator-(const CLinearTimeAbsolute& lta, const CLinearTimeDelta& ltd)
{
    CLinearTimeAbsolute t;
    t.m_tAbsolute = lta.m_tAbsolute - ltd.m_tDelta;
    return t;
}

CLinearTimeAbsolute::CLinearTimeAbsolute(const CLinearTimeAbsolute& ltaOrigin, const CLinearTimeDelta& ltdOffset)
{
    m_tAbsolute = ltaOrigin.m_tAbsolute + ltdOffset.m_tDelta;
}

bool ParseFractionalSecondsString(INT64 &i64, char *str)
{
    bool bMinus = false;

    i64 = 0;

    bool bGotOne;

    // Leading spaces.
    //
    while (mux_isspace(*str))
    {
        str++;
    }

    // Leading minus
    //
    if (*str == '-')
    {
        bMinus = true;
        str++;

        // But not if just a minus
        //
        if (!*str)
        {
            return false;
        }
    }

    // Need at least one digit.
    //
    bGotOne = false;
    char *pIntegerStart = str;
    if (mux_isdigit(*str))
    {
        bGotOne = true;
        str++;
    }

    // The number (int)
    //
    while (mux_isdigit(*str))
    {
        str++;
    }
    char *pIntegerEnd = str;

    // Decimal point.
    //
    if (*str == '.')
    {
        str++;
    }

    // Need at least one digit
    //
    char *pFractionalStart = str;
    if (mux_isdigit(*str))
    {
        bGotOne = true;
        str++;
    }

    // The number (fract)
    //
    while (mux_isdigit(*str))
    {
        str++;
    }
    char *pFractionalEnd = str;

    // Trailing spaces.
    //
    while (mux_isspace(*str))
    {
        str++;
    }

    if (*str || !bGotOne)
    {
        return false;
    }

#define PFSS_PRECISION 7
    char   aBuffer[64];
    size_t nBufferAvailable = sizeof(aBuffer) - PFSS_PRECISION - 1;
    char  *p = aBuffer;

    // Sign.
    //
    if (bMinus)
    {
        *p++ = '-';
        nBufferAvailable--;
    }

    // Integer part.
    //
    bool bOverUnderflow = false;
    size_t n = pIntegerEnd - pIntegerStart;
    if (n > 0)
    {
        if (n > nBufferAvailable)
        {
            bOverUnderflow = true;
            n = nBufferAvailable;
        }
        memcpy(p, pIntegerStart, n);
        p += n;
        nBufferAvailable -= n;
    }

    // Fractional part.
    //
    n = pFractionalEnd - pFractionalStart;
    if (n > 0)
    {
        if (n > PFSS_PRECISION)
        {
            n = PFSS_PRECISION;
        }
        memcpy(p, pFractionalStart, n);
        p += n;
        nBufferAvailable -= n;
    }

    // Handle trailing zeroes.
    //
    n = PFSS_PRECISION - n;
    if (n > 0)
    {
        memset(p, '0', n);
        p += n;
    }
    *p++ = '\0';

    if (bOverUnderflow)
    {
        if (bMinus)
        {
            i64 = INT64_MIN_VALUE;
        }
        else
        {
            i64 = INT64_MAX_VALUE;
        }
    }
    else
    {
        i64 = mux_atoi64(aBuffer);
    }
    return true;
}

void ConvertToSecondsString(char *buffer, INT64 n64, int nFracDigits)
{
    INT64 Leftover;
    INT64 lt = i64FloorDivisionMod(n64, FACTOR_100NS_PER_SECOND, &Leftover);

    size_t n = mux_i64toa(lt, buffer);
    if (Leftover == 0)
    {
        return;
    }

    // Sanitize Precision Request.
    //
    const int maxFracDigits = 7;
    const int minFracDigits = 0;
    if (nFracDigits < minFracDigits)
    {
        nFracDigits = minFracDigits;
    }
    else if (maxFracDigits < nFracDigits)
    {
        nFracDigits = maxFracDigits;
    }
    if (0 < nFracDigits)
    {
        char *p = buffer + n;
        *p++ = '.';
        char *q = p;

        char buf[maxFracDigits+1];
        size_t m = mux_i64toa(Leftover, buf);
        memset(p, '0', maxFracDigits - m);
        p += maxFracDigits - m;
        memcpy(p, buf, m);
        p = q + nFracDigits - 1;
        while (*p == '0')
        {
            p--;
        }
        p++;
        *p = '\0';
    }
}

char *CLinearTimeDelta::ReturnSecondsString(int nFracDigits)
{
    ConvertToSecondsString(m_Buffer, m_tDelta, nFracDigits);
    return m_Buffer;
}

char *CLinearTimeAbsolute::ReturnSecondsString(int nFracDigits)
{
    ConvertToSecondsString(m_Buffer, m_tAbsolute - EPOCH_OFFSET, nFracDigits);
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

CLinearTimeDelta::CLinearTimeDelta(INT64 arg_t100ns)
{
    m_tDelta = arg_t100ns;
}

void CLinearTimeDelta::ReturnTimeValueStruct(struct timeval *tv)
{
    INT64 Leftover;
    tv->tv_sec = (long)i64FloorDivisionMod(m_tDelta, FACTOR_100NS_PER_SECOND, &Leftover);
    tv->tv_usec = (long)i64FloorDivision(Leftover, FACTOR_100NS_PER_MICROSECOND);
}

#ifdef HAVE_NANOSLEEP
void CLinearTimeDelta::ReturnTimeSpecStruct(struct timespec *ts)
{
    INT64 Leftover;
    ts->tv_sec = (long)i64FloorDivisionMod(m_tDelta, FACTOR_100NS_PER_SECOND, &Leftover);
    ts->tv_nsec = (long)Leftover*FACTOR_NANOSECONDS_PER_100NS;
}
#endif // HAVE_NANOSLEEP

void CLinearTimeDelta::SetTimeValueStruct(struct timeval *tv)
{
    m_tDelta = FACTOR_100NS_PER_SECOND * tv->tv_sec
             + FACTOR_100NS_PER_MICROSECOND * tv->tv_usec;
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
    return (long)(m_tDelta/FACTOR_100NS_PER_MILLISECOND);
}

INT64 CLinearTimeDelta::ReturnMicroseconds(void)
{
    return m_tDelta/FACTOR_100NS_PER_MICROSECOND;
}

void CLinearTimeDelta::SetSecondsString(char *arg_szSeconds)
{
    ParseFractionalSecondsString(m_tDelta, arg_szSeconds);
}

void CLinearTimeDelta::SetSeconds(INT64 arg_tSeconds)
{
    m_tDelta = arg_tSeconds * FACTOR_100NS_PER_SECOND;
}

void CLinearTimeDelta::Set100ns(INT64 arg_t100ns)
{
    m_tDelta = arg_t100ns;
}

INT64 CLinearTimeDelta::Return100ns(void)
{
    return m_tDelta;
}

void CLinearTimeAbsolute::Set100ns(INT64 arg_t100ns)
{
    m_tAbsolute = arg_t100ns;
}

INT64 CLinearTimeAbsolute::Return100ns(void)
{
    return m_tAbsolute;
}

void CLinearTimeAbsolute::SetSeconds(INT64 arg_tSeconds)
{
    m_tAbsolute = arg_tSeconds;
    m_tAbsolute *= FACTOR_100NS_PER_SECOND;

    // Epoch difference between (00:00:00 UTC, January 1, 1970) and
    // (00:00:00 UTC, January 1, 1601).
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

bool isLeapYear(long iYear)
{
    if (iMod(iYear, 4) != 0)
    {
        // Not a leap year.
        //
        return false;
    }
    unsigned long wMod = iMod(iYear, 400);
    if ((wMod == 100) || (wMod == 200) || (wMod == 300))
    {
        // Not a leap year.
        //
        return false;
    }
    return true;
}

bool isValidDate(int iYear, int iMonth, int iDay)
{
    if (iYear < -27256 || 30826 < iYear)
    {
        return false;
    }
    if (iMonth < 1 || 12 < iMonth)
    {
        return false;
    }
    if (iDay < 1 || daystab[iMonth-1] < iDay)
    {
        return false;
    }
    if (iMonth == 2 && iDay == 29 && !isLeapYear(iYear))
    {
        return false;
    }
    return true;
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

// do_convtime()
//
// converts time string to time structure (fielded time). Returns 1 on
// success, 0 on fail. Time string format is:
//
//     [Ddd] Mmm DD HH:MM:SS YYYY
//
// The initial Day-of-week token is optional.
//
int MonthTabHash[12] =
{
    0x004a414e, 0x00464542, 0x004d4152, 0x00415052,
    0x004d4159, 0x004a554e, 0x004a554c, 0x00415547,
    0x00534550, 0x004f4354, 0x004e4f56, 0x00444543
};

bool ParseThreeLetters(const char **pp, int *piHash)
{
    *piHash = 0;

    // Skip Initial spaces
    //
    const char *p = *pp;
    while (*p == ' ')
    {
        p++;
    }

    // Parse space-separate token.
    //
    const char *q = p;
    int iHash = 0;
    while (*q && *q != ' ')
    {
        if (!mux_isalpha(*q))
        {
            return false;
        }
        iHash = (iHash << 8) | mux_toupper(*q);
        q++;
    }

    // Must be exactly 3 letters long.
    //
    if (q - p != 3)
    {
        return false;
    }
    p = q;

    // Skip final spaces
    //
    while (*p == ' ')
    {
        p++;
    }

    *pp = p;
    *piHash = iHash;
    return true;
}

void ParseDecimalSeconds(size_t n, const char *p, unsigned short *iMilli,
                         unsigned short *iMicro, unsigned short *iNano)
{
   char aBuffer[10];
   if (n > sizeof(aBuffer) - 1)
   {
       n = sizeof(aBuffer) - 1;
   }
   memcpy(aBuffer, p, n);
   memset(aBuffer + n, '0', sizeof(aBuffer) - n - 1);
   aBuffer[sizeof(aBuffer) - 1] = '\0';
   int ns = mux_atol(aBuffer);
   *iNano = ns % 1000;
   ns /= 1000;
   *iMicro = ns % 1000;
   *iMilli = ns / 1000;
}

bool do_convtime(const char *str, FIELDEDTIME *ft)
{
    memset(ft, 0, sizeof(FIELDEDTIME));
    if (!str || !ft)
    {
        return false;
    }

    // Day-of-week OR month.
    //
    const char *p = str;
    int i, iHash;
    if (!ParseThreeLetters(&p, &iHash))
    {
        return false;
    }
    for (i = 0; (i < 12) && iHash != MonthTabHash[i]; i++) ;
    if (i == 12)
    {
        // The above three letters were probably the Day-Of-Week, the
        // next three letters are required to be the month name.
        //
        if (!ParseThreeLetters(&p, &iHash))
        {
            return false;
        }
        for (i = 0; (i < 12) && iHash != MonthTabHash[i]; i++) ;
        if (i == 12)
        {
            return false;
        }
    }
    ft->iMonth = i + 1; // January = 1, February = 2, etc.

    // Day of month.
    //
    ft->iDayOfMonth = (unsigned short)mux_atol(p);
    if (ft->iDayOfMonth < 1 || daystab[i] < ft->iDayOfMonth)
    {
        return false;
    }
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    // Hours
    //
    ft->iHour = (unsigned short)mux_atol(p);
    if (ft->iHour > 23 || (ft->iHour == 0 && *p != '0'))
    {
        return false;
    }
    while (*p && *p != ':') p++;
    if (*p == ':') p++;
    while (*p == ' ') p++;

    // Minutes
    //
    ft->iMinute = (unsigned short)mux_atol(p);
    if (ft->iMinute > 59 || (ft->iMinute == 0 && *p != '0'))
    {
        return false;
    }
    while (*p && *p != ':') p++;
    if (*p == ':') p++;
    while (*p == ' ') p++;

    // Seconds
    //
    ft->iSecond = (unsigned short)mux_atol(p);
    if (ft->iSecond > 59 || (ft->iSecond == 0 && *p != '0'))
    {
        return false;
    }
    while (mux_isdigit(*p))
    {
        p++;
    }

    // Milliseconds, Microseconds, and Nanoseconds
    //
    if (*p == '.')
    {
        p++;
        size_t n;
        const char *q = strchr(p, ' ');
        if (q)
        {
            n = q - p;
        }
        else
        {
            n = strlen(p);
        }

        ParseDecimalSeconds(n, p, &ft->iMillisecond, &ft->iMicrosecond,
            &ft->iNanosecond);
    }
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    // Year
    //
    ft->iYear = (short)mux_atol(p);
    while (mux_isdigit(*p))
    {
        p++;
    }
    while (*p == ' ') p++;
    if (*p != '\0')
    {
        return false;
    }

    // DayOfYear and DayOfWeek
    //
    ft->iDayOfYear = 0;
    ft->iDayOfWeek = 0;

    return isValidDate(ft->iYear, ft->iMonth, ft->iDayOfMonth);
}

CLinearTimeDelta::CLinearTimeDelta(CLinearTimeAbsolute t0, CLinearTimeAbsolute t1)
{
    m_tDelta = t1.m_tAbsolute - t0.m_tAbsolute;
}

long CLinearTimeDelta::ReturnDays(void)
{
    return (long)(m_tDelta/FACTOR_100NS_PER_DAY);
}

long CLinearTimeDelta::ReturnSeconds(void)
{
    return (long)(m_tDelta/FACTOR_100NS_PER_SECOND);
}

bool CLinearTimeAbsolute::ReturnFields(FIELDEDTIME *arg_tStruct)
{
    return LinearTimeToFieldedTime(m_tAbsolute, arg_tStruct);
}

bool CLinearTimeAbsolute::SetString(const char *arg_tBuffer)
{
    FIELDEDTIME ft;
    if (do_convtime(arg_tBuffer, &ft))
    {
        if (FieldedTimeToLinearTime(&ft, &m_tAbsolute))
        {
            return true;
        }
    }
    m_tAbsolute = 0;
    return false;
}

void CLinearTimeDelta::operator+=(const CLinearTimeDelta& ltd)
{
    m_tDelta += ltd.m_tDelta;
}

CLinearTimeDelta operator-(const CLinearTimeAbsolute& ltaA, const CLinearTimeAbsolute& ltaB)
{
    CLinearTimeDelta ltd;
    ltd.m_tDelta = ltaA.m_tAbsolute - ltaB.m_tAbsolute;
    return ltd;
}

CLinearTimeDelta operator-(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb)
{
    CLinearTimeDelta ltd;
    ltd.m_tDelta = lta.m_tDelta - ltb.m_tDelta;
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

bool operator<(const CLinearTimeDelta& ltdA, const CLinearTimeDelta& ltdB)
{
    return ltdA.m_tDelta < ltdB.m_tDelta;
}

bool operator>(const CLinearTimeDelta& ltdA, const CLinearTimeDelta& ltdB)
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

bool CLinearTimeAbsolute::SetFields(FIELDEDTIME *arg_tStruct)
{
    m_tAbsolute = 0;
    return FieldedTimeToLinearTime(arg_tStruct, &m_tAbsolute);
}

void SetStructTm(FIELDEDTIME *ft, struct tm *ptm)
{
    ft->iYear = ptm->tm_year + 1900;
    ft->iMonth = ptm->tm_mon + 1;
    ft->iDayOfMonth = ptm->tm_mday;
    ft->iDayOfWeek = ptm->tm_wday;
    ft->iDayOfYear = 0;
    ft->iHour = ptm->tm_hour;
    ft->iMinute = ptm->tm_min;
    ft->iSecond = ptm->tm_sec;
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

char *CLinearTimeAbsolute::ReturnDateString(int nFracDigits)
{
    FIELDEDTIME ft;
    if (LinearTimeToFieldedTime(m_tAbsolute, &ft))
    {
        // Sanitize Precision Request.
        //
        const int maxFracDigits = 7;
        const int minFracDigits = 0;
        if (nFracDigits < minFracDigits)
        {
            nFracDigits = minFracDigits;
        }
        else if (maxFracDigits < nFracDigits)
        {
            nFracDigits = maxFracDigits;
        }

        char buffer[11];
        buffer[0] = '\0';
        if (  0 < nFracDigits
           && (  ft.iMillisecond != 0
              || ft.iMicrosecond != 0
              || ft.iNanosecond != 0))
        {
            sprintf(buffer, ".%03d%03d%03d", ft.iMillisecond, ft.iMicrosecond,
                ft.iNanosecond);

            // Remove trailing zeros.
            //
            char *p = (buffer + 1) + (nFracDigits - 1);
            while (*p == '0')
            {
                p--;
            }
            p++;
            *p = '\0';
        }
        sprintf(m_Buffer, "%s %s %02d %02d:%02d:%02d%s %04d",
            DayOfWeekString[ft.iDayOfWeek], monthtab[ft.iMonth-1],
            ft.iDayOfMonth, ft.iHour, ft.iMinute, ft.iSecond, buffer,
            ft.iYear);
    }
    else
    {
        m_Buffer[0] = '\0';
    }
    return m_Buffer;
}

void CLinearTimeAbsolute::GetUTC(void)
{
    GetUTCLinearTime(&m_tAbsolute);
}

void CLinearTimeAbsolute::GetLocal(void)
{
    GetUTCLinearTime(&m_tAbsolute);
    UTC2Local();
}

bool FieldedTimeToLinearTime(FIELDEDTIME *ft, INT64 *plt)
{
    if (!isValidDate(ft->iYear, ft->iMonth, ft->iDayOfMonth))
    {
        *plt = 0;
        return false;
    }

    int iFixedDay = FixedFromGregorian_Adjusted(ft->iYear, ft->iMonth, ft->iDayOfMonth);
    ft->iDayOfWeek = iMod(iFixedDay+1, 7);

    INT64 lt;
    lt  = iFixedDay * FACTOR_100NS_PER_DAY;
    lt += ft->iHour * FACTOR_100NS_PER_HOUR;
    lt += ft->iMinute * FACTOR_100NS_PER_MINUTE;
    lt += ft->iSecond * FACTOR_100NS_PER_SECOND;
    lt += ft->iMicrosecond * FACTOR_100NS_PER_MICROSECOND;
    lt += ft->iMillisecond * FACTOR_100NS_PER_MILLISECOND;
    lt += ft->iNanosecond / FACTOR_NANOSECONDS_PER_100NS;

    *plt = lt;
    return true;
}

bool LinearTimeToFieldedTime(INT64 lt, FIELDEDTIME *ft)
{
    INT64 ns100;
    int iYear, iMonth, iDayOfYear, iDayOfMonth, iDayOfWeek;

    memset(ft, 0, sizeof(FIELDEDTIME));
    int d0 = (int)i64FloorDivisionMod(lt, FACTOR_100NS_PER_DAY, &ns100);
    GregorianFromFixed_Adjusted(d0, iYear, iMonth, iDayOfYear, iDayOfMonth, iDayOfWeek);
    if (!isValidDate(iYear, iMonth, iDayOfMonth))
    {
        return false;
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

    ft->iMillisecond = (int)(ns100 / FACTOR_100NS_PER_MILLISECOND);
    ns100 = ns100 % FACTOR_100NS_PER_MILLISECOND;
    ft->iMicrosecond = (int)(ns100 / FACTOR_100NS_PER_MICROSECOND);
    ns100 = ns100 % FACTOR_100NS_PER_MICROSECOND;
    ft->iNanosecond = (int)(ns100 * FACTOR_NANOSECONDS_PER_100NS);

    return true;
}

void CLinearTimeAbsolute::SetSecondsString(char *arg_szSeconds)
{
    ParseFractionalSecondsString(m_tAbsolute, arg_szSeconds);
    m_tAbsolute += EPOCH_OFFSET;
}

// OS Dependent Routines:
//
#ifdef WIN32

// This calculates (FACTOR_100NS_PER_SECOND*x)/y accurately without
// overflow.
//
class CxyDiv
{
public:
    void SetDenominator(const INT64 y_arg);
    INT64 Convert(const INT64 x_arg);
    CxyDiv(void);
private:
    INT64 A, B, C, D;
};

CxyDiv::CxyDiv(void)
{
    A = B = C = D = 0;
}

void CxyDiv::SetDenominator(const INT64 y_arg)
{
    A = FACTOR_100NS_PER_SECOND / y_arg;
    B = FACTOR_100NS_PER_SECOND % y_arg;
    C = y_arg/2;
    D = y_arg;
}

INT64 CxyDiv::Convert(const INT64 x_arg)
{
    return A*x_arg + (B*x_arg + C)/D;
}

CxyDiv Ticks2Seconds;
INT64  xIntercept = 0;
INT64  liInit;
INT64  tInit;
INT64  tError;
bool   bQueryPerformanceAvailable = false;
bool   bUseQueryPerformance = false;

const INT64 TargetError = 5*FACTOR_100NS_PER_MILLISECOND;

BOOL CalibrateQueryPerformance(void)
{
    if (!bQueryPerformanceAvailable)
    {
        return false;
    }

    INT64 li;
    INT64 t;

    MuxAlarm.SurrenderSlice();
    if (QueryPerformanceCounter((LARGE_INTEGER *)&li))
    {
        GetSystemTimeAsFileTime((struct _FILETIME *)&t);

        // Estimate Error.
        //
        // x = y/m + b;
        //
        tError = Ticks2Seconds.Convert(li) + xIntercept - t;
        if (  -TargetError < tError
           && tError < TargetError)
        {
            bUseQueryPerformance = true;
        }

        // x = y/m + b
        // m = dy/dx = (y1 - y0)/(x1 - x0)
        //
        // y is ticks and x is seconds.
        //
        INT64 dli = li - liInit;
        INT64 dt  =  t -  tInit;

        CxyDiv Ticks2Freq;

        Ticks2Freq.SetDenominator(dt);
        INT64 liFreq = Ticks2Freq.Convert(dli);
        Ticks2Seconds.SetDenominator(liFreq);

        // Therefore, b = x - y/m
        //
        xIntercept = t - Ticks2Seconds.Convert(li);
        return true;
    }
    else
    {
        bQueryPerformanceAvailable = false;
        bUseQueryPerformance = false;
        return false;
    }
}

void InitializeQueryPerformance(void)
{
    // The frequency returned is the number of ticks per second.
    //
    INT64 liFreq;
    if (QueryPerformanceFrequency((LARGE_INTEGER *)&liFreq))
    {
        Ticks2Seconds.SetDenominator(liFreq);

        MuxAlarm.SurrenderSlice();
        if (QueryPerformanceCounter((LARGE_INTEGER *)&liInit))
        {
            GetSystemTimeAsFileTime((struct _FILETIME *)&tInit);
            xIntercept = tInit - Ticks2Seconds.Convert(liInit);
            bQueryPerformanceAvailable = true;
        }
    }
}

void GetUTCLinearTime(INT64 *plt)
{
    if (bUseQueryPerformance)
    {
        INT64 li;
        if (QueryPerformanceCounter((LARGE_INTEGER *)&li))
        {
            *plt = Ticks2Seconds.Convert(li) + xIntercept;
            return;
        }
        bQueryPerformanceAvailable = false;
        bUseQueryPerformance = false;
    }
    GetSystemTimeAsFileTime((struct _FILETIME *)plt);
}

DWORD WINAPI AlarmProc(LPVOID lpParameter)
{
    CMuxAlarm *pthis = (CMuxAlarm *)lpParameter;
    DWORD dwWait = pthis->dwWait;
    for (;;)
    {
        HANDLE hSemAlarm = pthis->hSemAlarm;
        if (hSemAlarm == INVALID_HANDLE_VALUE)
        {
            break;
        }
        DWORD dwReason = WaitForSingleObject(hSemAlarm, dwWait);
        if (dwReason == WAIT_TIMEOUT)
        {
            pthis->bAlarmed = true;
            dwWait = INFINITE;
        }
        else
        {
            dwWait = pthis->dwWait;
        }
    }
    return 1;
}

CMuxAlarm::CMuxAlarm(void)
{
    hSemAlarm = CreateSemaphore(NULL, 0, 1, NULL);
    Clear();
    hThread = CreateThread(NULL, 0, AlarmProc, (LPVOID)this, 0, NULL);
}

CMuxAlarm::~CMuxAlarm()
{
    HANDLE hSave = hSemAlarm;
    hSemAlarm = INVALID_HANDLE_VALUE;
    ReleaseSemaphore(hSave, 1, NULL);
    WaitForSingleObject(hThread, 15*FACTOR_100NS_PER_SECOND);
    CloseHandle(hSave);
}

void CMuxAlarm::Sleep(CLinearTimeDelta ltd)
{
    ::Sleep(ltd.ReturnMilliseconds());
}

void CMuxAlarm::SurrenderSlice(void)
{
    ::Sleep(0);
}

void CMuxAlarm::Set(CLinearTimeDelta ltd)
{
    dwWait = ltd.ReturnMilliseconds();
    ReleaseSemaphore(hSemAlarm, 1, NULL);
    bAlarmed  = false;
    bAlarmSet = true;
}

void CMuxAlarm::Clear(void)
{
    dwWait = INFINITE;
    ReleaseSemaphore(hSemAlarm, 1, NULL);
    bAlarmed  = false;
    bAlarmSet = false;
}

#else // !WIN32

void GetUTCLinearTime(INT64 *plt)
{
#ifdef HAVE_GETTIMEOFDAY
    struct timeval tv;
    struct timezone tz;
    tz.tz_minuteswest = 0;
    tz.tz_dsttime = 0;

    gettimeofday(&tv, &tz);

    *plt = (((INT64)tv.tv_sec) * FACTOR_100NS_PER_SECOND)
         + (tv.tv_usec * FACTOR_100NS_PER_MICROSECOND)
         + EPOCH_OFFSET;
#else
    time_t t;

    time(&t);

    *plt = t*FACTOR_100NS_PER_SECOND;
#endif
}

CMuxAlarm::CMuxAlarm(void)
{
    bAlarmed = false;
    bAlarmSet = false;
}

void CMuxAlarm::Sleep(CLinearTimeDelta ltd)
{
#if   defined(HAVE_NANOSLEEP)
    struct timespec req;
    ltd.ReturnTimeSpecStruct(&req);
    while (!mudstate.shutdown_flag)
    {
        struct timespec rem;
        if (  nanosleep(&req, &rem) == -1
           && errno == EINTR)
        {
            req = rem;
        }
        else
        {
            break;
        }
    }
#else
#ifdef HAVE_SETITIMER
    struct itimerval oit;
    bool   bSaved = false;
    if (bAlarmSet)
    {
        // Save existing timer and disable.
        //
        struct itimerval it;
        it.it_value.tv_sec = 0;
        it.it_value.tv_usec = 0;
        it.it_interval.tv_sec = 0;
        it.it_interval.tv_usec = 0;
        setitimer(ITIMER_PROF, &it, &oit);
        bSaved = true;
        bAlarmSet = false;
    }
#endif
#if   defined(HAVE_USLEEP)
#define TIME_1S 1000000
    unsigned long usec;
    INT64 usecTotal = ltd.ReturnMicroseconds();

    while (  usecTotal
          && mudstate.shutdown_flag)
    {
        usec = usecTotal;
        if (usecTotal < TIME_1S)
        {
            usec = usecTotal;
        }
        else
        {
            usec = TIME_1S-1;
        }
        usleep(usec);
        usecTotal -= usec;
    }
#else
    ::sleep(ltd.ReturnSeconds());
#endif
#ifdef HAVE_SETITIMER
    if (bSaved)
    {
        // Restore and re-enabled timer.
        //
        setitimer(ITIMER_PROF, &oit, NULL);
        bAlarmSet = true;
    }
#endif
#endif
}

void CMuxAlarm::SurrenderSlice(void)
{
    ::sleep(0);
}

void CMuxAlarm::Set(CLinearTimeDelta ltd)
{
#ifdef HAVE_SETITIMER
    struct itimerval it;
    ltd.ReturnTimeValueStruct(&it.it_value);
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 0;
    setitimer(ITIMER_PROF, &it, NULL);
    bAlarmSet = true;
    bAlarmed  = false;
#endif
}

void CMuxAlarm::Clear(void)
{
#ifdef HAVE_SETITIMER
    // Turn off the timer.
    //
    struct itimerval it;
    it.it_value.tv_sec = 0;
    it.it_value.tv_usec = 0;
    it.it_interval.tv_sec = 0;
    it.it_interval.tv_usec = 0;
    setitimer(ITIMER_PROF, &it, NULL);
    bAlarmSet = false;
    bAlarmed  = false;
#endif
}

void CMuxAlarm::Signal(void)
{
    bAlarmSet = false;
    bAlarmed  = true;
}

#endif // !WIN32

static int YearType(int iYear)
{
    FIELDEDTIME ft;
    memset(&ft, 0, sizeof(FIELDEDTIME));
    ft.iYear        = iYear;
    ft.iMonth       = 1;
    ft.iDayOfMonth  = 1;

    CLinearTimeAbsolute ltaLocal;
    ltaLocal.SetFields(&ft);
    if (isLeapYear(iYear))
    {
        return ft.iDayOfWeek + 8;
    }
    else
    {
        return ft.iDayOfWeek + 1;
    }
}

static CLinearTimeAbsolute ltaLowerBound;
static CLinearTimeAbsolute ltaUpperBound;
static CLinearTimeDelta    ltdTimeZoneStandard;

// Because of signed-ness and -LONG_MAX overflowing, we need to be
// particularly careful with finding the mid-point.
//
time_t time_t_midpoint(time_t tLower, time_t tUpper)
{
    time_t tDiff = (tUpper-2) - tLower;
    return tLower+tDiff/2+1;
}

time_t time_t_largest(void)
{
    time_t t;
#ifdef _WIN64
    // Not only can Windows not handle negative time_t values, but it also
    // cannot handle positive 64-bit values which are 'too large'.  Even
    // though the interface to localtime() provides for a NULL return value
    // for any unsupported arguments, with VS 2005, Microsoft has decided that
    // an assert is more useful.
    //
    // The logic of their assert is based on private #defines which are not
    // available to applications. Also, the values have changed from VS 2003
    // (0x100000000000i64) to VS 2005 (32535215999). The latter corresponds to
    // December 31, 2999, 23:59:59 UTC.
    //
    // The message here is that they really don't think anyone should be using
    // localtime(), but if you do use it, they get to decide unilaterially and
    // without hints whether your application is making reasonable calls.
    //
    const time_t WIN_MAX__TIME64_T = 32535215999;
    t = WIN_MAX__TIME64_T;
#else
    t = 1;

    // Multiply to search within half the largest number.
    //
    time_t next;
    for (next = 2*t; t < next; next = 2*t)
    {
        t = next;
    }

    // Divide by powers of 2 to reach within a few values.
    //
    time_t d;
    for (d = t/2; d != 0; d = d/2)
    {
        next = t + d;
        if (t < next)
        {
            t = next;
        }
    }

    // Increment until at the largest number.
    //
    for (next = t+1; t < next; next = t+1)
    {
        t = next;
    }
    return t;
#endif
}


// This code creates overflows intentionally in order to find the most
// negative time_t supported by the data type.
//
time_t time_t_smallest(void)
{
    time_t t;
#ifdef WIN32
    t = 0;
#else
    t = -1;

    // Multiply to search within half the smallest number.
    //
    time_t next;
    for (next = 2*t; next < t; next = 2*t)
    {
        t = next;
    }

    // Divide by powers of 2 to reach within a few values.
    //
    time_t d = t/2;
    for (d = t/2; d != 0; d = d/2)
    {
        next = t + d;
        if (next < t)
        {
            t = next;
        }
    }

    // Decrement until at the smallest number.
    //
    for (next = t-1; next < t; next = t-1)
    {
        t = next;
    }
#endif
    return t;
}

// This determines the valid range of time_t and finds a 'standard'
// time zone near the earliest supported time_t.
//
void test_time_t(void)
{
    // Search for the highest supported value of time_t.
    //
    time_t tUpper = time_t_largest();
    time_t tLower = 0;
    time_t tMid;
    while (tLower < tUpper)
    {
        tMid = time_t_midpoint(tLower+1, tUpper);
        if (localtime(&tMid))
        {
            tLower = tMid;
        }
        else
        {
            tUpper = tMid-1;
        }
    }
    ltaUpperBound.SetSeconds(tLower);

    // Search for the lowest supported value of time_t.
    //
    tUpper = 0;
    tLower = time_t_smallest();
    while (tLower < tUpper)
    {
        tMid = time_t_midpoint(tLower, tUpper-1);
        if (localtime(&tMid))
        {
            tUpper = tMid;
        }
        else
        {
            tLower = tMid+1;
        }
    }
    ltaLowerBound.SetSeconds(tUpper);

    // Find a time near tLower for which DST is not in affect.
    //
    for (;;)
    {
        struct tm *ptm = localtime(&tLower);

        if (ptm->tm_isdst <= 0)
        {
            // Daylight savings time is either not in effect or
            // we have no way of knowing whether it is in effect
            // or not.
            //
            FIELDEDTIME ft;
            SetStructTm(&ft, ptm);
            ft.iMillisecond = 0;
            ft.iMicrosecond = 0;
            ft.iNanosecond  = 0;

            CLinearTimeAbsolute ltaLocal;
            CLinearTimeAbsolute ltaUTC;
            ltaLocal.SetFields(&ft);
            ltaUTC.SetSeconds(tLower);
            ltdTimeZoneStandard = ltaLocal - ltaUTC;
            break;
        }

        // Advance the time by 1 month (expressed as seconds).
        //
        tLower += 30*24*60*60;
    }
}

int NearestYearOfType[15];
static CLinearTimeDelta ltdIntervalMinimum;
static bool bTimeInitialized = false;

void TIME_Initialize(void)
{
    bTimeInitialized = true;

    tzset();

    test_time_t();
    ltdIntervalMinimum = time_1w;
    int i;
    for (i = 0; i < 15; i++)
    {
        NearestYearOfType[i] = -1;
    }
    int cnt = 14;
    FIELDEDTIME ft;
    ltaUpperBound.ReturnFields(&ft);
    for (i = ft.iYear-1; cnt; i--)
    {
        int iYearType = YearType(i);
        if (NearestYearOfType[iYearType] < 0)
        {
            NearestYearOfType[iYearType] = i;
            cnt--;
        }
    }
#ifdef WIN32
    InitializeQueryPerformance();
#endif
}

// Explanation of the table.
//
// The table contains intervals of time for which (ltdOffset, isDST)
// tuples are known.
//
// Two intervals may be combined when they share the same tuple
// value and the time between them is less than ltdIntervalMinimum.
//
// Intervals are thrown away in a least-recently-used (LRU) fashion.
//
typedef struct
{
    CLinearTimeAbsolute ltaStart;
    CLinearTimeAbsolute ltaEnd;
    CLinearTimeDelta    ltdOffset;
    int                 nTouched;
    bool                isDST;
} OffsetEntry;

#define MAX_OFFSETS 50
int nOffsetTable = 0;
int nTouched0 = 0;
OffsetEntry OffsetTable[MAX_OFFSETS];

// This function finds the entry in the table (0...nOffsetTable-1)
// whose ltaStart is less than or equal to the search key.
// If no entry satisfies this search, -1 is returned.
//
static int FindOffsetEntry(const CLinearTimeAbsolute& lta)
{
    int lo = 0;
    int hi = nOffsetTable - 1;
    int mid = 0;
    while (lo <= hi)
    {
        mid = ((hi - lo) >> 1) + lo;
        if (OffsetTable[mid].ltaStart <= lta)
        {
            lo = mid + 1;
        }
        else
        {
            hi = mid - 1;
        }

    }
    return lo-1;
}

static bool QueryOffsetTable
(
    CLinearTimeAbsolute lta,
    CLinearTimeDelta *pltdOffset,
    bool *pisDST,
    int *piEntry
)
{
    nTouched0++;

    int i = FindOffsetEntry(lta);
    *piEntry = i;

    // Is the interval defined?
    //
    if (  0 <= i
       && lta <= OffsetTable[i].ltaEnd)
    {
        *pltdOffset = OffsetTable[i].ltdOffset;
        *pisDST = OffsetTable[i].isDST;
        OffsetTable[i].nTouched = nTouched0;
        return true;
    }
    return false;
}

static void UpdateOffsetTable
(
    CLinearTimeAbsolute &lta,
    CLinearTimeDelta ltdOffset,
    bool isDST,
    int i
)
{
Again:

    nTouched0++;

    // Is the interval defined?
    //
    if (  0 <= i
       && lta <= OffsetTable[i].ltaEnd)
    {
        OffsetTable[i].nTouched = nTouched0;
        return;
    }

    bool bTryMerge = false;

    // Coalesce new data point into this interval if:
    //
    //  1. Tuple for this interval matches the new tuple value.
    //  2. It's close enough that we can assume all intervening
    //     values are the same.
    //
    if (  0 <= i
       && OffsetTable[i].ltdOffset == ltdOffset
       && OffsetTable[i].isDST == isDST
       && lta <= OffsetTable[i].ltaEnd + ltdIntervalMinimum)
    {
        // Cool. We can just extend this interval to include our new
        // data point.
        //
        OffsetTable[i].ltaEnd = lta;
        OffsetTable[i].nTouched = nTouched0;

        // Since we have changed this interval, we may be able to
        // coalesce it with the next interval.
        //
        bTryMerge = true;
    }

    // Coalesce new data point into next interval if:
    //
    //  1. Next interval exists.
    //  2. Tuple in next interval matches the new tuple value.
    //  3. It's close enough that we can assume all intervening
    //     values are the same.
    //
    int iNext = i+1;
    if (  0 <= iNext
       && iNext < nOffsetTable
       && OffsetTable[iNext].ltdOffset == ltdOffset
       && OffsetTable[iNext].isDST == isDST
       && OffsetTable[iNext].ltaStart - ltdIntervalMinimum <= lta)
    {
        // Cool. We can just extend the next interval to include our
        // new data point.
        //
        OffsetTable[iNext].ltaStart = lta;
        OffsetTable[iNext].nTouched = nTouched0;

        // Since we have changed the next interval, we may be able
        // to coalesce it with the previous interval.
        //
        bTryMerge = true;
    }

    if (bTryMerge)
    {
        // We should combine the current and next intervals if we can.
        //
        if (  0 <= i
           && iNext < nOffsetTable
           && OffsetTable[i].ltdOffset == OffsetTable[iNext].ltdOffset
           && OffsetTable[i].isDST     == OffsetTable[iNext].isDST
           && OffsetTable[iNext].ltaStart - ltdIntervalMinimum
              <= OffsetTable[i].ltaEnd)
        {
            if (0 <= i && 0 <= iNext)
            {
                OffsetTable[i].ltaEnd = OffsetTable[iNext].ltaEnd;
            }
            int nSize = sizeof(OffsetEntry)*(nOffsetTable-i-2);
            memmove(OffsetTable+i+1, OffsetTable+i+2, nSize);
            nOffsetTable--;
        }
    }
    else
    {
        // We'll have'ta create a new interval.
        //
        if (nOffsetTable < MAX_OFFSETS)
        {
            size_t nSize = sizeof(OffsetEntry)*(nOffsetTable-i-1);
            memmove(OffsetTable+i+2, OffsetTable+i+1, nSize);
            nOffsetTable++;
            i++;
            OffsetTable[i].isDST = isDST;
            OffsetTable[i].ltdOffset = ltdOffset;
            OffsetTable[i].ltaStart= lta;
            OffsetTable[i].ltaEnd= lta;
            OffsetTable[i].nTouched = nTouched0;
        }
        else
        {
            // We ran out of room. Throw away the least used
            // interval and try again.
            //
            int nMinTouched = OffsetTable[0].nTouched;
            int iMinTouched = 0;
            for (int j = 1; j < nOffsetTable; j++)
            {
                if (OffsetTable[j].nTouched - nMinTouched < 0)
                {
                    nMinTouched = OffsetTable[j].nTouched;
                    iMinTouched = j;
                }
            }
            int nSize = sizeof(OffsetEntry)*(nOffsetTable-iMinTouched-1);
            memmove(OffsetTable+iMinTouched, OffsetTable+iMinTouched+1, nSize);
            nOffsetTable--;
            if (iMinTouched <= i)
            {
                i--;
            }
            goto Again;
        }
    }
}

static CLinearTimeDelta QueryLocalOffsetAt_Internal
(
    CLinearTimeAbsolute lta,
    bool *pisDST,
    int iEntry
)
{
    if (!bTimeInitialized)
    {
        TIME_Initialize();
    }

    // At this point, we must use localtime() to discover what the
    // UTC to local time offset is for the requested UTC time.
    //
    // However, localtime() does not support times beyond around
    // the 2038 year on machines with 32-bit integers, so to
    // compensant for this, and knowing that we are already dealing
    // with fictionalized adjustments, we associate a future year
    // that is outside the supported range with one that is inside
    // the support range of the same type (there are 14 different
    // year types depending on leap-year-ness and which day of the
    // week that January 1st falls on.
    //
    // Note: Laws beyond the current year have not been written yet
    // and are subject to change at any time. For example, Israel
    // doesn't have regular rules for DST but makes a directive each
    // year...sometimes to avoid conflicts with Jewish holidays.
    //
    if (lta > ltaUpperBound)
    {
        // Map the specified year to the closest year with the same
        // pattern of weeks.
        //
        FIELDEDTIME ft;
        lta.ReturnFields(&ft);
        ft.iYear = NearestYearOfType[YearType(ft.iYear)];
        lta.SetFields(&ft);
    }

    // Rely on localtime() to take a UTC count of seconds and convert
    // to a fielded local time complete with known timezone and DST
    // adjustments.
    //
    time_t lt = (time_t)lta.ReturnSeconds();
    struct tm *ptm = localtime(&lt);
    if (!ptm)
    {
        // This should never happen as we have already taken pains
        // to restrict the range of UTC seconds gives to localtime().
        //
        return ltdTimeZoneStandard;
    }

    // With the fielded (or broken down) time from localtime(), we
    // can convert to a linear time in the same time zone.
    //
    FIELDEDTIME ft;
    SetStructTm(&ft, ptm);
    ft.iMillisecond = 0;
    ft.iMicrosecond = 0;
    ft.iNanosecond  = 0;

    CLinearTimeAbsolute ltaLocal;
    CLinearTimeDelta ltdOffset;
    ltaLocal.SetFields(&ft);
    lta.SetSeconds(lt);
    ltdOffset = ltaLocal - lta;

    *pisDST = (ptm->tm_isdst > 0);

    // We now have a mapping from UTC lta to a (ltdOffset, *pisDST)
    // tuple which will will use to update the cache.
    //
    UpdateOffsetTable(lta, ltdOffset, *pisDST, iEntry);
    return ltdOffset;
}

static CLinearTimeDelta QueryLocalOffsetAtUTC
(
    const CLinearTimeAbsolute &lta,
    bool *pisDST
)
{
    *pisDST = false;

    // DST started in Britain in May 1916 and in the US in 1918.
    // Germany used it a little before May 1916, but I'm not sure
    // of exactly when.
    //
    // So, there is locale specific information about DST adjustments
    // that could reasonable be made between 1916 and 1970.
    // Because Unix supports negative time_t values while Win32 does
    // not, it can also support that 1916 to 1970 interval with
    // timezone information.
    //
    // Win32 only supports one timezone rule at a time, or rather
    // it doesn't have any historical timezone information, but you
    // can/must provide it yourself. So, in the Win32 case, unless we
    // are willing to provide historical information (from a tzfile
    // perhaps), it will only give us the current timezone rule
    // (the one selected by the control panel or by a TZ environment
    // variable). It projects this rule forwards and backwards.
    //
    // Feel free to fill that gap in yourself with a tzfile file
    // reader for Win32.
    //
    if (lta < ltaLowerBound)
    {
        return ltdTimeZoneStandard;
    }

    // Next, we check our table for whether this time falls into a
    // previously discovered interval. You could view this as a
    // cache, or you could also view it as a way of reading in the
    // tzfile without becoming system-dependent enough to actually
    // read the tzfile.
    //
    CLinearTimeDelta ltdOffset;
    int iEntry;
    if (QueryOffsetTable(lta, &ltdOffset, pisDST, &iEntry))
    {
        return ltdOffset;
    }
    ltdOffset = QueryLocalOffsetAt_Internal(lta, pisDST, iEntry);

    // Since the cache failed, let's make sure we have a useful
    // interval surrounding this last request so that future queries
    // nearby will be serviced by the cache.
    //
    CLinearTimeAbsolute ltaProbe;
    CLinearTimeDelta ltdDontCare;
    bool bDontCare;

    ltaProbe = lta - ltdIntervalMinimum;
    if (!QueryOffsetTable(ltaProbe, &ltdDontCare, &bDontCare, &iEntry))
    {
        QueryLocalOffsetAt_Internal(ltaProbe, &bDontCare, iEntry);
    }

    ltaProbe = lta + ltdIntervalMinimum;
    if (!QueryOffsetTable(ltaProbe, &ltdDontCare, &bDontCare, &iEntry))
    {
        QueryLocalOffsetAt_Internal(ltaProbe, &bDontCare, iEntry);
    }
    return ltdOffset;
}

void CLinearTimeAbsolute::UTC2Local(void)
{
    bool bDST;
    CLinearTimeDelta ltd = QueryLocalOffsetAtUTC(*this, &bDST);
    m_tAbsolute += ltd.m_tDelta;
}

void CLinearTimeAbsolute::Local2UTC(void)
{
    bool bDST1, bDST2;
    CLinearTimeDelta ltdOffset1 = QueryLocalOffsetAtUTC(*this, &bDST1);
    CLinearTimeAbsolute ltaUTC2 = *this - ltdOffset1;
    CLinearTimeDelta ltdOffset2 = QueryLocalOffsetAtUTC(ltaUTC2, &bDST2);

    CLinearTimeAbsolute ltaLocalGuess = ltaUTC2 + ltdOffset2;
    if (ltaLocalGuess == *this)
    {
        // We found an offset, UTC, local time combination that
        // works.
        //
        m_tAbsolute = ltaUTC2.m_tAbsolute;
    }
    else
    {
        CLinearTimeAbsolute ltaUTC1 = *this - ltdOffset2;
        m_tAbsolute = ltaUTC1.m_tAbsolute;
    }
}

// AUTOMAGIC DATE PARSING.

// We must deal with several levels at once. That is, a single
// character is overlapped by layers and layers of meaning from 'digit'
// to 'the second digit of the hours field of the timezone'.
//
typedef struct tag_pd_node
{
    // The following is a bitfield which contains a '1' bit for every
    // possible meaning associated with this token. This bitfield is
    // initially determined by looking at the token, and then we use
    // the following logic to refine this set further:
    //
    // 1. Suffix and Prefix hints. e.g., '1st', '2nd', etc. ':' with
    //    time fields, 'am'/'pm', timezone field must follow time
    //    field, 'Wn' is a week-of-year indicator, 'nTn' is an ISO
    //    date/time seperator, '<timefield>Z' shows that 'Z' is a
    //    military timezone letter, '-n' indicates that the field is
    //    either a year or a numeric timezone, '+n' indicates that the
    //    field can only be a timezone.
    //
    // 2. Single Field Exclusiveness. We only allow -one- timezone
    //    indicator in the field. Likewise, there can't be two months,
    //    two day-of-month fields, two years, etc.
    //
    // 3. Semantic exclusions. day-of-year, month/day-of-month, and
    //    and week-of-year/day-of-year(numeric) are mutually exclusive.
    //
    // If successful, this bitfield will ultimately only contain a single
    // '1' bit which tells us what it is.
    //
    unsigned  uCouldBe;

    // These fields deal with token things and we avoid mixing
    // them up in higher meanings. This is the lowest level.
    //
    // Further Notes:
    //
    // PDTT_SYMBOL is always a -single- (nToken==1) character.
    //
    // uTokenType must be one of the PDTT_* values.
    //
    // PDTT_NUMERIC_* and PDTT_TEXT types may have an
    // iToken value associated with them.
    //
#define PDTT_SYMBOL           1 // e.g., :/.-+
#define PDTT_NUMERIC_UNSIGNED 2 // [0-9]+
#define PDTT_SPACES           3 // One or more space/tab characters
#define PDTT_TEXT             4 // 'January' 'Jan' 'T' 'W'
#define PDTT_NUMERIC_SIGNED   5 // [+-][0-9]+
    unsigned  uTokenType;
    char     *pToken;
    size_t    nToken;
    int       iToken;

    // Link to previous and next node.
    //
    struct tag_pd_node *pNextNode;
    struct tag_pd_node *pPrevNode;

} PD_Node;

#define PDCB_NOTHING              0x00000000
#define PDCB_TIME_FIELD_SEPARATOR 0x00000001
#define PDCB_DATE_FIELD_SEPARATOR 0x00000002
#define PDCB_WHITESPACE           0x00000004
#define PDCB_DAY_OF_MONTH_SUFFIX  0x00000008
#define PDCB_SIGN                 0x00000010
#define PDCB_SECONDS_DECIMAL      0x00000020
#define PDCB_REMOVEABLE           0x00000040
#define PDCB_YEAR                 0x00000080
#define PDCB_MONTH                0x00000100
#define PDCB_DAY_OF_MONTH         0x00000200
#define PDCB_DAY_OF_WEEK          0x00000400
#define PDCB_WEEK_OF_YEAR         0x00000800
#define PDCB_DAY_OF_YEAR          0x00001000
#define PDCB_YD                   0x00002000
#define PDCB_YMD                  0x00004000
#define PDCB_MDY                  0x00008000
#define PDCB_DMY                  0x00010000
#define PDCB_DATE_TIME_SEPARATOR  0x00020000
#define PDCB_TIMEZONE             0x00040000
#define PDCB_WEEK_OF_YEAR_PREFIX  0x00080000
#define PDCB_MERIDIAN             0x00100000
#define PDCB_MINUTE               0x00200000
#define PDCB_SECOND               0x00400000
#define PDCB_SUBSECOND            0x00800000
#define PDCB_HOUR_TIME            0x01000000
#define PDCB_HMS_TIME             0x02000000
#define PDCB_HOUR_TIMEZONE        0x04000000
#define PDCB_HMS_TIMEZONE         0x08000000

extern PD_Node *PD_NewNode(void);
extern void PD_AppendNode(PD_Node *pNode);
extern void PD_InsertAfter(PD_Node *pWhere, PD_Node *pNode);
typedef void BREAK_DOWN_FUNC(PD_Node *pNode);
typedef struct tag_pd_breakdown
{
    unsigned int mask;
    BREAK_DOWN_FUNC *fpBreakDown;
} PD_BREAKDOWN;
extern const PD_BREAKDOWN BreakDownTable[];

#define NOT_PRESENT -9999999
typedef struct tag_AllFields
{
    int iYear;
    int iDayOfYear;
    int iMonthOfYear;
    int iDayOfMonth;
    int iWeekOfYear;
    int iDayOfWeek;
    int iHourTime;
    int iMinuteTime;
    int iSecondTime;
    int iMillisecondTime;
    int iMicrosecondTime;
    int iNanosecondTime;
    int iMinuteTimeZone;
} ALLFIELDS;

// isValidYear assumes numeric string.
//
bool isValidYear(size_t nStr, char *pStr, int iValue)
{
    // Year may be Y, YY, YYY, YYYY, or YYYYY.
    // Negative and zero years are permitted in general, but we aren't
    // give the leading sign.
    //
    if (1 <= nStr && nStr <= 5)
    {
        return true;
    }
    return false;
}

bool isValidMonth(size_t nStr, char *pStr, int iValue)
{
    // Month may be 1 through 9, 01 through 09, 10, 11, or 12.
    //
    if (  1 <= nStr && nStr <= 2
       && 1 <= iValue && iValue <= 12)
    {
        return true;
    }
    return false;
}

bool isValidDayOfMonth(size_t nStr, char *pStr, int iValue)
{
    // Day Of Month may be 1 through 9, 01 through 09, 10 through 19,
    // 20 through 29, 30, and 31.
    //
    if (  1 <= nStr && nStr <= 2
       && 1 <= iValue && iValue <= 31)
    {
        return true;
    }
    return false;
}

bool isValidDayOfWeek(size_t nStr, char *pStr, int iValue)
{
    // Day Of Week may be 1 through 7.
    //
    if (  1 == nStr
       && 1 <= iValue && iValue <= 7)
    {
        return true;
    }
    return false;
}

bool isValidDayOfYear(size_t nStr, char *pStr, int iValue)
{
    // Day Of Year 001 through 366
    //
    if (  3 == nStr
       && 1 <= iValue && iValue <= 366)
    {
        return true;
    }
    return false;
}

bool isValidWeekOfYear(size_t nStr, char *pStr, int iValue)
{
    // Week Of Year may be 01 through 53.
    //
    if (  2 == nStr
       && 1 <= iValue && iValue <= 53)
    {
        return true;
    }
    return false;
}

bool isValidHour(size_t nStr, char *pStr, int iValue)
{
    // Hour may be 0 through 9, 00 through 09, 10 through 19, 20 through 24.
    //
    if (  1 <= nStr && nStr <= 2
       && 0 <= iValue && iValue <= 24)
    {
        return true;
    }
    return false;
}

bool isValidMinute(size_t nStr, char *pStr, int iValue)
{
    // Minute may be 00 through 59.
    //
    if (  2 == nStr
       && 0 <= iValue && iValue <= 59)
    {
        return true;
    }
    return false;
}

bool isValidSecond(size_t nStr, char *pStr, int iValue)
{
    // Second may be 00 through 59. Leap seconds represented
    // by '60' are not dealt with.
    //
    if (  2 == nStr
       && 0 <= iValue && iValue <= 59)
    {
        return true;
    }
    return false;
}

bool isValidSubSecond(size_t nStr, char *pStr, int iValue)
{
    // Sub seconds can really be anything, but we limit
    // it's precision to 100 ns.
    //
    if (nStr <= 7)
    {
        return true;
    }
    return false;
}

// This function handles H, HH, HMM, HHMM, HMMSS, HHMMSS
//
bool isValidHMS(size_t nStr, char *pStr, int iValue)
{
    int iHour, iMinutes, iSeconds;
    switch (nStr)
    {
    case 1:
    case 2:
        return isValidHour(nStr, pStr, iValue);
        break;

    case 3:
    case 4:
        iHour    = iValue/100; iValue -= iHour*100;
        iMinutes = iValue;
        if (  isValidHour(nStr-2, pStr, iHour)
           && isValidMinute(2, pStr+nStr-2, iMinutes))
        {
            return true;
        }
        break;

    case 5:
    case 6:
        iHour    = iValue/10000;  iValue -= iHour*10000;
        iMinutes = iValue/100;    iValue -= iMinutes*100;
        iSeconds = iValue;
        if (  isValidHour(nStr-4, pStr, iHour)
           && isValidMinute(2, pStr+nStr-4, iMinutes)
           && isValidSecond(2, pStr+nStr-2, iSeconds))
        {
            return true;
        }
        break;
    }
    return false;
}

void SplitLastTwoDigits(PD_Node *pNode, unsigned mask)
{
    PD_Node *p = PD_NewNode();
    if (p)
    {
        *p = *pNode;
        p->uCouldBe = mask;
        p->nToken = 2;
        p->pToken += pNode->nToken - 2;
        p->iToken = pNode->iToken % 100;
        pNode->nToken -= 2;
        pNode->iToken /= 100;
        PD_InsertAfter(pNode, p);
    }
}

void SplitLastThreeDigits(PD_Node *pNode, unsigned mask)
{
    PD_Node *p = PD_NewNode();
    if (p)
    {
        *p = *pNode;
        p->uCouldBe = mask;
        p->nToken = 3;
        p->pToken += pNode->nToken - 3;
        p->iToken = pNode->iToken % 1000;
        pNode->nToken -= 3;
        pNode->iToken /= 1000;
        PD_InsertAfter(pNode, p);
    }
}

// This function breaks down H, HH, HMM, HHMM, HMMSS, HHMMSS
//
void BreakDownHMS(PD_Node *pNode)
{
    if (pNode->uCouldBe & PDCB_HMS_TIME)
    {
        pNode->uCouldBe = PDCB_HOUR_TIME;
    }
    else
    {
        pNode->uCouldBe = PDCB_HOUR_TIMEZONE;
    }
    switch (pNode->nToken)
    {
    case 5:
    case 6:
        SplitLastTwoDigits(pNode, PDCB_SECOND);

    case 3:
    case 4:
        SplitLastTwoDigits(pNode, PDCB_MINUTE);
    }
}


// This function handles YYMMDD, YYYMMDD, YYYYMMDD, YYYYYMMDD
//
bool isValidYMD(size_t nStr, char *pStr, int iValue)
{
    int iYear = iValue / 10000;
    iValue -= 10000 * iYear;
    int iMonth = iValue / 100;
    iValue -= 100 * iMonth;
    int iDay = iValue;

    if (  isValidMonth(2, pStr+nStr-4, iMonth)
       && isValidDayOfMonth(2, pStr+nStr-2, iDay)
       && isValidYear(nStr-4, pStr, iYear))
    {
        return true;
    }
    return false;
}

// This function breaks down YYMMDD, YYYMMDD, YYYYMMDD, YYYYYMMDD
//
void BreakDownYMD(PD_Node *pNode)
{
    pNode->uCouldBe = PDCB_YEAR;
    SplitLastTwoDigits(pNode, PDCB_DAY_OF_MONTH);
    SplitLastTwoDigits(pNode, PDCB_MONTH);
}

// This function handles MMDDYY
//
bool isValidMDY(size_t nStr, char *pStr, int iValue)
{
    int iMonth = iValue / 10000;
    iValue -= 10000 * iMonth;
    int iDay = iValue / 100;
    iValue -= 100 * iDay;
    int iYear = iValue;

    if (  6 == nStr
       && isValidMonth(2, pStr, iMonth)
       && isValidDayOfMonth(2, pStr+2, iDay)
       && isValidYear(2, pStr+4, iYear))
    {
        return true;
    }
    return false;
}

// This function breaks down MMDDYY
//
void BreakDownMDY(PD_Node *pNode)
{
    pNode->uCouldBe = PDCB_MONTH;
    SplitLastTwoDigits(pNode, PDCB_YEAR);
    SplitLastTwoDigits(pNode, PDCB_DAY_OF_MONTH);
}

// This function handles DDMMYY
//
bool isValidDMY(size_t nStr, char *pStr, int iValue)
{
    int iDay = iValue / 10000;
    iValue -= 10000 * iDay;
    int iMonth = iValue / 100;
    iValue -= 100 * iMonth;
    int iYear = iValue;

    if (  6 == nStr
       && isValidMonth(2, pStr+2, iMonth)
       && isValidDayOfMonth(2, pStr, iDay)
       && isValidYear(2, pStr+4, iYear))
    {
        return true;
    }
    return false;
}

// This function breaks down DDMMYY
//
void BreakDownDMY(PD_Node *pNode)
{
    pNode->uCouldBe = PDCB_DAY_OF_MONTH;
    SplitLastTwoDigits(pNode, PDCB_YEAR);
    SplitLastTwoDigits(pNode, PDCB_MONTH);
}

// This function handles YDDD, YYDDD, YYYDDD, YYYYDDD, YYYYYDDD
//
bool isValidYD(size_t nStr, char *pStr, int iValue)
{
    int iYear = iValue / 1000;
    iValue -= 1000*iYear;
    int iDay = iValue;

    if (  4 <= nStr && nStr <= 8
       && isValidDayOfYear(3, pStr+nStr-3, iDay)
       && isValidYear(nStr-3, pStr, iYear))
    {
        return true;
    }
    return false;
}

// This function breaks down YDDD, YYDDD, YYYDDD, YYYYDDD, YYYYYDDD
//
void BreakDownYD(PD_Node *pNode)
{
    pNode->uCouldBe = PDCB_YEAR;
    SplitLastThreeDigits(pNode, PDCB_DAY_OF_YEAR);
}

const int InitialCouldBe[9] =
{
    PDCB_YEAR|PDCB_MONTH|PDCB_DAY_OF_MONTH|PDCB_DAY_OF_WEEK|PDCB_HMS_TIME|PDCB_HMS_TIMEZONE|PDCB_HOUR_TIME|PDCB_HOUR_TIMEZONE|PDCB_SUBSECOND,  // 1
    PDCB_YEAR|PDCB_MONTH|PDCB_DAY_OF_MONTH|PDCB_WEEK_OF_YEAR|PDCB_HMS_TIME|PDCB_HMS_TIMEZONE|PDCB_HOUR_TIME|PDCB_HOUR_TIMEZONE|PDCB_MINUTE|PDCB_SECOND|PDCB_SUBSECOND, // 2
    PDCB_YEAR|PDCB_HMS_TIME|PDCB_HMS_TIMEZONE|PDCB_DAY_OF_YEAR|PDCB_SUBSECOND, // 3
    PDCB_YEAR|PDCB_HMS_TIME|PDCB_HMS_TIMEZONE|PDCB_YD|PDCB_SUBSECOND, // 4
    PDCB_YEAR|PDCB_HMS_TIME|PDCB_HMS_TIMEZONE|PDCB_YD|PDCB_SUBSECOND, // 5
    PDCB_HMS_TIME|PDCB_HMS_TIMEZONE|PDCB_YMD|PDCB_MDY|PDCB_DMY|PDCB_YD|PDCB_SUBSECOND, // 6
    PDCB_YMD|PDCB_YD|PDCB_SUBSECOND, // 7
    PDCB_YMD|PDCB_YD, // 8
    PDCB_YMD  // 9
};

typedef bool PVALIDFUNC(size_t nStr, char *pStr, int iValue);

typedef struct tag_pd_numeric_valid
{
    unsigned mask;
    PVALIDFUNC *fnValid;
} NUMERIC_VALID_RECORD;

const NUMERIC_VALID_RECORD NumericSet[] =
{
    { PDCB_YEAR,         isValidYear       },
    { PDCB_MONTH,        isValidMonth      },
    { PDCB_DAY_OF_MONTH, isValidDayOfMonth },
    { PDCB_DAY_OF_YEAR,  isValidDayOfYear  },
    { PDCB_WEEK_OF_YEAR, isValidWeekOfYear },
    { PDCB_DAY_OF_WEEK,  isValidDayOfWeek  },
    { PDCB_HMS_TIME|PDCB_HMS_TIMEZONE, isValidHMS },
    { PDCB_YMD,          isValidYMD        },
    { PDCB_MDY,          isValidMDY        },
    { PDCB_DMY,          isValidDMY        },
    { PDCB_YD,           isValidYD         },
    { PDCB_HOUR_TIME|PDCB_HOUR_TIMEZONE, isValidHour },
    { PDCB_MINUTE,       isValidMinute     },
    { PDCB_SECOND,       isValidSecond     },
    { PDCB_SUBSECOND,    isValidSubSecond  },
    { 0, 0},
};

// This function looks at the numeric token and assigns the initial set
// of possibilities.
//
void ClassifyNumericToken(PD_Node *pNode)
{
    size_t nToken = pNode->nToken;
    char  *pToken = pNode->pToken;
    int    iToken = pNode->iToken;

    unsigned int uCouldBe = InitialCouldBe[nToken-1];

    int i = 0;
    int mask = 0;
    while ((mask = NumericSet[i].mask) != 0)
    {
        if (  (mask & uCouldBe)
           && !(NumericSet[i].fnValid(nToken, pToken, iToken)))
        {
            uCouldBe &= ~mask;
        }
        i++;
    }
    pNode->uCouldBe = uCouldBe;
}

typedef struct
{
    char        *szText;
    unsigned int uCouldBe;
    int          iValue;
} PD_TEXT_ENTRY;

const PD_TEXT_ENTRY PD_TextTable[] =
{
    {"sun",       PDCB_DAY_OF_WEEK,   7 },
    {"mon",       PDCB_DAY_OF_WEEK,   1 },
    {"tue",       PDCB_DAY_OF_WEEK,   2 },
    {"wed",       PDCB_DAY_OF_WEEK,   3 },
    {"thu",       PDCB_DAY_OF_WEEK,   4 },
    {"fri",       PDCB_DAY_OF_WEEK,   5 },
    {"sat",       PDCB_DAY_OF_WEEK,   6 },
    {"jan",       PDCB_MONTH,         1 },
    {"feb",       PDCB_MONTH,         2 },
    {"mar",       PDCB_MONTH,         3 },
    {"apr",       PDCB_MONTH,         4 },
    {"may",       PDCB_MONTH,         5 },
    {"jun",       PDCB_MONTH,         6 },
    {"jul",       PDCB_MONTH,         7 },
    {"aug",       PDCB_MONTH,         8 },
    {"sep",       PDCB_MONTH,         9 },
    {"oct",       PDCB_MONTH,        10 },
    {"nov",       PDCB_MONTH,        11 },
    {"dec",       PDCB_MONTH,        12 },
    {"january",   PDCB_MONTH,         1 },
    {"february",  PDCB_MONTH,         2 },
    {"march",     PDCB_MONTH,         3 },
    {"april",     PDCB_MONTH,         4 },
    {"may",       PDCB_MONTH,         5 },
    {"june",      PDCB_MONTH,         6 },
    {"july",      PDCB_MONTH,         7 },
    {"august",    PDCB_MONTH,         8 },
    {"september", PDCB_MONTH,         9 },
    {"october",   PDCB_MONTH,        10 },
    {"november",  PDCB_MONTH,        11 },
    {"december",  PDCB_MONTH,        12 },
    {"sunday",    PDCB_DAY_OF_WEEK,   7 },
    {"monday",    PDCB_DAY_OF_WEEK,   1 },
    {"tuesday",   PDCB_DAY_OF_WEEK,   2 },
    {"wednesday", PDCB_DAY_OF_WEEK,   3 },
    {"thursday",  PDCB_DAY_OF_WEEK,   4 },
    {"friday",    PDCB_DAY_OF_WEEK,   5 },
    {"saturday",  PDCB_DAY_OF_WEEK,   6 },
    {"a",         PDCB_TIMEZONE,    100 },
    {"b",         PDCB_TIMEZONE,    200 },
    {"c",         PDCB_TIMEZONE,    300 },
    {"d",         PDCB_TIMEZONE,    400 },
    {"e",         PDCB_TIMEZONE,    500 },
    {"f",         PDCB_TIMEZONE,    600 },
    {"g",         PDCB_TIMEZONE,    700 },
    {"h",         PDCB_TIMEZONE,    800 },
    {"i",         PDCB_TIMEZONE,    900 },
    {"k",         PDCB_TIMEZONE,   1000 },
    {"l",         PDCB_TIMEZONE,   1100 },
    {"m",         PDCB_TIMEZONE,   1200 },
    {"n",         PDCB_TIMEZONE,   -100 },
    {"o",         PDCB_TIMEZONE,   -200 },
    {"p",         PDCB_TIMEZONE,   -300 },
    {"q",         PDCB_TIMEZONE,   -400 },
    {"r",         PDCB_TIMEZONE,   -500 },
    {"s",         PDCB_TIMEZONE,   -600 },
    {"t",         PDCB_DATE_TIME_SEPARATOR|PDCB_TIMEZONE, -700},
    {"u",         PDCB_TIMEZONE,   -800 },
    {"v",         PDCB_TIMEZONE,   -900 },
    {"w",         PDCB_WEEK_OF_YEAR_PREFIX|PDCB_TIMEZONE,  -1000 },
    {"x",         PDCB_TIMEZONE,  -1100 },
    {"y",         PDCB_TIMEZONE,  -1200 },
    {"z",         PDCB_TIMEZONE,      0 },
    {"hst",       PDCB_TIMEZONE,  -1000 },
    {"akst",      PDCB_TIMEZONE,   -900 },
    {"pst",       PDCB_TIMEZONE,   -800 },
    {"mst",       PDCB_TIMEZONE,   -700 },
    {"cst",       PDCB_TIMEZONE,   -600 },
    {"est",       PDCB_TIMEZONE,   -500 },
    {"ast",       PDCB_TIMEZONE,   -400 },
    {"akdt",      PDCB_TIMEZONE,   -800 },
    {"pdt",       PDCB_TIMEZONE,   -700 },
    {"mdt",       PDCB_TIMEZONE,   -600 },
    {"cdt",       PDCB_TIMEZONE,   -500 },
    {"edt",       PDCB_TIMEZONE,   -400 },
    {"adt",       PDCB_TIMEZONE,   -300 },
    {"bst",       PDCB_TIMEZONE,    100 },
    {"ist",       PDCB_TIMEZONE,    100 },
    {"cet",       PDCB_TIMEZONE,    100 },
    {"cest",      PDCB_TIMEZONE,    200 },
    {"eet",       PDCB_TIMEZONE,    200 },
    {"eest",      PDCB_TIMEZONE,    300 },
    {"aest",      PDCB_TIMEZONE,   1000 },
    {"gmt",       PDCB_TIMEZONE,      0 },
    {"ut",        PDCB_TIMEZONE,      0 },
    {"utc",       PDCB_TIMEZONE,      0 },
    {"st",        PDCB_DAY_OF_MONTH_SUFFIX, 0 },
    {"nd",        PDCB_DAY_OF_MONTH_SUFFIX, 0 },
    {"rd",        PDCB_DAY_OF_MONTH_SUFFIX, 0 },
    {"th",        PDCB_DAY_OF_MONTH_SUFFIX, 0 },
    {"am",        PDCB_MERIDIAN,      0 },
    {"pm",        PDCB_MERIDIAN,     12 },
    { 0, 0, 0}
};

#define PD_LEX_INVALID 0
#define PD_LEX_SYMBOL  1
#define PD_LEX_DIGIT   2
#define PD_LEX_SPACE   3
#define PD_LEX_ALPHA   4
#define PD_LEX_EOS     5

const char LexTable[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    5, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,  // 2
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 1, 0, 0, 0, 0, 0,  // 3
    0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  // 4
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0,  // 5
    0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  // 6
    4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0, 0, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

int nNodes = 0;
#define MAX_NODES 200
PD_Node Nodes[MAX_NODES];

PD_Node *PD_Head = 0;
PD_Node *PD_Tail = 0;

void PD_Reset(void)
{
    nNodes = 0;
    PD_Head = 0;
    PD_Tail = 0;
}

PD_Node *PD_NewNode(void)
{
    if (nNodes < MAX_NODES)
    {
        return Nodes+(nNodes++);
    }
    return NULL;
}

PD_Node *PD_FirstNode(void)
{
    return PD_Head;
}

PD_Node *PD_LastNode(void)
{
    return PD_Tail;
}

PD_Node *PD_NextNode(PD_Node *pNode)
{
    return pNode->pNextNode;
}

PD_Node *PD_PrevNode(PD_Node *pNode)
{
    return pNode->pPrevNode;
}

// PD_AppendNode - Appends a node onto the end of the list. It's used during
// the first pass over the date string; these nodes are therefore always
// elemental nodes. It might be possible to append a group node. However,
// usually group nodes a created at a later from recognizing a deeper semantic
// meaning of an elemental node and this promotion event could happen anywhere
// in the sequence. The order of promotion isn't clear during the first pass
// over the date string.
//
void PD_AppendNode(PD_Node *pNode)
{
    if (!PD_Head)
    {
        PD_Head = PD_Tail = pNode;
        return;
    }
    pNode->pNextNode = 0;
    PD_Tail->pNextNode = pNode;
    pNode->pPrevNode = PD_Tail;
    PD_Tail = pNode;
}

void PD_InsertAfter(PD_Node *pWhere, PD_Node *pNode)
{
    pNode->pPrevNode = pWhere;
    pNode->pNextNode = pWhere->pNextNode;

    pWhere->pNextNode = pNode;
    if (pNode->pNextNode)
    {
        pNode->pNextNode->pPrevNode = pNode;
    }
    else
    {
        PD_Tail = pNode;
    }
}

void PD_RemoveNode(PD_Node *pNode)
{
    if (pNode == PD_Head)
    {
        if (pNode == PD_Tail)
        {
            PD_Head = PD_Tail = 0;
        }
        else
        {
            PD_Head = pNode->pNextNode;
            PD_Head->pPrevNode = 0;
            pNode->pNextNode = 0;
        }
    }
    else if (pNode == PD_Tail)
    {
        PD_Tail = pNode->pPrevNode;
        PD_Tail->pNextNode = 0;
        pNode->pPrevNode = 0;
    }
    else
    {
        pNode->pNextNode->pPrevNode = pNode->pPrevNode;
        pNode->pPrevNode->pNextNode = pNode->pNextNode;
        pNode->pNextNode = 0;
        pNode->pPrevNode = 0;
    }
}

PD_Node *PD_ScanNextToken(char **ppString)
{
    char *p = *ppString;
    int ch = *p;
    if (ch == 0)
    {
        return NULL;
    }
    PD_Node *pNode;
    int iType = LexTable[ch];
    if (iType == PD_LEX_EOS || iType == PD_LEX_INVALID)
    {
        return NULL;
    }
    else if (iType == PD_LEX_SYMBOL)
    {
        pNode = PD_NewNode();
        if (!pNode)
        {
            return NULL;
        }
        pNode->pNextNode = 0;
        pNode->pPrevNode = 0;
        pNode->iToken = 0;
        pNode->nToken = 1;
        pNode->pToken = p;
        pNode->uTokenType = PDTT_SYMBOL;
        if (ch == ':')
        {
            pNode->uCouldBe = PDCB_TIME_FIELD_SEPARATOR;
        }
        else if (ch == '-')
        {
            pNode->uCouldBe = PDCB_DATE_FIELD_SEPARATOR|PDCB_SIGN;
        }
        else if (ch == '+')
        {
            pNode->uCouldBe = PDCB_SIGN;
        }
        else if (ch == '/')
        {
            pNode->uCouldBe = PDCB_DATE_FIELD_SEPARATOR;
        }
        else if (ch == '.')
        {
            pNode->uCouldBe = PDCB_DATE_FIELD_SEPARATOR|PDCB_SECONDS_DECIMAL;
        }
        else if (ch == ',')
        {
            pNode->uCouldBe = PDCB_REMOVEABLE|PDCB_SECONDS_DECIMAL|PDCB_DAY_OF_MONTH_SUFFIX;
        }

        p++;
    }
    else
    {
        char *pSave = p;
        do
        {
            p++;
            ch = *p;
        } while (iType == LexTable[ch]);

        pNode = PD_NewNode();
        if (!pNode)
        {
            return NULL;
        }
        pNode->pNextNode = 0;
        pNode->pPrevNode = 0;
        size_t nLen = p - pSave;
        pNode->nToken = nLen;
        pNode->pToken = pSave;
        pNode->iToken = 0;
        pNode->uCouldBe = PDCB_NOTHING;

        if (iType == PD_LEX_DIGIT)
        {
            pNode->uTokenType = PDTT_NUMERIC_UNSIGNED;

            if (1 <= nLen && nLen <= 9)
            {
                char buff[10];
                memcpy(buff, pSave, nLen);
                buff[nLen] = '\0';
                pNode->iToken = mux_atol(buff);
                ClassifyNumericToken(pNode);
            }
        }
        else if (iType == PD_LEX_SPACE)
        {
            pNode->uTokenType = PDTT_SPACES;
            pNode->uCouldBe = PDCB_WHITESPACE;
        }
        else if (iType == PD_LEX_ALPHA)
        {
            pNode->uTokenType = PDTT_TEXT;

            // Match Text.
            //
            int j = 0;
            bool bFound = false;
            while (PD_TextTable[j].szText)
            {
                if (  strlen(PD_TextTable[j].szText) == nLen
                   && mux_memicmp(PD_TextTable[j].szText, pSave, nLen) == 0)
                {
                    pNode->uCouldBe = PD_TextTable[j].uCouldBe;
                    pNode->iToken = PD_TextTable[j].iValue;
                    bFound = true;
                    break;
                }
                j++;
            }
            if (!bFound)
            {
                return NULL;
            }
        }
    }
    *ppString = p;
    return pNode;
}

const PD_BREAKDOWN BreakDownTable[] =
{
    { PDCB_HMS_TIME, BreakDownHMS },
    { PDCB_HMS_TIMEZONE, BreakDownHMS },
    { PDCB_YD,  BreakDownYD },
    { PDCB_YMD, BreakDownYMD },
    { PDCB_MDY, BreakDownMDY },
    { PDCB_DMY, BreakDownDMY },
    { 0, 0 }
};

void PD_Pass2(void)
{
    PD_Node *pNode = PD_FirstNode();
    while (pNode)
    {
        PD_Node *pPrev = PD_PrevNode(pNode);
        PD_Node *pNext = PD_NextNode(pNode);

        // Absorb information from PDCB_TIME_FIELD_SEPARATOR.
        //
        if (pNode->uCouldBe & PDCB_TIME_FIELD_SEPARATOR)
        {
            if (pPrev && pNext)
            {
                if (  (pPrev->uCouldBe & (PDCB_HOUR_TIME|PDCB_HOUR_TIMEZONE))
                   && (pNext->uCouldBe & PDCB_MINUTE))
                {
                    pPrev->uCouldBe &= (PDCB_HOUR_TIME|PDCB_HOUR_TIMEZONE);
                    pNext->uCouldBe = PDCB_MINUTE;
                }
                else if (  (pPrev->uCouldBe & PDCB_MINUTE)
                        && (pNext->uCouldBe & PDCB_SECOND))
                {
                    pPrev->uCouldBe = PDCB_MINUTE;
                    pNext->uCouldBe = PDCB_SECOND;
                }
            }
            pNode->uCouldBe = PDCB_REMOVEABLE;
        }

        // Absorb information from PDCB_SECONDS_DECIMAL.
        //
        if (pNode->uCouldBe & PDCB_SECONDS_DECIMAL)
        {
            if (  pPrev
               && pNext
               && pPrev->uCouldBe == PDCB_SECOND
               && (pNext->uCouldBe & PDCB_SUBSECOND))
            {
                pNode->uCouldBe = PDCB_SECONDS_DECIMAL;
                pNext->uCouldBe = PDCB_SUBSECOND;
            }
            else
            {
                pNode->uCouldBe &= ~PDCB_SECONDS_DECIMAL;
            }
            pNode->uCouldBe = PDCB_REMOVEABLE;
        }

        // Absorb information from PDCB_SUBSECOND
        //
        if (pNode->uCouldBe != PDCB_SUBSECOND)
        {
            pNode->uCouldBe &= ~PDCB_SUBSECOND;
        }

        // Absorb information from PDCB_DATE_FIELD_SEPARATOR.
        //
        if (pNode->uCouldBe & PDCB_DATE_FIELD_SEPARATOR)
        {
            pNode->uCouldBe &= ~PDCB_DATE_FIELD_SEPARATOR;

#define PDCB_SEPS (PDCB_YEAR|PDCB_MONTH|PDCB_DAY_OF_MONTH|PDCB_DAY_OF_YEAR|PDCB_WEEK_OF_YEAR|PDCB_DAY_OF_WEEK)
            if (  pPrev
               && pNext
               && (pPrev->uCouldBe & PDCB_SEPS)
               && (pNext->uCouldBe & PDCB_SEPS))
            {
                pPrev->uCouldBe &= PDCB_SEPS;
                pNext->uCouldBe &= PDCB_SEPS;
                pNode->uCouldBe = PDCB_REMOVEABLE;
            }
        }


        // Process PDCB_DAY_OF_MONTH_SUFFIX
        //
        if (pNode->uCouldBe & PDCB_DAY_OF_MONTH_SUFFIX)
        {
            pNode->uCouldBe = PDCB_REMOVEABLE;
            if (  pPrev
               && (pPrev->uCouldBe & PDCB_DAY_OF_MONTH))
            {
                pPrev->uCouldBe = PDCB_DAY_OF_MONTH;
            }
        }

        // Absorb semantic meaning of PDCB_SIGN.
        //
        if (pNode->uCouldBe == PDCB_SIGN)
        {
#define PDCB_SIGNABLES_POS (PDCB_HMS_TIME|PDCB_HMS_TIMEZONE)
#define PDCB_SIGNABLES_NEG (PDCB_YEAR|PDCB_YD|PDCB_SIGNABLES_POS|PDCB_YMD)
            unsigned Signable;
            if (pNode->pToken[0] == '-')
            {
                Signable = PDCB_SIGNABLES_NEG;
            }
            else
            {
                Signable = PDCB_SIGNABLES_POS;
            }
            if (  pNext
               && (pNext->uCouldBe & Signable))
            {
                pNext->uCouldBe &= Signable;
            }
            else
            {
                pNode->uCouldBe = PDCB_REMOVEABLE;
            }
        }

        // A timezone HOUR or HMS requires a leading sign.
        //
        if (pNode->uCouldBe & (PDCB_HMS_TIMEZONE|PDCB_HOUR_TIMEZONE))
        {
            if (  !pPrev
               || pPrev->uCouldBe != PDCB_SIGN)
            {
                pNode->uCouldBe &= ~(PDCB_HMS_TIMEZONE|PDCB_HOUR_TIMEZONE);
            }
        }

        // Likewise, a PDCB_HOUR_TIME or PDCB_HMS_TIME cannot have a
        // leading sign.
        //
        if (pNode->uCouldBe & (PDCB_HMS_TIME|PDCB_HOUR_TIME))
        {
            if (  pPrev
               && pPrev->uCouldBe == PDCB_SIGN)
            {
                pNode->uCouldBe &= ~(PDCB_HMS_TIME|PDCB_HOUR_TIME);
            }
        }

        // Remove PDCB_WHITESPACE.
        //
        if (pNode->uCouldBe & (PDCB_WHITESPACE|PDCB_REMOVEABLE))
        {
            PD_RemoveNode(pNode);
        }
        pNode = pNext;
    }
}

typedef struct tag_pd_cantbe
{
    unsigned int mask;
    unsigned int cantbe;
} PD_CANTBE;

const PD_CANTBE CantBeTable[] =
{
    { PDCB_YEAR,         PDCB_YEAR|PDCB_YD|PDCB_YMD|PDCB_MDY|PDCB_DMY },
    { PDCB_MONTH,        PDCB_MONTH|PDCB_WEEK_OF_YEAR|PDCB_DAY_OF_YEAR|PDCB_YD|PDCB_YMD|PDCB_MDY|PDCB_DMY|PDCB_WEEK_OF_YEAR_PREFIX },
    { PDCB_DAY_OF_MONTH, PDCB_DAY_OF_MONTH|PDCB_WEEK_OF_YEAR|PDCB_DAY_OF_YEAR|PDCB_YD|PDCB_YMD|PDCB_MDY|PDCB_DMY|PDCB_WEEK_OF_YEAR_PREFIX },
    { PDCB_DAY_OF_WEEK,  PDCB_DAY_OF_WEEK },
    { PDCB_WEEK_OF_YEAR, PDCB_WEEK_OF_YEAR|PDCB_MONTH|PDCB_DAY_OF_MONTH|PDCB_YMD|PDCB_MDY|PDCB_DMY },
    { PDCB_DAY_OF_YEAR,  PDCB_DAY_OF_YEAR|PDCB_MONTH|PDCB_DAY_OF_MONTH|PDCB_WEEK_OF_YEAR|PDCB_YMD|PDCB_MDY|PDCB_DMY|PDCB_WEEK_OF_YEAR_PREFIX },
    { PDCB_YD,           PDCB_YEAR|PDCB_YD|PDCB_MONTH|PDCB_DAY_OF_MONTH|PDCB_WEEK_OF_YEAR|PDCB_YMD|PDCB_MDY|PDCB_DMY|PDCB_WEEK_OF_YEAR_PREFIX },
    { PDCB_YMD,          PDCB_YEAR|PDCB_YMD|PDCB_MDY|PDCB_DMY|PDCB_MONTH|PDCB_DAY_OF_MONTH|PDCB_WEEK_OF_YEAR_PREFIX|PDCB_WEEK_OF_YEAR|PDCB_YD|PDCB_DAY_OF_YEAR },
    { PDCB_MDY,          PDCB_YEAR|PDCB_YMD|PDCB_MDY|PDCB_DMY|PDCB_MONTH|PDCB_DAY_OF_MONTH|PDCB_WEEK_OF_YEAR_PREFIX|PDCB_WEEK_OF_YEAR|PDCB_YD|PDCB_DAY_OF_YEAR },
    { PDCB_DMY,          PDCB_YEAR|PDCB_YMD|PDCB_MDY|PDCB_DMY|PDCB_MONTH|PDCB_DAY_OF_MONTH|PDCB_WEEK_OF_YEAR_PREFIX|PDCB_WEEK_OF_YEAR|PDCB_YD|PDCB_DAY_OF_YEAR },
    { PDCB_YMD|PDCB_MDY, PDCB_YEAR|PDCB_YMD|PDCB_MDY|PDCB_DMY|PDCB_MONTH|PDCB_DAY_OF_MONTH|PDCB_WEEK_OF_YEAR_PREFIX|PDCB_WEEK_OF_YEAR|PDCB_YD|PDCB_DAY_OF_YEAR },
    { PDCB_MDY|PDCB_DMY, PDCB_YEAR|PDCB_YMD|PDCB_MDY|PDCB_DMY|PDCB_MONTH|PDCB_DAY_OF_MONTH|PDCB_WEEK_OF_YEAR_PREFIX|PDCB_WEEK_OF_YEAR|PDCB_YD|PDCB_DAY_OF_YEAR },
    { PDCB_YMD|PDCB_DMY, PDCB_YEAR|PDCB_YMD|PDCB_MDY|PDCB_DMY|PDCB_MONTH|PDCB_DAY_OF_MONTH|PDCB_WEEK_OF_YEAR_PREFIX|PDCB_WEEK_OF_YEAR|PDCB_YD|PDCB_DAY_OF_YEAR },
    { PDCB_YMD|PDCB_DMY|PDCB_MDY, PDCB_YEAR|PDCB_YMD|PDCB_MDY|PDCB_DMY|PDCB_MONTH|PDCB_DAY_OF_MONTH|PDCB_WEEK_OF_YEAR_PREFIX|PDCB_WEEK_OF_YEAR|PDCB_YD|PDCB_DAY_OF_YEAR },
    { PDCB_TIMEZONE, PDCB_TIMEZONE|PDCB_HMS_TIMEZONE|PDCB_HOUR_TIMEZONE },
    { PDCB_HOUR_TIME, PDCB_HMS_TIME|PDCB_HOUR_TIME },
    { PDCB_HOUR_TIMEZONE, PDCB_TIMEZONE|PDCB_HMS_TIMEZONE|PDCB_HOUR_TIMEZONE },
    { PDCB_HMS_TIME, PDCB_HMS_TIME|PDCB_HOUR_TIME },
    { PDCB_HMS_TIMEZONE, PDCB_TIMEZONE|PDCB_HMS_TIMEZONE|PDCB_HOUR_TIMEZONE },
    { 0, 0 }
};

void PD_Deduction(void)
{
    PD_Node *pNode = PD_FirstNode();
    while (pNode)
    {
        int j =0;
        while (CantBeTable[j].mask)
        {
            if (pNode->uCouldBe == CantBeTable[j].mask)
            {
                PD_Node *pNodeInner = PD_FirstNode();
                while (pNodeInner)
                {
                    pNodeInner->uCouldBe &= ~CantBeTable[j].cantbe;
                    pNodeInner = PD_NextNode(pNodeInner);
                }
                pNode->uCouldBe = CantBeTable[j].mask;
                break;
            }
            j++;
        }
        pNode = PD_NextNode(pNode);
    }
}

void PD_BreakItDown(void)
{
    PD_Node *pNode = PD_FirstNode();
    while (pNode)
    {
        int j =0;
        while (BreakDownTable[j].mask)
        {
            if (pNode->uCouldBe == BreakDownTable[j].mask)
            {
                BreakDownTable[j].fpBreakDown(pNode);
                break;
            }
            j++;
        }
        pNode = PD_NextNode(pNode);
    }
}

void PD_Pass5(void)
{
    bool bHaveSeenTimeHour = false;
    bool bMightHaveSeenTimeHour = false;
    PD_Node *pNode = PD_FirstNode();
    while (pNode)
    {
        PD_Node *pPrev = PD_PrevNode(pNode);
        PD_Node *pNext = PD_NextNode(pNode);

        // If all that is left is PDCB_HMS_TIME and PDCB_HOUR_TIME, then
        // it's PDCB_HOUR_TIME.
        //
        if (pNode->uCouldBe == (PDCB_HMS_TIME|PDCB_HOUR_TIME))
        {
            pNode->uCouldBe = PDCB_HOUR_TIME;
        }
        if (pNode->uCouldBe == (PDCB_HMS_TIMEZONE|PDCB_HOUR_TIMEZONE))
        {
            pNode->uCouldBe = PDCB_HOUR_TIMEZONE;
        }

        // PDCB_MINUTE must follow an PDCB_HOUR_TIME or PDCB_HOUR_TIMEZONE.
        //
        if (pNode->uCouldBe & PDCB_MINUTE)
        {
            if (  !pPrev
               || !(pPrev->uCouldBe & (PDCB_HOUR_TIME|PDCB_HOUR_TIMEZONE)))
            {
                pNode->uCouldBe &= ~PDCB_MINUTE;
            }
        }

        // PDCB_SECOND must follow an PDCB_MINUTE.
        //
        if (pNode->uCouldBe & PDCB_SECOND)
        {
            if (  !pPrev
               || !(pPrev->uCouldBe & PDCB_MINUTE))
            {
                pNode->uCouldBe &= ~PDCB_SECOND;
            }
        }

        // YMD MDY DMY
        //
        // PDCB_DAY_OF_MONTH cannot follow PDCB_YEAR.
        //
        if (  (pNode->uCouldBe & PDCB_DAY_OF_MONTH)
           && pPrev
           && pPrev->uCouldBe == PDCB_YEAR)
        {
            pNode->uCouldBe &= ~PDCB_DAY_OF_MONTH;
        }

        // Timezone cannot occur before the time.
        //
        if (  (pNode->uCouldBe & PDCB_TIMEZONE)
           && !bMightHaveSeenTimeHour)
        {
            pNode->uCouldBe &= ~PDCB_TIMEZONE;
        }

        // TimeDateSeparator cannot occur after the time.
        //
        if (  (pNode->uCouldBe & PDCB_DATE_TIME_SEPARATOR)
           && bHaveSeenTimeHour)
        {
            pNode->uCouldBe &= ~PDCB_DATE_TIME_SEPARATOR;
        }

        if (pNode->uCouldBe == PDCB_DATE_TIME_SEPARATOR)
        {
            PD_Node *pNodeInner = PD_FirstNode();
            while (pNodeInner && pNodeInner != pNode)
            {
                pNodeInner->uCouldBe &= ~(PDCB_TIMEZONE|PDCB_HOUR_TIME|PDCB_HOUR_TIMEZONE|PDCB_MINUTE|PDCB_SECOND|PDCB_SUBSECOND|PDCB_MERIDIAN|PDCB_HMS_TIME|PDCB_HMS_TIMEZONE);
                pNodeInner = PD_NextNode(pNodeInner);
            }
            pNodeInner = pNext;
            while (pNodeInner)
            {
                pNodeInner->uCouldBe &= ~(PDCB_WEEK_OF_YEAR_PREFIX|PDCB_YD|PDCB_YMD|PDCB_MDY|PDCB_DMY|PDCB_YEAR|PDCB_MONTH|PDCB_DAY_OF_MONTH|PDCB_DAY_OF_WEEK|PDCB_WEEK_OF_YEAR|PDCB_DAY_OF_YEAR);
                pNodeInner = PD_NextNode(pNodeInner);
            }
            pNode->uCouldBe = PDCB_REMOVEABLE;
        }

        if (pNode->uCouldBe & PDCB_WEEK_OF_YEAR_PREFIX)
        {
            if (  pNext
               && (pNext->uCouldBe & PDCB_WEEK_OF_YEAR))
            {
                pNext->uCouldBe = PDCB_WEEK_OF_YEAR;
                pNode->uCouldBe = PDCB_REMOVEABLE;
            }
            else if (pNode->uCouldBe == PDCB_WEEK_OF_YEAR_PREFIX)
            {
                pNode->uCouldBe = PDCB_REMOVEABLE;
            }
        }

        if (pNode->uCouldBe & (PDCB_HOUR_TIME|PDCB_HMS_TIME))
        {
            if ((pNode->uCouldBe & ~(PDCB_HOUR_TIME|PDCB_HMS_TIME)) == 0)
            {
                bHaveSeenTimeHour = true;
            }
            bMightHaveSeenTimeHour = true;
        }

        // Remove PDCB_REMOVEABLE.
        //
        if (pNode->uCouldBe & PDCB_REMOVEABLE)
        {
            PD_RemoveNode(pNode);
        }
        pNode = pNext;
    }
}

void PD_Pass6(void)
{
    int cYear = 0;
    int cMonth = 0;
    int cDayOfMonth = 0;
    int cWeekOfYear = 0;
    int cDayOfYear = 0;
    int cDayOfWeek = 0;
    int cTime = 0;

    PD_Node *pNode = PD_FirstNode();
    while (pNode)
    {
        if (pNode->uCouldBe & (PDCB_HMS_TIME|PDCB_HOUR_TIME))
        {
            cTime++;
        }
        if (pNode->uCouldBe & PDCB_WEEK_OF_YEAR)
        {
            cWeekOfYear++;
        }
        if (pNode->uCouldBe & (PDCB_YEAR|PDCB_YD|PDCB_YMD|PDCB_MDY|PDCB_DMY))
        {
            cYear++;
        }
        if (pNode->uCouldBe & (PDCB_MONTH|PDCB_YMD|PDCB_MDY|PDCB_DMY))
        {
            cMonth++;
        }
        if (pNode->uCouldBe & (PDCB_DAY_OF_MONTH|PDCB_YMD|PDCB_MDY|PDCB_DMY))
        {
            cDayOfMonth++;
        }
        if (pNode->uCouldBe & PDCB_DAY_OF_WEEK)
        {
            cDayOfWeek++;
        }
        if (pNode->uCouldBe & (PDCB_DAY_OF_YEAR|PDCB_YD))
        {
            cDayOfYear++;
        }
        pNode = PD_NextNode(pNode);
    }

    unsigned OnlyOneMask = 0;
    unsigned CantBeMask = 0;
    if (cYear == 1)
    {
        OnlyOneMask |= PDCB_YEAR|PDCB_YD|PDCB_YMD|PDCB_MDY|PDCB_DMY;
    }
    if (cTime == 1)
    {
        OnlyOneMask |= PDCB_HMS_TIME|PDCB_HOUR_TIME;
    }
    if (cMonth == 0 || cDayOfMonth == 0)
    {
        CantBeMask |= PDCB_MONTH|PDCB_DAY_OF_MONTH;
    }
    if (cDayOfWeek == 0)
    {
        CantBeMask |= PDCB_WEEK_OF_YEAR;
    }
    if (  cMonth == 1 && cDayOfMonth == 1
       && (cWeekOfYear != 1 || cDayOfWeek != 1)
       && cDayOfYear != 1)
    {
        OnlyOneMask |= PDCB_MONTH|PDCB_YMD|PDCB_MDY|PDCB_DMY;
        OnlyOneMask |= PDCB_DAY_OF_MONTH;
        CantBeMask |= PDCB_WEEK_OF_YEAR|PDCB_YD;
    }
    else if (cDayOfYear == 1 && (cWeekOfYear != 1 || cDayOfWeek != 1))
    {
        OnlyOneMask |= PDCB_DAY_OF_YEAR|PDCB_YD;
        CantBeMask |= PDCB_WEEK_OF_YEAR|PDCB_MONTH|PDCB_YMD|PDCB_MDY|PDCB_DMY;
        CantBeMask |= PDCB_DAY_OF_MONTH;
    }
    else if (cWeekOfYear == 1 && cDayOfWeek == 1)
    {
        OnlyOneMask |= PDCB_WEEK_OF_YEAR;
        OnlyOneMask |= PDCB_DAY_OF_WEEK;
        CantBeMask |= PDCB_YD|PDCB_MONTH|PDCB_YMD|PDCB_MDY|PDCB_DMY;
        CantBeMask |= PDCB_DAY_OF_MONTH;
    }

    // Also, if we match OnlyOneMask, then force only something in
    // OnlyOneMask.
    //
    pNode = PD_FirstNode();
    while (pNode)
    {
        if (pNode->uCouldBe & OnlyOneMask)
        {
            pNode->uCouldBe &= OnlyOneMask;
        }
        if (pNode->uCouldBe & ~CantBeMask)
        {
            pNode->uCouldBe &= ~CantBeMask;
        }
        pNode = PD_NextNode(pNode);
    }
}

bool PD_GetFields(ALLFIELDS *paf)
{
    paf->iYear            = NOT_PRESENT;
    paf->iDayOfYear       = NOT_PRESENT;
    paf->iMonthOfYear     = NOT_PRESENT;
    paf->iDayOfMonth      = NOT_PRESENT;
    paf->iWeekOfYear      = NOT_PRESENT;
    paf->iDayOfWeek       = NOT_PRESENT;
    paf->iHourTime        = NOT_PRESENT;
    paf->iMinuteTime      = NOT_PRESENT;
    paf->iSecondTime      = NOT_PRESENT;
    paf->iMillisecondTime = NOT_PRESENT;
    paf->iMicrosecondTime = NOT_PRESENT;
    paf->iNanosecondTime  = NOT_PRESENT;
    paf->iMinuteTimeZone  = NOT_PRESENT;

    PD_Node *pNode = PD_FirstNode();
    while (pNode)
    {
        if (pNode->uCouldBe == PDCB_YEAR)
        {
            paf->iYear = pNode->iToken;
            PD_Node *pPrev = PD_PrevNode(pNode);
            if (  pPrev
               && pPrev->uCouldBe == PDCB_SIGN
               && pPrev->pToken[0] == '-')
            {
                paf->iYear = -paf->iYear;
            }
        }
        else if (pNode->uCouldBe == PDCB_DAY_OF_YEAR)
        {
            paf->iDayOfYear = pNode->iToken;
        }
        else if (pNode->uCouldBe == PDCB_MONTH)
        {
            paf->iMonthOfYear = pNode->iToken;
        }
        else if (pNode->uCouldBe == PDCB_DAY_OF_MONTH)
        {
            paf->iDayOfMonth = pNode->iToken;
        }
        else if (pNode->uCouldBe == PDCB_WEEK_OF_YEAR)
        {
            paf->iWeekOfYear = pNode->iToken;
        }
        else if (pNode->uCouldBe == PDCB_DAY_OF_WEEK)
        {
            paf->iDayOfWeek = pNode->iToken;
        }
        else if (pNode->uCouldBe == PDCB_HOUR_TIME)
        {
            paf->iHourTime = pNode->iToken;
            pNode = PD_NextNode(pNode);
            if (  pNode
               && pNode->uCouldBe == PDCB_MINUTE)
            {
                paf->iMinuteTime = pNode->iToken;
                pNode = PD_NextNode(pNode);
                if (  pNode
                   && pNode->uCouldBe == PDCB_SECOND)
                {
                    paf->iSecondTime = pNode->iToken;
                    pNode = PD_NextNode(pNode);
                    if (  pNode
                       && pNode->uCouldBe == PDCB_SUBSECOND)
                    {
                        unsigned short ms, us, ns;
                        ParseDecimalSeconds(pNode->nToken, pNode->pToken, &ms,
                            &us, &ns);

                        paf->iMillisecondTime = ms;
                        paf->iMicrosecondTime = us;
                        paf->iNanosecondTime  = ns;
                        pNode = PD_NextNode(pNode);
                    }
                }
            }
            if (  pNode
               && pNode->uCouldBe == PDCB_MERIDIAN)
            {
                if (paf->iHourTime == 12)
                {
                    paf->iHourTime = 0;
                }
                paf->iHourTime += pNode->iToken;
                pNode = PD_NextNode(pNode);
            }
            continue;
        }
        else if (pNode->uCouldBe == PDCB_HOUR_TIMEZONE)
        {
            paf->iMinuteTimeZone = pNode->iToken * 60;
            PD_Node *pPrev = PD_PrevNode(pNode);
            if (  pPrev
               && pPrev->uCouldBe == PDCB_SIGN
               && pPrev->pToken[0] == '-')
            {
                paf->iMinuteTimeZone = -paf->iMinuteTimeZone;
            }
            pNode = PD_NextNode(pNode);
            if (  pNode
               && pNode->uCouldBe == PDCB_MINUTE)
            {
                if (paf->iMinuteTimeZone < 0)
                {
                    paf->iMinuteTimeZone -= pNode->iToken;
                }
                else
                {
                    paf->iMinuteTimeZone += pNode->iToken;
                }
                pNode = PD_NextNode(pNode);
            }
            continue;
        }
        else if (pNode->uCouldBe == PDCB_TIMEZONE)
        {
            if (pNode->iToken < 0)
            {
                paf->iMinuteTimeZone = (pNode->iToken / 100) * 60
                                - ((-pNode->iToken) % 100);
            }
            else
            {
                paf->iMinuteTimeZone = (pNode->iToken / 100) * 60
                                + pNode->iToken % 100;
            }
        }
        else if (pNode->uCouldBe & (PDCB_SIGN|PDCB_DATE_TIME_SEPARATOR))
        {
            ; // Nothing
        }
        else
        {
            return false;
        }
        pNode = PD_NextNode(pNode);
    }
    return true;
}

bool ConvertAllFieldsToLinearTime(CLinearTimeAbsolute &lta, ALLFIELDS *paf)
{
    FIELDEDTIME ft;
    memset(&ft, 0, sizeof(ft));

    int iExtraDays = 0;
    if (paf->iYear == NOT_PRESENT)
    {
        return false;
    }
    ft.iYear = paf->iYear;

    if (paf->iMonthOfYear != NOT_PRESENT && paf->iDayOfMonth != NOT_PRESENT)
    {
        ft.iMonth = paf->iMonthOfYear;
        ft.iDayOfMonth = paf->iDayOfMonth;
    }
    else if (paf->iDayOfYear != NOT_PRESENT)
    {
        iExtraDays = paf->iDayOfYear - 1;
        ft.iMonth = 1;
        ft.iDayOfMonth = 1;
    }
    else if (paf->iWeekOfYear != NOT_PRESENT && paf->iDayOfWeek != NOT_PRESENT)
    {
        // Remember that iYear in this case represents an ISO year, which
        // is not exactly the same thing as a Gregorian year.
        //
        FIELDEDTIME ftWD;
        memset(&ftWD, 0, sizeof(ftWD));
        ftWD.iYear = paf->iYear - 1;
        ftWD.iMonth = 12;
        ftWD.iDayOfMonth = 27;
        if (!lta.SetFields(&ftWD))
        {
            return false;
        }
        INT64 i64 = lta.Return100ns();
        INT64 j64;
        i64FloorDivisionMod(i64+FACTOR_100NS_PER_DAY, FACTOR_100NS_PER_WEEK, &j64);
        i64 -= j64;

        // i64 now corresponds to the Sunday that strickly preceeds before
        // December 28th, and the 28th is guaranteed to be in the previous
        // year so that the ISO and Gregorian Years are the same thing.
        //
        i64 += FACTOR_100NS_PER_WEEK*paf->iWeekOfYear;
        i64 += FACTOR_100NS_PER_DAY*paf->iDayOfWeek;
        lta.Set100ns(i64);
        lta.ReturnFields(&ft);

        // Validate that this week actually has a week 53.
        //
        if (paf->iWeekOfYear == 53)
        {
            int iDOW_ISO = (ft.iDayOfWeek == 0) ? 7 : ft.iDayOfWeek;
            int j = ft.iDayOfMonth - iDOW_ISO;
            if (ft.iMonth == 12)
            {
                if (28 <= j)
                {
                    return false;
                }
            }
            else // if (ft.iMonth == 1)
            {
                if (-3 <= j)
                {
                    return false;
                }
            }
        }
    }
    else
    {
        // Under-specified.
        //
        return false;
    }

    if (paf->iHourTime != NOT_PRESENT)
    {
        ft.iHour = paf->iHourTime;
        if (paf->iMinuteTime != NOT_PRESENT)
        {
            ft.iMinute = paf->iMinuteTime;
            if (paf->iSecondTime != NOT_PRESENT)
            {
                ft.iSecond = paf->iSecondTime;
                if (paf->iMillisecondTime != NOT_PRESENT)
                {
                    ft.iMillisecond = paf->iMillisecondTime;
                    ft.iMicrosecond = paf->iMicrosecondTime;
                    ft.iNanosecond = paf->iNanosecondTime;
                }
            }
        }
    }

    if (lta.SetFields(&ft))
    {
        CLinearTimeDelta ltd;
        if (paf->iMinuteTimeZone != NOT_PRESENT)
        {
            ltd.SetSeconds(60 * paf->iMinuteTimeZone);
            lta -= ltd;
        }
        if (iExtraDays)
        {
            ltd.Set100ns(FACTOR_100NS_PER_DAY);
            lta += ltd * iExtraDays;
        }
        return true;
    }
    return false;
}

bool ParseDate
(
    CLinearTimeAbsolute &lt,
    char *pDateString,
    bool *pbZoneSpecified
)
{
    PD_Reset();

    char *p = pDateString;
    PD_Node *pNode;
    while ((pNode = PD_ScanNextToken(&p)))
    {
        PD_AppendNode(pNode);
    }

    PD_Pass2();

    PD_Deduction();
    PD_BreakItDown();
    PD_Pass5();
    PD_Pass6();

    PD_Deduction();
    PD_BreakItDown();
    PD_Pass5();
    PD_Pass6();

    ALLFIELDS af;
    if (  PD_GetFields(&af)
       && ConvertAllFieldsToLinearTime(lt, &af))
    {
        *pbZoneSpecified = (af.iMinuteTimeZone != NOT_PRESENT);
        return true;
    }
    return false;
}
