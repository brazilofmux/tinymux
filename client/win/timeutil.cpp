/*! \file timeutil.cpp
 * \brief CLinearTimeAbsolute and CLinearTimeDelta modules.
 *
 * $Id: timeutil.cpp 3226 2008-01-23 08:30:29Z brazilofmux $
 *
 * Date/Time code based on algorithms presented in "Calendrical Calculations",
 * Cambridge Press, 1998.
 *
 * The two primary classes are CLinearTimeAbsolute and CLinearTimeDelta deal
 * with civil time from a fixed Epoch and time differences generally,
 * respectively.
 *
 * This file also contains code related to parsing date strings and glossing
 * over time-related platform differences.
 */

#include "stdafx.h"

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
inline int iRemainder(int x, int y)
{
    return x % y;
}

// Provide SGEQ division on a SGEQ platform.
//
inline int iDivision(int x, int y)
{
    return x / y;
}

// Provide LLEQ division on a SGEQ platform.
//
static int iFloorDivision(int x, int y)
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

static INT64 i64FloorDivisionMod(INT64 x, INT64 y, INT64 *piMod)
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
inline int iMod(int x, int y)
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
inline int iFloorDivision(int x, int y)
{
    return x / y;
}

inline INT64 i64FloorDivisionMod(INT64 x, INT64 y, INT64 *piMod)
{
    *piMod = x % y;
    return x / y;
}
#endif // LARGEST_INT_LTE_NEG_QUOTIENT

#if 0
static int iModAdjusted(int x, int y)
{
    return iMod(x - 1, y) + 1;
}

static INT64 i64ModAdjusted(INT64 x, INT64 y)
{
    return i64Mod(x - 1, y) + 1;
}
#endif // 0

const INT64 FACTOR_100NS_PER_SECOND = INT64_C(10000000);
const INT64 FACTOR_100NS_PER_MINUTE = FACTOR_100NS_PER_SECOND*60;
const INT64 FACTOR_100NS_PER_HOUR   = FACTOR_100NS_PER_MINUTE*60;
const INT64 FACTOR_100NS_PER_DAY = FACTOR_100NS_PER_HOUR*24;
const INT64 FACTOR_100NS_PER_WEEK = FACTOR_100NS_PER_DAY*7;

int CLinearTimeAbsolute::m_nCount = 0;
WCHAR CLinearTimeAbsolute::m_Buffer[I64BUF_SIZE*2];
WCHAR CLinearTimeDelta::m_Buffer[I64BUF_SIZE*2];

static void GetUTCLinearTime(INT64 *plt);

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
    tv->tv_sec = static_cast<long>(i64FloorDivisionMod(m_tDelta, FACTOR_100NS_PER_SECOND, &Leftover));
    tv->tv_usec = static_cast<long>(i64FloorDivision(Leftover, FACTOR_100NS_PER_MICROSECOND));
}

#ifdef HAVE_NANOSLEEP
void CLinearTimeDelta::ReturnTimeSpecStruct(struct timespec *ts)
{
    INT64 Leftover;
    ts->tv_sec = static_cast<long>(i64FloorDivisionMod(m_tDelta, FACTOR_100NS_PER_SECOND, &Leftover));
    ts->tv_nsec = static_cast<long>(Leftover*FACTOR_NANOSECONDS_PER_100NS);
}
#endif // HAVE_NANOSLEEP

void CLinearTimeDelta::SetTimeValueStruct(struct timeval *tv)
{
    m_tDelta = FACTOR_100NS_PER_SECOND * tv->tv_sec
             + FACTOR_100NS_PER_MICROSECOND * tv->tv_usec;
}

static const UTF8 daystab[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };

void CLinearTimeDelta::SetMilliseconds(unsigned long arg_dwMilliseconds)
{
    m_tDelta = arg_dwMilliseconds * FACTOR_100NS_PER_MILLISECOND;
}

long CLinearTimeDelta::ReturnMilliseconds(void)
{
    return static_cast<long>(m_tDelta/FACTOR_100NS_PER_MILLISECOND);
}

INT64 CLinearTimeDelta::ReturnMicroseconds(void)
{
    return m_tDelta/FACTOR_100NS_PER_MICROSECOND;
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

static bool isValidDate(int iYear, int iMonth, int iDay)
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

static int FixedFromGregorian(int iYear, int iMonth, int iDay)
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

static int FixedFromGregorian_Adjusted(int iYear, int iMonth, int iDay)
{
    int iFixedDay = FixedFromGregorian(iYear, iMonth, iDay);

    // At this point, iFixedDay has an epoch of 1 R.D.
    // We need an Epoch of (00:00:00 UTC, January 1, 1601)
    //
    return iFixedDay - 584389;
}


// Epoch of iFixedDay should be 1 R.D.
//
static void GregorianFromFixed(int iFixedDay, int &iYear, int &iMonth,  int &iDayOfYear, int &iDayOfMonth, int &iDayOfWeek)
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

static void GregorianFromFixed_Adjusted(int iFixedDay, int &iYear, int &iMonth, int &iDayOfYear, int &iDayOfMonth, int &iDayOfWeek)
{
    // We need to convert the Epoch to 1 R.D. from
    // (00:00:00 UTC, January 1, 1601)
    //
    GregorianFromFixed(iFixedDay + 584389, iYear, iMonth, iDayOfYear, iDayOfMonth, iDayOfWeek);
}

CLinearTimeDelta::CLinearTimeDelta(CLinearTimeAbsolute t0, CLinearTimeAbsolute t1)
{
    m_tDelta = t1.m_tAbsolute - t0.m_tAbsolute;
}

long CLinearTimeDelta::ReturnDays(void)
{
    return static_cast<long>(m_tDelta/FACTOR_100NS_PER_DAY);
}

long CLinearTimeDelta::ReturnSeconds(void)
{
    return static_cast<long>(m_tDelta/FACTOR_100NS_PER_SECOND);
}

bool CLinearTimeAbsolute::ReturnFields(FIELDEDTIME *arg_tStruct)
{
    return LinearTimeToFieldedTime(m_tAbsolute, arg_tStruct);
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
    int iResult = static_cast<int>(ltdA.m_tDelta / ltdB.m_tDelta);
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

static void SetStructTm(FIELDEDTIME *ft, struct tm *ptm)
{
    ft->iYear       = static_cast<short>(ptm->tm_year + 1900);
    ft->iMonth      = static_cast<unsigned short>(ptm->tm_mon + 1);
    ft->iDayOfMonth = static_cast<unsigned short>(ptm->tm_mday);
    ft->iDayOfWeek  = static_cast<unsigned short>(ptm->tm_wday);
    ft->iDayOfYear  = 0;
    ft->iHour       = static_cast<unsigned short>(ptm->tm_hour);
    ft->iMinute     = static_cast<unsigned short>(ptm->tm_min);
    ft->iSecond     = static_cast<unsigned short>(ptm->tm_sec);
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
    ft->iDayOfWeek = static_cast<unsigned short>(iMod(iFixedDay+1, 7));

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
    int d0 = static_cast<int>(i64FloorDivisionMod(lt, FACTOR_100NS_PER_DAY, &ns100));
    GregorianFromFixed_Adjusted(d0, iYear, iMonth, iDayOfYear, iDayOfMonth, iDayOfWeek);
    if (!isValidDate(iYear, iMonth, iDayOfMonth))
    {
        return false;
    }

    ft->iYear       = static_cast<short>(iYear);
    ft->iMonth      = static_cast<unsigned short>(iMonth);
    ft->iDayOfYear  = static_cast<unsigned short>(iDayOfYear);
    ft->iDayOfMonth = static_cast<unsigned short>(iDayOfMonth);
    ft->iDayOfWeek  = static_cast<unsigned short>(iDayOfWeek);

    ft->iHour = static_cast<unsigned short>(ns100 / FACTOR_100NS_PER_HOUR);
    ns100 = ns100 % FACTOR_100NS_PER_HOUR;
    ft->iMinute = static_cast<unsigned short>(ns100 / FACTOR_100NS_PER_MINUTE);
    ns100 = ns100 % FACTOR_100NS_PER_MINUTE;
    ft->iSecond = static_cast<unsigned short>(ns100 / FACTOR_100NS_PER_SECOND);
    ns100 = ns100 % FACTOR_100NS_PER_SECOND;

    ft->iMillisecond = static_cast<unsigned short>(ns100 / FACTOR_100NS_PER_MILLISECOND);
    ns100 = ns100 % FACTOR_100NS_PER_MILLISECOND;
    ft->iMicrosecond = static_cast<unsigned short>(ns100 / FACTOR_100NS_PER_MICROSECOND);
    ns100 = ns100 % FACTOR_100NS_PER_MICROSECOND;
    ft->iNanosecond = static_cast<unsigned short>(ns100 * FACTOR_NANOSECONDS_PER_100NS);

    return true;
}

// OS Dependent Routines:
//
#if defined(WINDOWS_TIME)

void GetUTCLinearTime(INT64 *plt)
{
    GetSystemTimeAsFileTime((struct _FILETIME *)plt);
}

static DWORD WINAPI AlarmProc(LPVOID lpParameter)
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

#elif defined(UNIX_TIME)

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

#endif // UNIX_TIME

static int YearType(int iYear)
{
    FIELDEDTIME ft;
    memset(&ft, 0, sizeof(FIELDEDTIME));
    ft.iYear        = static_cast<short>(iYear);
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
static time_t time_t_midpoint(time_t tLower, time_t tUpper)
{
    time_t tDiff = (tUpper-2) - tLower;
    return tLower+tDiff/2+1;
}

static time_t time_t_largest(void)
{
    time_t t;
    if (sizeof(INT64) <= sizeof(time_t))
    {
        t = static_cast<time_t>(INT64_MAX_VALUE);
    }
    else
    {
        t = static_cast<time_t>(INT32_MAX_VALUE);
    }

#if defined(TIMEUTIL_TIME_T_MAX_VALUE)
    INT64 t64 = static_cast<INT64>(t);
    if (TIMEUTIL_TIME_T_MAX_VALUE < t64)
    {
        t = static_cast<time_t>(TIMEUTIL_TIME_T_MAX_VALUE);
    }
#endif
#if defined(LOCALTIME_TIME_T_MAX_VALUE)
    // Windows cannot handle negative time_t values, and some versions have
    // an upper limit as well. Values which are too large cause an assert.
    //
    // In VS 2003, the limit is 0x100000000000i64 (beyond the size of a
    // time_t). In VS 2005, the limit is December 31, 2999, 23:59:59 UTC
    // (or 32535215999).
    //
    if (LOCALTIME_TIME_T_MAX_VALUE < t)
    {
        t = static_cast<time_t>(LOCALTIME_TIME_T_MAX_VALUE);
    }
#endif
    return t;
}


static time_t time_t_smallest(void)
{
    time_t t;
    if (sizeof(INT64) <= sizeof(time_t))
    {
        t = static_cast<time_t>(INT64_MIN_VALUE);
    }
    else
    {
        t = static_cast<time_t>(INT32_MIN_VALUE);
    }
#if defined(TIMEUTIL_TIME_T_MIN_VALUE)
    INT64 t64 = static_cast<INT64>(t);
    if (t64 < TIMEUTIL_TIME_T_MIN_VALUE)
    {
        t = static_cast<time_t>(TIMEUTIL_TIME_T_MIN_VALUE);
    }
#endif
#if defined(LOCALTIME_TIME_T_MIN_VALUE)
    if (t < LOCALTIME_TIME_T_MIN_VALUE)
    {
        t = static_cast<time_t>(LOCALTIME_TIME_T_MIN_VALUE);
    }
#endif
    return t;
}

static bool mux_localtime(struct tm *ptm_arg, const time_t *pt_arg)
{
#if defined(WINDOWS_TIME) && !defined(__INTEL_COMPILER) && (_MSC_VER >= 1400)
    // 1400 is Visual C++ 2005
    //
    return (_localtime64_s(ptm_arg, pt_arg) == 0);
#elif defined(HAVE_LOCALTIME_R)
    return (localtime_r(pt_arg, ptm_arg) != NULL);
#else
    struct tm *ptm = localtime(pt_arg);
    if (ptm)
    {
        *ptm_arg = *ptm;
        return true;
    }
    else
    {
        return false;
    }
#endif // WINDOWS_TIME
}

// This determines the valid range of localtime() and finds a 'standard'
// time zone near the earliest supported time_t.
//
static void test_time_t(void)
{
    struct tm _tm;

    // Search for the highest supported value of time_t.
    //
    time_t tUpper = time_t_largest();
    time_t tLower = 0;
    time_t tMid;
    while (tLower < tUpper)
    {
        tMid = time_t_midpoint(tLower+1, tUpper);
        if (mux_localtime(&_tm, &tMid))
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
        if (mux_localtime(&_tm, &tMid))
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
        mux_localtime(&_tm, &tLower);

        if (_tm.tm_isdst <= 0)
        {
            // Daylight savings time is either not in effect or
            // we have no way of knowing whether it is in effect
            // or not.
            //
            FIELDEDTIME ft;
            SetStructTm(&ft, &_tm);
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

static short NearestYearOfType[15];
static CLinearTimeDelta ltdIntervalMinimum;
static bool bTimeInitialized = false;

void TIME_Initialize(void)
{
    if (bTimeInitialized)
    {
        return;
    }
    bTimeInitialized = true;

#ifdef HAVE_TZSET
    mux_tzset();
#endif // HAVE_TZSET

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
            NearestYearOfType[iYearType] = static_cast<short>(i);
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
    int                 nTouched;
    bool                isDST;
} OffsetEntry;

#define MAX_OFFSETS 50
static int nOffsetTable = 0;
static int nTouched0 = 0;
static OffsetEntry OffsetTable[MAX_OFFSETS];

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
    if (  i < 0
       || MAX_OFFSETS <= i)
    {
        return;
    }

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
    struct tm _tm;
    time_t lt = static_cast<time_t>(lta.ReturnSeconds());
    if (!mux_localtime(&_tm, &lt))
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
    SetStructTm(&ft, &_tm);
    ft.iMillisecond = 0;
    ft.iMicrosecond = 0;
    ft.iNanosecond  = 0;

    CLinearTimeAbsolute ltaLocal;
    CLinearTimeDelta ltdOffset;
    ltaLocal.SetFields(&ft);
    lta.SetSeconds(lt);
    ltdOffset = ltaLocal - lta;

    *pisDST = (_tm.tm_isdst > 0);

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
    // Because Unix supports negative time_t values while Windows does
    // not, it can also support that 1916 to 1970 interval with
    // timezone information.
    //
    // Windows only supports one timezone rule at a time, or rather
    // it doesn't have any historical timezone information, but you
    // can/must provide it yourself. So, in the Windows case, unless we
    // are willing to provide historical information (from a tzfile
    // perhaps), it will only give us the current timezone rule
    // (the one selected by the control panel or by a TZ environment
    // variable). It projects this rule forwards and backwards.
    //
    // Feel free to fill that gap in yourself with a tzfile file
    // reader for Windows.
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
