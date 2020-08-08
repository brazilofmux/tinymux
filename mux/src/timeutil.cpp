/*! \file timeutil.cpp
 * \brief Time-related helper functions.
 *
 * Date/Time code based on algorithms presented in "Calendrical Calculations",
 * Cambridge Press, 1998.
 *
 * This contains conversions between linear and fielded time as well helper
 * functions to gloss over time-related platform differences.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "mathutil.h"

const INT64 FACTOR_MS_PER_SECOND    = INT64_C(1000);
const INT64 FACTOR_US_PER_SECOND    = INT64_C(1000000);
const INT64 FACTOR_100NS_PER_SECOND = INT64_C(10000000);
const INT64 FACTOR_100NS_PER_MINUTE = FACTOR_100NS_PER_SECOND*60;
const INT64 FACTOR_100NS_PER_HOUR   = FACTOR_100NS_PER_MINUTE*60;
const INT64 FACTOR_100NS_PER_DAY    = FACTOR_100NS_PER_HOUR*24;
const INT64 FACTOR_100NS_PER_WEEK   = FACTOR_100NS_PER_DAY*7;

const UTF8 *DayOfWeekString[7] =
{
    T("Sun"),
    T("Mon"),
    T("Tue"),
    T("Wed"),
    T("Thu"),
    T("Fri"),
    T("Sat")
};

const char daystab[12] =
{
    31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
};

const UTF8 *monthtab[12] =
{
    T("Jan"),
    T("Feb"),
    T("Mar"),
    T("Apr"),
    T("May"),
    T("Jun"),
    T("Jul"),
    T("Aug"),
    T("Sep"),
    T("Oct"),
    T("Nov"),
    T("Dec")
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
            if (INT_MIN == x && -1 == y)
            {
                return 0;
            }
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
            if (INT64_MIN_VALUE == x && -1 == y)
            {
                return 0;
            }
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
    if (INT_MIN == x && -1 == y)
    {
        return 0;
    }
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
            if (INT_MIN == x && -1 == y)
            {
                *piMod = 0;
            }
            else
            {
                *piMod = x % y;
            }
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
            if (INT64_MIN_VALUE == x && -1 == y)
            {
                *piMod = 0;
            }
            else
            {
                *piMod = x % y;
            }
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
    if (INT_MIN == x && -1 == y)
    {
        return 0;
    }
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
            if (INT_MIN == x && -1 == y)
            {
                return 0;
            }
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
            if (INT64_MIN_VALUE == x && -1 == y)
            {
                return 0;
            }
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
    if (INT64_MIN_VALUE == x && -1 == y)
    {
        *piMod = 0;
    }
    else
    {
        *piMod = x % y;
    }
    return x / y;
}
#endif // LARGEST_INT_LTE_NEG_QUOTIENT

bool ParseFractionalSecondsString(INT64 &i64, UTF8 *str)
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
    UTF8 *pIntegerStart = str;
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
    UTF8 *pIntegerEnd = str;

    // Decimal point.
    //
    if (*str == '.')
    {
        str++;
    }

    // Need at least one digit
    //
    UTF8 *pFractionalStart = str;
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
    UTF8 *pFractionalEnd = str;

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
    UTF8   aBuffer[64];
    size_t nBufferAvailable = sizeof(aBuffer) - PFSS_PRECISION - 1;
    UTF8  *p = aBuffer;

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

void ConvertToSecondsString(UTF8 *buffer, INT64 n64, int nFracDigits)
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
        UTF8 *p = buffer + n;
        *p++ = '.';
        UTF8 *q = p;

        UTF8 buf[maxFracDigits+1];
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

// do_convtime()
//
// converts time string to time structure (fielded time). Returns 1 on
// success, 0 on fail. Time string format is:
//
//     [Ddd] Mmm DD HH:MM:SS YYYY
//
// The initial Day-of-week token is optional.
//
static int MonthTabHash[12] =
{
    0x004a414e, 0x00464542, 0x004d4152, 0x00415052,
    0x004d4159, 0x004a554e, 0x004a554c, 0x00415547,
    0x00534550, 0x004f4354, 0x004e4f56, 0x00444543
};

static bool ParseThreeLetters(const UTF8 **pp, int *piHash)
{
    *piHash = 0;

    // Skip Initial spaces
    //
    const UTF8 *p = *pp;
    while (*p == ' ')
    {
        p++;
    }

    // Parse space-separate token.
    //
    const UTF8 *q = p;
    int iHash = 0;
    while (*q && *q != ' ')
    {
        if (!mux_isalpha(*q))
        {
            return false;
        }
        iHash = (iHash << 8) | mux_toupper_ascii(*q);
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

void ParseDecimalSeconds(size_t n, const UTF8 *p, unsigned short *iMilli,
                         unsigned short *iMicro, unsigned short *iNano)
{
    UTF8 aBuffer[10];
    if (n > sizeof(aBuffer) - 1)
    {
        n = sizeof(aBuffer) - 1;
    }
    memcpy(aBuffer, p, n);
    memset(aBuffer + n, '0', sizeof(aBuffer) - n - 1);
    aBuffer[sizeof(aBuffer) - 1] = '\0';
    long ns = mux_atol(aBuffer);
    *iNano = static_cast<unsigned short>(ns % 1000);
    ns /= 1000;
    *iMicro = static_cast<unsigned short>(ns % 1000);
    *iMilli = static_cast<unsigned short>(ns / 1000);
}

bool do_convtime(const UTF8 *str, FIELDEDTIME *ft)
{
    memset(ft, 0, sizeof(FIELDEDTIME));
    if (!str || !ft)
    {
        return false;
    }

    // Day-of-week OR month.
    //
    const UTF8 *p = str;
    int i, iHash;
    if (!ParseThreeLetters(&p, &iHash))
    {
        return false;
    }

    for (i = 0; (i < 12) && iHash != MonthTabHash[i]; i++)
    {
        ; // Nothing.
    }

    if (i == 12)
    {
        // The above three letters were probably the Day-Of-Week, the
        // next three letters are required to be the month name.
        //
        if (!ParseThreeLetters(&p, &iHash))
        {
            return false;
        }

        for (i = 0; (i < 12) && iHash != MonthTabHash[i]; i++)
        {
            ; // Nothing.
        }

        if (i == 12)
        {
            return false;
        }
    }

    // January = 1, February = 2, etc.
    //
    ft->iMonth = static_cast<unsigned short>(i + 1);

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
        const UTF8 *q = (UTF8 *)strchr((char *)p, ' ');
        if (q)
        {
            n = q - p;
        }
        else
        {
            n = strlen((char *)p);
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
