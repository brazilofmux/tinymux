/*! \file timezone.cpp
 * \brief Timezone-related helper functions.
 *
 * This contains conversions between local and UTC timezones.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

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
    return (localtime_r(pt_arg, ptm_arg) != nullptr);
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

CLinearTimeDelta QueryLocalOffsetAtUTC
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
