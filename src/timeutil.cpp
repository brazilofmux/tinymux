// timeutil.cpp -- CLinearTimeAbsolute and CLinearTimeDelta modules.
//
// $Id: timeutil.cpp,v 1.11 2001-02-12 07:05:48 sdennis Exp $
//
// Date/Time code based on algorithms presented in "Calendrical Calculations",
// Cambridge Press, 1998.
//
// do_convtime() is heavily modified from previous game server code.
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

// for tzset() and localtime()
//
#include <time.h>

#include "timeutil.h"
#include "stringutil.h"


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
int DCL_INLINE iRemainder(int x, int y)
{
    return x % y;
}

// Provide SGEQ division on a SGEQ platform.
//
int DCL_INLINE iDivision(int x, int y)
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
#endif

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


int DCL_INLINE iFloorDivisionMod(int x, int y, int *piMod)
{
    *piMod = x % y;
    return x / y;
}

INT64 DCL_INLINE i64FloorDivisionMod(INT64 x, INT64 y, INT64 *piMod)
{
    *piMod = x % y;
    return x / y;
}
#endif

int iModAdjusted(int x, int y)
{
    return iMod(x - 1, y) + 1;
}
#if 0
INT64 i64ModAdjusted(INT64 x, INT64 y)
{
    return i64Mod(x - 1, y) + 1;
}
#endif

const INT64 FACTOR_100NS_PER_MINUTE = FACTOR_100NS_PER_SECOND*60;
const INT64 FACTOR_100NS_PER_HOUR   = FACTOR_100NS_PER_MINUTE*60;
const INT64 FACTOR_100NS_PER_DAY = FACTOR_100NS_PER_HOUR*24;
const INT64 FACTOR_100NS_PER_WEEK = FACTOR_100NS_PER_DAY*7;

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

int operator==(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb)
{
    return lta.m_tDelta == ltb.m_tDelta;
}

int operator!=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb)
{
    return lta.m_tDelta != ltb.m_tDelta;
}

int operator<=(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb)
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

BOOL isLeapYear(long iYear)
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

BOOL ParseThreeLetters(const char **pp, int *piHash)
{
    *piHash = 0;

    // Skip Initial spaces
    //
    const char *p = *pp;
    while (*p == ' ')
    {
        p++;
    }

    // Parse space-seperate token.
    //
    const char *q = p;
    int iHash = 0;
    while (*q && *q != ' ')
    {
        if (!Tiny_IsAlpha[(unsigned char)*q])
        {
            return FALSE;
        }
        iHash = (iHash << 8) | Tiny_ToUpper[(unsigned char)*q];
        q++;
    }

    // Must be exactly 3 letters long.
    //
    if (q - p != 3)
    {
        return FALSE;
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
    return TRUE;
}

int do_convtime(const char *str, FIELDEDTIME *ft)
{
    if (!str || !ft)
    {
        return 0;
    }

    // Day-of-week OR month.
    //
    const char *p = str;
    int i, iHash;
    if (!ParseThreeLetters(&p, &iHash))
    {
        return 0;
    }
    for (i = 0; (i < 12) && iHash != MonthTabHash[i]; i++) ;
    if (i == 12)
    {
        // The above three letters were probably the Day-Of-Week, the
        // next three letters are required to be the month name.
        //
        if (!ParseThreeLetters(&p, &iHash))
        {
            return 0;
        }
        for (i = 0; (i < 12) && iHash != MonthTabHash[i]; i++) ;
        if (i == 12)
        {
            return 0;
        }
    }
    ft->iMonth = i + 1; // January = 1, February = 2, etc.
    
    // Day of month.
    //
    ft->iDayOfMonth = (unsigned short)Tiny_atol(p);
    if (ft->iDayOfMonth < 1 || daystab[i] < ft->iDayOfMonth)
    {
        return 0;
    }
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    // Hours
    //
    ft->iHour = (unsigned short)Tiny_atol(p);
    if (ft->iHour > 23 || (ft->iHour == 0 && *p != '0'))
    {
        return 0;
    }
    while (*p && *p != ':') p++;
    if (*p == ':') p++;
    while (*p == ' ') p++;

    // Minutes
    //
    ft->iMinute = (unsigned short)Tiny_atol(p);
    if (ft->iMinute > 59 || (ft->iMinute == 0 && *p != '0'))
    {
        return 0;
    }
    while (*p && *p != ':') p++;
    if (*p == ':') p++;
    while (*p == ' ') p++;

    // Seconds
    //
    ft->iSecond = (unsigned short)Tiny_atol(p);
    if (ft->iSecond > 59 || (ft->iSecond == 0 && *p != '0'))
    {
        return 0;
    }
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;

    // Year
    //
    ft->iYear = (short)Tiny_atol(p);
    if (ft->iYear == 0 && *p != '0')
    {
        return 0;
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

BOOL CLinearTimeAbsolute::ReturnFields(FIELDEDTIME *arg_tStruct)
{
    return LinearTimeToFieldedTime(m_tAbsolute, arg_tStruct);
}

BOOL CLinearTimeAbsolute::SetString(const char *arg_tBuffer)
{
    FIELDEDTIME ft;
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

void GetUTCLinearTime(INT64 *plt)
{
    GetSystemTimeAsFileTime((struct _FILETIME *)plt);
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

#else // !WIN32

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

    SetStructTm(ft, ptm);
    ft->iMillisecond = (tv.tv_usec/1000);
    ft->iMicrosecond = (tv.tv_usec%1000);
    ft->iNanosecond = 0;
}

#endif // !WIN32

#if 0
CLinearTimeAbsolute FirstInMonth(int iYear, int iMonth, int iDayOfWeek)
{
    FIELDEDTIME ft;
    memset(&ft, 0, sizeof(FIELDEDTIME));
    ft.iYear = iYear;
    ft.iMonth = iMonth;
    ft.iDayOfMonth = 1;
    CLinearTimeAbsolute lta;
    lta.SetFields(&ft);
    ft.iDayOfMonth = iModAdjusted(ft.iDayOfMonth - ft.iDayOfWeek + iDayOfWeek, 7);
    lta.SetFields(&ft);
    return lta;
}

CLinearTimeAbsolute LastInMonth(int iYear, int iMonth, int iDayOfWeek)
{
    FIELDEDTIME ft;
    memset(&ft, 0, sizeof(FIELDEDTIME));
    if (iMonth == 12)
    {
        ft.iYear = iYear+1;
        ft.iMonth = 1;
    }
    else
    {
        ft.iYear = iYear;
        ft.iMonth = iMonth+1;
    }
    ft.iDayOfMonth = 1;
    CLinearTimeAbsolute lta;
    lta.SetFields(&ft);
    ft.iDayOfMonth = iModAdjusted(ft.iDayOfMonth - ft.iDayOfWeek + iDayOfWeek, 7);
    lta.SetFields(&ft);
    lta.Set100ns(lta.Return100ns()-FACTOR_100NS_PER_WEEK);
    return lta;
}
#endif

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
time_t time_t_midpoint(time_t ulLower, time_t ulUpper)
{
    time_t ulDiff = (ulUpper-2) - ulLower;
    return ulLower+ulDiff/2+1;
}

// This determines the valid range of time_t and finds a 'standard'
// time zone near the earliest supported time_t.
//
void test_time_t(void)
{
    // Search for the highest supported value of time_t.
    //
    time_t ulUpper = LONG_MAX;
    time_t ulLower = 0;
    time_t ulMid;
    while (ulLower < ulUpper)
    {
        ulMid = time_t_midpoint(ulLower+1, ulUpper);
        if (localtime(&ulMid))
        {
            ulLower = ulMid;
        }
        else
        {
            ulUpper = ulMid-1;
        }
    }
    ltaUpperBound.SetSeconds(ulLower);

    // Search for the lowest supported value of time_t.
    //
    ulUpper = 0;
    ulLower = LONG_MIN;
    while (ulLower < ulUpper)
    {
        ulMid = time_t_midpoint(ulLower, ulUpper-1);
        if (localtime(&ulMid))
        {
            ulUpper = ulMid;
        }
        else
        {
            ulLower = ulMid+1;
        }
    }
    ltaLowerBound.SetSeconds(ulUpper);

    // Find a time near ulLower for which DST is not in affect.
    //
    for (;;)
    {
        struct tm *ptm = localtime(&ulLower);

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
            ltaUTC.SetSeconds(ulLower);
            ltdTimeZoneStandard = ltaLocal - ltaUTC;
            break;
        }

        // Advance the time by 1 month (expressed as seconds).
        //
        ulLower += 30*24*60*60;
    }
}

int NearestYearOfType[15];
static CLinearTimeDelta ltdIntervalMinimum;

void TIME_Initialize(void)
{
    tzset();

    test_time_t();
    ltdIntervalMinimum.Set100ns(FACTOR_100NS_PER_WEEK);
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
    BOOL                isDST;
    int                 nTouched;
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

static BOOL QueryOffsetTable
(
    CLinearTimeAbsolute lta,
    CLinearTimeDelta *pltdOffset,
    BOOL *pisDST,
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
        return TRUE;
    }
    return FALSE;
}

static void UpdateOffsetTable
(
    CLinearTimeAbsolute &lta,
    CLinearTimeDelta ltdOffset,
    BOOL isDST,
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

    BOOL bTryMerge = FALSE;

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
        bTryMerge = TRUE;
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
        bTryMerge = TRUE;
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
            int nSize = sizeof(OffsetEntry)*(nOffsetTable-i-1);
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
            goto Again;
        }
    }
}

static CLinearTimeDelta QueryLocalOffsetAt_Internal
(
    CLinearTimeAbsolute lta,
    BOOL *pisDST,
    int iEntry
)
{
    // At this point, we much use localtime() to discover what the
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
    ltdOffset = ltaLocal - lta;

    *pisDST = ptm->tm_isdst > 0 ? TRUE: FALSE;

    // We now have a mapping from UTC lta to a (ltdOffset, *pisDST)
    // tuple which will will use to update the cache.
    //
    UpdateOffsetTable(lta, ltdOffset, *pisDST, iEntry);
    return ltdOffset;
}

static CLinearTimeDelta QueryLocalOffsetAtUTC
(
    const CLinearTimeAbsolute &lta,
    BOOL *pisDST
)
{
    *pisDST = FALSE;

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
    BOOL bDontCare;

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
    BOOL bDST;
    CLinearTimeDelta ltd = QueryLocalOffsetAtUTC(*this, &bDST);
    m_tAbsolute += ltd.m_tDelta;
}

void CLinearTimeAbsolute::Local2UTC(void)
{
    BOOL bDST1, bDST2;
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
