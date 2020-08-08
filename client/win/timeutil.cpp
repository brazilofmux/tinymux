/*! \file timeutil.cpp
 * \brief Time-related helper functions.
 *
 * Date/Time code based on algorithms presented in "Calendrical Calculations",
 * Cambridge Press, 1998.
 *
 * This contains conversions between linear and fielded time as well helper
 * functions to gloss over time-related platform differences.
 */

#include "stdafx.h"

const INT64 FACTOR_MS_PER_SECOND    = INT64_C(1000);
const INT64 FACTOR_US_PER_SECOND    = INT64_C(1000000);
const INT64 FACTOR_100NS_PER_SECOND = INT64_C(10000000);
const INT64 FACTOR_100NS_PER_MINUTE = FACTOR_100NS_PER_SECOND*60;
const INT64 FACTOR_100NS_PER_HOUR   = FACTOR_100NS_PER_MINUTE*60;
const INT64 FACTOR_100NS_PER_DAY    = FACTOR_100NS_PER_HOUR*24;
const INT64 FACTOR_100NS_PER_WEEK   = FACTOR_100NS_PER_DAY*7;

const char daystab[12] =
{
    31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

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

#ifdef HAVE_NANOSLEEP
void CLinearTimeDelta::ReturnTimeSpecStruct(struct timespec *ts)
{
    INT64 Leftover;
    ts->tv_sec = static_cast<long>(i64FloorDivisionMod(m_tDelta, FACTOR_100NS_PER_SECOND, &Leftover));
    ts->tv_nsec = static_cast<long>(Leftover*FACTOR_NANOSECONDS_PER_100NS);
}
#endif // HAVE_NANOSLEEP

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

#endif // UNIX_TIME
