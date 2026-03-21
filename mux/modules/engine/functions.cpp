/*! \file functions.cpp
 * \brief MUX function handlers
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "ast.h"
#include "art_scan.h"
#include "sqlite_backend.h"
#include "engine_api.h"

extern "C" {
#include "color_ops.h"
}
using namespace std;

// Factory class declaration — internal to engine.so (no DCL_EXPORT).
//
class CFunctionsFactory : public mux_IClassFactory
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);
    CFunctionsFactory(void);
    virtual ~CFunctionsFactory();
private:
    uint32_t m_cRef;
};

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#if defined(INLINESQL)
#include <mysql.h>

extern MYSQL *mush_database;
#endif // INLINESQL

std::list<UFUN> ufun_list;

SEP sepSpace = { 1, " " };

// Trim off leading and trailing spaces if the separator char is a
// space -- known length version.
//
UTF8 *trim_space_sep_LEN(UTF8 *str, size_t nStr, const SEP &sep, size_t *nTrim)
{
    if (  sep.n != 1
       || sep.str[0] != ' ')
    {
        *nTrim = nStr;
        return str;
    }

    // Advance over leading spaces.
    //
    UTF8 *pBegin = str;
    UTF8 *pEnd = str + nStr - 1;
    while (*pBegin == ' ')
    {
        pBegin++;
    }

    // Advance over trailing spaces.
    //
    for (; pEnd > pBegin && *pEnd == ' '; pEnd--)
    {
        // Nothing.
    }
    pEnd++;

    *pEnd = '\0';
    *nTrim = pEnd - pBegin;
    return pBegin;
}


// Trim off leading and trailing spaces if the separator char is a space.
//
UTF8 *trim_space_sep(UTF8 *str, const SEP &sep)
{
    if (  sep.n != 1
       || sep.str[0] != ' ')
    {
        return str;
    }
    while (*str == ' ')
    {
        str++;
    }
    UTF8 *p;
    for (p = str; *p; p++)
    {
        // Nothing.
    }
    for (p--; p > str && *p == ' '; p--)
    {
        // Nothing.
    }
    p++;
    *p = '\0';
    return str;
}

// next_token: Point at start of next token in string
//
UTF8 *next_token(UTF8 *str, const SEP &sep)
{
    if (sep.n == 1)
    {
        while (  *str != '\0'
              && *str != sep.str[0])
        {
            str++;
        }
        if (!*str)
        {
            return nullptr;
        }
        str++;
        if (sep.str[0] == ' ')
        {
            while (*str == ' ')
            {
                str++;
            }
        }
    }
    else
    {
        UTF8 *p = reinterpret_cast<UTF8 *>(const_cast<char *>(strstr(reinterpret_cast<const char *>(str), reinterpret_cast<const char *>(sep.str))));
        if (p)
        {
            str = p + sep.n;
        }
        else
        {
            return nullptr;
        }
    }
    return str;
}

// split_token: Get next token from string as null-term string.  String is
// destructively modified.
//
UTF8 *split_token(UTF8 **sp, const SEP &sep)
{
    UTF8 *str = *sp;
    UTF8 *save = str;

    if (!str)
    {
        *sp = nullptr;
        return nullptr;
    }
    if (sep.n == 1)
    {
        while (  *str
              && *str != sep.str[0])
        {
            str++;
        }
        if (*str)
        {
            *str++ = '\0';
            if (sep.str[0] == ' ')
            {
                while (*str == ' ')
                {
                    str++;
                }
            }
        }
        else
        {
            str = nullptr;
        }
    }
    else
    {
        UTF8 *p = reinterpret_cast<UTF8 *>(const_cast<char *>(strstr(reinterpret_cast<const char *>(str), reinterpret_cast<const char *>(sep.str))));
        if (p)
        {
            *p = '\0';
            str = p + sep.n;
        }
        else
        {
            str = nullptr;
        }
    }
    *sp = str;
    return save;
}

/* ---------------------------------------------------------------------------
 * List management utilities.
 */

#define ASCII_LIST      1
#define NUMERIC_LIST    2
#define DBREF_LIST      4
#define FLOAT_LIST      8
#define CI_ASCII_LIST   16
#define UNICODE_LIST    32
#define CI_UNICODE_LIST 64
#define ALL_LIST        (ASCII_LIST|NUMERIC_LIST|DBREF_LIST|FLOAT_LIST)

class AutoDetect
{
private:
    int    m_CouldBe;

public:
    AutoDetect(void);
    void ExamineList(int nitems, UTF8 *ptrs[]);
    int GetType(void);
};

AutoDetect::AutoDetect(void)
{
    m_CouldBe = ALL_LIST;
}

void AutoDetect::ExamineList(int nitems, UTF8 *ptrs[])
{
    for (int i = 0; i < nitems && ASCII_LIST != m_CouldBe; i++)
    {
        UTF8 *p = strip_color(ptrs[i]);
        if (p[0] != NUMBER_TOKEN)
        {
            m_CouldBe &= ~DBREF_LIST;
        }

        if (  (m_CouldBe & DBREF_LIST)
           && !is_integer(p+1, nullptr))
        {
            m_CouldBe &= ~(DBREF_LIST|NUMERIC_LIST|FLOAT_LIST);
        }

        if (  (m_CouldBe & FLOAT_LIST)
           && !is_real(p))
        {
            m_CouldBe &= ~(NUMERIC_LIST|FLOAT_LIST);
        }

        if (  (m_CouldBe & NUMERIC_LIST)
           && !is_integer(p, nullptr))
        {
            m_CouldBe &= ~NUMERIC_LIST;
        }
    }

    if (m_CouldBe & NUMERIC_LIST)
    {
        m_CouldBe = NUMERIC_LIST;
    }
    else if (m_CouldBe & FLOAT_LIST)
    {
        m_CouldBe = FLOAT_LIST;
    }
    else if (m_CouldBe & DBREF_LIST)
    {
        m_CouldBe = DBREF_LIST;
    }
    else
    {
        m_CouldBe = UNICODE_LIST;
    }
}

int AutoDetect::GetType(void)
{
    return m_CouldBe;
}

int list2arr(UTF8 *arr[], int maxlen, UTF8 *list, const SEP &sep)
{
    list = trim_space_sep(list, sep);
    if (list[0] == '\0')
    {
        return 0;
    }
    UTF8 *p = split_token(&list, sep);
    int i;
    for (i = 0; p && i < maxlen; i++, p = split_token(&list, sep))
    {
        arr[i] = p;
    }
    return i;
}

void arr2list(UTF8 *arr[], int alen, UTF8 *list, UTF8 **bufc, const SEP &sep)
{
    int i;
    for (i = 0; i < alen-1; i++)
    {
        safe_str(arr[i], list, bufc);
        print_sep(sep, list, bufc);
    }
    if (alen)
    {
        safe_str(arr[i], list, bufc);
    }
}

static int dbnum(UTF8 *dbr)
{
    if (  '#'  != dbr[0]
       || '\0' == dbr[1])
    {
        return 0;
    }
    else
    {
        return mux_atol(dbr + 1);
    }
}

/* ---------------------------------------------------------------------------
 * nearby_or_control: Check if player is near or controls thing
 */

static bool nearby_or_control(dbref player, dbref thing)
{
    if (  !Good_obj(player)
       || !Good_obj(thing))
    {
        return false;
    }

    if (Controls(player, thing))
    {
        return true;
    }

    if (!nearby(player, thing))
    {
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * delim_check: obtain delimiter
 */
bool delim_check
(
    UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int   eval,
    UTF8 *fargs[], int nfargs,
    const UTF8 *cargs[], int ncargs,
    int sep_arg, SEP *sep, int dflags
)
{
    bool bSuccess = true;
    if (sep_arg <= nfargs)
    {
        // First, we decide whether to evalute fargs[sep_arg-1] or not.
        //
        UTF8 *tstr = fargs[sep_arg-1];
        size_t tlen = strlen(reinterpret_cast<char *>(tstr));

        if (tlen <= 1)
        {
            dflags &= ~DELIM_EVAL;
        }

        if (dflags & DELIM_EVAL)
        {
            UTF8 *str = tstr;
            UTF8 *bp = tstr = alloc_lbuf("delim_check");
            mux_exec(str, tlen, tstr, &bp, executor, caller, enactor,
                     eval|EV_EVAL|EV_FCHECK, cargs, ncargs);
            *bp = '\0';
            tlen = bp - tstr;
        }

        // Regardless of evaulation or no, tstr contains what we need to
        // look at, and tlen is the length of this string.
        //
        if (tlen == 1)
        {
            sep->n      = 1;
            memcpy(sep->str, tstr, tlen+1);
        }
        else if (tlen == 0)
        {
            sep->n      = 1;
            memcpy(sep->str, " ", 2);
        }
        else if (  tlen == 2
                && (dflags & DELIM_NULL)
                && memcmp(tstr, NULL_DELIM_VAR, 2) == 0)
        {
            sep->n      = 0;
            sep->str[0] = '\0';
        }
        else if (  tlen == 2
                && (dflags & DELIM_EVAL)
                && memcmp(tstr, "\r\n", 2) == 0)
        {
            sep->n      = 2;
            memcpy(sep->str, "\r\n", 3);
        }
        else if (dflags & DELIM_STRING)
        {
            if (tlen <= MAX_SEP_LEN)
            {
                sep->n = tlen;
                memcpy(sep->str, tstr, tlen);
                sep->str[sep->n] = '\0';
            }
            else
            {
                safe_str(T("#-1 SEPARATOR IS TOO LARGE"), buff, bufc);
                bSuccess = false;
            }
        }
        else
        {
            safe_str(T("#-1 SEPARATOR MUST BE ONE CHARACTER"), buff, bufc);
            bSuccess = false;
        }

        // Clean up the evaluation buffer.
        //
        if (dflags & DELIM_EVAL)
        {
            free_lbuf(tstr);
        }
    }
    else if (!(dflags & DELIM_INIT))
    {
        sep->n      = 1;
        memcpy(sep->str, " ", 2);
    }
    return bSuccess;
}

/* ---------------------------------------------------------------------------
 * fun_words: Returns number of words in a string.
 * Added 1/28/91 Philip D. Wasson
 */

int countwords(UTF8 *str, const SEP &sep)
{
    int n;

    str = trim_space_sep(str, sep);
    if (!*str)
    {
        return 0;
    }
    for (n = 0; str; str = next_token(str, sep), n++)
    {
        ; // Nothing.
    }
    return n;
}

static FUNCTION(fun_words)
{
    if (nfargs == 0)
    {
        safe_chr('0', buff, bufc);
        return;
    }

    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    safe_ltoa(countwords(strip_color(fargs[0]), sep), buff, bufc);
}

/* ---------------------------------------------------------------------------
 * fun_flags: Returns the flags on an object or an object's attribute.
 * Because @switch is case-insensitive, not quite as useful as it could be.
 */

static FUNCTION(fun_flags)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it;
    ATTR  *pattr;
    if (parse_attrib(executor, fargs[0], &it, &pattr))
    {
        if (  pattr
           && See_attr(executor, it, pattr))
        {
            dbref aowner;
            int   aflags;
            atr_pget_info(it, pattr->number, &aowner, &aflags);
            UTF8 xbuf[11];
            decode_attr_flags(aflags, xbuf);
            safe_str(xbuf, buff, bufc);
        }
    }
    else
    {
        it = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(it))
        {
            safe_match_result(it, buff, bufc);
            return;
        }
        if (  mudconf.pub_flags
           || Examinable(executor, it)
           || it == enactor)
        {
            UTF8 *buff2 = decode_flags(executor, &(db[it].fs));
            safe_str(buff2, buff, bufc);
            free_sbuf(buff2);
        }
        else
        {
            safe_noperm(buff, bufc);
        }
    }
}

/* ---------------------------------------------------------------------------
 * fun_rand: Return a random number from 0 to arg1-1
 */

static FUNCTION(fun_rand)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int nDigits;
    switch (nfargs)
    {
    case 1:
        if (is_integer(fargs[0], &nDigits))
        {
            int num = mux_atol(fargs[0]);
            if (num < 1)
            {
                safe_chr('0', buff, bufc);
            }
            else
            {
                safe_ltoa(RandomINT32(0, num-1), buff, bufc);
            }
        }
        else
        {
            safe_str(T("#-1 ARGUMENT MUST BE INTEGER"), buff, bufc);
        }
        break;

    case 2:
        if (  is_integer(fargs[0], &nDigits)
           && is_integer(fargs[1], &nDigits))
        {
            int lower = mux_atol(fargs[0]);
            int higher = mux_atol(fargs[1]);
            if (  lower <= higher
               && static_cast<unsigned int>(higher-lower) <= INT32_MAX)
            {
                safe_ltoa(RandomINT32(lower, higher), buff, bufc);
            }
            else
            {
                safe_range(buff, bufc);
            }
        }
        else
        {
            safe_str(T("#-1 ARGUMENT MUST BE INTEGER"), buff, bufc);
        }
        break;
    }
}

// ---------------------------------------------------------------------------
// fun_time:
//
// With no arguments, it returns local time in the 'Ddd Mmm DD HH:MM:SS YYYY'
// format.
//
// If an argument is provided, "utc" gives a UTC time string, and "local"
// gives the local time string.
//
static FUNCTION(fun_time)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    CLinearTimeAbsolute ltaNow;
    if (  nfargs == 0
       || mux_stricmp(T("utc"), fargs[0]) != 0)
    {
        ltaNow.GetLocal();
    }
    else
    {
        ltaNow.GetUTC();
    }
    int nPrecision = 0;
    if (nfargs == 2)
    {
        nPrecision = mux_atol(fargs[1]);
    }
    const UTF8 *temp = ltaNow.ReturnDateString(nPrecision);
    safe_str(temp, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_secs.
//
// With no arguments, it returns seconds since Jan 01 00:00:00 1970 UTC not
// counting leap seconds.
//
// If an argument is provided, "utc" gives UTC seconds, and "local" gives
// an integer which corresponds to a local time string. It is not useful
// as a count, but it can be given to convsecs(secs(),raw) to get the
// corresponding time string.
//
static FUNCTION(fun_secs)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    CLinearTimeAbsolute ltaNow;
    if (  nfargs == 0
       || mux_stricmp(T("local"), fargs[0]) != 0)
    {
        ltaNow.GetUTC();
    }
    else
    {
        ltaNow.GetLocal();
    }
    int nPrecision = 0;
    if (nfargs == 2)
    {
        nPrecision = mux_atol(fargs[1]);
    }
    safe_str(ltaNow.ReturnSecondsString(nPrecision), buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_convsecs.
//
// With one arguments, it converts seconds from Jan 01 00:00:00 1970 UTC to a
// local time string in the 'Ddd Mmm DD HH:MM:SS YYYY' format.
//
// If a second argument is given, it is the <zonename>:
//
//   local - indicates that a conversion for timezone/DST of the server should
//           be applied (default if no second argument is given).
//
//   utc   - indicates that no timezone/DST conversions should be applied.
//           This is useful to give a unique one-to-one mapping between an
//           integer and it's corresponding text-string.
//
static FUNCTION(fun_convsecs)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    CLinearTimeAbsolute lta;
    if (lta.SetSecondsString(fargs[0]))
    {
        if (  nfargs == 1
           || mux_stricmp(T("utc"), fargs[1]) != 0)
        {
            lta.UTC2Local();
        }
        int nPrecision = 0;
        if (nfargs == 3)
        {
            nPrecision = mux_atol(fargs[2]);
        }
        const UTF8 *temp = lta.ReturnDateString(nPrecision);
        safe_str(temp, buff, bufc);
    }
    else
    {
        safe_str(T("#-1 INVALID DATE"), buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_convtime.
//
// With one argument, it converts a local time string in the format
//'[Ddd] Mmm DD HH:MM:SS YYYY' to a count of seconds from Jan 01 00:00:00 1970
// UTC.
//
// If a second argument is given, it is the <zonename>:
//
//   local - indicates that the given time string is for the local timezone
//           local DST adjustments (default if no second argument is given).
//
//   utc   - indicates that no timezone/DST conversions should be applied.
//           This is useful to give a unique one-to-one mapping between an
//           integer and it's corresponding text-string.
//
// This function returns -1 if there was a problem parsing the time string.
//
static FUNCTION(fun_convtime)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    CLinearTimeAbsolute lta;
    bool bZoneSpecified = false;
    if (  lta.SetString(fargs[0])
       || ParseDate(lta, fargs[0], &bZoneSpecified))
    {
        if (  !bZoneSpecified
           && (  nfargs == 1
              || mux_stricmp(T("utc"), fargs[1]) != 0))
        {
            lta.Local2UTC();
        }
        int nPrecision = 0;
        if (nfargs == 3)
        {
            nPrecision = mux_atol(fargs[2]);
        }
        safe_str(lta.ReturnSecondsString(nPrecision), buff, bufc);
    }
    else
    {
        safe_str(T("#-1 INVALID DATE"), buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_starttime: What time did this system last reboot?
 */

static FUNCTION(fun_starttime)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    CLinearTimeAbsolute lta;
    lta = mudstate.start_time;
    lta.UTC2Local();
    safe_str(lta.ReturnDateString(), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_restarttime: Time at which the last @restart occured or original
 * *   starttime if no @restart has occured.
 */

static FUNCTION(fun_restarttime)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    CLinearTimeAbsolute lta;
    lta = mudstate.restart_time;
    lta.UTC2Local();
    safe_str(lta.ReturnDateString(), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_restarts: Number of @restarts since initial startup
 */

static FUNCTION(fun_restarts)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str(tprintf(T("%d"), mudstate.restart_count), buff, bufc);
}

// fun_timefmt
//
// timefmt(<format>[, <secs>])
//
// If <secs> isn't given, the current time is used. Escape sequences
// in <format> are expanded out.
//
// All escape sequences start with a $. Any unrecognized codes or other
// text will be returned unchanged.
//
static const UTF8 *DayOfWeekStringLong[7] =
{
    T("Sunday"),
    T("Monday"),
    T("Tuesday"),
    T("Wednesday"),
    T("Thursday"),
    T("Friday"),
    T("Saturday")
};

static const UTF8 *MonthTableLong[] =
{
    T("January"),
    T("February"),
    T("March"),
    T("April"),
    T("May"),
    T("June"),
    T("July"),
    T("August"),
    T("September"),
    T("October"),
    T("November"),
    T("December")
};

static const int Map24to12[24] =
{
    12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
    12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};

static FUNCTION(fun_timefmt)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    CLinearTimeAbsolute lta, ltaUTC;
    if (nfargs == 2)
    {
        ltaUTC.SetSecondsString(fargs[1]);
    }
    else
    {
        ltaUTC.GetUTC();
    }
    lta = ltaUTC;
    lta.UTC2Local();

    FIELDEDTIME ft;
    lta.ReturnFields(&ft);

    // Calculate Time Zone Info
    //
    CLinearTimeDelta ltd = lta - ltaUTC;
    int iTZSecond = ltd.ReturnSeconds();
    int iTZSign;
    if (iTZSecond < 0)
    {
        iTZSign = '-';
        iTZSecond = -iTZSecond;
    }
    else
    {
        iTZSign = '+';
    }
    int iTZHour = iTZSecond / 3600;
    iTZSecond %= 3600;
    int iTZMinute = iTZSecond/60;
    int iHour12 = Map24to12[ft.iHour];

    // Calculate Monday and Sunday-oriented week numbers.
    //
    int iWeekOfYearSunday = (ft.iDayOfYear-ft.iDayOfWeek+6)/7;
    int iDayOfWeekMonday  = (ft.iDayOfWeek == 0)?7:ft.iDayOfWeek;
    int iWeekOfYearMonday = (ft.iDayOfYear-iDayOfWeekMonday+7)/7;

    // Calculate ISO Week and Year.  Remember that the ISO Year can be the
    // same, one year ahead, or one year behind of the Gregorian Year.
    //
    int iYearISO = ft.iYear;
    int iWeekISO = 0;
    int iTemp = 0;
    if (  ft.iMonth == 12
       && 35 <= 7 + ft.iDayOfMonth - iDayOfWeekMonday)
    {
        iYearISO++;
        iWeekISO = 1;
    }
    else if (  ft.iMonth == 1
            && ft.iDayOfMonth <= 3
            && (iTemp = 7 - ft.iDayOfMonth + iDayOfWeekMonday) >= 11)
    {
        iYearISO--;
        if (  iTemp == 11
           || (  iTemp == 12
              && isLeapYear(iYearISO)))
        {
            iWeekISO = 53;
        }
        else
        {
            iWeekISO = 52;
        }
    }
    else
    {
        iWeekISO = (7 + ft.iDayOfYear - iDayOfWeekMonday)/7;
        if (4 <= (7 + ft.iDayOfYear - iDayOfWeekMonday)%7)
        {
            iWeekISO++;
        }
    }

    const UTF8 *pValidLongMonth = nullptr;
    const UTF8 *pValidShortMonth = nullptr;
    if (  1 <= ft.iMonth
       && ft.iMonth <= 12)
    {
        pValidLongMonth = MonthTableLong[ft.iMonth-1];
        pValidShortMonth = monthtab[ft.iMonth-1];
    }
    else
    {
        pValidLongMonth = T("");
        pValidShortMonth = T("");
    }

    const UTF8 *pValidLongDayOfWeek = nullptr;
    const UTF8 *pValidShortDayOfWeek = nullptr;
    if (ft.iDayOfWeek <= 6)
    {
        pValidLongDayOfWeek = DayOfWeekStringLong[ft.iDayOfWeek];
        pValidShortDayOfWeek = DayOfWeekString[ft.iDayOfWeek];
    }
    else
    {
        pValidLongDayOfWeek = T("");
        pValidShortDayOfWeek = T("");
    }

    UTF8 *q;
    UTF8 *p = fargs[0];
    while ((q = reinterpret_cast<UTF8 *>(strchr(reinterpret_cast<char *>(p), '$'))) != nullptr)
    {
        size_t nLen = q - p;
        safe_copy_buf(p, nLen, buff, bufc);
        p = q;

        // Now, p points to a '$'.
        //
        p++;

        // Handle modifiers
        //
        int  iOption = 0;
        int ch = *p++;
        if (ch == '#' || ch == 'E' || ch == 'O')
        {
            iOption = ch;
            ch = *p++;
        }

        // Handle format letter.
        //
        switch (ch)
        {
        case 'a': // $a - Abbreviated weekday name
            safe_str(pValidShortDayOfWeek, buff, bufc);
            break;

        case 'A': // $A - Full weekday name
            safe_str(pValidLongDayOfWeek, buff, bufc);
            break;

        case 'b': // $b - Abbreviated month name
        case 'h':
            safe_str(pValidShortMonth, buff, bufc);
            break;

        case 'B': // $B - Full month name
            safe_str(pValidLongMonth, buff, bufc);
            break;

        case 'c': // $c - Date and time
            if (iOption == '#')
            {
                // Long version.
                //
                safe_tprintf_str(buff, bufc, T("%s, %s %d, %d, %02d:%02d:%02d"),
                    pValidLongDayOfWeek,
                    pValidLongMonth,
                    ft.iDayOfMonth, ft.iYear, ft.iHour, ft.iMinute,
                    ft.iSecond);
            }
            else
            {
                safe_str(lta.ReturnDateString(7), buff, bufc);
            }
            break;

        case 'C': // $C - The century (year/100).
            safe_tprintf_str(buff, bufc, T("%d"), ft.iYear / 100);
            break;

        case 'd': // $d - Day of Month as decimal number (1-31)
            safe_tprintf_str(buff, bufc, (iOption=='#')?T("%d"):T("%02d"),
                ft.iDayOfMonth);
            break;

        case 'x': // $x - Date
            if (iOption == '#')
            {
                safe_tprintf_str(buff, bufc, T("%s, %s %d, %d"),
                    pValidLongDayOfWeek,
                    pValidLongMonth,
                    ft.iDayOfMonth, ft.iYear);
                break;
            }

            // FALL THROUGH

        case 'D': // $D - Equivalent to %m/%d/%y
            safe_tprintf_str(buff, bufc, T("%02d/%02d/%02d"), ft.iMonth,
                ft.iDayOfMonth, ft.iYear % 100);
            break;

        case 'e': // $e - Like $d, the day of the month as a decimal number,
                  // but a leading zero is replaced with a space.
            safe_tprintf_str(buff, bufc, T("%2d"), ft.iDayOfMonth);
            break;

        case 'F': // $F - The ISO 8601 formated date.
            safe_tprintf_str(buff, bufc, T("%d-%02d-%02d"), ft.iYear, ft.iMonth,
                ft.iDayOfMonth);
            break;

        case 'g': // $g - Like $G, two-digit ISO 8601 year.
            safe_tprintf_str(buff, bufc, T("%02d"), iYearISO%100);
            break;

        case 'G': // $G - The ISO 8601 year.
            safe_tprintf_str(buff, bufc, T("%04d"), iYearISO);
            break;

        case 'H': // $H - Hour of the 24-hour day.
            safe_tprintf_str(buff, bufc, (iOption=='#')?T("%d"):T("%02d"), ft.iHour);
            break;

        case 'I': // $I - Hour of the 12-hour day
            safe_tprintf_str(buff, bufc, (iOption=='#')?T("%d"):T("%02d"), iHour12);
            break;

        case 'j': // $j - Day of the year.
            safe_tprintf_str(buff, bufc, (iOption=='#')?T("%d"):T("%03d"),
                ft.iDayOfYear);
            break;

        case 'k': // $k - Hour of the 24-hour day. Pad with a space.
            safe_tprintf_str(buff, bufc, T("%2d"), ft.iHour);
            break;

        case 'l': // $l - Hour of the 12-hour clock. Pad with a space.
            safe_tprintf_str(buff, bufc, T("%2d"), iHour12);
            break;

        case 'm': // $m - Month of the year
            safe_tprintf_str(buff, bufc, (iOption=='#')?T("%d"):T("%02d"),
                ft.iMonth);
            break;

        case 'M': // $M - Minutes after the hour
            safe_tprintf_str(buff, bufc, (iOption=='#')?T("%d"):T("%02d"),
                ft.iMinute);
            break;

        case 'n': // $n - Newline.
            safe_str(T("\r\n"), buff, bufc);
            break;

        case 'p': // $p - AM/PM
            safe_str((ft.iHour < 12)?T("AM"):T("PM"), buff, bufc);
            break;

        case 'P': // $p - am/pm
            safe_str((ft.iHour < 12)?T("am"):T("pm"), buff, bufc);
            break;

        case 'r': // $r - Equivalent to $I:$M:$S $p
            safe_tprintf_str( buff, bufc,
                (iOption=='#')?T("%d:%02d:%02d %s"):T("%02d:%02d:%02d %s"),
                iHour12, ft.iMinute, ft.iSecond, (ft.iHour<12)?T("AM"):T("PM"));
            break;

        case 'R': // $R - Equivalent to $H:$M
            safe_tprintf_str(buff, bufc, (iOption=='#')?T("%d:%02d"):T("%02d:%02d"),
                ft.iHour, ft.iMinute);
            break;

        case 's': // $s - Number of seconds since the epoch.
            safe_str(ltaUTC.ReturnSecondsString(7), buff, bufc);
            break;

        case 'S': // $S - Seconds after the minute
            safe_tprintf_str(buff, bufc, (iOption=='#')?T("%d"):T("%02d"),
                ft.iSecond);
            break;

        case 't':
            safe_chr('\t', buff, bufc);
            break;

        case 'X': // $X - Time
        case 'T': // $T - Equivalent to $H:$M:$S
            safe_tprintf_str(buff, bufc,
                (iOption=='#')?T("%d:%02d:%02d"):T("%02d:%02d:%02d"),
                ft.iHour, ft.iMinute, ft.iSecond);
            break;

        case 'u': // $u - Day of the Week, range 1 to 7. Monday = 1.
            safe_ltoa(iDayOfWeekMonday, buff, bufc);
            break;

        case 'U': // $U - Week of the year from 1st Sunday
            safe_tprintf_str(buff, bufc, (iOption=='#')?T("%d"):T("%02d"),
                iWeekOfYearSunday);
            break;

        case 'V': // $V - ISO 8601:1988 week number.
            safe_tprintf_str(buff, bufc, T("%02d"), iWeekISO);
            break;

        case 'w': // $w - Day of the week. 0 = Sunday
            safe_ltoa(ft.iDayOfWeek, buff, bufc);
            break;

        case 'W': // $W - Week of the year from 1st Monday
            safe_tprintf_str(buff, bufc, (iOption=='#')?T("%d"):T("%02d"),
                iWeekOfYearMonday);
            break;

        case 'y': // $y - Two-digit year
            safe_tprintf_str(buff, bufc, (iOption=='#')?T("%d"):T("%02d"),
                ft.iYear % 100);
            break;

        case 'Y': // $Y - All-digit year
            safe_tprintf_str(buff, bufc, (iOption=='#')?T("%d"):T("%04d"),
                ft.iYear);
            break;

        case 'z': // $z - Time zone
            safe_tprintf_str(buff, bufc, T("%c%02d%02d"), iTZSign, iTZHour,
                iTZMinute);
            break;

        case 'Z': // $Z - Time zone name
            // TODO
            break;

        case '$': // $$
            safe_chr(ch, buff, bufc);
            break;

        default:
            safe_chr('$', buff, bufc);
            p = q + 1;
            break;
        }
    }
    safe_str(p, buff, bufc);
}

static FUNCTION(fun_etimefmt)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    CLinearTimeDelta ltd;
    ltd.SetSecondsString(fargs[1]);

    long seconds = ltd.ReturnSeconds();
    long minutes = seconds/60;
    seconds -= 60*minutes;
    long hours   = minutes/60;
    minutes -= 60*hours;
    long days    = hours/24;
    hours -= 24*days;

    UTF8 *q;
    UTF8 *p = fargs[0];
    while ((q = reinterpret_cast<UTF8 *>(strchr(reinterpret_cast<char *>(p), '$'))) != nullptr)
    {
        size_t nLen = q - p;
        safe_copy_buf(p, nLen, buff, bufc);
        p = q;

        // Now, p points to a '$'.
        //
        p++;
        int ch = *p++;

        // Look for a field width.
        //
        bool bHasWidth = false;
        int width = 0;
        if (mux_isdigit(ch))
        {
            bHasWidth = true;
            do
            {
                width = 10 * width + (ch - '0');
                ch = *p++;
            } while (mux_isdigit(ch));

            if (11 < width)
            {
                width = 11;
            }
        }

        // Handle modifiers.
        // ASCII-only: format modifier characters are always ASCII.
        //
        bool bDoSuffix = false;
        bool bZeroIsBlank = false;
        while (  'z' == mux_tolower_ascii(ch)
              || 'x' == mux_tolower_ascii(ch))
        {
            switch (ch)
            {
            case 'x':
            case 'X':
                bDoSuffix = true;
                break;

            case 'z':
            case 'Z':
                bZeroIsBlank = true;
                break;
            }
            ch = *p++;
        }

        UTF8 field[MBUF_SIZE];
        size_t n = 0;

        // Handle format letter.
        //
        switch (ch)
        {
        case 's': // $s - The number of seconds.
            if (  0 != seconds
               || !bZeroIsBlank)
            {
                if (bHasWidth)
                {
                    n = RightJustifyNumber(field, width, seconds, ' ');
                    field[n] = '\0';
                    safe_str(field, buff, bufc);
                }
                else
                {
                    safe_ltoa(seconds, buff, bufc);
                }

                if (bDoSuffix)
                {
                    safe_chr('s', buff, bufc);
                }
            }
            break;

        case 'S': // $S - The number of seconds, left-padded with zero.
            if (  0 != seconds
               || !bZeroIsBlank)
            {
                if (bHasWidth)
                {
                    n = RightJustifyNumber(field, width, seconds, '0');
                    field[n] = '\0';
                    safe_str(field, buff, bufc);
                }
                else
                {
                    safe_ltoa(seconds, buff, bufc);
                }

                if (bDoSuffix)
                {
                    safe_chr('s', buff, bufc);
                }
            }
            break;

        case 'm': // $m - The number of minutes.
            if (  0 != minutes
               || !bZeroIsBlank)
            {
                if (bHasWidth)
                {
                    n = RightJustifyNumber(field, width, minutes, ' ');
                    field[n] = '\0';
                    safe_str(field, buff, bufc);
                }
                else
                {
                    safe_ltoa(minutes, buff, bufc);
                }

                if (bDoSuffix)
                {
                    safe_chr('m', buff, bufc);
                }
            }
            break;

        case 'M': // $M - The number of minutes, left-padded with zero.
            if (  0 != minutes
               || !bZeroIsBlank)
            {
                if (bHasWidth)
                {
                    n = RightJustifyNumber(field, width, minutes, '0');
                    field[n] = '\0';
                    safe_str(field, buff, bufc);
                }
                else
                {
                    safe_ltoa(minutes, buff, bufc);
                }

                if (bDoSuffix)
                {
                    safe_chr('m', buff, bufc);
                }
            }
            break;

        case 'h': // $h - The number of hours.
            if (  0 != hours
               || !bZeroIsBlank)
            {
                if (bHasWidth)
                {
                    n = RightJustifyNumber(field, width, hours, ' ');
                    field[n] = '\0';
                    safe_str(field, buff, bufc);
                }
                else
                {
                    safe_ltoa(hours, buff, bufc);
                }

                if (bDoSuffix)
                {
                    safe_chr('h', buff, bufc);
                }
            }
            break;

        case 'H': // $H - The number of hours, left-padded with zero.
            if (  0 != hours
               || !bZeroIsBlank)
            {
                if (bHasWidth)
                {
                    n = RightJustifyNumber(field, width, hours, '0');
                    field[n] = '\0';
                    safe_str(field, buff, bufc);
                }
                else
                {
                    safe_ltoa(hours, buff, bufc);
                }

                if (bDoSuffix)
                {
                    safe_chr('h', buff, bufc);
                }
            }
            break;

        case 'd': // $d - The number of days.
            if (  0 != days
               || !bZeroIsBlank)
            {
                if (bHasWidth)
                {
                    n = RightJustifyNumber(field, width, days, ' ');
                    field[n] = '\0';
                    safe_str(field, buff, bufc);
                }
                else
                {
                    safe_ltoa(days, buff, bufc);
                }

                if (bDoSuffix)
                {
                    safe_chr('d', buff, bufc);
                }
            }
            break;

        case 'D': // $D - The number of days, left-padded with zero.
            if (  0 != days
               || !bZeroIsBlank)
            {
                if (bHasWidth)
                {
                    n = RightJustifyNumber(field, width, days, '0');
                    field[n] = '\0';
                    safe_str(field, buff, bufc);
                }
                else
                {
                    safe_ltoa(days, buff, bufc);
                }

                if (bDoSuffix)
                {
                    safe_chr('d', buff, bufc);
                }
            }
            break;

        case 'n': // $n - Newline.
            safe_str(T("\r\n"), buff, bufc);
            break;

        case '$': // $$
            safe_chr(ch, buff, bufc);
            break;

        default:
            safe_chr('$', buff, bufc);
            p = q + 1;
            break;
        }
    }
    safe_str(p, buff, bufc);
}

// pua_advance_columns — Advance through PUA-encoded UTF-8, counting visible
// columns.  Returns byte offset where max_cols visible columns are reached.
// *actual_cols receives the actual column count (may be less at end of string
// or if a wide character would exceed max_cols).
//
static size_t pua_advance_columns(const UTF8 *p, size_t len,
                                  size_t max_cols, size_t *actual_cols)
{
    const UTF8 *start = p;
    const UTF8 *pe = p + len;
    size_t cols = 0;

    while (p < pe)
    {
        // Skip BMP PUA color (3 bytes: 0xEF 0x94..0x9F xx).
        //
        if (p[0] == 0xEF && (p + 2) < pe
            && p[1] >= 0x94 && p[1] <= 0x9F)
        {
            p += 3;
            continue;
        }

        // Skip SMP PUA color (4 bytes: 0xF3 0xB0 0x80..0x97 xx).
        //
        if (p[0] == 0xF3 && (p + 3) < pe
            && p[1] == 0xB0
            && p[2] >= 0x80 && p[2] <= 0x97)
        {
            p += 4;
            continue;
        }

        // Visible code point — check display width.
        //
        int w = co_console_width(reinterpret_cast<const unsigned char *>(p));
        if (cols + static_cast<size_t>(w) > max_cols)
        {
            break;
        }
        cols += static_cast<size_t>(w);

        // Advance past UTF-8 sequence.
        //
        if (*p < 0x80)      p += 1;
        else if (*p < 0xE0) p += 2;
        else if (*p < 0xF0) p += 3;
        else                p += 4;
    }
    *actual_cols = cols;
    return static_cast<size_t>(p - start);
}

// pua_back_one — Move backward one visible character in PUA-encoded UTF-8.
// Returns the new byte offset, or the same offset if at start.
//
static size_t pua_back_one(const UTF8 *base, size_t off)
{
    if (0 == off)
    {
        return 0;
    }

    // Walk backward past continuation bytes (0x80..0xBF).
    //
    size_t pos = off;
    do
    {
        pos--;
    } while (pos > 0 && (base[pos] & 0xC0) == 0x80);

    // If we landed on a PUA color lead byte, keep going back.
    //
    if (base[pos] == 0xEF && (pos + 2) < off
        && base[pos + 1] >= 0x94 && base[pos + 1] <= 0x9F)
    {
        return pua_back_one(base, pos);
    }
    if (base[pos] == 0xF3 && (pos + 3) < off
        && base[pos + 1] == 0xB0
        && base[pos + 2] >= 0x80 && base[pos + 2] <= 0x97)
    {
        return pua_back_one(base, pos);
    }
    return pos;
}

// expand_tabs — Expand tabs in PUA-encoded UTF-8 string to spaces.
// Returns byte length of expanded string in out.
//
static size_t expand_tabs(const UTF8 *src, size_t src_len,
                          UTF8 *out, size_t out_size)
{
    const UTF8 *sp = src;
    const UTF8 *spe = src + src_len;
    UTF8 *wp = out;
    UTF8 *wpe = out + out_size - 1;
    size_t col = 0;

    while (sp < spe && wp < wpe)
    {
        if (*sp == '\t')
        {
            size_t nSpaces = 8 - (col % 8);
            for (size_t i = 0; i < nSpaces && wp < wpe; i++)
            {
                *wp++ = ' ';
            }
            col += nSpaces;
            sp++;
        }
        else if (*sp == '\r')
        {
            *wp++ = *sp++;
            col = 0;
        }
        else
        {
            // Skip BMP PUA color (3 bytes).
            //
            if (sp[0] == 0xEF && (sp + 2) < spe
                && sp[1] >= 0x94 && sp[1] <= 0x9F)
            {
                if (wp + 3 <= wpe) { wp[0]=sp[0]; wp[1]=sp[1]; wp[2]=sp[2]; wp += 3; }
                sp += 3;
                continue;
            }

            // Skip SMP PUA color (4 bytes).
            //
            if (sp[0] == 0xF3 && (sp + 3) < spe
                && sp[1] == 0xB0
                && sp[2] >= 0x80 && sp[2] <= 0x97)
            {
                if (wp + 4 <= wpe) { wp[0]=sp[0]; wp[1]=sp[1]; wp[2]=sp[2]; wp[3]=sp[3]; wp += 4; }
                sp += 4;
                continue;
            }

            // Visible code point — copy and count column width.
            //
            int w = co_console_width(reinterpret_cast<const unsigned char *>(sp));
            size_t cplen;
            if (*sp < 0x80)      cplen = 1;
            else if (*sp < 0xE0) cplen = 2;
            else if (*sp < 0xF0) cplen = 3;
            else                 cplen = 4;

            if (wp + cplen > wpe) break;
            for (size_t i = 0; i < cplen && sp + i < spe; i++)
            {
                wp[i] = sp[i];
            }
            wp += cplen;
            sp += cplen;
            col += static_cast<size_t>(w);
        }
    }
    *wp = '\0';
    return static_cast<size_t>(wp - out);
}

LBUF_OFFSET linewrap_general(const UTF8 *pStr,     LBUF_OFFSET nWidth,
                                   UTF8 *pBuffer,  size_t      nBuffer,
                             const UTF8 *pLeft,    LBUF_OFFSET nLeft,
                             const UTF8 *pRight,   LBUF_OFFSET nRight,
                                   int   iJustKey, LBUF_OFFSET nHanging,
                             const UTF8 *pOSep,    LBUF_OFFSET nOSepBytes,
                             LBUF_OFFSET nWidth0)
{
    // Expand tabs in the input string.
    //
    UTF8 *expanded = alloc_lbuf("linewrap.expand");
    size_t nExpanded = expand_tabs(pStr, mux_strlen(pStr), expanded, LBUF_SIZE);

    size_t nOSepCols = static_cast<size_t>(
        co_visual_width(reinterpret_cast<const unsigned char *>(pOSep),
                        static_cast<size_t>(nOSepBytes)));

    bool bFirst = true;
    mux_field fldLine, fldTemp, fldPad;
    LBUF_OFFSET nLineWidth = (0 < nWidth0 ? nWidth0 : nWidth);
    size_t pos = 0;  // Current byte position in expanded string.

    while (pos < nExpanded)
    {
        if (bFirst)
        {
            bFirst = false;
        }
        else if (nBuffer < static_cast<size_t>(fldLine.m_byte + nOSepBytes))
        {
            break;
        }
        else
        {
            // Emit output separator between lines.
            //
            mux_strncpy(pBuffer + fldLine.m_byte, pOSep,
                        nBuffer - fldLine.m_byte);
            fldLine(fldLine.m_byte + nOSepBytes,
                    fldLine.m_column + static_cast<LBUF_OFFSET>(nOSepCols));
            if (0 < nHanging)
            {
                fldLine = PadField(pBuffer, nBuffer,
                                   fldLine.m_column + nHanging, fldLine);
            }
            nLineWidth = nWidth;
        }

        // Emit left margin.
        //
        fldLine += StripTabsAndTruncate(pLeft, pBuffer + fldLine.m_byte,
                                        nBuffer - fldLine.m_byte, nLeft);

        // Find \r (forced line break) in current segment.
        //
        const UTF8 *pCur = expanded + pos;
        size_t remain = nExpanded - pos;
        const UTF8 *pCR = reinterpret_cast<const UTF8 *>(
            memchr(pCur, '\r', remain));
        size_t crOff = pCR ? static_cast<size_t>(pCR - pCur) : remain;

        // Find byte position at nLineWidth visible columns.
        //
        size_t actualCols;
        size_t colOff = pua_advance_columns(pCur, remain,
                                            static_cast<size_t>(nLineWidth),
                                            &actualCols);

        // Effective end: min of \r position and column limit.
        //
        size_t endOff, nextOff;
        if (crOff < colOff)
        {
            endOff = crOff;
            // Skip past \r (and \n if present).
            //
            nextOff = crOff + 1;
            if (nextOff < remain && pCur[nextOff] == '\n')
            {
                nextOff++;
            }
        }
        else
        {
            endOff = colOff;
            nextOff = colOff;
        }

        // Word wrap: if we hit the column limit (not \r, not end of string),
        // backtrack to the last space.
        //
        if (endOff < remain && endOff == colOff && crOff >= colOff)
        {
            if (endOff > 0 && mux_isspace(pCur[endOff]))
            {
                // Already at a space — advance next past it.
                //
                nextOff = endOff;
                while (nextOff < remain && pCur[nextOff] == ' ')
                {
                    nextOff++;
                }

                // Trim trailing spaces from end of line.
                //
                while (endOff > 0 && mux_isspace(pCur[endOff - 1]))
                {
                    endOff--;
                }
            }
            else
            {
                // Backtrack to last space for word wrap.
                //
                size_t spOff = endOff;
                while (spOff > 0 && !mux_isspace(pCur[pua_back_one(pCur, spOff)]))
                {
                    spOff = pua_back_one(pCur, spOff);
                }

                if (spOff > 0)
                {
                    // Found a space.  nextOff is just past the space.
                    //
                    nextOff = spOff;
                    while (nextOff < remain && pCur[nextOff] == ' ')
                    {
                        nextOff++;
                    }

                    // Trim trailing spaces from end of line.
                    //
                    while (spOff > 0 && mux_isspace(pCur[spOff - 1]))
                    {
                        spOff--;
                    }
                    endOff = spOff;
                }
                // else: no space found, force break at column limit.
            }
        }
        else if (endOff == remain)
        {
            // At end of string — trim trailing spaces for justification.
            //
            while (endOff > 0 && mux_isspace(pCur[endOff - 1]))
            {
                endOff--;
            }
        }

        // Compute visible column width of this line segment.
        //
        size_t segCols = co_visual_width(
            reinterpret_cast<const unsigned char *>(pCur), endOff);

        // Apply justification padding.
        //
        if (  segCols < static_cast<size_t>(nLineWidth)
           && CJC_LJUST != iJustKey)
        {
            LBUF_OFFSET nPadWidth;
            if (CJC_CENTER == iJustKey)
            {
                nPadWidth = fldLine.m_column
                          + static_cast<LBUF_OFFSET>(
                                (static_cast<size_t>(nLineWidth) - segCols) / 2);
            }
            else // CJC_RJUST
            {
                nPadWidth = fldLine.m_column
                          + static_cast<LBUF_OFFSET>(
                                static_cast<size_t>(nLineWidth) - segCols);
            }
            fldPad = PadField(pBuffer, nBuffer, nPadWidth, fldLine);
        }
        else
        {
            fldPad = fldLine;
        }

        // Copy line segment bytes (PUA is inline — just memcpy).
        //
        size_t nCopy = endOff;
        if (nCopy > nBuffer - fldPad.m_byte)
        {
            nCopy = nBuffer - fldPad.m_byte;
        }
        memcpy(pBuffer + fldPad.m_byte, pCur, nCopy);

        fldTemp(static_cast<LBUF_OFFSET>(nCopy),
                static_cast<LBUF_OFFSET>(segCols));
        if (CJC_RJUST == iJustKey)
        {
            fldLine = fldPad + fldTemp;
        }
        else
        {
            fldLine = PadField(pBuffer, nBuffer,
                               fldLine.m_column + nLineWidth,
                               fldPad + fldTemp);
        }

        // Guarantee forward progress — if nextOff is 0 (single char
        // wider than nLineWidth), skip at least one code point.
        //
        if (0 == nextOff)
        {
            const UTF8 *q = pCur;
            if (*q < 0x80)      nextOff = 1;
            else if (*q < 0xE0) nextOff = 2;
            else if (*q < 0xF0) nextOff = 3;
            else                nextOff = 4;
            if (nextOff > remain)
            {
                nextOff = remain;
            }
        }
        pos += nextOff;

        // Emit right margin.
        //
        fldLine += StripTabsAndTruncate(pRight, pBuffer + fldLine.m_byte,
                                        nBuffer - fldLine.m_byte, nRight);
    }

    free_lbuf(expanded);

    if (fldLine.m_byte <= nBuffer)
    {
        pBuffer[fldLine.m_byte] = '\0';
    }
    return fldLine.m_byte;
}

// fun_successes
//

#define MAXDICE 11
#define MAXDIFF 10
#define MAXBOUND 14

typedef struct dice_node
{
    short maxsuccs;
    short bound[MAXBOUND];
} dice_node;

static const dice_node dice_table[MAXDICE][MAXDIFF] =
{
    {   // Dice 1                                      // Difficulty
        {  2, { 444, 842, 977, 998, 1000, -1 } },      //  1
        {  2, { 359, 783, 963, 997, 1000, -1 } },      //  2
        {  2, { 282, 717, 946, 995, 1000, -1 } },      //  3
        {  2, { 215, 643, 923, 993, 1000, -1 } },      //  4
        {  2, { 156, 562, 896, 989, 1000, -1 } },      //  5
        {  2, { 108, 476, 861, 984, 999, 1000, -1 } }, //  6
        {  2, {  68, 385, 820, 977, 999, 1000, -1 } }, //  7
        {  2, {  39, 293, 770, 968, 998, 1000, -1 } }, //  8
        {  2, {  18, 199, 711, 955, 997, 1000, -1 } }, //  9
        {  2, {   5, 108, 642, 939, 996, 1000, -1 } }  // 10
    },
    {   // Dice 2                                           // Difficulty
        {  3, { 347, 755, 947, 994, 1000, -1 } },           //  1
        {  3, { 253, 658, 910, 987, 999, 1000, -1 } },      //  2
        {  3, { 178, 555, 861, 977, 998, 1000, -1 } },      //  3
        {  3, { 119, 451, 798, 962, 997, 1000, -1 } },      //  4
        {  3, {  74, 350, 721, 940, 994, 1000, -1 } },      //  5
        {  3, {  43, 255, 630, 910, 990, 1000, -1 } },      //  6
        {  3, {  22, 171, 526, 871, 984, 999, 1000, -1 } }, //  7
        {  3, {  10, 101, 411, 819, 975, 999, 1000, -1 } }, //  8
        {  3, {   3,  49, 287, 753, 962, 998, 1000, -1 } }, //  9
        {  2, {  15, 159, 670, 944, 997, 1000, -1 } }       // 10
    },
    {   // Dice 3                                                // Difficulty
        {  4, { 271, 665, 905, 983, 998, 1000, -1 } },           //  1
        {  4, { 178, 539, 836, 964, 995, 1000, -1 } },           //  2
        {  4, { 112, 415, 747, 934, 990, 999, 1000, -1 } },      //  3
        {  4, {  66, 302, 643, 888, 981, 998, 1000, -1 } },      //  4
        {  4, {  35, 205, 526, 825, 966, 997, 1000, -1 } },      //  5
        {  4, {  17, 128, 405, 742, 942, 994, 1000, -1 } },      //  6
        {  4, {   7,  70, 286, 638, 907, 989, 1000, -1 } },      //  7
        {  4, {   2,  32, 178, 512, 857, 981, 999, 1000, -1 } }, //  8
        {  4, {   1,  11,  90, 367, 789, 968, 998, 1000, -1 } }, //  9
        {  3, {   2,  29, 207, 696, 949, 997, 1000, -1 } }       // 10
    },
    {   // Dice 4                                                     // Difficulty
        {  5, { 212, 579, 853, 966, 995, 1000, -1 } },                //  1
        {  5, { 126, 432, 748, 926, 986, 998, 1000, -1 } },           //  2
        {  5, {  70, 302, 624, 864, 969, 996, 1000, -1 } },           //  3
        {  5, {  36, 196, 490, 778, 939, 991, 999, 1000, -1 } },      //  4
        {  5, {  17, 116, 358, 669, 892, 980, 998, 1000, -1 } },      //  5
        {  5, {   7,  61, 239, 540, 822, 963, 996, 1000, -1 } },      //  6
        {  5, {   2,  28, 140, 400, 725, 934, 992, 1000, -1 } },      //  7
        {  5, {   1,  10,  68, 261, 597, 888, 986, 999, 1000, -1 } }, //  8
        {  4, {   2,  24, 137, 439, 819, 974, 999, 1000, -1 } },      //  9
        {  3, {   4,  46, 254, 720, 954, 997, 1000, -1 } }            // 10
    },
    {   // Dice 5                                                     // Difficulty
        {  6, { 165, 499, 793, 941, 989, 999, 1000, -1 } },           //  1
        {  6, {  89, 342, 655, 874, 969, 995, 999, 1000, -1 } },      //  2
        {  6, {  44, 216, 505, 775, 930, 986, 998, 1000, -1 } },      //  3
        {  6, {  20, 125, 359, 649, 867, 968, 995, 1000, -1 } },      //  4
        {  6, {   8,  64, 232, 506, 775, 934, 989, 999, 1000, -1 } }, //  5
        {  6, {   3,  29, 132, 359, 653, 878, 976, 998, 1000, -1 } }, //  6
        {  6, {   1,  11,  64, 224, 505, 793, 953, 995, 1000, -1 } }, //  7
        {  5, {   3,  24, 116, 344, 669, 912, 989, 999, 1000, -1 } }, //  8
        {  4, {   6,  44, 189, 504, 846, 978, 999, 1000, -1 } },      //  9
        {  4, {   1,   8,  65, 298, 742, 958, 997, 1000, -1 } }       // 10
    },
    {   // Dice 6                                                          // Difficulty
        {  7, { 129, 426, 728, 909, 978, 996, 1000, -1 } },                //  1
        {  7, {  63, 267, 563, 809, 941, 987, 998, 1000, -1 } },           //  2
        {  7, {  28, 152, 398, 675, 873, 965, 994, 999, 1000, -1 } },      //  3
        {  7, {  11,  78, 254, 519, 770, 923, 983, 998, 1000, -1 } },      //  4
        {  7, {   4,  35, 144, 362, 634, 851, 960, 994, 999, 1000, -1 } }, //  5
        {  7, {   1,  13,  70, 223, 477, 743, 918, 985, 999, 1000, -1 } }, //  6
        {  6, {   4,  28, 116, 315, 598, 844, 966, 996, 1000, -1 } },      //  7
        {  6, {   1,   8,  47, 172, 424, 729, 931, 992, 1000, -1 } },      //  8
        {  5, {   1,  13,  69, 243, 563, 868, 981, 999, 1000, -1 } },      //  9
        {  4, {   1,  14,  87, 340, 763, 961, 998, 1000, -1 } }            // 10
    },
    {   // Dice 7                                                          // Difficulty
        {  8, { 101, 361, 662, 869, 963, 992, 999, 1000, -1 } },           //  1
        {  8, {  44, 207, 475, 736, 902, 973, 995, 999, 1000, -1 } },      //  2
        {  8, {  17, 106, 307, 572, 799, 931, 983, 997, 1000, -1 } },      //  3
        {  8, {   6,  48, 175, 401, 658, 854, 956, 991, 999, 1000, -1 } }, //  4
        {  8, {   2,  19,  87, 248, 492, 737, 903, 976, 996, 1000, -1 } }, //  5
        {  7, {   6,  36, 131, 324, 583, 813, 944, 990, 999, 1000, -1 } }, //  6
        {  7, {   1,  12,  56, 180, 407, 678, 884, 976, 998, 1000, -1 } }, //  7
        {  6, {   3,  18,  78, 235, 500, 779, 946, 994, 1000, -1 } },      //  8
        {  5, {   3,  22,  98, 298, 615, 888, 984, 999, 1000, -1 } },      //  9
        {  4, {   3,  21, 111, 380, 782, 965, 998, 1000, -1 } }            // 10
    },
    {   // Dice 8                                                               // Difficulty
        {  9, {  79, 304, 596, 824, 943, 986, 997, 1000, -1 } },                //  1
        {  9, {  31, 159, 396, 659, 853, 952, 989, 998, 1000, -1 } },           //  2
        {  9, {  11,  73, 232, 473, 715, 882, 964, 992, 999, 1000, -1 } },      //  3
        {  9, {   3,  29, 118, 300, 543, 766, 911, 975, 995, 999, 1000, -1 } }, //  4
        {  9, {   1,  10,  51, 164, 364, 609, 816, 938, 986, 998, 1000, -1 } }, //  5
        {  8, {   3,  18,  74, 209, 428, 675, 866, 963, 994, 999, 1000, -1 } }, //  6
        {  7, {   5,  26,  97, 254, 495, 745, 914, 983, 998, 1000, -1 } },      //  7
        {  7, {   1,   6,  33, 117, 300, 569, 821, 958, 995, 1000, -1 } },      //  8
        {  6, {   1,   7,  35, 132, 352, 662, 904, 987, 999, 1000, -1 } },      //  9
        {  4, {   4,  29, 137, 419, 799, 968, 998, 1000, -1 } }                 // 10
    },
    {   // Dice 9                                                               // Difficulty
        { 10, {  62, 255, 532, 774, 917, 977, 995, 999, 1000, -1 } },           //  1
        { 10, {  22, 121, 326, 582, 796, 923, 978, 995, 999, 1000, -1 } },      //  2
        { 10, {   7,  50, 173, 384, 625, 820, 933, 982, 996, 999, 1000, -1 } }, //  3
        { 10, {   2,  18,  78, 219, 434, 666, 846, 946, 986, 998, 1000, -1 } }, //  4
        {  9, {   5,  29, 105, 259, 481, 708, 874, 961, 992, 999, 1000, -1 } }, //  5
        {  9, {   1,   9,  40, 128, 296, 527, 751, 904, 975, 996, 1000, -1 } }, //  6
        {  8, {   2,  12,  49, 147, 332, 575, 799, 936, 988, 999, 1000, -1 } }, //  7
        {  7, {   2,  13,  53, 162, 367, 631, 855, 967, 996, 1000, -1 } },      //  8
        {  6, {   2,  11,  52, 170, 405, 703, 918, 989, 999, 1000, -1 } },      //  9
        {  5, {   1,   7,  40, 164, 455, 815, 971, 998, 1000, -1 } }            // 10
    },
    {   // Dice 10                                                                   // Difficulty
        { 11, {  48, 213, 472, 721, 886, 963, 991, 998, 1000, -1 } },                //  1
        { 11, {  15,  92, 266, 506, 733, 885, 962, 990, 998, 1000, -1 } },           //  2
        { 11, {   4,  34, 127, 306, 536, 748, 891, 964, 991, 998, 1000, -1 } },      //  3
        { 11, {   1,  11,  51, 156, 338, 562, 766, 901, 969, 993, 999, 1000, -1 } }, //  4
        { 10, {   3,  17,  65, 178, 365, 589, 787, 915, 975, 995, 999, 1000, -1 } }, //  5
        {  9, {   4,  21,  75, 195, 389, 617, 813, 933, 984, 998, 1000, -1 } },      //  6
        {  9, {   1,   5,  24,  81, 207, 411, 648, 844, 953, 991, 999, 1000, -1 } }, //  7
        {  8, {   1,   5,  23,  80, 213, 432, 687, 882, 974, 997, 1000, -1 } },      //  8
        {  6, {   3,  18,  72, 210, 456, 740, 931, 991, 1000, -1 } },                //  9
        {  5, {   1,  10,  51, 191, 489, 830, 974, 998, 1000, -1 } }                 // 10
    },
    {   // Dice 11                                                                        // Difficulty
        { 12, {  38, 177, 415, 667, 850, 946, 985, 997, 999, -1 } },                      //  1
        { 12, {  11,  69, 214, 435, 666, 840, 939, 982, 996, 999, 1000, -1 } },           //  2
        { 12, {   3,  23,  93, 239, 450, 669, 838, 937, 981, 996, 999, 1000, -1 } },      //  3
        { 12, {   1,   6,  33, 109, 256, 462, 675, 841, 939, 982, 996, 999, 1000, -1 } }, //  4
        { 11, {   1,   9,  40, 119, 267, 471, 683, 848, 944, 985, 997, 1000, -1 } },      //  5
        { 10, {   2,  11,  43, 123, 273, 480, 695, 861, 953, 989, 998, 1000, -1 } },      //  6
        {  9, {   2,  11,  42, 122, 273, 487, 711, 879, 965, 994, 999, 1000, -1 } },      //  7
        {  8, {   2,   9,  37, 113, 267, 495, 735, 905, 980, 998, 1000, -1 } },           //  8
        {  7, {   1,   6,  28,  96, 252, 505, 773, 941, 992, 1000, -1 } },                //  9
        {  5, {   2,  14,  65, 220, 521, 844, 976, 999, 1000, -1 } },                     // 10
    }
};

#define OLDSUCC_DIE_TO_ROLL 10
#define DIE_TO_ROLL         1000
#define NUMBER_TOO_LARGE    (-200)

/*
 * Roll a 10-sided die. If it's equal to or higher than the difficulty,
 * return true.
 */
static int simple_success(int diff)
{
    int rand = RandomINT32(1, OLDSUCC_DIE_TO_ROLL);
    return rand >= diff;
}

/* The lookup function: Given a table in the form of table[dice][diff]
   and a given number this function returns the number of successes
   corresponding to that number in the table.
   If the number is larger than the largest boundary, it will return
   NUMBER_TOO_LARGE.
*/
static int lookup_succ_table(const dice_node *row, int *psucc)
{
    int randnum = RandomINT32(0, DIE_TO_ROLL-1);
    int succs = row->maxsuccs;
    for (int i = 0; i < MAXBOUND && 0 < row->bound[i]; i++)
    {
        if (randnum < row->bound[i])
        {
            *psucc = succs;
            return 0;
        }
        succs--;
    }
    return NUMBER_TOO_LARGE;
}

/* A simple function to trigger the lookup: Translates the dice, diff
   and random number into an entry point for the table and retrieves
   the appropriate number of successes.
   If the request is for a result outside of the table, use the following
   simple algorithm:
   Get the result from this algorithm with MAXDICE, then, for every die
   over the max, roll one die. If it's over the diff, add a success.
   If the diff is higher than MAXDIFF, return 0 successes.
*/
static int getsuccs(int dice, int diff, int *psucc)
{
    if (dice <= 0)
    {
        *psucc = 0;
        return 0;
    }

    if (diff <= 0)
    {
        *psucc = dice;
        return 0;
    }
    else if (MAXDIFF < diff)
    {
        *psucc = 0;
        return 0;
    }

    int extra_successes = 0;
    if (MAXDICE < dice)
    {
        for (int i = MAXDICE; i < dice; i++)
        {
            if (simple_success(diff))
            {
                extra_successes++;
            }
        }
        dice = MAXDICE;
    }

    int succs;
    const dice_node *node = &dice_table[dice-1][diff-1];
    int err = lookup_succ_table(node, &succs);
    if (0 == err)
    {
        *psucc = succs + extra_successes;
    }
    return err;
}

/* The MUX-style function */

FUNCTION(fun_successes)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Number of dice and difficulty.
    //
    if (  !is_integer(fargs[0], nullptr)
       || !is_integer(fargs[1], nullptr))
    {
        safe_str(T("#-1 ARGUMENTS MUST BE INTEGERS"), buff, bufc);
        return;
    }

    int ver = 1;
    if (3 <= nfargs)
    {
        ver = mux_atol(fargs[2]);
    }

    int num_dice   = mux_atol(fargs[0]);

    if (0 == num_dice)
    {
        safe_str(T("0"), buff, bufc);
    }
    else if (num_dice < 0)
    {
        safe_str(T("#-1 NUMBER OF DICE SHOULD BE > 0"), buff, bufc);
    }
    else if (100 < num_dice)
    {
        safe_str(T("#-1 TOO MANY DICE FOR ME TO ROLL"), buff, bufc);
    }
    else
    {
        int difficulty = mux_atol(fargs[1]);
        int successes = 0;
        if (1 == ver)
        {
            switch (getsuccs(num_dice, difficulty, &successes))
            {
            case 0:
                safe_tprintf_str(buff, bufc, T("%d"), successes);
                break;

            case NUMBER_TOO_LARGE:
                safe_str(T("#-1 INVALID SUCCESS TABLE"), buff, bufc);
                break;

            default:
                safe_str(T("#-1 UNKNOWN ERROR"), buff, bufc);
                break;
            }
        }
        else
        {
            // Roll some number of dice equal to num_dice and count successes and botches
            //
            int i;
            for (i = 0; i < num_dice; i++)
            {
                int roll = RandomINT32(1, OLDSUCC_DIE_TO_ROLL);
                if (1 == roll)
                {
                    // Botch -- decrement successes.
                    //
                    --successes;
                }
                else if (difficulty <= roll)
                {
                    // Success -- increment successes.
                    //
                    ++successes;
                }
            }

            if (difficulty < num_dice)
            {
                if (successes < 0)
                {
                    successes = 0;
                }
                else if (successes == 0)
                {
                    successes = 1;
                }
            }

            // Return final number of successes (positive, negative, or zero).
            //
            safe_ltoa(successes, buff, bufc);
        }
    }
}


/*
 * ---------------------------------------------------------------------------
 * * fun_get, fun_get_eval: Get attribute from object.
 */
#define GET_GET     1
#define GET_XGET    2
#define GET_EVAL    4
#define GET_GEVAL   8

static void get_handler(UTF8 *buff, UTF8 **bufc, dbref executor, UTF8 *fargs[], int key)
{
    bool bFreeBuffer = false;
    UTF8 *pRefAttrib = fargs[0];

    if (  key == GET_XGET
       || key == GET_EVAL)
    {
        pRefAttrib = alloc_lbuf("get_handler");
        UTF8 *bufp = pRefAttrib;
        safe_tprintf_str(pRefAttrib, &bufp, T("%s/%s"), fargs[0], fargs[1]);
        bFreeBuffer = true;
    }
    dbref thing;
    ATTR *pattr;
    bool bNoMatch = !parse_attrib(executor, pRefAttrib, &thing, &pattr);
    if (bFreeBuffer)
    {
        free_lbuf(pRefAttrib);
    }

    if (bNoMatch)
    {
        safe_nomatch(buff, bufc);
        return;
    }

    if (!pattr)
    {
        return;
    }

    if (  (pattr->flags & AF_IS_LOCK)
       || !bCanReadAttr(executor, thing, pattr, true))
    {
        safe_noperm(buff, bufc);
        return;
    }

    dbref aowner;
    int   aflags;
    size_t nLen = 0;
    UTF8 *atr_gotten = atr_pget_LEN(thing, pattr->number, &aowner, &aflags, &nLen);

    if (  key == GET_EVAL
       || key == GET_GEVAL)
    {
        if ((aflags & AF_NOEVAL) || NoEval(thing))
        {
            if (nLen)
            {
                safe_copy_buf(atr_gotten, nLen, buff, bufc);
            }
        }
        else
        {
            mux_exec(atr_gotten, nLen, buff, bufc, thing, executor, executor,
                AttrTrace(aflags, EV_FIGNORE|EV_EVAL), nullptr, 0);
        }
    }
    else
    {
        if (nLen)
        {
            safe_copy_buf(atr_gotten, nLen, buff, bufc);
        }
    }
    free_lbuf(atr_gotten);
}

static FUNCTION(fun_get)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    get_handler(buff, bufc, executor, fargs, GET_GET);
}

static FUNCTION(fun_xget)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!*fargs[0] || !*fargs[1])
    {
        return;
    }

    get_handler(buff, bufc, executor, fargs, GET_XGET);
}


static FUNCTION(fun_get_eval)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    get_handler(buff, bufc, executor, fargs, GET_GEVAL);
}

static FUNCTION(fun_subeval)
{
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    mux_exec(fargs[0], LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
             eval|EV_EVAL|EV_NO_LOCATION|EV_NOFCHECK|EV_FIGNORE|EV_NO_COMPRESS,
             nullptr, 0);
}

static FUNCTION(fun_eval)
{
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs == 1)
    {
        mux_exec(fargs[0], LBUF_SIZE-1, buff, bufc, executor, caller, enactor, eval|EV_EVAL,
                 nullptr, 0);
        return;
    }
    if (!*fargs[0] || !*fargs[1])
    {
        return;
    }

    get_handler(buff, bufc, executor, fargs, GET_EVAL);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_u and fun_ulocal:  Call a user-defined function.
 */

static void do_ufun(UTF8 *buff, UTF8 **bufc, dbref executor, dbref caller,
            dbref enactor,
            UTF8 *fargs[], int nfargs,
            const UTF8 *cargs[], int ncargs,
            bool is_local)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UTF8 *atext;
    dbref thing;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    // If we're evaluating locally, preserve the global registers.
    //
    reg_ref **preserve = nullptr;
    if (is_local)
    {
        preserve = PushRegisters(MAX_GLOBAL_REGS);
        save_global_regs(preserve);
    }

    // Evaluate it using the rest of the passed function args.
    //
    if ((aflags & AF_NOEVAL) || NoEval(thing))
    {
        size_t nLen = strlen((const char *)atext);
        safe_copy_buf(atext, nLen, buff, bufc);
    }
    else
    {
        mux_exec(atext, LBUF_SIZE-1, buff, bufc, thing, executor, enactor,
            AttrTrace(aflags, EV_FCHECK|EV_EVAL),
            (const UTF8 **)&(fargs[1]), nfargs - 1);
    }
    free_lbuf(atext);

    // If we're evaluating locally, restore the preserved registers.
    //
    if (is_local)
    {
        restore_global_regs(preserve);
        PopRegisters(preserve, MAX_GLOBAL_REGS);
    }
}

static FUNCTION(fun_u)
{
    UNUSED_PARAMETER(eval);
    do_ufun(buff, bufc, executor, caller, enactor, fargs, nfargs, cargs,
            ncargs, false);
}

static FUNCTION(fun_ulocal)
{
    UNUSED_PARAMETER(eval);
    do_ufun(buff, bufc, executor, caller, enactor, fargs, nfargs, cargs,
            ncargs, true);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_parent: Get parent of object.
 */

static FUNCTION(fun_parent)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (1 < nfargs)
    {
        if (check_command(executor, T("@parent"), buff, bufc))
        {
            return;
        }
        do_parent(executor, caller, enactor, eval, 0, 2, fargs[0], fargs[1],
                nullptr, 0);
    }
    else
    {
        dbref it = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(it))
        {
            safe_match_result(it, buff, bufc);
            return;
        }
        if (  Examinable(executor, it)
           || it == enactor)
        {
            safe_tprintf_str(buff, bufc, T("#%d"), Parent(it));
        }
        else
        {
            safe_noperm(buff, bufc);
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_parenmatch: Highlights brackets by nesting depth using ANSI colors.
 * * Mismatched brackets are shown in red.
 */

static FUNCTION(fun_parenmatch)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const UTF8 *pInput = fargs[0];
    size_t nLen = strlen(reinterpret_cast<const char *>(pInput));

    // Depth color cycle (6 colors).
    //
    static const char *depthColors[] =
    {
        COLOR_FG_GREEN,
        COLOR_FG_YELLOW,
        COLOR_FG_BLUE,
        COLOR_FG_MAGENTA,
        COLOR_FG_CYAN,
        COLOR_FG_WHITE
    };
    static const int nColors = 6;

    // Bracket type definitions.
    //
    static const UTF8 openers[] = { '(', '[', '{' };
    static const UTF8 closers[] = { ')', ']', '}' };
    static const int nBracketTypes = 3;

    // Pass 1: Identify mismatched brackets.
    //
    bool *bMismatch = new bool[nLen];
    memset(bMismatch, 0, nLen * sizeof(bool));

    for (int bt = 0; bt < nBracketTypes; bt++)
    {
        UTF8 chOpen = openers[bt];
        UTF8 chClose = closers[bt];

        // Forward scan: detect excess closers.
        //
        int depth = 0;
        for (size_t i = 0; i < nLen; i++)
        {
            if (pInput[i] == chOpen)
            {
                depth++;
            }
            else if (pInput[i] == chClose)
            {
                if (depth > 0)
                {
                    depth--;
                }
                else
                {
                    bMismatch[i] = true;
                }
            }
        }

        // Backward scan: detect excess openers.
        //
        depth = 0;
        for (size_t i = nLen; i > 0; i--)
        {
            size_t idx = i - 1;
            if (pInput[idx] == chClose)
            {
                depth++;
            }
            else if (pInput[idx] == chOpen)
            {
                if (depth > 0)
                {
                    depth--;
                }
                else
                {
                    bMismatch[idx] = true;
                }
            }
        }
    }

    // Pass 2: Output with colors.
    //
    int depths[3] = { 0, 0, 0 };

    for (size_t i = 0; i < nLen; i++)
    {
        UTF8 ch = pInput[i];

        // Check if this is a bracket character.
        //
        int bt;
        bool bOpener = false;
        bool bCloser = false;
        for (bt = 0; bt < nBracketTypes; bt++)
        {
            if (ch == openers[bt])
            {
                bOpener = true;
                break;
            }
            else if (ch == closers[bt])
            {
                bCloser = true;
                break;
            }
        }

        if (bOpener || bCloser)
        {
            if (bMismatch[i])
            {
                safe_str(reinterpret_cast<const UTF8 *>(COLOR_FG_RED), buff, bufc);
                safe_chr(ch, buff, bufc);
                safe_str(reinterpret_cast<const UTF8 *>(COLOR_RESET), buff, bufc);
            }
            else if (bOpener)
            {
                int d = depths[bt];
                safe_str(reinterpret_cast<const UTF8 *>(depthColors[d % nColors]), buff, bufc);
                safe_chr(ch, buff, bufc);
                safe_str(reinterpret_cast<const UTF8 *>(COLOR_RESET), buff, bufc);
                depths[bt]++;
            }
            else // bCloser
            {
                depths[bt]--;
                if (depths[bt] < 0)
                {
                    depths[bt] = 0;
                }
                int d = depths[bt];
                safe_str(reinterpret_cast<const UTF8 *>(depthColors[d % nColors]), buff, bufc);
                safe_chr(ch, buff, bufc);
                safe_str(reinterpret_cast<const UTF8 *>(COLOR_RESET), buff, bufc);
            }
        }
        else
        {
            safe_chr(ch, buff, bufc);
        }
    }

    delete[] bMismatch;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_mid: mid(foobar,2,3) returns oba
 */

static FUNCTION(fun_mid)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int iStart = mux_atol(fargs[1]);
    int nMid   = mux_atol(fargs[2]);

    if (nMid < 0)
    {
        // The range should end at iStart, inclusive.
        //
        iStart += 1 + nMid;
        nMid = -nMid;
    }

    if (iStart < 0)
    {
        // Start at the beginning of the string,
        // but end at the same place the range would end
        // if negative starting positions were valid.
        //
        nMid += iStart;
        iStart = 0;
    }

    if (  nMid <= 0
       || LBUF_SIZE <= iStart)
    {
        // The range doesn't select any characters.
        //
        return;
    }

    // At this point, iStart and nMid are nonnegative numbers
    // which may -still- not refer to valid data in the string.
    //
    const unsigned char *p = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t slen = strlen(reinterpret_cast<const char *>(p));

    unsigned char out[LBUF_SIZE];
    size_t n = co_mid_cluster(out, p, slen,
        static_cast<size_t>(iStart), static_cast<size_t>(nMid));

    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    if (n > nMax) n = nMax;
    memcpy(*bufc, out, n);
    *bufc += n;
    **bufc = '\0';
}

// ---------------------------------------------------------------------------
// fun_right: right(foobar,2) returns ar
// ---------------------------------------------------------------------------

static FUNCTION(fun_right)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int nRight = mux_atol(fargs[1]);
    if (nRight < 0)
    {
        safe_range(buff, bufc);
        return;
    }
    else if (0 == nRight)
    {
        return;
    }

    const unsigned char *p = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t slen = strlen(reinterpret_cast<const char *>(p));
    size_t nClusters = co_cluster_count(p, slen);

    if (static_cast<size_t>(nRight) < nClusters)
    {
        unsigned char out[LBUF_SIZE];
        size_t n = co_mid_cluster(out, p, slen,
            nClusters - static_cast<size_t>(nRight),
            static_cast<size_t>(nRight));

        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        if (n > nMax) n = nMax;
        memcpy(*bufc, out, n);
        *bufc += n;
        **bufc = '\0';
    }
    else
    {
        safe_str(fargs[0], buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_first: Returns first word in a string
 */

static FUNCTION(fun_first)
{
    // If we are passed an empty arglist return a null string.
    //
    if (nfargs == 0)
    {
        return;
    }

    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    UTF8 *s = trim_space_sep(fargs[0], sep);
    UTF8 *first = split_token(&s, sep);
    if (first)
    {
        safe_str(first, buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_rest: Returns all but the first word in a string
 */


static FUNCTION(fun_rest)
{
    // If we are passed an empty arglist return a null string.
    //
    if (nfargs == 0)
    {
        return;
    }

    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    UTF8 *s = trim_space_sep(fargs[0], sep);
    split_token(&s, sep);
    if (s)
    {
        safe_str(s, buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_v: Function form of %-substitution
 */

static FUNCTION(fun_v)
{
    UNUSED_PARAMETER(nfargs);

    dbref aowner;
    int aflags;
    UTF8 *sbuf, *sbufc;
    ATTR *ap;

    UTF8 *tbuf = fargs[0];
    if (mux_isattrnameinitial(tbuf))
    {
        size_t nAdvance = utf8_FirstByte[static_cast<unsigned char>(*tbuf)];
        if (nAdvance >= UTF8_CONTINUE)
        {
            nAdvance = UTF8_SIZE1;
        }
        bool bValid = true;
        for (size_t i = 1; i < nAdvance; i++)
        {
            if ('\0' == tbuf[i] || UTF8_CONTINUE != utf8_FirstByte[static_cast<unsigned char>(tbuf[i])])
            {
                bValid = false;
                break;
            }
        }
        if (!bValid)
        {
            nAdvance = UTF8_SIZE1;
        }

        if ('\0' != tbuf[nAdvance])
        {
            // Fetch an attribute from me. First see if it exists,
            // returning a null string if it does not.
            //
            ap = atr_str(fargs[0]);
            if (!ap)
            {
                return;
            }

            // If we can access it, return it, otherwise return a null
            // string.
            //
            size_t nLen;
            tbuf = atr_pget_LEN(executor, ap->number, &aowner, &aflags, &nLen);
            if (See_attr(executor, executor, ap))
            {
                safe_copy_buf(tbuf, nLen, buff, bufc);
            }
            free_lbuf(tbuf);
        }
        else
        {
            // Single letter, process as %<arg>
            //
            sbuf = alloc_sbuf("fun_v");
            sbufc = sbuf;
            safe_sb_chr('%', sbuf, &sbufc);
            safe_sb_str(fargs[0], sbuf, &sbufc);
            *sbufc = '\0';
            mux_exec(sbuf, SBUF_SIZE-1, buff, bufc, executor, caller, enactor, eval|EV_EVAL|EV_FIGNORE,
                     cargs, ncargs);
            free_sbuf(sbuf);
        }
        return;
    }

    // Leading digit, process as argument number.
    //
    int i = mux_atol(fargs[0]);
    if (  0 <= i
       && i < ncargs
       && nullptr != cargs[i])
    {
        safe_str(cargs[i], buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_s: Force substitution to occur.
 */

static FUNCTION(fun_s)
{
    UNUSED_PARAMETER(nfargs);

    mux_exec(fargs[0], LBUF_SIZE-1, buff, bufc, executor, caller, enactor, eval|EV_FIGNORE|EV_EVAL,
             cargs, ncargs);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_con: Returns first item in contents list of object/room
 */

static FUNCTION(fun_con)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
    }
    else if (Has_contents(it))
    {
        if (  Examinable(executor, it)
           || where_is(executor) == it
           || it == enactor)
        {
            safe_tprintf_str(buff, bufc, T("#%d"), Contents(it));
        }
        else
        {
            safe_noperm(buff, bufc);
        }
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_exit: Returns first exit in exits list of room.
 */

static FUNCTION(fun_exit)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
    }
    else if (  Has_exits(it)
            && Good_obj(Exits(it)))
    {
        int key = 0;
        if (Examinable(executor, it))
        {
            key |= VE_LOC_XAM;
        }
        if (Dark(it))
        {
            key |= VE_LOC_DARK;
        }
        dbref exit;
        DOLIST(exit, Exits(it))
        {
            if (exit_visible(exit, executor, key))
            {
                safe_tprintf_str(buff, bufc, T("#%d"), exit);
                return;
            }
        }
        safe_notfound(buff, bufc);
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_next: return next thing in contents or exits chain
 */

static FUNCTION(fun_next)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
    }
    else if (Has_siblings(it))
    {
        dbref loc = where_is(it);
        bool ex_here = Good_obj(loc) ? Examinable(executor, loc) : false;
        if (  ex_here
           || loc == executor
           || loc == where_is(executor))
        {
            if (!isExit(it))
            {
                safe_tprintf_str(buff, bufc, T("#%d"), Next(it));
            }
            else
            {
                int key = 0;
                if (ex_here)
                {
                    key |= VE_LOC_XAM;
                }
                if (Dark(loc))
                {
                    key |= VE_LOC_DARK;
                }
                dbref exit;
                DOLIST(exit, it)
                {
                    if (  exit != it
                       && exit_visible(exit, executor, key))
                    {
                        safe_tprintf_str(buff, bufc, T("#%d"), exit);
                        return;
                    }
                }
                safe_notfound(buff, bufc);
            }
        }
        else
        {
            safe_noperm(buff, bufc);
        }
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_loc: Returns the location of something
 */

static FUNCTION(fun_loc)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
    }
    else if (locatable(executor, it, enactor))
    {
        safe_tprintf_str(buff, bufc, T("#%d"), Location(it));
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_where: Returns the "true" location of something
 */

static FUNCTION(fun_where)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
    }
    else if (locatable(executor, it, enactor))
    {
        safe_tprintf_str(buff, bufc, T("#%d"), where_is(it));
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_rloc: Returns the recursed location of something (specifying #levels)
 */

static FUNCTION(fun_rloc)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int levels = mux_atol(fargs[1]);
    if (levels > mudconf.ntfy_nest_lim)
    {
        levels = mudconf.ntfy_nest_lim;
    }

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
    }
    else if (locatable(executor, it, enactor))
    {
        for (int i = 0; i < levels; i++)
        {
            if (  Good_obj(it)
               && (  isExit(it)
                  || Has_location(it)))
            {
                it = Location(it);
            }
            else
            {
                break;
            }
        }
        safe_tprintf_str(buff, bufc, T("#%d"), it);
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_room: Find the room an object is ultimately in.
 */

static FUNCTION(fun_room)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
    }
    else if (locatable(executor, it, enactor))
    {
        int count;
        for (count = mudconf.ntfy_nest_lim; count > 0; count--)
        {
            it = Location(it);
            if (!Good_obj(it))
            {
                break;
            }
            if (isRoom(it))
            {
                safe_tprintf_str(buff, bufc, T("#%d"), it);
                return;
            }
        }
        safe_nothing(buff, bufc);
    }
    else if (isRoom(it))
    {
        safe_tprintf_str(buff, bufc, T("#%d"), it);
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_owner: Return the owner of an object.
 */

static FUNCTION(fun_owner)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it;
    ATTR *pattr;
    if (parse_attrib(executor, fargs[0], &it, &pattr))
    {
        if (  !pattr
           || !See_attr(executor, it, pattr))
        {
            safe_nothing(buff, bufc);
            return;
        }
        else
        {
            dbref aowner;
            int   aflags;
            atr_pget_info(it, pattr->number, &aowner, &aflags);
            it = aowner;
        }
    }
    else
    {
        it = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(it))
        {
            safe_match_result(it, buff, bufc);
            return;
        }
        it = Owner(it);
    }
    safe_tprintf_str(buff, bufc, T("#%d"), it);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_controls: Does x control y?
 */

static FUNCTION(fun_controls)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref x = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(x))
    {
        safe_match_result(x, buff, bufc);
        safe_str(T(" (ARG1)"), buff, bufc);
        return;
    }
    dbref y = match_thing_quiet(executor, fargs[1]);
    if (!Good_obj(y))
    {
        safe_match_result(x, buff, bufc);
        safe_str(T(" (ARG2)"), buff, bufc);
        return;
    }
    safe_bool(Controls(x,y), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_fullname: Return the fullname of an object (good for exits)
 */

static FUNCTION(fun_fullname)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    if (!mudconf.read_rem_name)
    {
        if (  !nearby_or_control(executor, it)
           && !isPlayer(it))
        {
            safe_str(T("#-1 TOO FAR AWAY TO SEE"), buff, bufc);
            return;
        }
    }
    safe_str(Name(it), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_name: Return the name of an object
 */

static FUNCTION(fun_name)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (1 < nfargs)
    {
        if (  !fargs[0]
           || !fargs[1]
           || check_command(executor, T("@name"), buff, bufc))
        {
            return;
        }
        do_name(executor, caller, enactor, eval, 0, 2, fargs[0], fargs[1],
                nullptr, 0);
    }
    else
    {
        dbref it = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(it))
        {
            safe_match_result(it, buff, bufc);
            return;
        }
        if (!mudconf.read_rem_name)
        {
            if (  !nearby_or_control(executor, it)
               && !isPlayer(it)
               && !Long_Fingers(executor))
            {
                safe_str(T("#-1 TOO FAR AWAY TO SEE"), buff, bufc);
                return;
            }
        }
        UTF8 *temp = *bufc;
        safe_str(Name(it), buff, bufc);
        if (isExit(it))
        {
            UTF8 *s;
            for (s = temp; (s != *bufc) && (*s != ';'); s++)
            {
                // Do nothing
                //
                ;
            }
            if (*s == ';')
            {
                *bufc = s;
            }
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_match, fun_strmatch: Match arg2 against each word of arg1 returning
 * * index of first match, or against the whole string.
 */

static FUNCTION(fun_match)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    // Check each word individually, returning the word number of the first
    // one that matches.  If none match, return 0.
    //
    int wcount = 1;
    UTF8 *s = trim_space_sep(fargs[0], sep);
    do {
        UTF8 *r = split_token(&s, sep);
        mudstate.wild_invk_ctr = 0;
        if (quick_wild(fargs[1], r))
        {
            safe_ltoa(wcount, buff, bufc);
            return;
        }
        wcount++;
    } while (s);
    safe_chr('0', buff, bufc);
}

static FUNCTION(fun_strmatch)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Check if we match the whole string.  If so, return 1.
    //
    mudstate.wild_invk_ctr = 0;
    bool cc = quick_wild(fargs[1], fargs[0]);
    safe_bool(cc, buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_extract: extract words from string:
 * * extract(foo bar baz,1,2) returns 'foo bar'
 * * extract(foo bar baz,2,1) returns 'bar'
 * * extract(foo bar baz,2,2) returns 'bar baz'
 * *
 * * Now takes optional separator extract(foo-bar-baz,1,2,-) returns 'foo-bar'
 */

static FUNCTION(fun_extract)
{
    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(5, osep, DELIM_NULL|DELIM_CRLF|DELIM_INIT|DELIM_STRING))
    {
        return;
    }

    int iFirstWord = mux_atol(fargs[1]);
    int nWordsToCopy = mux_atol(fargs[2]);

    if (  iFirstWord < 1
       || nWordsToCopy < 1)
    {
        return;
    }

    UTF8 *bp = trim_space_sep(fargs[0], sep);

    if (1 == sep.n && 1 == osep.n)
    {
        // Single-char delimiter: use co_extract.
        // Space compresses consecutive delimiters; non-space treats each as significant.
        //
        size_t slen = strlen(reinterpret_cast<const char *>(bp));
        unsigned char out[LBUF_SIZE];
        size_t nOut = co_extract(out,
            reinterpret_cast<const unsigned char *>(bp), slen,
            static_cast<size_t>(iFirstWord),
            static_cast<size_t>(nWordsToCopy),
            static_cast<unsigned char>(sep.str[0]),
            static_cast<unsigned char>(osep.str[0]));

        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        if (nOut > nMax) nOut = nMax;
        memcpy(*bufc, out, nOut);
        *bufc += nOut;
        **bufc = '\0';
    }
    else
    {
        // Multi-char delimiter: use co_split_words.
        //
        const unsigned char *pData = reinterpret_cast<const unsigned char *>(bp);
        size_t nLen = strlen(reinterpret_cast<const char *>(bp));
        size_t wstarts[LBUF_SIZE], wends[LBUF_SIZE];
        size_t nWords = co_split_words(pData, nLen,
                            reinterpret_cast<const unsigned char *>(sep.str),
                            sep.n, wstarts, wends, LBUF_SIZE);

        iFirstWord--;
        if (iFirstWord < static_cast<int>(nWords))
        {
            if (static_cast<int>(nWords) < iFirstWord + nWordsToCopy)
            {
                nWordsToCopy = static_cast<int>(nWords) - iFirstWord;
            }

            bool bFirst = true;

            for (int i = iFirstWord; i < iFirstWord + nWordsToCopy; i++)
            {
                if (!bFirst)
                {
                    print_sep(osep, buff, bufc);
                }
                else
                {
                    bFirst = false;
                }
                size_t nb = wends[i] - wstarts[i];
                size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
                if (nb > nMax) nb = nMax;
                memcpy(*bufc, pData + wstarts[i], nb);
                *bufc += nb;
            }
            **bufc = '\0';
        }
    }
}

// xlate() controls the subtle definition of a softcode boolean.
//
bool xlate(UTF8 *arg)
{
    const UTF8 *p = arg;
    if (p[0] == '#')
    {
        if (p[1] == '-')
        {
            // '#-...' is false. This includes '#-0000' and '#-ABC'.
            // This cases are unlikely in practice. We can always come back
            // and cover these.
            //
            return false;
        }
        return true;
    }

    PARSE_FLOAT_RESULT pfr;
    if (ParseFloat(&pfr, p))
    {
        // Examine whether number was a zero value.
        //
        if (pfr.iString)
        {
            // This covers NaN, +Inf, -Inf, and Ind.
            //
            return false;
        }

        // We can ignore leading sign, exponent sign, and exponent as 0, -0,
        // and +0. Also, 0E+100 and 0.0e-100 are all zero.
        //
        // However, we need to cover 00000.0 and 0.00000 cases.
        //
        while (pfr.nDigitsA--)
        {
            if (*pfr.pDigitsA != '0')
            {
                return true;
            }
            pfr.pDigitsA++;
        }
        while (pfr.nDigitsB--)
        {
            if (*pfr.pDigitsB != '0')
            {
                return true;
            }
            pfr.pDigitsB++;
        }
        return false;
    }
    while (mux_isspace(*p))
    {
        p++;
    }
    if (p[0] == '\0')
    {
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------------
 * fun_index:  like extract(), but it works with an arbitrary separator.
 * index(a b | c d e | f g h | i j k, |, 2, 1) => c d e
 * index(a b | c d e | f g h | i j k, |, 2, 2) => c d e | f g h
 */

static FUNCTION(fun_index)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int start, end;
    UTF8 c, *s, *p;

    s = fargs[0];
    c = *fargs[1];
    start = mux_atol(fargs[2]);
    end = mux_atol(fargs[3]);

    if ((start < 1) || (end < 1) || (*s == '\0'))
    {
        return;
    }
    if (c == '\0')
    {
        c = ' ';
    }

    // Move s to point to the start of the item we want.
    //
    start--;
    while (start && s && *s)
    {
        if ((s = reinterpret_cast<UTF8 *>(strchr(reinterpret_cast<char *>(s), c))) != nullptr)
        {
            s++;
        }
        start--;
    }

    // Skip over just spaces.
    //
    while (s && (*s == ' '))
    {
        s++;
    }
    if (!s || !*s)
    {
        return;
    }

    // Figure out where to end the string.
    //
    p = s;
    while (end && p && *p)
    {
        if ((p = reinterpret_cast<UTF8 *>(strchr(reinterpret_cast<char *>(p), c))) != nullptr)
        {
            if (--end == 0)
            {
                do {
                    p--;
                } while ((*p == ' ') && (p > s));
                *(++p) = '\0';
                safe_str(s, buff, bufc);
                return;
            }
            else
            {
                p++;
            }
        }
    }

    // if we've gotten this far, we've run off the end of the string.
    //
    safe_str(s, buff, bufc);
}


static FUNCTION(fun_cat)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs)
    {
        safe_str(fargs[0], buff, bufc);
        for (int i = 1; i < nfargs; i++)
        {
            safe_chr(' ', buff, bufc);
            safe_str(fargs[i], buff, bufc);
        }
    }
}

static FUNCTION(fun_version)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str(mudstate.version, buff, bufc);
}

static FUNCTION(fun_strlen)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    size_t n = 0;
    if (nfargs >= 1)
    {
        size_t nBytes = 0;
        UTF8 *pStripped = strip_color(fargs[0], &nBytes, nullptr);
        n = utf8_cluster_count(pStripped, nBytes);
    }
    safe_ltoa(static_cast<long>(n), buff, bufc);
}

static FUNCTION(fun_strmem)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    size_t n = 0;
    if (nfargs >= 1)
    {
        n = strlen(reinterpret_cast<char *>(fargs[0]));
    }
    safe_ltoa(static_cast<long>(n), buff, bufc);
}

static FUNCTION(fun_num)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_tprintf_str(buff, bufc, T("#%d"), match_thing_quiet(executor, fargs[0]));
}

static FUNCTION(fun_objid)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref thing = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(thing))
    {
        safe_tprintf_str(buff, bufc, T("#%d"), thing);
        return;
    }

    int64_t csecs = creation_seconds(thing);
    if (0 != csecs)
    {
        UTF8 tbuf[I64BUF_SIZE];
        mux_i64toa(csecs, tbuf);
        safe_tprintf_str(buff, bufc, T("#%d:%s"), thing, tbuf);
    }
    else
    {
        safe_tprintf_str(buff, bufc, T("#%d"), thing);
    }
}

static void internalPlayerFind
(
    UTF8  *buff,
    UTF8 **bufc,
    dbref  player,
    UTF8  *name,
    int    bVerifyPlayer
)
{
    dbref thing;
    if (*name == '#')
    {
        thing = match_thing_quiet(player, name);
        if (bVerifyPlayer)
        {
            if (!Good_obj(thing))
            {
                safe_match_result(thing, buff, bufc);
                return;
            }
            if (!isPlayer(thing))
            {
                safe_nomatch(buff, bufc);
                return;
            }
        }
    }
    else
    {
        UTF8 *nptr = name;
        if (*nptr == '*')
        {
            // Start with the second character in the name string.
            //
            nptr++;
        }
        thing = lookup_player(player, nptr, true);
        if (  (!Good_obj(thing))
           || (!isPlayer(thing) && bVerifyPlayer))
        {
            safe_nomatch(buff, bufc);
            return;
        }
    }
    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    ItemToList_AddInteger(&pContext, thing);
    ItemToList_Final(&pContext);
}


static FUNCTION(fun_pmatch)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    internalPlayerFind(buff, bufc, executor, fargs[0], true);
}

static FUNCTION(fun_pfind)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    internalPlayerFind(buff, bufc, executor, fargs[0], false);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_comp: string compare.
 */

static FUNCTION(fun_comp)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int x;

    if (3 <= nfargs)
    {
        switch (fargs[2][0])
        {
        case 'a':
        case 'A':
            // Legacy ASCII comparison.
            //
            x = strcmp(reinterpret_cast<char *>(fargs[0]),
                       reinterpret_cast<char *>(fargs[1]));
            break;

        case 'c':
        case 'C':
            {
                // Case-insensitive Unicode collation.
                //
                size_t nA = strlen(reinterpret_cast<char *>(fargs[0]));
                size_t nB = strlen(reinterpret_cast<char *>(fargs[1]));
                x = mux_collate_cmp_ci(fargs[0], nA, fargs[1], nB);
            }
            break;

        default:
            {
                // Default: Unicode collation comparison.
                //
                size_t nA = strlen(reinterpret_cast<char *>(fargs[0]));
                size_t nB = strlen(reinterpret_cast<char *>(fargs[1]));
                x = mux_collate_cmp(fargs[0], nA, fargs[1], nB);
            }
            break;
        }
    }
    else
    {
        // Default: Unicode collation comparison.
        //
        size_t nA = strlen(reinterpret_cast<char *>(fargs[0]));
        size_t nB = strlen(reinterpret_cast<char *>(fargs[1]));
        x = mux_collate_cmp(fargs[0], nA, fargs[1], nB);
    }

    if (x < 0)
    {
        safe_str(T("-1"), buff, bufc);
    }
    else
    {
        safe_bool((x != 0), buff, bufc);
    }
}

#if defined(WOD_REALMS) || defined(REALITY_LVLS)
static FUNCTION(fun_cansee)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref looker = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(looker))
    {
        safe_match_result(looker, buff, bufc);
        safe_str(T(" (LOOKER)"), buff, bufc);
        return;
    }
    dbref lookee = match_thing_quiet(executor, fargs[1]);
    if (!Good_obj(lookee))
    {
        safe_match_result(looker, buff, bufc);
        safe_str(T(" (LOOKEE)"), buff, bufc);
        return;
    }
    int mode;
    if (nfargs == 3)
    {
        mode = mux_atol(fargs[2]);
        switch (mode)
        {
        case ACTION_IS_STATIONARY:
        case ACTION_IS_MOVING:
        case ACTION_IS_TALKING:
            break;

        default:
            mode = ACTION_IS_STATIONARY;
            break;
        }
    }
    else
    {
        mode = ACTION_IS_STATIONARY;
    }

    // Do it.
    //
    int Realm_Do = DoThingToThingVisibility(looker, lookee, mode);
    bool bResult = false;
    if ((Realm_Do & REALM_DO_MASK) != REALM_DO_HIDDEN_FROM_YOU)
    {
#ifdef REALITY_LVLS
        bResult = (!Dark(lookee) && IsReal(looker, lookee));
#else
        bResult = !Dark(lookee);
#endif // REALITY_LVLS
    }
    safe_bool(bResult, buff, bufc);
}
#endif

typedef enum
{
    lconAny = 0,
    lconPlayer,
    lconObject,
    lconConnect,
    lconPuppet,
    lconListen
} lconSubset;

static struct lconSubsetTable
{
    const UTF8 *name;
    lconSubset subset;
}
SubsetTable[] =
{
    { T("PLAYER"),  lconPlayer },
    { T("OBJECT"),  lconObject },
    { T("CONNECT"), lconConnect},
    { T("PUPPET"),  lconPuppet },
    { T("LISTEN"),  lconListen },
    { nullptr, lconAny }
};

/*
 * ---------------------------------------------------------------------------
 * * fun_lcon: Return a list of contents.
 */

static FUNCTION(fun_lcon)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);

    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }

    if (!Has_contents(it))
    {
        safe_nothing(buff, bufc);
        return;
    }

    lconSubset i_subset = lconAny;
    if (2 == nfargs)
    {
        // PLAYER  -- include only Player objects.
        // OBJECT  -- include only non-Player objects.
        // CONNECT -- include only Connected Players.
        // PUPPET  -- include only Puppets.
        // LISTEN  -- include only Listening objects.

        lconSubsetTable *p = SubsetTable;
        while (nullptr != p->name)
        {
            if (mux_stricmp(fargs[1], p->name) == 0)
            {
                i_subset = p->subset;
                break;
            }
            p++;
        }
    }

    if (  Examinable(executor, it)
       || Location(executor) == it
       || it == enactor)
    {
        dbref thing;
        ITL pContext;
        ItemToList_Init(&pContext, buff, bufc, '#');
        DOLIST(thing, Contents(it))
        {
#if defined(WOD_REALMS) || defined(REALITY_LVLS)
            int iRealmAction = DoThingToThingVisibility(executor, thing,
                ACTION_IS_STATIONARY);
            if (iRealmAction != REALM_DO_HIDDEN_FROM_YOU)
            {
#endif
                if (  Can_Hide(thing)
                   && Hidden(thing)
                   && !See_Hidden(executor))
                {
                    continue;
                }

                if (lconAny != i_subset)
                {
                    if (  (  lconPlayer == i_subset
                          && !isPlayer(thing))
                       || (  lconObject == i_subset
                          && isPlayer(thing))
                       || (  lconConnect == i_subset
                          && !Connected(thing))
                       || (  lconPuppet == i_subset
                          && !Puppet(thing))
                       || (  lconListen == i_subset
                          && !H_Listen(thing)))
                    {
                        continue;
                    }
                }

                if (!ItemToList_AddInteger(&pContext, thing))
                {
                    break;
                }
#if defined(WOD_REALMS) || defined(REALITY_LVLS)
            }
#endif
        }
        ItemToList_Final(&pContext);
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_lexits: Return a list of exits.
 */

static FUNCTION(fun_lexits)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    if (!Has_exits(it))
    {
        safe_nothing(buff, bufc);
        return;
    }
    bool bExam = Examinable(executor, it);
    if (  !bExam
       && where_is(executor) != it
       && it != enactor)
    {
        safe_noperm(buff, bufc);
        return;
    }

    // Return info for all parent levels.
    //
    bool bDone = false;
    dbref parent;
    int lev;
    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    ITER_PARENTS(it, parent, lev)
    {
        // Look for exits at each level.
        //
        if (!Has_exits(parent))
        {
            continue;
        }
        int key = 0;
        if (Examinable(executor, parent))
        {
            key |= VE_LOC_XAM;
        }
        if (Dark(parent))
        {
            key |= VE_LOC_DARK;
        }
        if (Dark(it))
        {
            key |= VE_BASE_DARK;
        }

        dbref thing;
        DOLIST(thing, Exits(parent))
        {
            if (  exit_visible(thing, executor, key)
               && !ItemToList_AddInteger(&pContext, thing))
            {
                bDone = true;
                break;
            }
        }
        if (bDone)
        {
            break;
        }
    }
    ItemToList_Final(&pContext);
}

// ---------------------------------------------------------------------------
// fun_entrances: Search database for entrances (inverse of exits).
//
FUNCTION(fun_entrances)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UTF8 *p;
    dbref i;

    dbref low_bound = 0;
    if (3 <= nfargs)
    {
        p = fargs[2];
        if (NUMBER_TOKEN == p[0])
        {
            p++;
        }
        i = mux_atol(p);
        if (Good_dbref(i))
        {
            low_bound = i;
        }
    }

    dbref high_bound = mudstate.db_top - 1;
    if (4 == nfargs)
    {
        p = fargs[3];
        if (NUMBER_TOKEN == p[0])
        {
            p++;
        }
        i = mux_atol(p);
        if (Good_dbref(i))
        {
            high_bound = i;
        }
    }

    bool find_ex = false;
    bool find_th = false;
    bool find_pl = false;
    bool find_rm = false;

    if (2 <= nfargs)
    {
        for (p = fargs[1]; *p; p++)
        {
            switch (*p)
            {
            case 'a':
            case 'A':
                find_ex = find_th = find_pl = find_rm = true;
                break;

            case 'e':
            case 'E':
                find_ex = true;
                break;

            case 't':
            case 'T':
                find_th = true;
                break;

            case 'p':
            case 'P':
                find_pl = true;
                break;

            case 'r':
            case 'R':
                find_rm = true;
                break;

            default:
                safe_str(T("#-1 INVALID TYPE"), buff, bufc);
                return;
            }
        }
    }

    if (!(find_ex || find_th || find_pl || find_rm))
    {
        find_ex = find_th = find_pl = find_rm = true;
    }

    dbref thing;
    if (  nfargs == 0
       || *fargs[0] == '\0')
    {
        if (Has_location(executor))
        {
            thing = Location(executor);
        }
        else
        {
            thing = executor;
        }
        if (!Good_obj(thing))
        {
            safe_nothing(buff, bufc);
            return;
        }
    }
    else
    {
        init_match(executor, fargs[0], NOTYPE);
        match_everything(MAT_EXIT_PARENTS);
        thing = noisy_match_result();
        if (!Good_obj(thing))
        {
            safe_nothing(buff, bufc);
            return;
        }
    }

    if (!payfor(executor, mudconf.searchcost))
    {
        notify(executor, tprintf(T("You don\xE2\x80\x99t have enough %s."),
            mudconf.many_coins));
        safe_nothing(buff, bufc);
        return;
    }

    int control_thing = Examinable(executor, thing);
    ITL itl;
    ItemToList_Init(&itl, buff, bufc, '#');
    for (i = low_bound; i <= high_bound; i++)
    {
        if (  control_thing
           || Examinable(executor, i))
        {
            if (  (  find_ex
                  && isExit(i)
                  && Location(i) == thing)
               || (  find_rm
                  && isRoom(i)
                  && Dropto(i) == thing)
               || (  find_th
                  && isThing(i)
                  && Home(i) == thing)
               || (  find_pl
                  && isPlayer(i)
                  && Home(i) == thing))
            {
                if (!ItemToList_AddInteger(&itl, i))
                {
                    break;
                }
            }
        }
    }
    ItemToList_Final(&itl);
}

/*
 * --------------------------------------------------------------------------
 * * fun_home: Return an object's home
 */

static FUNCTION(fun_home)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
    }
    else if (!Examinable(executor, it))
    {
        safe_noperm(buff, bufc);
    }
    else if (Has_home(it))
    {
        safe_tprintf_str(buff, bufc, T("#%d"), Home(it));
    }
    else if (Has_dropto(it))
    {
        safe_tprintf_str(buff, bufc, T("#%d"), Dropto(it));
    }
    else if (isExit(it))
    {
        safe_tprintf_str(buff, bufc, T("#%d"), where_is(it));
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_money: Return an object's value
 */

static FUNCTION(fun_money)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    if (Examinable(executor, it))
    {
        safe_ltoa(Pennies(it), buff, bufc);
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_pos: Find a word in a string
 */

static FUNCTION(fun_pos)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const unsigned char *pPat = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t nPat = strlen(reinterpret_cast<const char *>(pPat));
    const unsigned char *pStr = reinterpret_cast<const unsigned char *>(fargs[1]);
    size_t nStr = strlen(reinterpret_cast<const char *>(pStr));

    const unsigned char *match = co_search(pStr, nStr, pPat, nPat);
    if (nullptr != match)
    {
        // Count clusters in the prefix before the match position.
        //
        size_t nBytesBefore = static_cast<size_t>(match - pStr);
        size_t nClustersBefore = co_cluster_count(pStr, nBytesBefore);
        safe_ltoa(static_cast<long>(nClustersBefore + 1), buff, bufc);
    }
    else
    {
        safe_nothing(buff, bufc);
    }
}

/* ---------------------------------------------------------------------------
 * fun_lpos: Find all occurrences of a character in a string, and return
 * a space-separated list of the positions, starting at 0. i.e.,
 * lpos(a-bc-def-g,-) ==> 1 4 8
 */

static FUNCTION(fun_lpos)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const unsigned char *pStr = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t nStr = strlen(reinterpret_cast<const char *>(pStr));
    if (0 == nStr)
    {
        return;
    }

    const unsigned char *pPat = reinterpret_cast<const unsigned char *>(fargs[1]);
    size_t nPat = strlen(reinterpret_cast<const char *>(pPat));

    // Empty pattern defaults to space.
    //
    unsigned char spacebuf[2] = {' ', '\0'};
    if (0 == nPat)
    {
        pPat = spacebuf;
        nPat = 1;
    }

    bool bFirst = true;
    const unsigned char *pCur = pStr;
    size_t nRemain = nStr;

    while (nRemain > 0)
    {
        const unsigned char *match = co_search(pCur, nRemain, pPat, nPat);
        if (nullptr == match)
        {
            break;
        }

        if (!bFirst)
        {
            safe_chr(' ', buff, bufc);
        }
        bFirst = false;

        // Count clusters from the beginning of the string to this match.
        //
        size_t nBytesBefore = static_cast<size_t>(match - pStr);
        size_t nClustersBefore = co_cluster_count(pStr, nBytesBefore);
        safe_ltoa(static_cast<long>(nClustersBefore), buff, bufc);

        // Advance past this match by one visible code point.
        //
        const unsigned char *pe = pStr + nStr;
        const unsigned char *pNext = co_skip_color(match, pe);
        if (pNext < pe)
        {
            unsigned char ch = *pNext;
            size_t cplen;
            if (ch < 0x80)       cplen = 1;
            else if (ch < 0xE0)  cplen = 2;
            else if (ch < 0xF0)  cplen = 3;
            else                 cplen = 4;
            pNext += cplen;
        }
        nRemain = static_cast<size_t>(pe - pNext);
        pCur = pNext;
    }
}


/*
 * ---------------------------------------------------------------------------
 * * ldelete: Remove a word from a string by place
 * *  ldelete(<list>,<position>[,<separator>])
 * *
 * * insert: insert a word into a string by place
 * *  insert(<list>,<position>,<new item> [,<separator>])
 * *
 * * replace: replace a word into a string by place
 * *  replace(<list>,<position>,<new item>[,<separator>])
 */

#define IF_DELETE   0
#define IF_REPLACE  1
#define IF_INSERT   2

static void do_itemfuns(UTF8 *buff, UTF8 **bufc,
   const unsigned char *pList, size_t nListLen,
   int nPositions, int aPositions[],
   const unsigned char *pWord, size_t nWordLen,
   const SEP &sep, const SEP &osep, int flag)
{
    int j;
    if (nullptr == pList || 0 == nListLen)
    {
        if (IF_INSERT != flag || 0 == nListLen)
        {
            if (IF_INSERT != flag)
            {
                return;
            }
            // For an insert operation on an empty string, the only valid positions are 1 and -1.
            //
            bool fFoundOne = false;
            for (j = 0; j < nPositions; j++)
            {
                if (   1 == aPositions[j]
                   || -1 == aPositions[j])
                {
                    fFoundOne = true;
                    break;
                }
            }

            if (!fFoundOne)
            {
                return;
            }
        }
    }

    // Parse list into words using co_split_words.
    //
    size_t wstarts[LBUF_SIZE], wends[LBUF_SIZE];
    size_t nWords = co_split_words(pList, nListLen,
                        reinterpret_cast<const unsigned char *>(sep.str),
                        sep.n, wstarts, wends, LBUF_SIZE);

    // Remove positions which are out of bounds.
    //
    for (j = 0; j < nPositions; )
    {
        // Transform negative positions and translate to zero-origin.
        //
        if (aPositions[j] < 0)
        {
            if (IF_INSERT == flag)
            {
                aPositions[j] += static_cast<int>(nWords) + 1;
            }
            else
            {
                aPositions[j] += static_cast<int>(nWords);
            }
        }
        else
        {
            aPositions[j] -= 1;
        }

        if (  aPositions[j] < 0
           || (  static_cast<int>(nWords) <= aPositions[j]
              && (  flag != IF_INSERT
                 || static_cast<int>(nWords) < aPositions[j])))
        {
            // Remove position from list.
            //
            aPositions[j] = aPositions[nPositions - 1];
            nPositions--;
        }
        else
        {
            j++;
        }
    }

    // Sort remaining positions.
    //
    std::sort(aPositions, aPositions + nPositions);

    bool fFirst = true;
    size_t i = 0;

    for (j = 0; j < nPositions; j++)
    {
        while (i < static_cast<size_t>(aPositions[j]))
        {
            if (!fFirst)
            {
                print_sep(osep, buff, bufc);
            }
            else
            {
                fFirst = false;
            }
            size_t nb = wends[i] - wstarts[i];
            size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
            if (nb > nMax) nb = nMax;
            memcpy(*bufc, pList + wstarts[i], nb);
            *bufc += nb;
            i++;
        }

        if (IF_DELETE != flag)
        {
            if (!fFirst)
            {
                print_sep(osep, buff, bufc);
            }
            else
            {
                fFirst = false;
            }

            if (pWord && nWordLen > 0)
            {
                size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
                if (nWordLen > nMax) nWordLen = nMax;
                memcpy(*bufc, pWord, nWordLen);
                *bufc += nWordLen;
            }
        }

        if (IF_INSERT != flag)
        {
            i++;

            // For IF_DELETE and IF_REPLACE, skip duplicate positions. Once a
            // position has been deleted or replaced, it cannot be deleted or
            // replaced again.
            //
            while (  j < nPositions-1
                  && aPositions[j] == aPositions[j+1])
            {
                j++;
            }
        }
    }

    while (i < nWords)
    {
        if (!fFirst)
        {
            print_sep(osep, buff, bufc);
        }
        else
        {
            fFirst = false;
        }
        size_t nb = wends[i] - wstarts[i];
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        if (nb > nMax) nb = nMax;
        memcpy(*bufc, pList + wstarts[i], nb);
        *bufc += nb;
        i++;
    }
    **bufc = '\0';
}

int DecodeListOfIntegers(UTF8 *pIntegerList, int ai[])
{
    int n = 0;
    UTF8 *cp = trim_space_sep(pIntegerList, sepSpace);
    while (  cp
          && n < MAX_WORDS)
    {
        UTF8 *curr = split_token(&cp, sepSpace);
        ai[n++] = mux_atol(curr);
    }
    return n;
}

static FUNCTION(fun_ldelete)
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

    const unsigned char *pList = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t nListLen = strlen(reinterpret_cast<const char *>(fargs[0]));
    int ai[MAX_WORDS];
    int nai = DecodeListOfIntegers(fargs[1], ai);

    // Delete a word at position X of a list.
    //
    do_itemfuns(buff, bufc, pList, nListLen, nai, ai,
                nullptr, 0, sep, osep, IF_DELETE);
}

static FUNCTION(fun_replace)
{
    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(5, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }

    const unsigned char *pList = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t nListLen = strlen(reinterpret_cast<const char *>(fargs[0]));
    const unsigned char *pWord = reinterpret_cast<const unsigned char *>(fargs[2]);
    size_t nWordLen = strlen(reinterpret_cast<const char *>(fargs[2]));

    // Replace a word at position X of a list.
    //
    int ai[MAX_WORDS];
    int nai = DecodeListOfIntegers(fargs[1], ai);
    do_itemfuns(buff, bufc, pList, nListLen, nai, ai,
                pWord, nWordLen, sep, osep, IF_REPLACE);
}

static FUNCTION(fun_insert)
{
    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(5, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }

    const unsigned char *pList = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t nListLen = strlen(reinterpret_cast<const char *>(fargs[0]));
    const unsigned char *pWord = reinterpret_cast<const unsigned char *>(fargs[2]);
    size_t nWordLen = strlen(reinterpret_cast<const char *>(fargs[2]));

    int ai[MAX_WORDS];
    int nai = DecodeListOfIntegers(fargs[1], ai);

    // Insert a word at position X of a list.
    //
    do_itemfuns(buff, bufc, pList, nListLen, nai, ai,
                pWord, nWordLen, sep, osep, IF_INSERT);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_remove: Remove a word from a string
 */

static FUNCTION(fun_remove)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_INIT|DELIM_STRING))
    {
        return;
    }

    const unsigned char *pWord = reinterpret_cast<const unsigned char *>(fargs[1]);
    size_t nWordLen = strlen(reinterpret_cast<const char *>(fargs[1]));

    // Check that the word to remove doesn't contain the delimiter.
    //
    if (co_search(pWord, nWordLen,
                  reinterpret_cast<const unsigned char *>(sep.str), sep.n))
    {
        safe_str(T("#-1 CAN ONLY REMOVE ONE ELEMENT"), buff, bufc);
        return;
    }

    const unsigned char *pList = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t nListLen = strlen(reinterpret_cast<const char *>(fargs[0]));

    size_t wstarts[LBUF_SIZE], wends[LBUF_SIZE];
    size_t nWords = co_split_words(pList, nListLen,
                        reinterpret_cast<const unsigned char *>(sep.str),
                        sep.n, wstarts, wends, LBUF_SIZE);

    // Strip color from the word for comparison.
    //
    unsigned char wordPlain[LBUF_SIZE];
    size_t nWordPlain = co_strip_color(wordPlain, pWord, nWordLen);

    // Walk through the string copying words until (if ever) we get to
    // one that matches the target word.
    //
    bool bFirst = true, bFound = false;
    for (size_t i = 0; i < nWords; i++)
    {
        if (!bFound)
        {
            // Strip color from this word for comparison.
            //
            unsigned char wPlain[LBUF_SIZE];
            size_t nwp = co_strip_color(wPlain,
                             pList + wstarts[i],
                             wends[i] - wstarts[i]);

            if (nwp == nWordPlain
                && 0 == memcmp(wPlain, wordPlain, nwp))
            {
                bFound = true;
                continue;
            }
        }

        if (bFirst)
        {
            bFirst = false;
        }
        else
        {
            print_sep(osep, buff, bufc);
        }
        size_t nb = wends[i] - wstarts[i];
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        if (nb > nMax) nb = nMax;
        memcpy(*bufc, pList + wstarts[i], nb);
        *bufc += nb;
    }
    **bufc = '\0';
}

/*
 * ---------------------------------------------------------------------------
 * * fun_member: Is a word in a string
 */

static FUNCTION(fun_member)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    int wcount;
    UTF8 *r, *s;

    wcount = 1;
    s = trim_space_sep(fargs[0], sep);
    do
    {
        r = split_token(&s, sep);
        if (!strcmp(reinterpret_cast<char *>(fargs[1]), reinterpret_cast<char *>(r)))
        {
            safe_ltoa(wcount, buff, bufc);
            return;
        }
        wcount++;
    } while (s);
    safe_chr('0', buff, bufc);
}

// fun_secure: This function replaces any character in the set
// '%$\[](){},;' with a space. It handles ANSI by computing and
// preserving the color of each visual character in the string.
//
static FUNCTION(fun_secure)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    size_t nStr = strlen(reinterpret_cast<const char *>(fargs[0]));
    if (0 == nStr)
    {
        return;
    }

    // Replace $%(),;[\]{} with spaces via co_transform.
    //
    static const unsigned char from_set[] = "$%(),;[\\]{}";
    static const unsigned char to_set[]   = "           ";

    unsigned char out[LBUF_SIZE];
    size_t nOut = co_transform(out,
        reinterpret_cast<const unsigned char *>(fargs[0]), nStr,
        from_set, sizeof(from_set) - 1,
        to_set, sizeof(to_set) - 1);

    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    if (nOut > nMax) nOut = nMax;
    memcpy(*bufc, out, nOut);
    *bufc += nOut;
    **bufc = '\0';
}

// fun_escape: This function prepends a '\' to the beginning of a
// string and before any character which occurs in the set '%\[]{};,()^$'.
// It handles ANSI by computing and preserving the color of each
// visual character in the string.
//
static FUNCTION(fun_escape)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const unsigned char *p = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t slen = strlen(reinterpret_cast<const char *>(p));

    unsigned char out[LBUF_SIZE];
    size_t n = co_escape(out, p, slen);

    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    if (n > nMax) n = nMax;
    memcpy(*bufc, out, n);
    *bufc += n;
    **bufc = '\0';
}

/*
 * Take a character position and return which word that char is in.
 * * wordpos(<string>, <charpos>)
 */
static FUNCTION(fun_wordpos)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    size_t ncp;
    UTF8 *cp = strip_color(fargs[0], 0, &ncp);
    unsigned int charpos = mux_atol(fargs[1]);

    if (  charpos > 0
       && charpos <= ncp)
    {
        size_t ncp_trimmed;
        UTF8 *tp = &(cp[charpos - 1]);
        cp = trim_space_sep_LEN(cp, ncp, sep, &ncp_trimmed);
        UTF8 *xp = split_token(&cp, sep);

        int i;
        for (i = 1; xp; i++)
        {
            if (tp < xp + strlen(reinterpret_cast<char *>(xp)))
            {
                break;
            }
            xp = split_token(&cp, sep);
        }
        safe_ltoa(i, buff, bufc);
        return;
    }
    safe_nothing(buff, bufc);
}

static FUNCTION(fun_type)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    switch (Typeof(it))
    {
    case TYPE_ROOM:
        safe_str(T("ROOM"), buff, bufc);
        break;
    case TYPE_EXIT:
        safe_str(T("EXIT"), buff, bufc);
        break;
    case TYPE_PLAYER:
        safe_str(T("PLAYER"), buff, bufc);
        break;
    case TYPE_THING:
        safe_str(T("THING"), buff, bufc);
        break;
    default:
        safe_str(T("#-1 ILLEGAL TYPE"), buff, bufc);
    }
}

typedef struct
{
    const UTF8 *pName;
    int  iMask;
} ATR_HAS_FLAG_ENTRY;

static ATR_HAS_FLAG_ENTRY atr_has_flag_table[] =
{
    { T("dark"),       AF_DARK    },
    { T("wizard"),     AF_WIZARD  },
    { T("hidden"),     AF_MDARK   },
    { T("html"),       AF_HTML    },
    { T("locked"),     AF_LOCK    },
    { T("no_command"), AF_NOPROG  },
    { T("no_eval"),    AF_NOEVAL  },
    { T("no_name"),    AF_NONAME  },
    { T("no_parse"),   AF_NOPARSE },
    { T("regexp"),     AF_REGEXP  },
    { T("god"),        AF_GOD     },
    { T("visual"),     AF_VISUAL  },
    { T("no_inherit"), AF_PRIVATE },
    { T("const"),      AF_CONST   },
    { nullptr,      0     }
};

static bool atr_has_flag
(
    dbref player,
    dbref thing,
    ATTR* pattr,
    dbref aowner,
    int   aflags,
    const UTF8 *flagname
)
{
    UNUSED_PARAMETER(aowner);

    if (See_attr(player, thing, pattr))
    {
        ATR_HAS_FLAG_ENTRY *pEntry = atr_has_flag_table;
        while (pEntry->pName)
        {
            if (string_prefix(pEntry->pName, flagname))
            {
                return ((aflags & (pEntry->iMask)) ? true : false);
            }
            pEntry++;
        }
    }
    return false;
}

static FUNCTION(fun_hasflag)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it;
    ATTR *pattr;

    if (parse_attrib(executor, fargs[0], &it, &pattr))
    {
        if (  !pattr
           || !See_attr(executor, it, pattr))
        {
            safe_notfound(buff, bufc);
        }
        else
        {
            int aflags;
            dbref aowner;
            atr_pget_info(it, pattr->number, &aowner, &aflags);
            bool cc = atr_has_flag(executor, it, pattr, aowner, aflags, fargs[1]);
            safe_bool(cc, buff, bufc);
        }
    }
    else
    {
        it = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(it))
        {
            safe_match_result(it, buff, bufc);
        }
        else if (  mudconf.pub_flags
                || Examinable(executor, it)
                || it == enactor)
        {
            bool cc = has_flag(executor, it, fargs[1]);
            safe_bool(cc, buff, bufc);
        }
        else
        {
            safe_noperm(buff, bufc);
        }
    }
}

static FUNCTION(fun_haspower)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    if (  mudconf.pub_flags
       || Examinable(executor, it)
       || it == enactor)
    {
        safe_bool(has_power(executor, it, fargs[1]), buff, bufc);
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

#ifdef REALITY_LVLS
static FUNCTION(fun_hasrxlevel)
{
    dbref it;
    RLEVEL rl;

    it = match_thing(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_str(T("#-1 NOT FOUND"), buff, bufc);
        return;
    }
    rl = find_rlevel(fargs[1]);
    if (!rl)
    {
        safe_str(T("#-1 INVALID RLEVEL"), buff, bufc);
        return;
    }
    if (Examinable(executor, it))
    {
        if ((RxLevel(it) & rl) == rl)
        {
            safe_chr('1', buff, bufc);
        }
        else
        {
            safe_chr('0', buff, bufc);
        }
    }
    else
    {
        safe_str(T("#-1 PERMISSION DENIED"), buff, bufc);
    }
}

static FUNCTION(fun_hastxlevel)
{
    dbref it;
    RLEVEL rl;

    it = match_thing(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_str(T("#-1 NOT FOUND"), buff, bufc);
        return;
    }
    rl = find_rlevel(fargs[1]);
    if (!rl)
    {
        safe_str(T("#-1 INVALID RLEVEL"), buff, bufc);
        return;
    }
    if (Examinable(executor, it))
    {
        if ((TxLevel(it) & rl) == rl)
        {
            safe_chr('1', buff, bufc);
        }
        else
        {
             safe_chr('0', buff, bufc);
        }
    }
    else
    {
        safe_str(T("#-1 PERMISSION DENIED"), buff, bufc);
    }
}

static FUNCTION(fun_listrlevels)
{
    int i, add_space;

    int cmp_z;
    int cmp_x = sizeof(mudconf.reality_level);
    int cmp_y = sizeof(mudconf.reality_level[0]);
    if (0 == cmp_y)
    {
        cmp_z = 0;
    }
    else
    {
        cmp_z = cmp_x / cmp_y;
    }
    if (mudconf.no_levels < 1)
    {
        safe_str(T("#-1 NO REALITY LEVELS DEFINED"), buff, bufc);
    }
    else
    {
        for (add_space = i = 0; i < mudconf.no_levels && i < cmp_z; i++)
        {
            if (add_space)
            {
                safe_chr(' ', buff, bufc);
            }
            safe_str(mudconf.reality_level[i].name, buff, bufc);
            add_space = 1;
        }
    }
}

static FUNCTION(fun_rxlevel)
{
    dbref it;
    UTF8 levelbuff[2048];
    int i;
    RLEVEL lev;

    it = match_thing(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_str(T("#-1 NOT FOUND"), buff, bufc);
        return;
    }
    if (Examinable(executor, it))
    {
        lev = RxLevel(it);
        levelbuff[0]='\0';
        for (i = 0; i < mudconf.no_levels; ++i)
        {
            if ((lev & mudconf.reality_level[i].value) == mudconf.reality_level[i].value)
            {
                strcat(reinterpret_cast<char *>(levelbuff), reinterpret_cast<char *>(mudconf.reality_level[i].name));
                strcat(reinterpret_cast<char *>(levelbuff), " ");
            }
        }
        safe_tprintf_str(buff, bufc, T("%s"), levelbuff);
    }
    else
    {
        safe_str(T("#-1 PERMISSION DENIED"), buff, bufc);
    }
}

static FUNCTION(fun_txlevel)
{
    dbref it;
    UTF8 levelbuff[2048];
    int i;
    RLEVEL lev;

    it = match_thing(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_str(T("#-1 NOT FOUND"), buff, bufc);
        return;
    }
    if (Examinable(executor, it))
    {
        lev = TxLevel(it);
        levelbuff[0]='\0';
        for (i = 0; i < mudconf.no_levels; ++i)
        {
            if ((lev & mudconf.reality_level[i].value) == mudconf.reality_level[i].value)
            {
                strcat(reinterpret_cast<char *>(levelbuff), reinterpret_cast<char *>(mudconf.reality_level[i].name));
                strcat(reinterpret_cast<char *>(levelbuff), " ");
            }
        }
        safe_tprintf_str(buff, bufc, T("%s"), levelbuff);
    }
    else
    {
        safe_str(T("#-1 PERMISSION DENIED"), buff, bufc);
    }
}
#endif // REALITY_LVLS

static FUNCTION(fun_powers)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    if (  mudconf.pub_flags
       || Examinable(executor, it)
       || it == enactor)
    {
        UTF8 *buf = powers_list(executor, it);
        safe_str(buf, buff, bufc);
        free_lbuf(buf);
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

static FUNCTION(fun_delete)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int iStart = mux_atol(fargs[1]);
    int nDelete = mux_atol(fargs[2]);

    if (nDelete < 0)
    {
        // The range should end at iStart, inclusive.
        //
        iStart += 1 + nDelete;
        nDelete = -nDelete;
    }

    if (iStart < 0)
    {
        // Start at the beginning of the string,
        // but end at the same place the range would end
        // if negative starting positions were valid.
        //
        nDelete += iStart;
        iStart = 0;
    }

    if (  nDelete <= 0
       || LBUF_SIZE <= iStart)
    {
        // The range doesn't select any characters.
        //
        safe_str(fargs[0], buff, bufc);
        return;
    }

    // At this point, iStart and nDelete are nonnegative numbers
    // which may -still- not refer to valid data in the string.
    //
    const unsigned char *p = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t slen = strlen(reinterpret_cast<const char *>(p));

    unsigned char out[LBUF_SIZE];
    size_t n = co_delete_cluster(out, p, slen,
        static_cast<size_t>(iStart), static_cast<size_t>(nDelete));

    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    if (n > nMax) n = nMax;
    memcpy(*bufc, out, n);
    *bufc += n;
    **bufc = '\0';
}

static FUNCTION(fun_lock)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it, aowner;
    int aflags;
    ATTR *pattr;
    struct boolexp *pBoolExp;

    // Parse the argument into obj + lock
    //
    if (!get_obj_and_lock(executor, fargs[0], &it, &pattr, buff, bufc))
    {
        return;
    }

    // Side-effect: set or clear the lock when a second argument is given.
    //
    if (1 < nfargs)
    {
        if (check_command(executor, T("@lock"), buff, bufc))
        {
            return;
        }
        if (!Controls(executor, it))
        {
            safe_noperm(buff, bufc);
            return;
        }
        if (NoModify(it) && !WizRoy(executor))
        {
            safe_noperm(buff, bufc);
            return;
        }
        if ('\0' == fargs[1][0])
        {
            // Empty second arg: clear the lock.
            //
            atr_clr(it, pattr->number);
            set_modified(it);
        }
        else
        {
            // Non-empty second arg: set the lock.
            //
            struct boolexp *okey = parse_boolexp(executor, fargs[1], false);
            if (okey == TRUE_BOOLEXP)
            {
                safe_str(T("#-1 I DON'T UNDERSTAND THAT KEY"), buff, bufc);
                return;
            }
            atr_add_raw(it, pattr->number,
                unparse_boolexp_quiet(executor, okey));
            free_boolexp(okey);
        }
    }

    // Get the attribute and decode it if we can read it
    //
    UTF8 *tbuf = atr_get("fun_lock.4685", it, pattr->number, &aowner, &aflags);
    if (bCanReadAttr(executor, it, pattr, false))
    {
        pBoolExp = parse_boolexp(executor, tbuf, true);
        free_lbuf(tbuf);
        tbuf = unparse_boolexp_function(executor, pBoolExp);
        free_boolexp(pBoolExp);
        safe_str(tbuf, buff, bufc);
    }
    else
    {
        free_lbuf(tbuf);
    }
}

static FUNCTION(fun_elock)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it, aowner;
    int aflags;
    ATTR *pattr;
    struct boolexp *pBoolExp;

    // Parse lock supplier into obj + lock.
    //
    if (!get_obj_and_lock(executor, fargs[0], &it, &pattr, buff, bufc))
    {
        return;
    }
    else if (!locatable(executor, it, enactor))
    {
        safe_nothing(buff, bufc);
    }

    // Get the victim and ensure we can do it.
    //
    dbref victim = match_thing_quiet(executor, fargs[1]);
    if (!Good_obj(victim))
    {
        safe_match_result(victim, buff, bufc);
    }
    else if (!locatable(executor, victim, enactor))
    {
        safe_nothing(buff, bufc);
    }
    else if (  nearby_or_control(executor, victim)
            || nearby_or_control(executor, it))
    {
        UTF8 *tbuf = atr_get("fun_elock.4738", it, pattr->number, &aowner, &aflags);
        if (  pattr->number == A_LOCK
           || bCanReadAttr(executor, it, pattr, false))
        {
            pBoolExp = parse_boolexp(executor, tbuf, true);
            safe_bool(eval_boolexp(victim, it, it, pBoolExp), buff, bufc);
            free_boolexp(pBoolExp);
        }
        else
        {
            safe_chr('0', buff, bufc);
        }
        free_lbuf(tbuf);
    }
    else
    {
        safe_str(T("#-1 TOO FAR AWAY"), buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// lockencode(): Validate and normalize a lock expression to canonical form.
//
// lockencode(<lock-string>)
//
// Parses the lock expression using the standard lock parser (with name
// matching), then returns it in the quiet/canonical form with (#N) dbrefs.
// Returns #-1 error if the expression is invalid.
//
static FUNCTION(fun_lockencode)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (fargs[0][0] == '\0')
    {
        return;
    }

    BOOLEXP *okey = parse_boolexp(executor, fargs[0], false);
    if (okey == TRUE_BOOLEXP)
    {
        safe_str(T("#-1 I DON'T UNDERSTAND THAT KEY"), buff, bufc);
        return;
    }

    safe_str(unparse_boolexp_quiet(executor, okey), buff, bufc);
    free_boolexp(okey);
}

// ---------------------------------------------------------------------------
// lockdecode(): Convert a canonical lock string to human-readable form.
//
// lockdecode(<encoded-string>)
//
// Parses a lock expression in internal (#N) format and returns it in
// the function format with *PlayerName notation.
//
static FUNCTION(fun_lockdecode)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (fargs[0][0] == '\0')
    {
        return;
    }

    BOOLEXP *okey = parse_boolexp(executor, fargs[0], true);
    if (okey == TRUE_BOOLEXP)
    {
        safe_str(T("#-1 I DON'T UNDERSTAND THAT KEY"), buff, bufc);
        return;
    }

    UTF8 *tbuf = unparse_boolexp_function(executor, okey);
    safe_str(tbuf, buff, bufc);
    free_boolexp(okey);
}

// ---------------------------------------------------------------------------
// mailsend(): Send mail from softcode.
//
// mailsend(<recipients>, <subject>, <message>)
//
// Sends mail as the executor to the named recipients.  Returns 1 on success,
// or a #-1 error string on failure.
//
static FUNCTION(fun_mailsend)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (check_command(executor, T("@mail"), buff, bufc))
    {
        return;
    }

    // make_numlist modifies its input, so copy recipients.
    //
    UTF8 *recipients = alloc_lbuf("fun_mailsend.recip");
    mux_strncpy(recipients, fargs[0], LBUF_SIZE-1);

    const UTF8 *err = do_mail_send_softcode(executor, recipients, fargs[1], fargs[2]);
    free_lbuf(recipients);

    if (err)
    {
        safe_str(err, buff, bufc);
    }
    else
    {
        safe_chr('1', buff, bufc);
    }
}

/* ---------------------------------------------------------------------------
 * fun_lwho: Return list of connected users.
 */

static FUNCTION(fun_lwho)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool bPorts = false;
    if (nfargs == 1)
    {
        bPorts = xlate(fargs[0]);
        if (  bPorts
           && !Wizard(executor))
        {
            safe_noperm(buff, bufc);
            return;
        }
    }
    make_ulist(executor, buff, bufc, bPorts);
}

// ---------------------------------------------------------------------------
// fun_lports: Return list of ports of connected users.
// ---------------------------------------------------------------------------

static FUNCTION(fun_lports)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    make_port_ulist(executor, buff, bufc);
}

/* ---------------------------------------------------------------------------
 * fun_nearby: Return whether or not obj1 is near obj2.
 */

static FUNCTION(fun_nearby)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref obj1 = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(obj1))
    {
        safe_match_result(obj1, buff, bufc);
        safe_str(T(" (ARG1)"), buff, bufc);
        return;
    }
    dbref obj2 = match_thing_quiet(executor, fargs[1]);
    if (!Good_obj(obj2))
    {
        safe_match_result(obj2, buff, bufc);
        safe_str(T(" (ARG2)"), buff, bufc);
        return;
    }
    bool bResult = (  (  nearby_or_control(executor, obj1)
                      || nearby_or_control(executor, obj2))
                      && nearby(obj1, obj2));
    safe_bool(bResult, buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_obj, fun_poss, and fun_subj: perform pronoun sub for object.
 */

static void process_sex(dbref player, UTF8 *what, UTF8 *token, UTF8 *buff, UTF8 **bufc)
{
    dbref it = match_thing_quiet(player, strip_color(what));
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    if (  !isPlayer(it)
       && !nearby_or_control(player, it))
    {
        safe_nomatch(buff, bufc);
    }
    else
    {
        mux_exec(token, LBUF_SIZE-1, buff, bufc, it, it, it, EV_EVAL, nullptr, 0);
    }
}

static FUNCTION(fun_obj)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    process_sex(executor, fargs[0], const_cast<UTF8 *>(T("%o")), buff, bufc);
}

static FUNCTION(fun_poss)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    process_sex(executor, fargs[0], const_cast<UTF8 *>(T("%p")), buff, bufc);
}

static FUNCTION(fun_subj)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    process_sex(executor, fargs[0], const_cast<UTF8 *>(T("%s")), buff, bufc);
}

static FUNCTION(fun_aposs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    process_sex(executor, fargs[0], const_cast<UTF8 *>(T("%a")), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_mudname: Return the name of the mud.
 */

static FUNCTION(fun_mudname)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str(mudconf.mud_name, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_connrecord: Return the record number of connected players.
// ---------------------------------------------------------------------------

static FUNCTION(fun_connrecord)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_ltoa(mudstate.record_players, buff, bufc);
}

/*! \brief Return current function invocation counter.
 *
 * If no argument is given, FCOUNT() returns the current function invocation
 * counter which represents the number of functions invoked since the
 * beginning of the queue cycle.
 *
 * If given an argument, FCOUNT() returns the number of invocations required
 * to evaluate the argument.
 *
 */

FUNCTION(fun_fcount)
{
    if (0 == nfargs)
    {
        safe_ltoa(mudstate.func_invk_ctr, buff, bufc);
    }
    else if (1 == nfargs)
    {
        long ficBefore = mudstate.func_invk_ctr;
        UTF8 *bufc_save = *bufc;
        mux_exec(fargs[0], LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
            eval|EV_FCHECK|EV_STRIP_CURLY|EV_EVAL, cargs, ncargs);
        long ficAfter = mudstate.func_invk_ctr;
        *bufc = bufc_save;
        safe_ltoa(ficAfter-ficBefore, buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_fdepth: Return the current function nesting depth.
// ---------------------------------------------------------------------------

FUNCTION(fun_fdepth)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_ltoa(mudstate.func_nest_lev, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_trace: Return the current function nesting depth.
// ---------------------------------------------------------------------------

FUNCTION(fun_trace)
{
    mux_exec(fargs[0], LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
        eval|EV_FCHECK|EV_STRIP_CURLY|EV_EVAL|EV_TRACE, cargs, ncargs);
}

// ---------------------------------------------------------------------------
// fun_ctime: Return the value of an object's CREATED attribute.
// ---------------------------------------------------------------------------

static FUNCTION(fun_ctime)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref thing;
    if (nfargs == 1)
    {
        thing = match_thing_quiet(executor, fargs[0]);
    }
    else
    {
        thing = executor;
    }
    if (!Good_obj(thing))
    {
        safe_match_result(thing, buff, bufc);
    }
    else if (Examinable(executor, thing))
    {
        safe_str(atr_get_raw(thing, A_CREATED), buff, bufc);
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_mtime: Return the value of an object's Modified attribute.
// ---------------------------------------------------------------------------

static FUNCTION(fun_mtime)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref thing;
    if (nfargs == 1)
    {
        thing = match_thing_quiet(executor, fargs[0]);
    }
    else
    {
        thing = executor;
    }
    if (!Good_obj(thing))
    {
        safe_match_result(thing, buff, bufc);
    }
    else if (Examinable(executor, thing))
    {
        safe_str(atr_get_raw(thing, A_MODIFIED), buff, bufc);
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_moniker: Return the value of an object's @moniker attribute.
// ---------------------------------------------------------------------------
static FUNCTION(fun_moniker)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref thing;
    if (nfargs == 1)
    {
        thing = match_thing_quiet(executor, fargs[0]);
    }
    else
    {
        thing = executor;
    }
    if (!Good_obj(thing))
    {
        safe_match_result(thing, buff, bufc);
        return;
    }
    safe_str(Moniker(thing), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_lcstr, fun_ucstr, fun_capstr: Lowercase, uppercase, or capitalize str.
 */

static FUNCTION(fun_lcstr)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    size_t nLen = strlen(reinterpret_cast<const char *>(fargs[0]));
    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    unsigned char out[LBUF_SIZE];
    size_t n = co_tolower(out, reinterpret_cast<const unsigned char *>(fargs[0]), nLen);
    if (n > nMax) n = nMax;
    memcpy(*bufc, out, n);
    *bufc += n;
    **bufc = '\0';
}

static FUNCTION(fun_ucstr)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    size_t nLen = strlen(reinterpret_cast<const char *>(fargs[0]));
    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    unsigned char out[LBUF_SIZE];
    size_t n = co_toupper(out, reinterpret_cast<const unsigned char *>(fargs[0]), nLen);
    if (n > nMax) n = nMax;
    memcpy(*bufc, out, n);
    *bufc += n;
    **bufc = '\0';
}

static FUNCTION(fun_capstr)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    size_t nLen = strlen(reinterpret_cast<const char *>(fargs[0]));
    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    unsigned char out[LBUF_SIZE];
    size_t n = co_totitle(out, reinterpret_cast<const unsigned char *>(fargs[0]), nLen);
    if (n > nMax) n = nMax;
    memcpy(*bufc, out, n);
    *bufc += n;
    **bufc = '\0';
}

/*
 * ---------------------------------------------------------------------------
 * * fun_lnum: Return a list of numbers.
 */
static FUNCTION(fun_lnum)
{
    SEP sep;
    if (  nfargs == 0
       || !OPTIONAL_DELIM(3, sep, DELIM_NULL|DELIM_CRLF|DELIM_STRING))
    {
        return;
    }

    int bot = 0, top;
    int step = 1;
    if (nfargs == 1)
    {
        top = mux_atol(fargs[0]) - 1;
        if (top < 0)
        {
            return;
        }
    }
    else
    {
        bot = mux_atol(fargs[0]);
        top = mux_atol(fargs[1]);
        if (nfargs == 4)
        {
            step = mux_atol(fargs[3]);
            if (step < 1)
            {
                step = 1;
            }
        }
    }

    int i;
    if (bot == top)
    {
        safe_ltoa(bot, buff, bufc);
    }
    else if (bot < top)
    {
        safe_ltoa(bot, buff, bufc);
        for (i = bot+step; i <= top; i += step)
        {
            print_sep(sep, buff, bufc);
            UTF8 *p = *bufc;
            safe_ltoa(i, buff, bufc);
            if (p == *bufc)
            {
                return;
            }
        }
    }
    else if (top < bot)
    {
        safe_ltoa(bot, buff, bufc);
        for (i = bot-step; i >= top; i -= step)
        {
            print_sep(sep, buff, bufc);
            UTF8 *p = *bufc;
            safe_ltoa(i, buff, bufc);
            if (p == *bufc)
            {
                return;
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 * fun_lattr, fun_lattrp: Return list of attributes I can see on the object.
 */

static void lattr_handler(UTF8 *buff, UTF8 **bufc, dbref executor, UTF8 *fargs[],
                   bool bCheckParent)
{
    dbref thing;
    int ca;
    bool bFirst;

    // Check for wildcard matching.  parse_attrib_wild checks for read
    // permission, so we don't have to.  Have p_a_w assume the
    // slash-star if it is missing.
    //
    bFirst = true;
    olist_push();
    if (parse_attrib_wild(executor, fargs[0], &thing, bCheckParent, false, true))
    {
        for (ca = olist_first(); ca != NOTHING; ca = olist_next())
        {
            ATTR *pattr = atr_num(ca);
            if (pattr)
            {
                if (!bFirst)
                {
                    safe_chr(' ', buff, bufc);
                }
                bFirst = false;
                safe_str(pattr->name, buff, bufc);
            }
        }
    }
    else
    {
        safe_nomatch(buff, bufc);
    }
    olist_pop();
}

static FUNCTION(fun_lattr)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    lattr_handler(buff, bufc, executor, fargs, false);
}

static FUNCTION(fun_lattrp)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    lattr_handler(buff, bufc, executor, fargs, true);
}
// ---------------------------------------------------------------------------
// fun_attrcnt: Return number of attributes I can see on the object.
// ---------------------------------------------------------------------------

static FUNCTION(fun_attrcnt)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref thing;
    int ca, count = 0;

    // Mechanism from lattr.
    //
    olist_push();
    if (parse_attrib_wild(executor, fargs[0], &thing, false, false, true))
    {
        for (ca = olist_first(); ca != NOTHING; ca = olist_next())
        {
            ATTR *pattr = atr_num(ca);
            if (pattr)
            {
                count++;
            }
        }
        safe_ltoa(count, buff, bufc);
    }
    else
    {
        safe_nomatch(buff, bufc);
    }
    olist_pop();
}

// ---------------------------------------------------------------------------
// dynhelp(): Retrieve help text from attributes on an object.
//
// dynhelp(<object>, <topic>[, <prefix>])
//
// Looks for attribute <PREFIX><TOPIC> on <object>.  Default prefix is HELP_.
// Supports prefix matching: if no exact match, finds unique prefix match
// among attributes starting with <prefix>.
//
static FUNCTION(fun_dynhelp)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref thing = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(thing))
    {
        safe_match_result(thing, buff, bufc);
        return;
    }

    if (!Examinable(executor, thing))
    {
        safe_noperm(buff, bufc);
        return;
    }

    // Determine the prefix (default HELP_).
    //
    const UTF8 *prefix = T("HELP_");
    if (3 <= nfargs && fargs[2][0] != '\0')
    {
        prefix = fargs[2];
    }

    // Build the full attribute name: PREFIX + TOPIC, uppercased.
    //
    UTF8 fullname[SBUF_SIZE];
    UTF8 *fnp = fullname;
    safe_str(prefix, fullname, &fnp);
    safe_str(fargs[1], fullname, &fnp);
    *fnp = '\0';

    size_t nFullname;
    UTF8 *pUpper = mux_strupr(fullname, nFullname);

    // Try exact match first.
    //
    ATTR *pattr = atr_str(pUpper);
    if (pattr && See_attr(executor, thing, pattr))
    {
        dbref aowner;
        int   aflags;
        UTF8 *tbuf = atr_get("fun_dynhelp", thing, pattr->number,
            &aowner, &aflags);
        safe_str(tbuf, buff, bufc);
        free_lbuf(tbuf);
        return;
    }

    // No exact match.  Try prefix matching among attributes starting
    // with the prefix.
    //
    size_t nPrefix;
    UTF8 prefixUpper[SBUF_SIZE];
    mux_strncpy(prefixUpper, mux_strupr(prefix, nPrefix), SBUF_SIZE-1);

    size_t nTopic;
    UTF8 topicUpper[SBUF_SIZE];
    mux_strncpy(topicUpper, mux_strupr(fargs[1], nTopic), SBUF_SIZE-1);

    ATTR *match = nullptr;
    bool bAmbiguous = false;

    atr_push();
    unsigned char *as;
    for (int ca = atr_head(thing, &as); ca; ca = atr_next(&as))
    {
        ATTR *pa = atr_num(ca);
        if (!pa || !See_attr(executor, thing, pa))
        {
            continue;
        }

        // Check if attr name starts with the prefix.
        //
        if (mux_memicmp(pa->name, prefixUpper, nPrefix) != 0)
        {
            continue;
        }

        // Get the topic portion (after the prefix).
        //
        const UTF8 *attrTopic = pa->name + nPrefix;
        size_t nAttrTopic = strlen(reinterpret_cast<const char *>(attrTopic));

        // Check if our topic is a prefix of this attribute's topic.
        //
        if (  nTopic <= nAttrTopic
           && mux_memicmp(attrTopic, topicUpper, nTopic) == 0)
        {
            if (nTopic == nAttrTopic)
            {
                // Exact match — use it immediately.
                //
                match = pa;
                bAmbiguous = false;
                break;
            }
            if (nullptr == match)
            {
                match = pa;
            }
            else
            {
                bAmbiguous = true;
            }
        }
    }
    atr_pop();

    if (bAmbiguous)
    {
        safe_str(T("#-1 AMBIGUOUS TOPIC"), buff, bufc);
        return;
    }

    if (nullptr == match)
    {
        safe_str(T("#-1 NO SUCH TOPIC"), buff, bufc);
        return;
    }

    dbref aowner;
    int   aflags;
    UTF8 *tbuf = atr_get("fun_dynhelp", thing, match->number,
        &aowner, &aflags);
    safe_str(tbuf, buff, bufc);
    free_lbuf(tbuf);
}

// ---------------------------------------------------------------------------
// reglattr_handler: Return list or count of attributes matching a regex.
//
// reglattr(obj, regexp)   — list matching attr names
// regnattr(obj, regexp)   — count matching attr names
// reglattri(obj, regexp)  — case-insensitive list
// regnattri(obj, regexp)  — case-insensitive count
// ---------------------------------------------------------------------------

static void reglattr_handler(UTF8 *buff, UTF8 **bufc, dbref executor,
    UTF8 *fargs[], bool bCount, bool bCaseInsens)
{
    dbref thing = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(thing))
    {
        safe_match_result(thing, buff, bufc);
        return;
    }

    if (!Examinable(executor, thing))
    {
        safe_noperm(buff, bufc);
        return;
    }

    if (!fargs[1] || !*fargs[1])
    {
        safe_str(T("#-1 INVALID REGEXP"), buff, bufc);
        return;
    }

    PCRE2_SIZE erroffset;
    int errcode;
    uint32_t options = PCRE2_UTF;
    if (bCaseInsens)
    {
        options |= PCRE2_CASELESS;
    }

    pcre2_code *re = pcre2_compile_8(
        fargs[1],
        PCRE2_ZERO_TERMINATED,
        options,
        &errcode,
        &erroffset,
        nullptr);

    if (!re)
    {
        PCRE2_UCHAR errbuf[256];
        pcre2_get_error_message(errcode, errbuf, sizeof(errbuf));
        safe_str(T("#-1 REGEXP ERROR "), buff, bufc);
        safe_str(reinterpret_cast<UTF8 *>(errbuf), buff, bufc);
        return;
    }

    pcre2_match_data *match_data = pcre2_match_data_create_from_pattern(re, nullptr);
    if (!match_data)
    {
        pcre2_code_free(re);
        safe_str(T("#-1 REGEXP MATCH DATA ERROR"), buff, bufc);
        return;
    }

    bool bFirst = true;
    int count = 0;

    atr_push();
    unsigned char *as;
    for (int ca = atr_head(thing, &as); ca; ca = atr_next(&as))
    {
        ATTR *pattr = atr_num(ca);
        if (!pattr)
        {
            continue;
        }

        if (!See_attr(executor, thing, pattr))
        {
            continue;
        }

        int rc = pcre2_match(re, pattr->name,
            PCRE2_ZERO_TERMINATED, 0, 0, match_data, nullptr);
        if (rc >= 0)
        {
            if (bCount)
            {
                count++;
            }
            else
            {
                if (!bFirst)
                {
                    safe_chr(' ', buff, bufc);
                }
                bFirst = false;
                safe_str(pattr->name, buff, bufc);
            }
        }
    }
    atr_pop();

    pcre2_match_data_free(match_data);
    pcre2_code_free(re);

    if (bCount)
    {
        safe_ltoa(count, buff, bufc);
    }
}

static FUNCTION(fun_reglattr)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    reglattr_handler(buff, bufc, executor, fargs, false, false);
}

static FUNCTION(fun_reglattri)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    reglattr_handler(buff, bufc, executor, fargs, false, true);
}

static FUNCTION(fun_regnattr)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    reglattr_handler(buff, bufc, executor, fargs, true, false);
}

static FUNCTION(fun_regnattri)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    reglattr_handler(buff, bufc, executor, fargs, true, true);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_reverse, fun_revwords: Reverse things.
 */

static FUNCTION(fun_reverse)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    size_t nLen = strlen(reinterpret_cast<const char *>(fargs[0]));
    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    unsigned char out[LBUF_SIZE];
    size_t n = co_reverse(out, reinterpret_cast<const unsigned char *>(fargs[0]), nLen);
    if (n > nMax) n = nMax;
    memcpy(*bufc, out, n);
    *bufc += n;
    **bufc = '\0';
}

static FUNCTION(fun_revwords)
{
    // If we are passed an empty arglist return a null string.
    //
    if (nfargs == 0)
    {
        return;
    }

    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(3, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }

    if (1 == sep.n && 1 == osep.n)
    {
        // Single-char delimiter: use co_words_count + co_extract in reverse.
        //
        UTF8 *bp = trim_space_sep(fargs[0], sep);
        const unsigned char *p = reinterpret_cast<const unsigned char *>(bp);
        size_t slen = strlen(reinterpret_cast<const char *>(p));
        unsigned char delim = static_cast<unsigned char>(sep.str[0]);
        unsigned char out_delim = static_cast<unsigned char>(osep.str[0]);

        size_t nWords = co_words_count(p, slen, delim);
        bool bFirst = true;
        for (size_t i = 0; i < nWords; i++)
        {
            if (!bFirst)
            {
                safe_chr(static_cast<UTF8>(out_delim), buff, bufc);
            }
            else
            {
                bFirst = false;
            }

            unsigned char word[LBUF_SIZE];
            size_t nWord = co_extract(word, p, slen,
                nWords - i, 1, delim, delim);

            size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
            if (nWord > nMax) nWord = nMax;
            memcpy(*bufc, word, nWord);
            *bufc += nWord;
        }
        **bufc = '\0';
    }
    else
    {
        // Multi-char delimiter: use co_split_words.
        //
        const unsigned char *pData = reinterpret_cast<const unsigned char *>(fargs[0]);
        size_t nLen = strlen(reinterpret_cast<const char *>(fargs[0]));
        size_t wstarts[LBUF_SIZE], wends[LBUF_SIZE];
        size_t nWords = co_split_words(pData, nLen,
                            reinterpret_cast<const unsigned char *>(sep.str),
                            sep.n, wstarts, wends, LBUF_SIZE);

        bool bFirst = true;
        for (size_t i = 0; i < nWords; i++)
        {
            if (!bFirst)
            {
                print_sep(osep, buff, bufc);
            }
            else
            {
                bFirst = false;
            }
            size_t w = nWords - i - 1;
            size_t nb = wends[w] - wstarts[w];
            size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
            if (nb > nMax) nb = nMax;
            memcpy(*bufc, pData + wstarts[w], nb);
            *bufc += nb;
        }
        **bufc = '\0';
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_after, fun_before: Return substring after or before a specified string.
 */

static FUNCTION(fun_after)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Default pattern is a single space.
    //
    const unsigned char *pat = reinterpret_cast<const unsigned char *>(fargs[1]);
    size_t plen = (1 < nfargs) ? strlen(reinterpret_cast<const char *>(fargs[1])) : 0;

    UTF8 *bp = fargs[0];
    if (0 == plen)
    {
        pat = reinterpret_cast<const unsigned char *>(" ");
        plen = 1;
        bp = trim_space_sep(bp, sepSpace);
    }

    size_t slen = strlen(reinterpret_cast<const char *>(bp));
    const unsigned char *str = reinterpret_cast<const unsigned char *>(bp);

    const unsigned char *match = co_search(str, slen, pat, plen);
    if (nullptr != match)
    {
        // Advance past the matched visible code points.
        //
        size_t nPatVis = co_visible_length(pat, plen);
        const unsigned char *after = co_visible_advance(match,
            str + slen, nPatVis, nullptr);

        size_t nBytes = static_cast<size_t>((str + slen) - after);
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        if (nBytes > nMax) nBytes = nMax;
        memcpy(*bufc, after, nBytes);
        *bufc += nBytes;
        **bufc = '\0';
    }
}

static FUNCTION(fun_before)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Default pattern is a single space.
    //
    const unsigned char *pat = reinterpret_cast<const unsigned char *>(fargs[1]);
    size_t plen = (1 < nfargs) ? strlen(reinterpret_cast<const char *>(fargs[1])) : 0;

    UTF8 *bp = fargs[0];
    if (0 == plen)
    {
        pat = reinterpret_cast<const unsigned char *>(" ");
        plen = 1;
        bp = trim_space_sep(bp, sepSpace);
    }

    size_t slen = strlen(reinterpret_cast<const char *>(bp));
    const unsigned char *str = reinterpret_cast<const unsigned char *>(bp);

    const unsigned char *match = co_search(str, slen, pat, plen);
    if (nullptr != match)
    {
        // Return everything before the match.
        //
        size_t nBytes = static_cast<size_t>(match - str);
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        if (nBytes > nMax) nBytes = nMax;
        memcpy(*bufc, str, nBytes);
        *bufc += nBytes;
        **bufc = '\0';
    }
    else
    {
        // Pattern not found — return entire string.
        //
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        if (slen > nMax) slen = nMax;
        memcpy(*bufc, str, slen);
        *bufc += slen;
        **bufc = '\0';
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_search: Search the db for things, returning a list of what matches
 */

static FUNCTION(fun_search)
{
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UTF8 *pArg = nullptr;
    if (nfargs != 0)
    {
        pArg = fargs[0];
    }

    // Set up for the search.  If any errors, abort.
    //
    SEARCH searchparm;
    if (!search_setup(executor, pArg, &searchparm))
    {
        safe_str(T("#-1 ERROR DURING SEARCH"), buff, bufc);
        return;
    }

    // Do the search and report the results.
    //
    olist_push();
    search_perform(executor, caller, enactor, &searchparm);
    dbref thing;
    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    for (thing = olist_first(); thing != NOTHING; thing = olist_next())
    {
        if (!ItemToList_AddInteger(&pContext, thing))
        {
            break;
        }
    }
    ItemToList_Final(&pContext);
    olist_pop();
}

/*
 * ---------------------------------------------------------------------------
 * * fun_stats: Get database size statistics.
 */

static FUNCTION(fun_stats)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref who;

    if (  nfargs == 0
       || (!fargs[0])
       || !*fargs[0]
       || !string_compare(fargs[0], T("all")))
    {
        who = NOTHING;
    }
    else
    {
        who = lookup_player(executor, fargs[0], true);
        if (who == NOTHING)
        {
            safe_str(T("#-1 PLAYER NOT FOUND"), buff, bufc);
            return;
        }
    }
    STATS statinfo;
    if (!get_stats(executor, who, &statinfo))
    {
        safe_str(T("#-1 ERROR GETTING STATS"), buff, bufc);
        return;
    }
    safe_tprintf_str(buff, bufc, T("%d %d %d %d %d %d"), statinfo.s_total, statinfo.s_rooms,
            statinfo.s_exits, statinfo.s_things, statinfo.s_players, statinfo.s_garbage);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_merge:  given two strings and a character, merge the two strings
 * *   by replacing characters in string1 that are the same as the given
 * *   character by the corresponding character in string2 (by position).
 * *   The strings must be of the same length.
 */

static FUNCTION(fun_merge)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    size_t n;
    if (  !utf8_strlen(fargs[2], n)
       || 1 < n)
    {
        safe_str(T("#-1 TOO MANY CHARACTERS"), buff, bufc);
        return;
    }

    const unsigned char *pA = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t lenA = strlen(reinterpret_cast<const char *>(pA));
    const unsigned char *pB = reinterpret_cast<const unsigned char *>(fargs[1]);
    size_t lenB = strlen(reinterpret_cast<const char *>(pB));
    const unsigned char *pC = reinterpret_cast<const unsigned char *>(fargs[2]);
    size_t lenC = strlen(reinterpret_cast<const char *>(pC));

    // Check visible lengths are equal.
    //
    size_t vlenA = co_visible_length(pA, lenA);
    size_t vlenB = co_visible_length(pB, lenB);
    if (vlenA != vlenB)
    {
        safe_str(T("#-1 STRING LENGTHS MUST BE EQUAL"), buff, bufc);
    }
    else
    {
        unsigned char out[LBUF_SIZE];
        size_t nOut = co_merge(out, pA, lenA, pB, lenB, pC, lenC);

        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        if (nOut > nMax) nOut = nMax;
        memcpy(*bufc, out, nOut);
        *bufc += nOut;
        **bufc = '\0';
    }
}

/* ---------------------------------------------------------------------------
 * fun_splice: similar to MERGE(), eplaces by word instead of by character.
 */

static FUNCTION(fun_splice)
{
    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    SEP osep = sep;
    if (!OPTIONAL_DELIM(5, osep, DELIM_NULL|DELIM_CRLF|DELIM_INIT|DELIM_STRING))
    {
        return;
    }

    // Length checks.
    //
    if (countwords(fargs[2], sep) > 1)
    {
        safe_str(T("#-1 TOO MANY WORDS"), buff, bufc);
        return;
    }
    int words = countwords(fargs[0], sep);
    if (words != countwords(fargs[1], sep))
    {
        safe_str(T("#-1 NUMBER OF WORDS MUST BE EQUAL"), buff, bufc);
        return;
    }

    // Loop through the two lists.
    //
    UTF8 *p1 = trim_space_sep(fargs[0], sep);
    UTF8 *q1 = trim_space_sep(fargs[1], sep);
    UTF8 *p2, *q2;
    bool first = true;
    int i;
    for (i = 0; i < words; i++)
    {
        p2 = split_token(&p1, sep);
        q2 = split_token(&q1, sep);
        if (!first)
        {
            print_sep(osep, buff, bufc);
        }
        if (strcmp(reinterpret_cast<char *>(p2), reinterpret_cast<char *>(fargs[2])) == 0)
        {
            safe_str(q2, buff, bufc); // replace
        }
        else
        {
            safe_str(p2, buff, bufc); // copy
        }
        first = false;
    }
}

/*---------------------------------------------------------------------------
 * fun_repeat: repeats a string
 */

static FUNCTION(fun_repeat)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int times = mux_atol(fargs[1]);
    if (times < 1 || *fargs[0] == '\0')
    {
        // Legal but no work to do.
        //
        return;
    }
    else if (times == 1)
    {
        // It turns into a string copy.
        //
        safe_str(fargs[0], buff, bufc);
    }
    else
    {
        size_t len = strlen(reinterpret_cast<char *>(fargs[0]));
        if (len == 1)
        {
            // It turns into a memset.
            //
            safe_fill(buff, bufc, *fargs[0], times);
        }
        else
        {
            size_t nSize = len*times;
            if (  times > LBUF_SIZE - 1
               || nSize > LBUF_SIZE - 1)
            {
                safe_str(T("#-1 STRING TOO LONG"), buff, bufc);
            }
            else
            {
                size_t nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                if (nSize > nBufferAvailable)
                {
                    nSize = nBufferAvailable;
                }

                size_t nFullCopies = nSize / len;
                size_t nPartial = nSize - nFullCopies * len;

                while (nFullCopies--)
                {
                    memcpy(*bufc, fargs[0], len);
                    *bufc += len;
                }

                if (nPartial)
                {
                    nPartial = TrimPartialSequence(nPartial, fargs[0]);
                    memcpy(*bufc, fargs[0], nPartial);
                    *bufc += nPartial;
                }
            }
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_iter: Make list from evaluating arg2 with each member of arg1.
 * * NOTE: This function expects that its arguments have not been evaluated.
 */

static FUNCTION(fun_iter)
{
    // Optional Input Delimiter.
    //
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_EVAL|DELIM_STRING))
    {
        return;
    }

    // Optional Output Delimiter.
    //
    SEP osep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_EVAL|DELIM_NULL|DELIM_CRLF|DELIM_STRING))
    {
        return;
    }

    UTF8 *curr = alloc_lbuf("fun_iter");
    UTF8 *dp = curr;
    mux_exec(fargs[0], LBUF_SIZE-1, curr, &dp, executor, caller, enactor,
        eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    *dp = '\0';
    size_t ncp;
    UTF8 *cp = trim_space_sep_LEN(curr, dp-curr, sep, &ncp);
    if (!*cp)
    {
        free_lbuf(curr);
        return;
    }
    bool first = true;
    int number = 0;
    bool bLoopInBounds = (  0 <= mudstate.in_loop
                         && mudstate.in_loop < MAX_ITEXT);
    if (bLoopInBounds)
    {
        mudstate.itext[mudstate.in_loop] = nullptr;
        mudstate.inum[mudstate.in_loop] = number;
    }
    mudstate.in_loop++;
    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim
          && !alarm_clock.alarmed)
    {
        if (!first)
        {
            print_sep(osep, buff, bufc);
        }
        first = false;
        number++;
        UTF8 *objstring = split_token(&cp, sep);
        if (bLoopInBounds)
        {
            mudstate.itext[mudstate.in_loop-1] = objstring;
            mudstate.inum[mudstate.in_loop-1]  = number;
        }
        mux_exec(fargs[1], LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    }
    mudstate.in_loop--;
    if (bLoopInBounds)
    {
        mudstate.itext[mudstate.in_loop] = nullptr;
        mudstate.inum[mudstate.in_loop] = 0;
    }
    free_lbuf(curr);
}

// citer() - Character iterator.  Like iter(), but processes each character
// individually rather than splitting on a delimiter.
//
// citer(<string>, <eval>[, <osep>])
//
static FUNCTION(fun_citer)
{
    // Optional Output Delimiter.
    //
    SEP osep;
    if (!OPTIONAL_DELIM(3, osep, DELIM_EVAL|DELIM_NULL|DELIM_CRLF|DELIM_STRING))
    {
        return;
    }

    UTF8 *curr = alloc_lbuf("fun_citer");
    UTF8 *dp = curr;
    mux_exec(fargs[0], LBUF_SIZE-1, curr, &dp, executor, caller, enactor,
        eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    *dp = '\0';

    if ('\0' == curr[0])
    {
        free_lbuf(curr);
        return;
    }

    bool first = true;
    int number = 0;
    bool bLoopInBounds = (  0 <= mudstate.in_loop
                         && mudstate.in_loop < MAX_ITEXT);
    if (bLoopInBounds)
    {
        mudstate.itext[mudstate.in_loop] = nullptr;
        mudstate.inum[mudstate.in_loop] = number;
    }
    mudstate.in_loop++;

    UTF8 *cp = curr;
    while (  '\0' != *cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim
          && !alarm_clock.alarmed)
    {
        if (!first)
        {
            print_sep(osep, buff, bufc);
        }
        first = false;
        number++;

        // Extract one UTF-8 code point into a small buffer.
        //
        size_t nLen = UTF8_SIZE1;
        size_t nAdvance = utf8_FirstByte[static_cast<unsigned char>(*cp)];
        if (0 < nAdvance && nAdvance < UTF8_CONTINUE)
        {
            bool bValid = true;
            for (size_t i = 1; i < nAdvance; i++)
            {
                if ('\0' == cp[i] || UTF8_CONTINUE != utf8_FirstByte[static_cast<unsigned char>(cp[i])])
                {
                    bValid = false;
                    break;
                }
            }
            if (bValid)
            {
                nLen = nAdvance;
            }
        }
        UTF8 *pNext = cp + nLen;
        UTF8 chbuf[5];
        memcpy(chbuf, cp, nLen);
        chbuf[nLen] = '\0';

        if (bLoopInBounds)
        {
            mudstate.itext[mudstate.in_loop-1] = chbuf;
            mudstate.inum[mudstate.in_loop-1]  = number;
        }

        mux_exec(fargs[1], LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        cp = pNext;
    }

    mudstate.in_loop--;
    if (bLoopInBounds)
    {
        mudstate.itext[mudstate.in_loop] = nullptr;
        mudstate.inum[mudstate.in_loop] = 0;
    }
    free_lbuf(curr);
}

static void iter_value(UTF8 *buff, UTF8 **bufc, UTF8 *fargs[], int nfargs, bool bWhich)
{
    int number = 0;
    if (nfargs > 0)
    {
        number = mux_atol(fargs[0]);
        if (number < 0)
        {
            number = 0;
        }
    }

    number++;
    int val = mudstate.in_loop - number;
    if (  0 <= val
       && val < MAX_ITEXT)
    {
        if (bWhich)
        {
            safe_ltoa(mudstate.inum[val], buff, bufc);
        }
        else
        {
            safe_str(mudstate.itext[val], buff, bufc);
        }
    }
}

static FUNCTION(fun_itext)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    iter_value(buff, bufc, fargs, nfargs, false);
}

static FUNCTION(fun_inum)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    iter_value(buff, bufc, fargs, nfargs, true);
}

static FUNCTION(fun_list)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_EVAL|DELIM_STRING))
    {
        return;
    }

    UTF8 *objstring, *result;

    UTF8 *curr = alloc_lbuf("fun_list");
    UTF8 *dp   = curr;
    mux_exec(fargs[0], LBUF_SIZE-1, curr, &dp, executor, caller, enactor,
        eval|EV_TOP|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    *dp = '\0';

    size_t ncp;
    UTF8 *cp = trim_space_sep_LEN(curr, dp-curr, sep, &ncp);
    if (!*cp)
    {
        free_lbuf(curr);
        return;
    }
    int number = 0;
    bool bLoopInBounds = (  0 <= mudstate.in_loop
                         && mudstate.in_loop < MAX_ITEXT);
    if (bLoopInBounds)
    {
        mudstate.itext[mudstate.in_loop] = nullptr;
        mudstate.inum[mudstate.in_loop] = number;
    }
    mudstate.in_loop++;
    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim
          && !alarm_clock.alarmed)
    {
        number++;
        objstring = split_token(&cp, sep);
        if (bLoopInBounds)
        {
            mudstate.itext[mudstate.in_loop-1] = objstring;
            mudstate.inum[mudstate.in_loop-1]  = number;
        }
        dp = result = alloc_lbuf("fun_list.2");
        mux_exec(fargs[1], LBUF_SIZE-1, result, &dp, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *dp = '\0';
        notify(enactor, result);
        free_lbuf(result);
    }
    mudstate.in_loop--;
    if (bLoopInBounds)
    {
        mudstate.itext[mudstate.in_loop] = nullptr;
        mudstate.inum[mudstate.in_loop] = 0;
    }
    free_lbuf(curr);
}

static FUNCTION(fun_ilev)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_ltoa(mudstate.in_loop-1, buff, bufc);
}

/* ---------------------------------------------------------------------------
 * fun_fold: Iteratively eval an attrib with a list of arguments and an
 *           optional base case.  With no base case, the first list element is
 *           passed as %0 and the second is %1.  The attrib is then evaluated
 *           with these args, the result is then used as %0 and the next arg
 *           is %1 and so it goes as long as there are elements left in the
 *           list. The optional base case gives the user a nice starting point.
 *
 *      > &REP_NUM object=[%0][repeat(%1,%1)]
 *      > say fold(OBJECT/REP_NUM,1 2 3 4 5,->)
 *      You say, "->122333444455555"
 *
 * NOTE: To use added list separator, you must use base case!
 */

static FUNCTION(fun_fold)
{
    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    UTF8 *atext;
    dbref thing;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    // Evaluate it using the rest of the passed function args.
    //
    UTF8 *curr = fargs[1];
    UTF8 *cp = curr;
    UTF8 *result, *bp;
    const UTF8 *clist[2];

    // May as well handle first case now.
    //
    if ( nfargs >= 3
       && fargs[2])
    {
        clist[0] = fargs[2];
        clist[1] = split_token(&cp, sep);
        result = bp = alloc_lbuf("fun_fold");
        mux_exec(atext, LBUF_SIZE-1, result, &bp, thing, executor, enactor,
            AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL),
            clist, 2);
        *bp = '\0';
    }
    else
    {
        clist[0] = split_token(&cp, sep);
        clist[1] = split_token(&cp, sep);
        result = bp = alloc_lbuf("fun_fold");
        mux_exec(atext, LBUF_SIZE-1, result, &bp, thing, executor, enactor,
            AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL),
            clist, 2);
        *bp = '\0';
    }

    UTF8 *rstore = result;
    result = alloc_lbuf("fun_fold");

    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim
          && !alarm_clock.alarmed)
    {
        clist[0] = rstore;
        clist[1] = split_token(&cp, sep);
        bp = result;
        mux_exec(atext, LBUF_SIZE-1, result, &bp, thing, executor, enactor,
            AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL),
            clist, 2);
        *bp = '\0';
        mux_strncpy(rstore, result, LBUF_SIZE-1);
    }
    free_lbuf(result);
    safe_str(rstore, buff, bufc);
    free_lbuf(rstore);
    free_lbuf(atext);
}

// Taken from PennMUSH with permission.
//
// itemize(<list>[,<delim>[,<conjunction>[,<punctuation>]]])
// It takes the elements of list and:
//  If there's just one, returns it.
//  If there's two, returns <e1> <conjunction> <e2>
//  If there's >2, returns <e1><punc> <e2><punc> ... <conjunction> <en>
// Default <conjunction> is "and", default punctuation is ","
//
static FUNCTION(fun_itemize)
{
    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    const UTF8 *lconj = T("and");
    if (nfargs > 2)
    {
        lconj = fargs[2];
    }
    const UTF8 *punc = T(",");
    if (nfargs > 3)
    {
        punc = fargs[3];
    }

    int pos = 1;
    UTF8 *cp = trim_space_sep(fargs[0], sep);
    UTF8 *word = split_token(&cp, sep);
    while (cp && *cp)
    {
        pos++;
        safe_str(word, buff, bufc);
        UTF8 *nextword = split_token(&cp, sep);

        if (!cp || !*cp)
        {
            // We're at the end.
            //
            if (pos >= 3)
            {
                safe_str(punc, buff, bufc);
            }
            safe_chr(' ', buff, bufc);
            safe_str(lconj, buff, bufc);
        }
        else
        {
            safe_str(punc, buff, bufc);
        }
        safe_chr(' ', buff, bufc);

        word = nextword;
    }
    safe_str(word, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_choose: Weighted random choice from a list.
//             choose(<list of items>,<list of weights>,<input delim>)
//
static FUNCTION(fun_choose)
{
    SEP isep;
    if (!OPTIONAL_DELIM(3, isep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    std::vector<PUTF8> elems(LBUF_SIZE/2);
    std::vector<PUTF8> weights(LBUF_SIZE/2);

    int n_elems   = list2arr(elems.data(), LBUF_SIZE/2, fargs[0], isep);
    int n_weights = list2arr(weights.data(), LBUF_SIZE/2, fargs[1], sepSpace);

    if (n_elems != n_weights)
    {
        safe_str(T("#-1 LISTS MUST BE OF EQUAL SIZE"), buff, bufc);
        return;
    }

    // Calculate the the breakpoints, not the weights themselves.
    //
    int i;
    int sum = 0;
    int ip[LBUF_SIZE/2];
    for (i = 0; i < n_weights; i++)
    {
        int num = mux_atol(weights[i]);
        if (num < 0)
        {
            num = 0;
        }
        if (num == 0)
        {
            ip[i] = 0;
        }
        else
        {
            int sum_next = sum + num;
            if (sum_next < sum)
            {
                safe_str(T("#-1 OVERFLOW"), buff, bufc);
                return;
            }
            sum = sum_next;
            ip[i] = sum;
        }
    }

    int32_t num = RandomINT32(0, sum-1);

    for (i = 0; i < n_weights; i++)
    {
        if (  ip[i] != 0
           && num < ip[i])
        {
            safe_str(elems[i], buff, bufc);
            break;
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * distribute: randomly distribute M total points into N total bins...
 * *             each bin has an equal 'weight'.
 *
 * syntax:   distribute(points, bins, outputsep)
 * example:  distribute(100, 5, |) might return "25|16|17|22|20"
 */
FUNCTION(fun_distribute)
{
    // Get parameters and evaluate each of them.
    //
    SEP osep;
    if (!OPTIONAL_DELIM(3, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING))
    {
        return;
    }

    // Validate arguments.
    //
    if (!is_integer(fargs[0], nullptr))
    {
        safe_str(T("#-1 ARG1 IS NOT AN INTEGER"), buff, bufc);
        return;
    }

    if (!is_integer(fargs[1], nullptr))
    {
        safe_str(T("#-1 ARG2 IS NOT AN INTEGER"), buff, bufc);
        return;
    }

    const int points_limit = 1000000;
    const int bins_limit   = 2000;
    int points = mux_atol(fargs[0]);
    int bins   = mux_atol(fargs[1]);
    if (  points < 0
       || points_limit < points
       || bins <= 0
       || bins_limit < bins)
    {
        safe_range(buff, bufc);
        return;
    }

    std::vector<int> bin_array(bins, 0);

    // Distribute points over bin.  For each point, pick a random bin for
    // it and increment that bin's count.
    //
    int current_point;
    for (current_point = 0; current_point < points; current_point++)
    {
        int which_bin = RandomINT32(0, bins-1);
        ++(bin_array[which_bin]);
    }

    // Convert the array to real output.
    //
    bool first = true;
    int current_bin;
    for (current_bin = 0; current_bin < bins; current_bin++)
    {
        if (!first)
        {
            print_sep(osep, buff, bufc);
        }
        first = false;
        safe_ltoa(bin_array[current_bin], buff, bufc);
    }
}

#if defined(INLINESQL)

/* sql() function -- Rachel 'Sparks' Blackman
 *                   2003/09/30
 *
 * A more-or-less functionally equivalent version of
 * TinyMUSH 3.1's sql() call. */
FUNCTION(fun_sql)
{
    UNUSED_PARAMETER(nfargs);

    if (!mush_database)
    {
        safe_str(T("#-1 NO DATABASE"), buff, bufc);
        return;
    }

    SEP sepRow;
    if (!OPTIONAL_DELIM(2, sepRow, DELIM_NULL|DELIM_CRLF|DELIM_STRING))
    {
        return;
    }

    SEP sepColumn = sepRow;
    if (!OPTIONAL_DELIM(3, sepColumn, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }


    UTF8 *curr = alloc_lbuf("fun_sql");
    UTF8 *dp = curr;
    mux_exec(fargs[0], LBUF_SIZE-1, curr, &dp, executor, caller, enactor,
        eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    *dp = '\0';

    UTF8 *cp = curr;
    cp = trim_space_sep(cp, sepSpace);
    if (!*cp)
    {
        free_lbuf(curr);
        return;
    }

    if (mysql_ping(mush_database))
    {
        free_lbuf(curr);
        safe_str(T("#-1 SQL UNAVAILABLE"), buff, bufc);
        return;
    }

    if (mysql_real_query(mush_database, reinterpret_cast<char *>(cp), strlen(reinterpret_cast<char *>(cp))))
    {
        free_lbuf(curr);
        safe_str(T("#-1 QUERY ERROR"), buff, bufc);
        return;
    }

    MYSQL_RES *result = mysql_store_result(mush_database);
    if (!result)
    {
        // Drain any remaining result sets from stored procedures.
        //
        while (mysql_next_result(mush_database) == 0)
        {
            MYSQL_RES *extra = mysql_store_result(mush_database);
            if (extra)
            {
                mysql_free_result(extra);
            }
        }
        free_lbuf(curr);
        return;
    }

    int num_fields = mysql_num_fields(result);

    MYSQL_ROW row = mysql_fetch_row(result);
    while (row)
    {
        int loop;
        for (loop = 0; loop < num_fields; loop++)
        {
            if (loop)
            {
                print_sep(sepColumn, buff, bufc);
            }
            safe_str(reinterpret_cast<UTF8 *>(row[loop]), buff, bufc);
        }
        row = mysql_fetch_row(result);
        if (row)
        {
            print_sep(sepRow, buff, bufc);
        }
    }

    free_lbuf(curr);
    mysql_free_result(result);

    // Drain any remaining result sets from stored procedures.
    //
    while (mysql_next_result(mush_database) == 0)
    {
        MYSQL_RES *extra = mysql_store_result(mush_database);
        if (extra)
        {
            mysql_free_result(extra);
        }
    }
}

// mapsql(obj/attr, query[, osep]) — for each SQL result row, call
// obj/attr with %0 = row number, %1..%N = column values.
//
FUNCTION(fun_mapsql)
{
    if (!mush_database)
    {
        safe_str(T("#-1 NO DATABASE"), buff, bufc);
        return;
    }

    SEP osep;
    if (!OPTIONAL_DELIM(3, osep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    // Parse obj/attr.
    //
    UTF8 *atext;
    dbref thing;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner,
            &aflags, buff, bufc))
    {
        return;
    }

    // Evaluate the query argument.
    //
    UTF8 *qbuf = alloc_lbuf("fun_mapsql");
    UTF8 *qp = qbuf;
    mux_exec(fargs[1], LBUF_SIZE-1, qbuf, &qp, executor, caller, enactor,
        eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    *qp = '\0';

    UTF8 *cp = trim_space_sep(qbuf, sepSpace);
    if (!*cp)
    {
        free_lbuf(qbuf);
        free_lbuf(atext);
        return;
    }

    if (mysql_ping(mush_database))
    {
        free_lbuf(qbuf);
        free_lbuf(atext);
        safe_str(T("#-1 SQL UNAVAILABLE"), buff, bufc);
        return;
    }

    if (mysql_real_query(mush_database, reinterpret_cast<char *>(cp),
            strlen(reinterpret_cast<char *>(cp))))
    {
        free_lbuf(qbuf);
        free_lbuf(atext);
        safe_str(T("#-1 QUERY ERROR"), buff, bufc);
        return;
    }

    MYSQL_RES *result = mysql_store_result(mush_database);
    if (!result)
    {
        while (mysql_next_result(mush_database) == 0)
        {
            MYSQL_RES *extra = mysql_store_result(mush_database);
            if (extra) mysql_free_result(extra);
        }
        free_lbuf(qbuf);
        free_lbuf(atext);
        return;
    }

    int num_fields = mysql_num_fields(result);
    if (num_fields > NUM_ENV_VARS - 1)
    {
        num_fields = NUM_ENV_VARS - 1;
    }

    // For each row, call the attribute with %0=rownum, %1..%N=columns.
    //
    const UTF8 *map_args[NUM_ENV_VARS];
    MYSQL_ROW row = mysql_fetch_row(result);
    bool first = true;
    int rownum = 0;

    while (  row
          && mudstate.func_invk_ctr < mudconf.func_invk_lim
          && !alarm_clock.alarmed)
    {
        if (!first)
        {
            print_sep(osep, buff, bufc);
        }
        first = false;
        rownum++;

        UTF8 rownumbuf[32];
        mux_ltoa(rownum, rownumbuf);
        map_args[0] = rownumbuf;

        for (int i = 0; i < num_fields; i++)
        {
            map_args[i + 1] = row[i]
                ? reinterpret_cast<UTF8 *>(row[i])
                : T("");
        }

        mux_exec(atext, LBUF_SIZE-1, buff, bufc, thing, executor, enactor,
            AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL),
            map_args, num_fields + 1);

        row = mysql_fetch_row(result);
    }

    mysql_free_result(result);
    while (mysql_next_result(mush_database) == 0)
    {
        MYSQL_RES *extra = mysql_store_result(mush_database);
        if (extra) mysql_free_result(extra);
    }

    free_lbuf(qbuf);
    free_lbuf(atext);
}

#endif // INLINESQL

/* ---------------------------------------------------------------------------
 * fun_filter: Iteratively perform a function with a list of arguments and
 *             return the arg, if the function evaluates to true using the arg.
 *
 *      > &IS_ODD object=mod(%0,2)
 *      > say filter(object/is_odd,1 2 3 4 5)
 *      You say, "1 3 5"
 *      > say filter(object/is_odd,1-2-3-4-5,-)
 *      You say, "1-3-5"
 *
 *  NOTE:  If you specify a separator, it is used to delimit the returned list.
 */

static void filter_handler(UTF8 *buff, UTF8 **bufc, dbref executor, dbref enactor,
                    UTF8 *fargs[], int nfargs, const SEP &sep, const SEP &osep, bool bBool)
{
    UTF8 *atext;
    dbref thing;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    // Process optional arguments %1-%9.
    //
    const UTF8 *filter_args[NUM_ENV_VARS];
    int   filter_nargs = 1;
    for (int iArg = 4; iArg < nfargs; iArg++)
    {
        filter_args[filter_nargs++] = fargs[iArg];
    }

    // Now iteratively eval the attrib with the argument list.
    //
    UTF8 *cp = trim_space_sep(fargs[1], sep);
    if ('\0' != cp[0])
    {
        UTF8 *result = alloc_lbuf("fun_filter");
        bool bFirst = true;
        while (  cp
              && mudstate.func_invk_ctr < mudconf.func_invk_lim
              && !alarm_clock.alarmed)
        {
            UTF8 *objstring = split_token(&cp, sep);
            UTF8 *bp = result;
            filter_args[0] = objstring;
            mux_exec(atext, LBUF_SIZE-1, result, &bp, thing, executor, enactor,
                AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL),
                filter_args, filter_nargs);
            *bp = '\0';

            if (  (  bBool
                  && xlate(result))
               || (  !bBool
                  && result[0] == '1'
                  && result[1] == '\0'))
            {
                if (!bFirst)
                {
                    print_sep(osep, buff, bufc);
                }
                safe_str(objstring, buff, bufc);
                bFirst = false;
            }
        }
        free_lbuf(result);
    }
    free_lbuf(atext);
}

static FUNCTION(fun_filter)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_INIT|DELIM_STRING))
    {
        return;
    }
    filter_handler(buff, bufc, executor, enactor, fargs, nfargs, sep, osep, false);
}

static FUNCTION(fun_filterbool)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }
    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_INIT|DELIM_STRING))
    {
        return;
    }
    filter_handler(buff, bufc, executor, enactor, fargs, nfargs, sep, osep, true);
}

/* ---------------------------------------------------------------------------
 * fun_map: Iteratively evaluate an attribute with a list of arguments.
 *
 *      > &DIV_TWO object=fdiv(%0,2)
 *      > say map(1 2 3 4 5,object/div_two)
 *      You say, "0.5 1 1.5 2 2.5"
 *      > say map(object/div_two,1-2-3-4-5,-)
 *      You say, "0.5-1-1.5-2-2.5"
 */

static FUNCTION(fun_map)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_INIT|DELIM_STRING))
    {
        return;
    }

    UTF8 *atext;
    dbref thing;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    // Process optional arguments %1-%9.
    //
    const UTF8 *map_args[NUM_ENV_VARS];
    int   map_nargs = 1;
    for (int iArg = 4; iArg < nfargs; iArg++)
    {
        map_args[map_nargs++] = fargs[iArg];
    }

    // Now process the list one element at a time.
    //
    UTF8 *cp = trim_space_sep(fargs[1], sep);
    if ('\0' != cp[0])
    {
        bool first = true;
        while (  cp
              && mudstate.func_invk_ctr < mudconf.func_invk_lim
              && !alarm_clock.alarmed)
        {
            if (!first)
            {
                print_sep(osep, buff, bufc);
            }
            first = false;
            UTF8 *objstring = split_token(&cp, sep);
            map_args[0] = objstring;
            mux_exec(atext, LBUF_SIZE-1, buff, bufc, thing, executor, enactor,
                AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL),
                map_args, map_nargs);
        }
    }
    free_lbuf(atext);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_ledit: Mass find-and-replace on a list.
 */

static FUNCTION(fun_ledit)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(5, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }

    // Split find and replace lists into arrays.
    //
    std::vector<UTF8*> findArr(LBUF_SIZE/2);
    std::vector<UTF8*> replArr(LBUF_SIZE/2);
    int nFind = list2arr(findArr.data(), LBUF_SIZE/2, fargs[1], sep);
    int nRepl = list2arr(replArr.data(), LBUF_SIZE/2, fargs[2], sep);

    // Iterate the original list.
    //
    UTF8 *cp = trim_space_sep(fargs[0], sep);
    bool first = true;
    while (cp)
    {
        UTF8 *word = split_token(&cp, sep);
        if (!first)
        {
            print_sep(osep, buff, bufc);
        }
        first = false;

        // Search for exact match in find array.
        //
        bool matched = false;
        for (int i = 0; i < nFind; i++)
        {
            if (strcmp((const char *)word, (const char *)findArr[i]) == 0)
            {
                if (i < nRepl)
                {
                    safe_str(replArr[i], buff, bufc);
                }
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            safe_str(word, buff, bufc);
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_edit: Edit text.
 */

static FUNCTION(fun_edit)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    unsigned char bufA[LBUF_SIZE], bufB[LBUF_SIZE];
    size_t nLen = strlen(reinterpret_cast<const char *>(fargs[0]));
    memcpy(bufA, fargs[0], nLen + 1);

    unsigned char *pSrc = bufA, *pDst = bufB;
    for (int i = 1; i + 1 < nfargs; i += 2)
    {
        const unsigned char *pFrom = reinterpret_cast<const unsigned char *>(fargs[i]);
        const unsigned char *pTo = reinterpret_cast<const unsigned char *>(fargs[i + 1]);
        size_t fLen = strlen(reinterpret_cast<const char *>(pFrom));
        size_t tLen = strlen(reinterpret_cast<const char *>(pTo));

        if (1 == fLen && '^' == pFrom[0])
        {
            // Prepend 'to' to string.
            //
            size_t nTotal = tLen + nLen;
            if (nTotal > LBUF_SIZE - 1) nTotal = LBUF_SIZE - 1;
            size_t nToCopy = (tLen < LBUF_SIZE - 1) ? tLen : LBUF_SIZE - 1;
            memcpy(pDst, pTo, nToCopy);
            size_t nRemain = (nTotal > nToCopy) ? nTotal - nToCopy : 0;
            if (nRemain > 0) memcpy(pDst + nToCopy, pSrc, nRemain);
            nLen = nTotal;
            pDst[nLen] = '\0';
        }
        else if (1 == fLen && '$' == pFrom[0])
        {
            // Append 'to' to string.
            //
            size_t nTotal = nLen + tLen;
            if (nTotal > LBUF_SIZE - 1) nTotal = LBUF_SIZE - 1;
            memcpy(pDst, pSrc, nLen);
            size_t nAppend = nTotal - nLen;
            if (nAppend > 0) memcpy(pDst + nLen, pTo, nAppend);
            nLen = nTotal;
            pDst[nLen] = '\0';
        }
        else
        {
            // Handle escaped ^ and $ (e.g., \^ or %^ → literal ^ or $).
            //
            const unsigned char *pFromActual = pFrom;
            size_t fLenActual = fLen;
            unsigned char fromBuf[LBUF_SIZE];
            if (   2 == fLen
                && ('\\' == pFrom[0] || '%' == pFrom[0])
                && ('^' == pFrom[1] || '$' == pFrom[1]))
            {
                fromBuf[0] = pFrom[1];
                fromBuf[1] = '\0';
                pFromActual = fromBuf;
                fLenActual = 1;
            }

            nLen = co_edit(pDst, pSrc, nLen,
                           pFromActual, fLenActual,
                           pTo, tLen);
        }

        unsigned char *tmp = pSrc;
        pSrc = pDst;
        pDst = tmp;
    }

    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    if (nLen > nMax) nLen = nMax;
    memcpy(*bufc, pSrc, nLen);
    *bufc += nLen;
    **bufc = '\0';
}

/* ---------------------------------------------------------------------------
 * fun_locate: Search for things with the perspective of another obj.
 */

static FUNCTION(fun_locate)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool check_locks, verbose, multiple, check_possessed;
    dbref thing, what;
    UTF8 *cp;

    int pref_type = NOTYPE;
    check_locks = verbose = multiple = check_possessed = false;

    // Find the thing to do the looking, make sure we control it.
    //
    if (See_All(executor))
    {
        thing = match_thing_quiet(executor, fargs[0]);
    }
    else
    {
        thing = match_controlled_quiet(executor, fargs[0]);
    }
    if (!Good_obj(thing))
    {
        safe_match_result(thing, buff, bufc);
        return;
    }

    // Get pre- and post-conditions and modifiers
    //
    for (cp = fargs[2]; *cp; cp++)
    {
        switch (*cp)
        {
        case 'E':
            pref_type = TYPE_EXIT;
            break;
        case 'L':
            check_locks = true;
            break;
        case 'P':
            pref_type = TYPE_PLAYER;
            break;
        case 'R':
            pref_type = TYPE_ROOM;
            break;
        case 'T':
            pref_type = TYPE_THING;
            break;
        case 'V':
            verbose = true;
            break;
        case 'X':
            multiple = true;
            break;
        }
    }

    // Set up for the search
    //
    if (check_locks)
    {
        init_match_check_keys(thing, fargs[1], pref_type);
    }
    else
    {
        init_match(thing, fargs[1], pref_type);
    }

    // Search for each requested thing
    //
    for (cp = fargs[2]; *cp; cp++)
    {
        switch (*cp)
        {
        case 'a':
            match_absolute();
            break;
        case 'c':
            match_carried_exit_with_parents();
            break;
        case 'e':
            match_exit_with_parents();
            break;
        case 'h':
            match_here();
            break;
        case 'i':
            match_possession();
            break;
        case 'm':
            match_me();
            break;
        case 'n':
            match_neighbor();
            break;
        case 'p':
            match_player();
            break;
        case 's':
            check_possessed = true;
            break;
        case '*':
            match_everything(MAT_EXIT_PARENTS);
            break;
        }
    }

    // Get the result and return it to the caller
    //
    if (multiple)
    {
        what = last_match_result();
    }
    else
    {
        what = match_result();
    }

    if (check_possessed && !Good_obj(what))
    {
        what = match_possessed(executor, thing, fargs[1], what, check_locks);
    }

    if (verbose)
    {
        (void)match_status(executor, what);
    }

    safe_tprintf_str(buff, bufc, T("#%d"), what);
}

static void switch_handler
(
    UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int   eval,
    UTF8 *fargs[], int nfargs,
    const UTF8 *cargs[], int ncargs,
    bool bSwitch
)
{
    // Evaluate the target in fargs[0].
    //
    UTF8 *mbuff = alloc_lbuf("fun_switch");
    UTF8 *bp = mbuff;
    mux_exec(fargs[0], LBUF_SIZE-1, mbuff, &bp, executor, caller, enactor,
        eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    *bp = '\0';

    UTF8 *tbuff = alloc_lbuf("fun_switch.2");

    // Loop through the patterns looking for a match.
    //
    int i;
    for (i = 1;  i < nfargs-1
              && fargs[i]
              && fargs[i+1]
              && !alarm_clock.alarmed; i += 2)
    {
        bp = tbuff;
        mux_exec(fargs[i], LBUF_SIZE-1, tbuff, &bp, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *bp = '\0';

        if (bSwitch ? wild_match(tbuff, mbuff) : strcmp(reinterpret_cast<char *>(tbuff), reinterpret_cast<char *>(mbuff)) == 0)
        {
            free_lbuf(tbuff);
            const UTF8 *save_switch = mudstate.switch_token;
            mudstate.switch_token = mbuff;
            mux_exec(fargs[i+1], LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
                eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
            mudstate.switch_token = save_switch;
            free_lbuf(mbuff);
            return;
        }
    }
    free_lbuf(tbuff);

    // Nope, return the default if there is one.
    //
    if (  i < nfargs
       && fargs[i])
    {
        const UTF8 *save_switch = mudstate.switch_token;
        mudstate.switch_token = mbuff;
        mux_exec(fargs[i], LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        mudstate.switch_token = save_switch;
    }
    free_lbuf(mbuff);
}

/* ---------------------------------------------------------------------------
 * fun_switch: Return value based on pattern matching (ala @switch)
 * NOTE: This function expects that its arguments have not been evaluated.
 */

static FUNCTION(fun_switch)
{
    switch_handler
    (
        buff, bufc,
        executor, caller, enactor,
        eval,
        fargs, nfargs,
        cargs, ncargs,
        true
    );
}

static FUNCTION(fun_case)
{
    switch_handler
    (
        buff, bufc,
        executor, caller, enactor,
        eval,
        fargs, nfargs,
        cargs, ncargs,
        false
    );
}

static void switchall_handler
(
    UTF8 *buff, UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int   eval,
    UTF8 *fargs[], int nfargs,
    const UTF8 *cargs[], int ncargs,
    bool bSwitch
)
{
    // Evaluate the target in fargs[0].
    //
    UTF8 *mbuff = alloc_lbuf("fun_switchall");
    UTF8 *bp = mbuff;
    mux_exec(fargs[0], LBUF_SIZE-1, mbuff, &bp, executor, caller, enactor,
        eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
    *bp = '\0';

    UTF8 *tbuff = alloc_lbuf("fun_switchall.2");

    // Loop through all patterns, evaluating every match.
    //
    bool bMatched = false;
    int i;
    for (i = 1;  i < nfargs-1
              && fargs[i]
              && fargs[i+1]
              && !alarm_clock.alarmed; i += 2)
    {
        bp = tbuff;
        mux_exec(fargs[i], LBUF_SIZE-1, tbuff, &bp, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *bp = '\0';

        if (bSwitch ? wild_match(tbuff, mbuff) : strcmp(reinterpret_cast<char *>(tbuff), reinterpret_cast<char *>(mbuff)) == 0)
        {
            bMatched = true;
            const UTF8 *save_switch = mudstate.switch_token;
            mudstate.switch_token = mbuff;
            mux_exec(fargs[i+1], LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
                eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
            mudstate.switch_token = save_switch;
        }
    }
    free_lbuf(tbuff);

    // If nothing matched, return the default if there is one.
    //
    if (  !bMatched
       && i < nfargs
       && fargs[i])
    {
        const UTF8 *save_switch = mudstate.switch_token;
        mudstate.switch_token = mbuff;
        mux_exec(fargs[i], LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        mudstate.switch_token = save_switch;
    }
    free_lbuf(mbuff);
}

static FUNCTION(fun_switchall)
{
    switchall_handler
    (
        buff, bufc,
        executor, caller, enactor,
        eval,
        fargs, nfargs,
        cargs, ncargs,
        true
    );
}

static FUNCTION(fun_caseall)
{
    switchall_handler
    (
        buff, bufc,
        executor, caller, enactor,
        eval,
        fargs, nfargs,
        cargs, ncargs,
        false
    );
}

/*
 * ---------------------------------------------------------------------------
 * * fun_space: Make spaces.
 */

static FUNCTION(fun_space)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Validate request.
    //
    int num;
    if (nfargs == 0 || *fargs[0] == '\0')
    {
        num = 1;
    }
    else
    {
        num = mux_atol(fargs[0]);
        if (num == 0)
        {
            // If 'space(0)', 'space(00)', ..., then allow num == 0,
            // otherwise, we force to num to be 1.
            //
            if (!is_integer(fargs[0], nullptr))
            {
                num = 1;
            }
        }
        else if (num < 0)
        {
            num = 0;
        }

    }
    safe_fill(buff, bufc, ' ', num);
}

static FUNCTION(fun_height)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    long nHeight = 24;
    if (is_rational(fargs[0]))
    {
        SOCKET s = mux_atol(fargs[0]);
        DESC* d = find_desc_by_socket(s);
        if (nullptr != d)
        {
            nHeight = desc_height(d);
        }
    }
    else
    {
        UTF8 *pTargetName = fargs[0];
        if ('*' == *pTargetName)
        {
            pTargetName++;
        }
        dbref target = lookup_player(executor, pTargetName, true);
        if (Good_obj(target))
        {
            nHeight = fetch_height(target);
        }
    }
    safe_ltoa(nHeight, buff, bufc);
}


static FUNCTION(fun_width)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    long nWidth = 78;
    if (is_rational(fargs[0]))
    {
        SOCKET s = mux_atol(fargs[0]);
        DESC* d = find_desc_by_socket(s);
        if (nullptr != d)
        {
            nWidth = desc_width(d);
        }
    }
    else
    {
        UTF8 *pTargetName = fargs[0];
        if ('*' == *pTargetName)
        {
            pTargetName++;
        }
        dbref target = lookup_player(executor, pTargetName, true);
        if (Good_obj(target))
        {
            nWidth = fetch_width(target);
        }
    }
    safe_ltoa(nWidth, buff, bufc);
}

static FUNCTION(fun_colordepth)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int nDepth = 0;
    dbref target = NOTHING;
    if (is_rational(fargs[0]))
    {
        SOCKET s = mux_atol(fargs[0]);
        DESC* d = find_desc_by_socket(s);
        if (nullptr != d)
        {
            target = desc_player(d);
        }
    }
    else
    {
        UTF8 *pTargetName = fargs[0];
        if ('*' == *pTargetName)
        {
            pTargetName++;
        }
        target = lookup_player(executor, pTargetName, true);
    }

    if (Good_obj(target))
    {
        if (Ansi(target))
        {
            if (Html(target))
            {
                nDepth = 24;
            }
            else if (Color256(target))
            {
                nDepth = 8;
            }
            else
            {
                nDepth = 4;
            }
        }
        else
        {
            nDepth = 0;
        }
    }
    safe_ltoa(nDepth, buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_idle, fun_conn: return seconds idle or connected.
 */

static FUNCTION(fun_idle)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    long nIdle = -1;
    if (is_rational(fargs[0]))
    {
        SOCKET s = mux_atol(fargs[0]);
        DESC *d = find_desc_by_socket(s);
        if (  nullptr != d
           && (  desc_player(d) == executor
              || Wizard_Who(executor)))
        {
            CLinearTimeAbsolute ltaNow;
            ltaNow.GetUTC();
            CLinearTimeDelta ltdResult = ltaNow - desc_last_time(d);
            nIdle = ltdResult.ReturnSeconds();
        }
    }
    else
    {
        UTF8 *pTargetName = fargs[0];
        if (*pTargetName == '*')
        {
            pTargetName++;
        }
        dbref target = lookup_player(executor, pTargetName, true);
        if (  Good_obj(target)
           && (  !Hidden(target)
              || See_Hidden(executor)))
        {
            nIdle = fetch_idle(target);
        }
    }
    safe_ltoa(nIdle, buff, bufc);
}

static FUNCTION(fun_conn)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    long nConnected = -1;
    if (is_rational(fargs[0]))
    {
        SOCKET s = mux_atol(fargs[0]);
        DESC *d = find_desc_by_socket(s);
        if (  nullptr != d
           && (  desc_player(d) == executor
              || Wizard_Who(executor)))
        {
            CLinearTimeAbsolute ltaNow;
            ltaNow.GetUTC();
            CLinearTimeDelta ltdResult = ltaNow - desc_connected_at(d);
            nConnected = ltdResult.ReturnSeconds();
        }
    }
    else
    {
        UTF8 *pTargetName = fargs[0];
        if (*pTargetName == '*')
        {
            pTargetName++;
        }
        dbref target = lookup_player(executor, pTargetName, true);
        if (  Good_obj(target)
           && (  !Hidden(target)
              || See_Hidden(executor)))
        {
            nConnected = fetch_connect(target);
        }
    }
    safe_ltoa(nConnected, buff, bufc);
}

static FUNCTION(fun_terminfo)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    DESC *d = nullptr;
    if (is_rational(fargs[0]))
    {
        SOCKET s = mux_atol(fargs[0]);
        d = find_desc_by_socket(s);

        if (  nullptr != d
           && (  desc_player(d) != executor
              && !Wizard_Who(executor)))
        {
            safe_noperm(buff, bufc);
            return;
        }
    }
    else
    {
        UTF8 *pTargetName = fargs[0];
        if (*pTargetName == '*')
        {
            pTargetName++;
        }
        dbref target = lookup_player(executor, pTargetName, true);
        if (  Good_obj(target)
           && !(  !Hidden(target)
              || See_Hidden(executor)))
        {
            // Obscure state of Hidden things.
            //
            safe_notconnected(buff, bufc);
            return;
        }

        d = find_desc_by_player(target);
    }

    if (nullptr == d)
    {
        safe_notconnected(buff, bufc);
        return;
    }

    if (desc_ttype(d))
    {
        safe_str(desc_ttype(d), buff, bufc);
        safe_str(T(" telnet"), buff, bufc);
    }
    else
    {
        safe_str(T("unknown"), buff, bufc);
        if (  desc_nvt_him_state(d, TELNET_NAWS)
           || desc_nvt_him_state(d, TELNET_SGA)
           || desc_nvt_him_state(d, TELNET_EOR))
        {
            safe_str(T(" telnet"), buff, bufc);
        }
    }

    if (Html(desc_player(d)))
    {
        safe_str(T(" pueblo"), buff, bufc);
    }

    if (CHARSET_UTF8 == desc_encoding(d))
    {
        safe_str(T(" unicode"), buff, bufc);
    }

#ifdef UNIX_SSL
    if (desc_socket_state(d) != SocketState::Accepted)
    {
        safe_str(T(" ssl"), buff, bufc);
    }
#endif // UNIX_SSL
}


/*
 * ---------------------------------------------------------------------------
 * * fun_sort: Sort lists.
 */

typedef struct qsort_record
{
    union
    {
        double d;
        long   l;
        int64_t  i64;
    } u;
    UTF8 *str;
    UTF8 *sortkey;      // Pre-computed UCA sort key (UNICODE_LIST only).
    size_t sortkeylen;
} q_rec;

typedef int (*CompareFunction)(const q_rec &, const q_rec &);

static int a_comp(const q_rec &r1, const q_rec &r2)
{
    UTF8 buf1[LBUF_SIZE];
    mux_strncpy(buf1, strip_color(r1.str), LBUF_SIZE-1);
    return strcmp(
        reinterpret_cast<const char *>(buf1),
        reinterpret_cast<const char *>(strip_color(r2.str)));
}

static int a_casecomp(const q_rec &r1, const q_rec &r2)
{
    UTF8 buf1[LBUF_SIZE];
    mux_strncpy(buf1, strip_color(r1.str), LBUF_SIZE-1);
    return mux_stricmp(buf1, strip_color(r2.str));
}

static int f_comp(const q_rec &r1, const q_rec &r2)
{
    if (r1.u.d > r2.u.d) return 1;
    if (r1.u.d < r2.u.d) return -1;
    return 0;
}

static int l_comp(const q_rec &r1, const q_rec &r2)
{
    if (r1.u.l > r2.u.l) return 1;
    if (r1.u.l < r2.u.l) return -1;
    return 0;
}

static int i64_comp(const q_rec &r1, const q_rec &r2)
{
    if (r1.u.i64 > r2.u.i64) return 1;
    if (r1.u.i64 < r2.u.i64) return -1;
    return 0;
}

static int u_collate(const q_rec &r1, const q_rec &r2)
{
    size_t nMin = (r1.sortkeylen < r2.sortkeylen) ? r1.sortkeylen : r2.sortkeylen;
    int cmp = memcmp(r1.sortkey, r2.sortkey, nMin);
    if (0 != cmp)
    {
        return cmp;
    }
    if (r1.sortkeylen < r2.sortkeylen) return -1;
    if (r1.sortkeylen > r2.sortkeylen) return 1;
    return 0;
}

typedef struct
{
    UTF8 **m_s;
    int    m_n;
    int    m_iSortType;
    q_rec *m_ptrs;

} SortContext;

static bool do_asort_start(SortContext *psc, int n, UTF8 *s[], int sort_type)
{
    if (  n < 0
       || LBUF_SIZE <= n)
    {
        return false;
    }

    psc->m_s = s;
    psc->m_n  = n;
    psc->m_iSortType = sort_type;
    psc->m_ptrs = nullptr;

    if (0 == n)
    {
        return true;
    }

    int i;

    psc->m_ptrs = static_cast<q_rec *>(MEMALLOC(n * sizeof(q_rec)));
    if (nullptr != psc->m_ptrs)
    {
        switch (sort_type)
        {
        case ASCII_LIST:
            for (i = 0; i < n; i++)
            {
                psc->m_ptrs[i].str = s[i];
            }
            std::sort(psc->m_ptrs, psc->m_ptrs + n,
                [](const q_rec &a, const q_rec &b) { return a_comp(a, b) < 0; });
            break;

        case NUMERIC_LIST:
            for (i = 0; i < n; i++)
            {
                psc->m_ptrs[i].str = s[i];
                psc->m_ptrs[i].u.i64 = mux_atoi64(strip_color(s[i]));
            }
            std::sort(psc->m_ptrs, psc->m_ptrs + n,
                [](const q_rec &a, const q_rec &b) { return a.u.i64 < b.u.i64; });
            break;

        case DBREF_LIST:
            for (i = 0; i < n; i++)
            {
                psc->m_ptrs[i].str = s[i];
                psc->m_ptrs[i].u.l = dbnum(strip_color(s[i]));
            }
            std::sort(psc->m_ptrs, psc->m_ptrs + n,
                [](const q_rec &a, const q_rec &b) { return a.u.l < b.u.l; });
            break;

        case FLOAT_LIST:
            for (i = 0; i < n; i++)
            {
                psc->m_ptrs[i].str = s[i];
                psc->m_ptrs[i].u.d = mux_atof(strip_color(s[i]), false);
            }
            std::sort(psc->m_ptrs, psc->m_ptrs + n,
                [](const q_rec &a, const q_rec &b) { return a.u.d < b.u.d; });
            break;

        case CI_ASCII_LIST:
            for (i = 0; i < n; i++)
            {
                psc->m_ptrs[i].str = s[i];
            }
            std::sort(psc->m_ptrs, psc->m_ptrs + n,
                [](const q_rec &a, const q_rec &b) { return a_casecomp(a, b) < 0; });
            break;

        case UNICODE_LIST:
            for (i = 0; i < n; i++)
            {
                psc->m_ptrs[i].str = s[i];
                size_t nBytes;
                const UTF8 *plain = strip_color(s[i], &nBytes, nullptr);
                UTF8 *key = static_cast<UTF8 *>(MEMALLOC(LBUF_SIZE));
                psc->m_ptrs[i].sortkeylen =
                    mux_collate_sortkey(plain, nBytes, key, LBUF_SIZE);
                psc->m_ptrs[i].sortkey = key;
            }
            std::sort(psc->m_ptrs, psc->m_ptrs + n,
                [](const q_rec &a, const q_rec &b) { return u_collate(a, b) < 0; });
            break;

        case CI_UNICODE_LIST:
            for (i = 0; i < n; i++)
            {
                psc->m_ptrs[i].str = s[i];
                size_t nBytes;
                const UTF8 *plain = strip_color(s[i], &nBytes, nullptr);
                UTF8 *key = static_cast<UTF8 *>(MEMALLOC(LBUF_SIZE));
                psc->m_ptrs[i].sortkeylen =
                    mux_collate_sortkey_ci(plain, nBytes, key, LBUF_SIZE);
                psc->m_ptrs[i].sortkey = key;
            }
            std::sort(psc->m_ptrs, psc->m_ptrs + n,
                [](const q_rec &a, const q_rec &b) { return u_collate(a, b) < 0; });
            break;
        }

        for (i = 0; i < n; i++)
        {
            s[i] = psc->m_ptrs[i].str;
        }
        return true;
    }
    return false;
}

static void do_asort_finish(SortContext *psc)
{
    if (nullptr != psc->m_ptrs)
    {
        if (  UNICODE_LIST == psc->m_iSortType
           || CI_UNICODE_LIST == psc->m_iSortType)
        {
            for (int i = 0; i < psc->m_n; i++)
            {
                MEMFREE(psc->m_ptrs[i].sortkey);
            }
        }
        MEMFREE(psc->m_ptrs);
        psc->m_ptrs = nullptr;
    }
}

static FUNCTION(fun_sort)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_INIT|DELIM_STRING))
    {
        return;
    }

    UTF8 *ptrs[LBUF_SIZE / 2];

    // Convert the list to an array.
    //
    UTF8 *list = alloc_lbuf("fun_sort");
    mux_strncpy(list, fargs[0], LBUF_SIZE-1);
    int nitems = list2arr(ptrs, LBUF_SIZE / 2, list, sep);

    int sort_type = ASCII_LIST;
    if (2 <= nfargs)
    {
        switch (fargs[1][0])
        {
        case 'd':
        case 'D':
            sort_type = DBREF_LIST;
            break;

        case 'n':
        case 'N':
            sort_type = NUMERIC_LIST;
            break;

        case 'f':
        case 'F':
            sort_type = FLOAT_LIST;
            break;

        case 'i':
        case 'I':
            sort_type = CI_ASCII_LIST;
            break;

        case 'u':
        case 'U':
            sort_type = UNICODE_LIST;
            break;

        case 'a':
        case 'A':
            sort_type = ASCII_LIST;
            break;

        case 'c':
        case 'C':
            sort_type = CI_UNICODE_LIST;
            break;

        case '?':
        case '\0':
            {
                AutoDetect ad;
                ad.ExamineList(nitems, ptrs);
                sort_type = ad.GetType();
            }
            break;
        }
    }
    else
    {
        AutoDetect ad;
        ad.ExamineList(nitems, ptrs);
        sort_type = ad.GetType();
    }

    SortContext sc;
    if (do_asort_start(&sc, nitems, ptrs, sort_type))
    {
        do_asort_finish(&sc);
    }

    arr2list(ptrs, nitems, buff, bufc, osep);
    free_lbuf(list);
}

// ---------------------------------------------------------------------------
// sortkey(): Sort a list by computed key values.
//
// sortkey([obj/]attr, list[, sort_type[, delim[, osep]]])
//
// For each element, evaluates attr with %0 = element to produce a key.
// Sorts the original elements by those keys using the specified sort type.
//
static FUNCTION(fun_sortkey)
{
    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(5, osep, DELIM_NULL|DELIM_CRLF|DELIM_STRING|DELIM_INIT))
    {
        return;
    }

    UTF8 *atext;
    dbref thing;
    dbref aowner;
    int   aflags;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, &aowner, &aflags, buff, bufc))
    {
        return;
    }

    if ((aflags & AF_NOEVAL) || NoEval(executor))
    {
        free_lbuf(atext);
        return;
    }

    // Split the list into an array.
    //
    UTF8 *list = alloc_lbuf("fun_sortkey.list");
    mux_strncpy(list, fargs[1], LBUF_SIZE-1);
    UTF8 *ptrs[LBUF_SIZE / 2];
    int nitems = list2arr(ptrs, LBUF_SIZE / 2, list, sep);

    if (nitems <= 1)
    {
        arr2list(ptrs, nitems, buff, bufc, osep);
        free_lbuf(list);
        free_lbuf(atext);
        return;
    }

    // Compute a sort key for each element.
    //
    UTF8 *keys[LBUF_SIZE / 2];
    int nkeys = 0;
    for (int i = 0; i < nitems; i++)
    {
        keys[i] = alloc_lbuf("fun_sortkey.key");
        UTF8 *bp = keys[i];

        UTF8 *tbuf = alloc_lbuf("fun_sortkey.attr");
        mux_strncpy(tbuf, atext, LBUF_SIZE-1);

        const UTF8 *elems[1] = { ptrs[i] };
        ast_exec(tbuf, LBUF_SIZE-1, keys[i], &bp, thing, executor,
            enactor, AttrTrace(aflags, EV_STRIP_CURLY|EV_FCHECK|EV_EVAL),
            elems, 1);
        *bp = '\0';

        free_lbuf(tbuf);
        nkeys = i + 1;

        if (  mudstate.func_invk_ctr > mudconf.func_invk_lim
           || alarm_clock.alarmed)
        {
            for (int j = 0; j < nkeys; j++)
            {
                free_lbuf(keys[j]);
            }
            free_lbuf(list);
            free_lbuf(atext);
            return;
        }
    }

    // Determine sort type from the keys.
    //
    int sort_type = ASCII_LIST;
    if (3 <= nfargs && fargs[2][0] != '\0')
    {
        switch (fargs[2][0])
        {
        case 'd': case 'D': sort_type = DBREF_LIST;      break;
        case 'n': case 'N': sort_type = NUMERIC_LIST;     break;
        case 'f': case 'F': sort_type = FLOAT_LIST;       break;
        case 'i': case 'I': sort_type = CI_ASCII_LIST;    break;
        case 'u': case 'U': sort_type = UNICODE_LIST;     break;
        case 'a': case 'A': sort_type = ASCII_LIST;       break;
        case 'c': case 'C': sort_type = CI_UNICODE_LIST;  break;
        case '?':
        case '\0':
            {
                AutoDetect ad;
                ad.ExamineList(nitems, keys);
                sort_type = ad.GetType();
            }
            break;
        }
    }
    else
    {
        AutoDetect ad;
        ad.ExamineList(nitems, keys);
        sort_type = ad.GetType();
    }

    // Sort keys using do_asort_start, which rearranges the keys[] array
    // in place.  We save the original key pointers alongside their element
    // pointers so we can follow the rearrangement.
    //
    // Build parallel arrays: save original (key, element) pairs.
    //
    struct sk_pair { UTF8 *key; UTF8 *elem; };
    sk_pair pairs[LBUF_SIZE / 2];
    for (int i = 0; i < nitems; i++)
    {
        pairs[i].key  = keys[i];
        pairs[i].elem = ptrs[i];
    }

    // Numeric-like key sorts are simpler and safer if we sort the
    // (key, elem) pairs directly instead of sorting keys first and
    // then remapping by pointer identity.
    if (  sort_type == NUMERIC_LIST
       || sort_type == DBREF_LIST
       || sort_type == FLOAT_LIST)
    {
        switch (sort_type)
        {
        case NUMERIC_LIST:
            std::sort(pairs, pairs + nitems,
                [](const sk_pair &a, const sk_pair &b) {
                    return mux_atoi64(strip_color(a.key))
                         < mux_atoi64(strip_color(b.key));
                });
            break;

        case DBREF_LIST:
            std::sort(pairs, pairs + nitems,
                [](const sk_pair &a, const sk_pair &b) {
                    return dbnum(strip_color(a.key))
                         < dbnum(strip_color(b.key));
                });
            break;

        case FLOAT_LIST:
            std::sort(pairs, pairs + nitems,
                [](const sk_pair &a, const sk_pair &b) {
                    return mux_atof(strip_color(a.key), false)
                         < mux_atof(strip_color(b.key), false);
                });
            break;
        }

        UTF8 *sorted[LBUF_SIZE / 2];
        for (int i = 0; i < nitems; i++)
        {
            sorted[i] = pairs[i].elem;
        }

        arr2list(sorted, nitems, buff, bufc, osep);

        for (int i = 0; i < nitems; i++)
        {
            free_lbuf(pairs[i].key);
        }
        free_lbuf(list);
        free_lbuf(atext);
        return;
    }

    SortContext sc;
    if (do_asort_start(&sc, nitems, keys, sort_type))
    {
        do_asort_finish(&sc);
    }

    // keys[] is now sorted.  For each sorted position, find the original
    // pair by pointer identity and emit the corresponding element.
    //
    UTF8 *sorted[LBUF_SIZE / 2];
    bool used[LBUF_SIZE / 2];
    memset(used, 0, sizeof(bool) * nitems);

    for (int i = 0; i < nitems; i++)
    {
        for (int j = 0; j < nitems; j++)
        {
            if (!used[j] && keys[i] == pairs[j].key)
            {
                sorted[i] = pairs[j].elem;
                used[j] = true;
                break;
            }
        }
    }

    arr2list(sorted, nitems, buff, bufc, osep);

    for (int i = 0; i < nitems; i++)
    {
        free_lbuf(pairs[i].key);
    }
    free_lbuf(list);
    free_lbuf(atext);
}

/* ---------------------------------------------------------------------------
 * fun_setunion, fun_setdiff, fun_setinter: Set management.
 */

#define SET_UNION     1
#define SET_INTERSECT 2
#define SET_DIFF      3

static void handle_sets
(
    int             nfargs,
    UTF8           *fargs[],
    UTF8           *buff,
    UTF8 **bufc,
    int             oper,
    const SEP      &sep,
    const SEP      &osep
)
{
    std::vector<UTF8*> ptrs1(LBUF_SIZE/2);
    std::vector<UTF8*> ptrs2(LBUF_SIZE/2);

    int val;

    UTF8 *list1 = alloc_lbuf("fun_setunion.1");
    mux_strncpy(list1, fargs[0], LBUF_SIZE-1);
    int n1 = list2arr(ptrs1.data(), LBUF_SIZE/2, list1, sep);

    UTF8 *list2 = alloc_lbuf("fun_setunion.2");
    mux_strncpy(list2, fargs[1], LBUF_SIZE-1);
    int n2 = list2arr(ptrs2.data(), LBUF_SIZE/2, list2, sep);

    int sort_type = ASCII_LIST;
    if (5 <= nfargs)
    {
        switch (fargs[4][0])
        {
        case 'd':
        case 'D':
            sort_type = DBREF_LIST;
            break;

        case 'n':
        case 'N':
            sort_type = NUMERIC_LIST;
            break;

        case 'f':
        case 'F':
            sort_type = FLOAT_LIST;
            break;

        case 'i':
        case 'I':
            sort_type = CI_ASCII_LIST;
            break;

        case 'u':
        case 'U':
            sort_type = UNICODE_LIST;
            break;

        case 'a':
        case 'A':
            sort_type = ASCII_LIST;
            break;

        case 'c':
        case 'C':
            sort_type = CI_UNICODE_LIST;
            break;

        case '?':
        case '\0':
            {
                AutoDetect ad;
                ad.ExamineList(n1, ptrs1.data());
                ad.ExamineList(n2, ptrs2.data());
                sort_type = ad.GetType();
            }
            break;
        }
    }

    SortContext sc1;
    if (!do_asort_start(&sc1, n1, ptrs1.data(), sort_type))
    {
        free_lbuf(list1);
        free_lbuf(list2);
        return;
    }

    SortContext sc2;
    if (!do_asort_start(&sc2, n2, ptrs2.data(), sort_type))
    {
        do_asort_finish(&sc1);
        free_lbuf(list1);
        free_lbuf(list2);
        return;
    }

    CompareFunction cf = nullptr;
    switch (sort_type)
    {
    case ASCII_LIST:
        cf = a_comp;
        break;

    case NUMERIC_LIST:
        cf = i64_comp;
        break;

    case DBREF_LIST:
        cf = l_comp;
        break;

    case FLOAT_LIST:
        cf = f_comp;
        break;

    case CI_ASCII_LIST:
        cf = a_casecomp;
        break;

    case UNICODE_LIST:
        cf = u_collate;
        break;

    case CI_UNICODE_LIST:
        cf = u_collate;
        break;
    }

    int i1 = 0;
    int i2 = 0;
    q_rec *oldp = nullptr;
    bool bFirst = true;

    switch (oper)
    {
    case SET_UNION:

        // Copy elements common to both lists.
        //
        // Handle case of two identical single-element lists.
        //
        if (  n1 == 1
           && n2 == 1
           && cf(sc1.m_ptrs[0], sc2.m_ptrs[0]) == 0)
        {
            safe_str(sc1.m_ptrs[0].str, buff, bufc);
            break;
        }

        // Process until one list is empty.
        //
        while (  i1 < n1
              && i2 < n2)
        {
            // Skip over duplicates.
            //
            if (  0 < i1
               || 0 < i2)
            {
                while (  i1 < n1
                      && oldp
                      && cf(sc1.m_ptrs[i1], *oldp) == 0)
                {
                    i1++;
                }

                while (  i2 < n2
                      && oldp
                      && cf(sc2.m_ptrs[i2], *oldp) == 0)
                {
                    i2++;
                }
            }

            // Compare and copy.
            //
            if (  i1 < n1
               && i2 < n2)
            {
                if (!bFirst)
                {
                    print_sep(osep, buff, bufc);
                }

                bFirst = false;
                if (cf(sc1.m_ptrs[i1], sc2.m_ptrs[i2]) < 0)
                {
                    oldp = &sc1.m_ptrs[i1];
                    safe_str(sc1.m_ptrs[i1].str, buff, bufc);
                    i1++;
                }
                else
                {
                    oldp = &sc2.m_ptrs[i2];
                    safe_str(sc2.m_ptrs[i2].str, buff, bufc);
                    i2++;
                }
            }
        }

        // Copy rest of remaining list, stripping duplicates.
        //
        for (; i1 < n1; i1++)
        {
            if (  !oldp
               || cf(*oldp, sc1.m_ptrs[i1]) != 0)
            {
                if (!bFirst)
                {
                    print_sep(osep, buff, bufc);
                }
                bFirst = false;
                oldp = &sc1.m_ptrs[i1];
                safe_str(sc1.m_ptrs[i1].str, buff, bufc);
            }
        }

        for (; i2 < n2; i2++)
        {
            if (  !oldp
               || cf(*oldp, sc2.m_ptrs[i2]) != 0)
            {
                if (!bFirst)
                {
                    print_sep(osep, buff, bufc);
                }
                bFirst = false;
                oldp = &sc2.m_ptrs[i2];
                safe_str(sc2.m_ptrs[i2].str, buff, bufc);
            }
        }
        break;

    case SET_INTERSECT:

        // Copy elements not in both lists.
        //
        while (  i1 < n1
              && i2 < n2)
        {
            val = cf(sc1.m_ptrs[i1], sc2.m_ptrs[i2]);
            if (!val)
            {
                // Got a match, copy it.
                //
                if (!bFirst)
                {
                    print_sep(osep, buff, bufc);
                }
                bFirst = false;
                oldp = &sc1.m_ptrs[i1];
                safe_str(sc1.m_ptrs[i1].str, buff, bufc);
                i1++;
                i2++;
                while (  i1 < n1
                      && cf(sc1.m_ptrs[i1], *oldp) == 0)
                {
                    i1++;
                }
                while (  i2 < n2
                      && cf(sc2.m_ptrs[i2], *oldp) == 0)
                {
                    i2++;
                }
            }
            else if (val < 0)
            {
                i1++;
            }
            else
            {
                i2++;
            }
        }
        break;

    case SET_DIFF:

        // Copy elements unique to list1.
        //
        while (  i1 < n1
              && i2 < n2)
        {
            val = cf(sc1.m_ptrs[i1], sc2.m_ptrs[i2]);
            if (!val)
            {
                // Got a match, increment pointers.
                //
                oldp = &sc1.m_ptrs[i1];
                while (  i1 < n1
                      && cf(sc1.m_ptrs[i1], *oldp) == 0)
                {
                    i1++;
                }
                while (  i2 < n2
                      && cf(sc2.m_ptrs[i2], *oldp) == 0)
                {
                    i2++;
                }
            }
            else if (val < 0)
            {
                // Item in list1 not in list2, copy.
                //
                if (!bFirst)
                {
                    print_sep(osep, buff, bufc);
                }
                bFirst = false;
                safe_str(sc1.m_ptrs[i1].str, buff, bufc);
                oldp = &sc1.m_ptrs[i1];
                i1++;
                while (  i1 < n1
                      && cf(sc1.m_ptrs[i1], *oldp) == 0)
                {
                    i1++;
                }
            }
            else
            {
                // Item in list2 but not in list1, discard.
                //
                oldp = &sc2.m_ptrs[i2];
                i2++;
                while (  i2 < n2
                      && cf(sc2.m_ptrs[i2], *oldp) == 0)
                {
                    i2++;
                }
            }
        }

        // Copy remainder of list1.
        //
        while (i1 < n1)
        {
            if (!bFirst)
            {
                print_sep(osep, buff, bufc);
            }
            bFirst = false;
            safe_str(sc1.m_ptrs[i1].str, buff, bufc);
            oldp = &sc1.m_ptrs[i1];
            i1++;
            while (  i1 < n1
                  && cf(sc1.m_ptrs[i1], *oldp) == 0)
            {
                i1++;
            }
        }
    }

    do_asort_finish(&sc1);
    do_asort_finish(&sc2);
    free_lbuf(list1);
    free_lbuf(list2);
}

static FUNCTION(fun_setunion)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_INIT|DELIM_STRING))
    {
        return;
    }

    handle_sets(nfargs, fargs, buff, bufc, SET_UNION, sep, osep);
}

static FUNCTION(fun_setdiff)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_INIT|DELIM_STRING))
    {
        return;
    }

    handle_sets(nfargs, fargs, buff, bufc, SET_DIFF, sep, osep);
}

static FUNCTION(fun_setinter)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    SEP osep = sep;
    if (!OPTIONAL_DELIM(4, osep, DELIM_NULL|DELIM_CRLF|DELIM_INIT|DELIM_STRING))
    {
        return;
    }

    handle_sets(nfargs, fargs, buff, bufc, SET_INTERSECT, sep, osep);
}

/* ---------------------------------------------------------------------------
 * rjust, ljust, center: Justify or center text, specifying fill character.
 */
static void centerjustcombo
(
    int iType,
    UTF8 *buff,
    UTF8 **bufc,
    UTF8 *fargs[],
    int nfargs,
    bool bTrunc
)
{
    // Width must be a number.
    //
    if (!is_integer(fargs[1], nullptr))
    {
        return;
    }
    LBUF_OFFSET nWidth = static_cast<LBUF_OFFSET>(mux_atol(strip_color(fargs[1])));
    if (0 == nWidth)
    {
        return;
    }

    if (LBUF_SIZE <= nWidth)
    {
        safe_range(buff, bufc);
        return;
    }

    const unsigned char *pStr = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t lenStr = strlen(reinterpret_cast<const char *>(pStr));

    const unsigned char *pFill = nullptr;
    size_t lenFill = 0;
    if (nfargs == 3 && *fargs[2])
    {
        pFill = reinterpret_cast<const unsigned char *>(fargs[2]);
        lenFill = strlen(reinterpret_cast<const char *>(pFill));
    }

    unsigned char out[LBUF_SIZE];
    size_t nOut = 0;

    if (iType == CJC_CENTER)
    {
        nOut = co_center(out, pStr, lenStr, nWidth,
                         pFill, lenFill, bTrunc ? 1 : 0);
    }
    else if (iType == CJC_LJUST)
    {
        nOut = co_ljust(out, pStr, lenStr, nWidth,
                        pFill, lenFill, bTrunc ? 1 : 0);
    }
    else
    {
        nOut = co_rjust(out, pStr, lenStr, nWidth,
                        pFill, lenFill, bTrunc ? 1 : 0);
    }

    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    if (nOut > nMax) nOut = nMax;
    memcpy(*bufc, out, nOut);
    *bufc += nOut;
    **bufc = '\0';
}

static FUNCTION(fun_ljust)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    centerjustcombo(CJC_LJUST, buff, bufc, fargs, nfargs, true);
}

static FUNCTION(fun_rjust)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    centerjustcombo(CJC_RJUST, buff, bufc, fargs, nfargs, true);
}

static FUNCTION(fun_center)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    centerjustcombo(CJC_CENTER, buff, bufc, fargs, nfargs, true);
}

static FUNCTION(fun_lpad)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    centerjustcombo(CJC_LJUST, buff, bufc, fargs, nfargs, false);
}

static FUNCTION(fun_rpad)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    centerjustcombo(CJC_RJUST, buff, bufc, fargs, nfargs, false);
}

static FUNCTION(fun_cpad)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    centerjustcombo(CJC_CENTER, buff, bufc, fargs, nfargs, false);
}

// printf(<format>, <arg1>, <arg2>, ...) — Formatted output.
//
// Format specifiers:
//   %s   — string (ANSI-aware width)
//   %d   — integer
//   %f   — floating point
//   %c   — first character
//   %%   — literal %
//
// Modifiers (between % and type):
//   -    — left-justify
//   =    — center (MU* extension)
//   0    — zero-pad (numeric only)
//   N    — field width (visible columns)
//   .N   — precision (max columns for %s, decimal places for %f)
//
static FUNCTION(fun_printf)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs < 1)
    {
        return;
    }

    const UTF8 *fmt = fargs[0];
    int iArg = 1; // next argument index

    while (*fmt)
    {
        if ('%' != *fmt)
        {
            // Literal character — copy it.
            //
            size_t d = utf8_FirstByte[*fmt];
            safe_copy_buf(fmt, d, buff, bufc);
            fmt += d;
            continue;
        }

        // Skip the '%'.
        //
        fmt++;

        // Parse flags.
        //
        bool bLeft = false;
        bool bCenter = false;
        bool bZero = false;

        bool bParsing = true;
        while (*fmt && bParsing)
        {
            switch (*fmt)
            {
            case '-': bLeft = true;   fmt++; break;
            case '^': bCenter = true; fmt++; break;
            case '0': bZero = true;   fmt++; break;
            default:  bParsing = false;      break;
            }
        }

        // Parse width.
        //
        int nWidth = 0;
        bool bWidth = false;
        while (mux_isdigit(*fmt))
        {
            nWidth = nWidth * 10 + (*fmt - '0');
            fmt++;
            bWidth = true;
        }

        // Parse precision.
        //
        int nPrec = -1;
        if ('.' == *fmt)
        {
            fmt++;
            nPrec = 0;
            while (mux_isdigit(*fmt))
            {
                nPrec = nPrec * 10 + (*fmt - '0');
                fmt++;
            }
        }

        // Parse conversion.
        //
        if ('\0' == *fmt)
        {
            break;
        }

        UTF8 cConv = *fmt++;

        if ('%' == cConv)
        {
            safe_chr('%', buff, bufc);
            continue;
        }

        // Consume the next argument.
        //
        const UTF8 *pArg = T("");
        if (iArg < nfargs)
        {
            pArg = fargs[iArg++];
        }

        // Format the value into a temporary buffer.
        //
        UTF8 valBuf[LBUF_SIZE];
        UTF8 *pVal = valBuf;

        switch (cConv)
        {
        case 's':
            pVal = const_cast<UTF8 *>(pArg);
            break;

        case 'd':
        case 'i':
            {
                long v = mux_atol(pArg);
                mux_ltoa(v, valBuf);
            }
            break;

        case 'f':
            {
                double v = mux_atof(pArg, false);
                int places = (nPrec >= 0) ? nPrec : 6;
                if (places > 20)
                {
                    places = 20;
                }

                // Use fval-style output for default precision,
                // or fixed-point for explicit precision.
                //
                if (nPrec < 0)
                {
                    fval(valBuf, &pVal, v);
                    pVal = valBuf;
                }
                else
                {
                    snprintf(reinterpret_cast<char *>(valBuf),
                             sizeof(valBuf), "%.*f", places, v);
                }
                nPrec = -1; // consumed by %f
            }
            break;

        case 'c':
            {
                size_t d = utf8_FirstByte[*pArg];
                if (d > 0 && *pArg)
                {
                    memcpy(valBuf, pArg, d);
                    valBuf[d] = '\0';
                }
                else
                {
                    valBuf[0] = '\0';
                }
            }
            break;

        default:
            // Unknown conversion — output literal.
            //
            safe_chr('%', buff, bufc);
            safe_chr(cConv, buff, bufc);
            continue;
        }

        // Apply width and alignment using co_* ANSI-aware primitives.
        //
        const unsigned char *pv = reinterpret_cast<const unsigned char *>(pVal);
        size_t pvlen = strlen(reinterpret_cast<const char *>(pVal));

        if (!bWidth || nWidth <= 0)
        {
            // No width — apply precision truncation for %s, then output.
            //
            if ('s' == cConv && nPrec >= 0)
            {
                unsigned char trunc[LBUF_SIZE];
                size_t nOut = co_copy_columns(trunc, pv, pv + pvlen,
                                  static_cast<size_t>(nPrec));
                size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
                if (nOut > nMax) nOut = nMax;
                memcpy(*bufc, trunc, nOut);
                *bufc += nOut;
            }
            else
            {
                safe_str(pVal, buff, bufc);
            }
            continue;
        }

        if (LBUF_SIZE <= static_cast<size_t>(nWidth))
        {
            safe_range(buff, bufc);
            continue;
        }

        // For numeric types with zero-pad, handle directly.
        //
        if (bZero && !bLeft && !bCenter
            && ('d' == cConv || 'i' == cConv || 'f' == cConv))
        {
            size_t vlen = strlen(reinterpret_cast<const char *>(pVal));
            bool bNeg = ('-' == pVal[0]);
            const UTF8 *pDigits = bNeg ? pVal + 1 : pVal;
            size_t dlen = bNeg ? vlen - 1 : vlen;

            if (bNeg)
            {
                safe_chr('-', buff, bufc);
            }
            for (size_t i = dlen; i < static_cast<size_t>(nWidth) - (bNeg ? 1 : 0); i++)
            {
                safe_chr('0', buff, bufc);
            }
            safe_str(pDigits, buff, bufc);
            continue;
        }

        // ANSI-aware padding using co_* primitives.
        //
        size_t nValWidth = co_visual_width(pv, pvlen);

        // Apply precision truncation for %s.
        //
        unsigned char truncVal[LBUF_SIZE];
        size_t truncLen = pvlen;
        const unsigned char *pOutput = pv;
        if ('s' == cConv && nPrec >= 0
            && nValWidth > static_cast<size_t>(nPrec))
        {
            truncLen = co_copy_columns(truncVal, pv, pv + pvlen,
                           static_cast<size_t>(nPrec));
            pOutput = truncVal;
            nValWidth = static_cast<size_t>(nPrec);
        }

        if (nValWidth >= static_cast<size_t>(nWidth))
        {
            // Value fills or exceeds width — truncate to width.
            //
            unsigned char out[LBUF_SIZE];
            size_t nOut = co_copy_columns(out, pOutput,
                              pOutput + truncLen,
                              static_cast<size_t>(nWidth));
            size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
            if (nOut > nMax) nOut = nMax;
            memcpy(*bufc, out, nOut);
            *bufc += nOut;
            continue;
        }

        size_t nPad = static_cast<size_t>(nWidth) - nValWidth;
        size_t nLeading = 0;
        size_t nTrailing = 0;

        if (bCenter)
        {
            nLeading = nPad / 2;
            nTrailing = nPad - nLeading;
        }
        else if (bLeft)
        {
            nTrailing = nPad;
        }
        else
        {
            nLeading = nPad;
        }

        // Leading padding.
        //
        for (size_t i = 0; i < nLeading; i++)
        {
            safe_chr(' ', buff, bufc);
        }

        // Value.
        //
        {
            size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
            if (truncLen > nMax) truncLen = nMax;
            memcpy(*bufc, pOutput, truncLen);
            *bufc += truncLen;
        }

        // Trailing padding.
        //
        for (size_t i = 0; i < nTrailing; i++)
        {
            safe_chr(' ', buff, bufc);
        }
    }
}

/* ---------------------------------------------------------------------------
 * setq, setr, r: set and read global registers.
 */

static FUNCTION(fun_setq)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs < 2 || (nfargs % 2) != 0)
    {
        safe_str(T("#-1 FUNCTION (SETQ) EXPECTS AN EVEN NUMBER OF ARGUMENTS"), buff, bufc);
        return;
    }

    for (int i = 0; i < nfargs; i += 2)
    {
        int regnum;
        if (IsSingleCharReg(fargs[i], regnum))
        {
            size_t n = strlen(reinterpret_cast<char *>(fargs[i + 1]));
            RegAssign(&mudstate.global_regs[regnum], n, fargs[i + 1]);
        }
        else
        {
            size_t nName = strlen(reinterpret_cast<char *>(fargs[i]));
            if (IsValidNamedReg(fargs[i], nName))
            {
                size_t n = strlen(reinterpret_cast<char *>(fargs[i + 1]));
                NamedRegAssign(mudstate.named_regs, fargs[i], nName, n, fargs[i + 1]);
            }
            else
            {
                safe_str(T("#-1 INVALID GLOBAL REGISTER"), buff, bufc);
                return;
            }
        }
    }
}

static FUNCTION(fun_setr)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs < 2 || (nfargs % 2) != 0)
    {
        safe_str(T("#-1 FUNCTION (SETR) EXPECTS AN EVEN NUMBER OF ARGUMENTS"), buff, bufc);
        return;
    }

    for (int i = 0; i < nfargs; i += 2)
    {
        int regnum;
        if (IsSingleCharReg(fargs[i], regnum))
        {
            size_t n = strlen(reinterpret_cast<char *>(fargs[i + 1]));
            RegAssign(&mudstate.global_regs[regnum], n, fargs[i + 1]);
            if (i + 2 >= nfargs)
            {
                safe_copy_buf(fargs[i + 1], n, buff, bufc);
            }
        }
        else
        {
            size_t nName = strlen(reinterpret_cast<char *>(fargs[i]));
            if (IsValidNamedReg(fargs[i], nName))
            {
                size_t n = strlen(reinterpret_cast<char *>(fargs[i + 1]));
                NamedRegAssign(mudstate.named_regs, fargs[i], nName, n, fargs[i + 1]);
                if (i + 2 >= nfargs)
                {
                    safe_copy_buf(fargs[i + 1], n, buff, bufc);
                }
            }
            else
            {
                safe_str(T("#-1 INVALID GLOBAL REGISTER"), buff, bufc);
                return;
            }
        }
    }
}

static FUNCTION(fun_r)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int regnum;
    if (IsSingleCharReg(fargs[0], regnum))
    {
        if (mudstate.global_regs[regnum])
        {
            safe_copy_buf(mudstate.global_regs[regnum]->reg_ptr,
                mudstate.global_regs[regnum]->reg_len, buff, bufc);
        }
    }
    else
    {
        size_t nName = strlen(reinterpret_cast<char *>(fargs[0]));
        if (IsValidNamedReg(fargs[0], nName))
        {
            reg_ref *rr = NamedRegRead(mudstate.named_regs, fargs[0], nName);
            if (rr && rr->reg_len > 0)
            {
                safe_copy_buf(rr->reg_ptr, rr->reg_len, buff, bufc);
            }
        }
        else
        {
            safe_str(T("#-1 INVALID GLOBAL REGISTER"), buff, bufc);
        }
    }
}

#if defined(STUB_SLAVE)
static FUNCTION(fun_rserror)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (mudstate.pResultsSet)
    {
        int iError = mudstate.pResultsSet->GetError();
        switch (iError)
        {
        case QS_SUCCESS:
            safe_str(T("SUCCESS"), buff, bufc);
            break;

        case QS_NO_SESSION:
            safe_str(T("#-2 NO SESSION"), buff, bufc);
            break;

        case QS_SQL_UNAVAILABLE:
            safe_str(T("#-3 UNAVAILABLE"), buff, bufc);
            break;

        case QS_QUERY_ERROR:
            safe_str(T("#-4 QUERY_ERROR"), buff, bufc);
            break;
        }
    }
    else
    {
        safe_str(T("#-1 NO RESULTS SET"), buff, bufc);
    }
}

static FUNCTION(fun_rsrelease)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (mudstate.pResultsSet)
    {
        mudstate.pResultsSet->Release();
        mudstate.pResultsSet = nullptr;
    }
    else
    {
        safe_str(T("#-1 NO RESULTS SET"), buff, bufc);
    }
}

static FUNCTION(fun_rsrows)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (mudstate.pResultsSet)
    {
        safe_str(mux_ltoa_t(mudstate.pResultsSet->GetRowCount()), buff, bufc);
    }
    else
    {
        safe_str(T("#-1 NO RESULTS SET"), buff, bufc);
    }
}

static FUNCTION(fun_rsnext)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (mudstate.pResultsSet)
    {
        int nRows;
        int i = mudstate.iRow + 1;
        if (  i < 0
           || (nRows = mudstate.pResultsSet->GetRowCount()) <= i)
        {
            safe_str(T("#-1 END OF TABLE"), buff, bufc);
        }
        else
        {
            mudstate.iRow = i;
            safe_str(mux_ltoa_t(i), buff, bufc);
        }
    }
    else
    {
        safe_str(T("#-1 NO RESULTS SET"), buff, bufc);
    }
}

static FUNCTION(fun_rsprev)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (mudstate.pResultsSet)
    {
        int nRows;
        int i = mudstate.iRow - 1;
        if (  i < 0
           || (nRows = mudstate.pResultsSet->GetRowCount()) <= i)
        {
            safe_str(T("#-1 END OF TABLE"), buff, bufc);
        }
        else
        {
            mudstate.iRow = i;
            safe_str(mux_ltoa_t(i), buff, bufc);
        }
    }
    else
    {
        safe_str(T("#-1 NO RESULTS SET"), buff, bufc);
    }
}

FUNCTION(fun_rsrec)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr == mudstate.pResultsSet)
    {
        safe_str(T("#-1 NO RESULTS SET"), buff, bufc);
        return;
    }

    int nRows;
    if (  mudstate.iRow < 0
       || (nRows = mudstate.pResultsSet->GetRowCount()) <= mudstate.iRow)
    {
        safe_str(T("#-1 END OF TABLE"), buff, bufc);
    }

    SEP sepColumn;
    if (!OPTIONAL_DELIM(1, sepColumn, DELIM_NULL|DELIM_CRLF|DELIM_STRING))
    {
        return;
    }

    bool bFirst = true;
    const UTF8 *pField = mudstate.pResultsSet->FirstField(mudstate.iRow);
    while (nullptr != pField)
    {
        if (!bFirst)
        {
            print_sep(sepColumn, buff, bufc);
        }
        else
        {
            bFirst = false;
        }
        const UTF8 *pStr = pField + sizeof(size_t);
        safe_str(pStr, buff, bufc);
        pField = mudstate.pResultsSet->NextField();
    }
}

FUNCTION(fun_rsrecnext)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!mudstate.pResultsSet)
    {
        safe_str(T("#-1 NO RESULTS SET"), buff, bufc);
        return;
    }

    SEP sepColumn;
    if (!OPTIONAL_DELIM(1, sepColumn, DELIM_NULL|DELIM_CRLF|DELIM_STRING))
    {
        return;
    }

    int nRows;
    if (  mudstate.iRow < 0
       || (nRows = mudstate.pResultsSet->GetRowCount()) <= mudstate.iRow)
    {
        safe_str(T("#-1 END OF TABLE"), buff, bufc);
        return;
    }

    bool bFirst = true;
    const UTF8 *pField = mudstate.pResultsSet->FirstField(mudstate.iRow);
    while (nullptr != pField)
    {
        if (!bFirst)
        {
            print_sep(sepColumn, buff, bufc);
        }
        else
        {
            bFirst = false;
        }
        const UTF8 *pStr = pField + sizeof(size_t);
        safe_str(pStr, buff, bufc);
        pField = mudstate.pResultsSet->NextField();
    }

    int i = mudstate.iRow + 1;
    if (  -1 <= i
       && i <= nRows)
    {
        mudstate.iRow = i;
    }
}

FUNCTION(fun_rsrecprev)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!mudstate.pResultsSet)
    {
        safe_str(T("#-1 NO RESULTS SET"), buff, bufc);
        return;
    }

    SEP sepColumn;
    if (!OPTIONAL_DELIM(1, sepColumn, DELIM_NULL|DELIM_CRLF|DELIM_STRING))
    {
        return;
    }

    int nRows;
    if (  mudstate.iRow < 0
       || (nRows = mudstate.pResultsSet->GetRowCount()) <= mudstate.iRow)
    {
        safe_str(T("#-1 END OF TABLE"), buff, bufc);
        return;
    }

    bool bFirst = true;
    const UTF8 *pField = mudstate.pResultsSet->FirstField(mudstate.iRow);
    while (nullptr != pField)
    {
        if (!bFirst)
        {
            print_sep(sepColumn, buff, bufc);
        }
        else
        {
            bFirst = false;
        }
        const UTF8 *pStr = pField + sizeof(size_t);
        safe_str(pStr, buff, bufc);
        pField = mudstate.pResultsSet->NextField();
    }

    int i = mudstate.iRow + 1;
    if (  -1 <= i
       && i <= nRows)
    {
        mudstate.iRow = i;
    }
}

#endif // STUB_SLAVE

/* ---------------------------------------------------------------------------
 * isdbref: is the argument a valid dbref?
 */

static FUNCTION(fun_isdbref)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool bResult = false;
    if (nfargs >= 1)
    {
        UTF8 *p = fargs[0];
        while (mux_isspace(*p))
        {
            p++;
        }
        if (NUMBER_TOKEN == p[0])
        {
            p++;
            dbref dbitem = parse_dbref(p);
            bResult = Good_obj(dbitem);
        }
    }
    safe_bool(bResult, buff, bufc);
}

static FUNCTION(fun_isobjid)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool bResult = false;
    if (nfargs >= 1)
    {
        UTF8 *p = fargs[0];
        while (mux_isspace(*p))
        {
            p++;
        }

        if (NUMBER_TOKEN == p[0])
        {
            p++;

            // Must have digits:digits pattern.
            //
            if (mux_isdigit(*p))
            {
                const UTF8 *q = p + 1;
                while (mux_isdigit(*q))
                {
                    q++;
                }

                if (':' == *q)
                {
                    dbref dbitem = parse_dbref(p);
                    bResult = Good_obj(dbitem);
                }
            }
        }
    }
    safe_bool(bResult, buff, bufc);
}

/* ---------------------------------------------------------------------------
 * trim: trim off unwanted white space.
 */
static FUNCTION(fun_trim)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool bLeft = true, bRight = true;
    const UTF8 *p = nullptr;
    size_t n = 0;

    if (nfargs >= 2)
    {
        switch (*fargs[1])
        {
        case 'l':
        case 'L':
            bRight = false;
            break;

        case 'r':
        case 'R':
            bLeft = false;
            break;
        }

        if (nfargs >= 3)
        {
            p = fargs[2];
            n = strlen(reinterpret_cast<const char *>(p));
        }
    }

    if (n <= 1)
    {
        // Single-char trim (or default space): use co_trim.
        //
        unsigned char trim_char = (n == 1) ? p[0] : ' ';
        int trim_flags = (bLeft ? 1 : 0) | (bRight ? 2 : 0);
        size_t nLen = strlen(reinterpret_cast<const char *>(fargs[0]));
        unsigned char out[LBUF_SIZE];
        size_t nOut = co_trim(out,
                              reinterpret_cast<const unsigned char *>(fargs[0]),
                              nLen, trim_char, trim_flags);
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        if (nOut > nMax) nOut = nMax;
        memcpy(*bufc, out, nOut);
        *bufc += nOut;
        **bufc = '\0';
    }
    else
    {
        // Pattern trim: use co_trim_pattern.
        //
        int trim_flags = (bLeft ? 1 : 0) | (bRight ? 2 : 0);
        size_t nLen = strlen(reinterpret_cast<const char *>(fargs[0]));
        unsigned char out[LBUF_SIZE];
        size_t nOut = co_trim_pattern(out,
                          reinterpret_cast<const unsigned char *>(fargs[0]),
                          nLen,
                          reinterpret_cast<const unsigned char *>(p), n,
                          trim_flags);
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        if (nOut > nMax) nOut = nMax;
        memcpy(*bufc, out, nOut);
        *bufc += nOut;
        **bufc = '\0';
    }
}

static FUNCTION(fun_config)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs == 1)
    {
        cf_display(executor, fargs[0], buff, bufc);
    }
    else
    {
        cf_list(executor, buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_bittype adapted from RhostMUSH. Used with permission.
//
static int return_bit(dbref player)
{
    if (God(player))
    {
        return 7;
    }
    // 6 is Rhost Immortal. We don't have an equivalent (yet?).
    if (Wizard(player))
    {
        return 5;
    }
    if (Royalty(player))
    {
        return 4;
    }
    if (Staff(player) || Builder(player))
    {
        return 3;
    }
    if (Head(player) || Immortal(player))
    {
        return 2;
    }
    if (!(Uninspected(player) || Guest(player)))
    {
        return 1;
    }
    return 0;
}

static FUNCTION(fun_bittype)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref target;
    if (nfargs == 1)
    {
        target = match_thing(executor, fargs[0]);
    }
    else
    {
        target = executor;
    }
    if (!Good_obj(target))
    {
        return;
    }
    safe_ltoa(return_bit(target), buff, bufc);
}

static FUNCTION(fun_error)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (  Good_obj(mudconf.global_error_obj)
       && !Going(mudconf.global_error_obj))
    {
        dbref aowner;
        int aflags;
        UTF8 *errtext = atr_get("fun_error.8353", mudconf.global_error_obj, A_VA, &aowner, &aflags);
        UTF8 *errbuff = alloc_lbuf("process_command.error_msg");
        UTF8 *errbufc = errbuff;
        if (nfargs == 1)
        {
            const UTF8 *arg = fargs[0];
            mux_exec(errtext, LBUF_SIZE-1, errbuff, &errbufc, mudconf.global_error_obj, caller, enactor,
                AttrTrace(aflags, EV_TOP|EV_EVAL|EV_FCHECK|EV_STRIP_CURLY), &arg, 1);
            *errbufc = '\0';
        }
        else
        {
            mux_exec(errtext, LBUF_SIZE-1, errbuff, &errbufc, mudconf.global_error_obj, caller, enactor,
                AttrTrace(aflags, EV_TOP|EV_EVAL|EV_FCHECK|EV_STRIP_CURLY),
                nullptr, 0);
            *errbufc = '\0';
        }
        safe_str(errbuff, buff, bufc);
        free_lbuf(errtext);
        free_lbuf(errbuff);
    }
    else
    {
        safe_str(T("Huh?  (Type \xE2\x80\x9Chelp\xE2\x80\x9D for help.)"), buff, bufc);
    }
}

static FUNCTION(fun_strip)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (fargs[0][0] == '\0')
    {
        return;
    }

    // Strip color from source to get plain text.
    //
    unsigned char plain[LBUF_SIZE];
    size_t nPlain = co_strip_color(plain,
        reinterpret_cast<const unsigned char *>(fargs[0]),
        strlen(reinterpret_cast<const char *>(fargs[0])));

    if (  1 < nfargs
       && '\0' != fargs[1][0])
    {
        // Build strip table from the character set.
        //
        bool strip_table[UCHAR_MAX + 1];
        memset(strip_table, false, sizeof(strip_table));

        const UTF8 *pSet = strip_color(fargs[1]);
        while ('\0' != *pSet)
        {
            UTF8 ch = *pSet++;
            if (mux_isprint_ascii(ch))
            {
                strip_table[static_cast<unsigned char>(ch)] = true;
            }
        }

        // Copy plain text, skipping stripped characters.
        //
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        for (size_t i = 0; i < nPlain && nMax > 0; i++)
        {
            if (!strip_table[plain[i]])
            {
                **bufc = static_cast<UTF8>(plain[i]);
                (*bufc)++;
                nMax--;
            }
        }
        **bufc = '\0';
    }
    else
    {
        // No strip set — just output plain text.
        //
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        if (nPlain > nMax) nPlain = nMax;
        memcpy(*bufc, plain, nPlain);
        *bufc += nPlain;
        **bufc = '\0';
    }
}

#define DEFAULT_WIDTH 78

static FUNCTION(fun_wrap)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // ARG 2: Width. Default: 78.
    //
    int nWidth = DEFAULT_WIDTH;
    if (  2 <= nfargs
       && '\0' != fargs[1][0])
    {
        nWidth = mux_atol(fargs[1]);
        if (  nWidth < 1
           || nWidth >= LBUF_SIZE)
        {
            safe_range(buff, bufc);
            return;
        }
    }

    // ARG 3: Justification. Default: Left.
    //
    int iJustKey = CJC_LJUST;
    if (  3 <= nfargs
       && '\0' != fargs[2][0])
    {
        UTF8 cJust = fargs[2][0];
        switch (cJust)
        {
        case 'l':
        case 'L':
            iJustKey = CJC_LJUST;
            break;

        case 'r':
        case 'R':
            iJustKey = CJC_RJUST;
            break;

        case 'c':
        case 'C':
            iJustKey = CJC_CENTER;
            break;

        default:
            safe_str(T("#-1 INVALID JUSTIFICATION SPECIFIED"), buff, bufc);
            return;
        }
    }

    // ARG 4: Left padding. Default: blank.
    //
    UTF8 *pLeft = nullptr;
    size_t nLeft = 0;
    if (  4 <= nfargs
       && '\0' != fargs[3][0])
    {
        pLeft = fargs[3];
        strip_color(pLeft, nullptr, &nLeft);
    }

    // ARG 5: Right padding. Default: blank.
    //
    UTF8 *pRight = nullptr;
    size_t nRight = 0;
    if (  5 <= nfargs
       && '\0' != fargs[4][0])
    {
        pRight = fargs[4];
        strip_color(pRight, nullptr, &nRight);
    }

    // ARG 6: Hanging indent. Default: 0.
    //
    int nHanging = 0;
    if (  6 <= nfargs
       && '\0' != fargs[5][0])
    {
        nHanging = mux_atol(fargs[5]);
    }

    // ARG 7: Output separator. Default: line break.
    //
    const UTF8 *pOSep = T("\r\n");
    LBUF_OFFSET nOSepBytes = 2;
    if (  7 <= nfargs
       && '\0' != fargs[6][0])
    {
        if (!strcmp(reinterpret_cast<char *>(fargs[6]), "@@"))
        {
            pOSep = T("");
            nOSepBytes = 0;
        }
        else
        {
            pOSep = fargs[6];
            nOSepBytes = static_cast<LBUF_OFFSET>(mux_strlen(pOSep));
        }
    }

    // ARG 8: First line width. Default: same as arg 2.
    int nFirstWidth = nWidth;
    if (  8 <= nfargs
       && '\0' != fargs[7][0])
    {
        nFirstWidth = mux_atol(fargs[7]);
        if (  nFirstWidth < 1
           || nFirstWidth >= LBUF_SIZE)
        {
            safe_range(buff, bufc);
            return;
        }
    }

    *bufc += linewrap_general( fargs[0], static_cast<LBUF_OFFSET>(nWidth),
                               *bufc, LBUF_SIZE - (*bufc - buff) - 1,
                               pLeft, static_cast<LBUF_OFFSET>(nLeft),
                               pRight, static_cast<LBUF_OFFSET>(nRight),
                               iJustKey, static_cast<LBUF_OFFSET>(nHanging),
                               pOSep, nOSepBytes, static_cast<LBUF_OFFSET>(nFirstWidth));
}

typedef struct
{
    int  iBase;
    UTF8 chLetter;
    int  nName;
    const UTF8 *pName;

} RADIX_ENTRY;

#define N_RADIX_ENTRIES 7
static const RADIX_ENTRY reTable[N_RADIX_ENTRIES] =
{
    { 31556926, 'y', 4, T("year")   },  // Average solar year.
    {  2629743, 'M', 5, T("month")  },  // Average month.
    {   604800, 'w', 4, T("week")   },  // 7 days.
    {    86400, 'd', 3, T("day")    },
    {     3600, 'h', 4, T("hour")   },
    {       60, 'm', 6, T("minute") },
    {        1, 's', 6, T("second") }
};

#define IYEARS   0
#define IMONTHS  1
#define IWEEKS   2
#define IDAYS    3
#define IHOURS   4
#define IMINUTES 5
#define ISECONDS 6

// This routine supports most of the time formats using the above
// table.
//
static void GeneralTimeConversion
(
    UTF8 *Buffer,
    long Seconds,
    int iStartBase,
    int iEndBase,
    bool bSingleTerm,
    bool bNames
)
{
    if (Seconds < 0)
    {
        Seconds = 0;
    }

    UTF8 *p = Buffer;
    int iValue;

    if (  iStartBase < 0
       || N_RADIX_ENTRIES <= iStartBase
       || iEndBase < 0
       || N_RADIX_ENTRIES <= iEndBase)
    {
        *p++ = '\0';
        return;
    }

    for (int i = iStartBase; i <= iEndBase; i++)
    {
        if (reTable[i].iBase <= Seconds || i == iEndBase)
        {
            // Division and remainder.
            //
            iValue = Seconds/reTable[i].iBase;
            Seconds -= iValue * reTable[i].iBase;

            if (iValue != 0 || i == iEndBase)
            {
                if (p != Buffer)
                {
                    *p++ = ' ';
                }
                p += mux_ltoa(iValue, p);
                if (bNames)
                {
                    // Use the names with the correct pluralization.
                    //
                    *p++ = ' ';
                    memcpy(p, reTable[i].pName, reTable[i].nName);
                    p += reTable[i].nName;
                    if (iValue != 1)
                    {
                        // More than one or zero.
                        //
                        *p++ = 's';
                    }
                }
                else
                {
                    *p++ = reTable[i].chLetter;
                }
            }
            if (bSingleTerm)
            {
                break;
            }
        }
    }
    *p++ = '\0';
}

// These buffers are used by:
//
//     digit_format  (23 bytes) uses TimeBuffer80,
//     time_format_1 (12 bytes) uses TimeBuffer80,
//     time_format_2 (17 bytes) uses TimeBuffer64,
//     expand_time   (34 bytes) uses TimeBuffer64,
//     write_time    (76 bytes) uses TimeBuffer80.
//
// time_format_1 and time_format_2 are called from within the same
// printf, so they must use different buffers.
//
// We pick 64 as a round number.
//
static UTF8 TimeBuffer64[64];
static UTF8 TimeBuffer80[80];

// Show time in days, hours, and minutes
//
// 2^63/86400 is 1.07E14 which is at most 15 digits.
// '(15)d (2):(2)\0' is at most 23 characters.
//
static const UTF8 *digit_format(int Seconds)
{
    if (Seconds < 0)
    {
        Seconds = 0;
    }

    // We are showing the time in minutes. 59s --> 0m
    //

    // Divide the time down into days, hours, and minutes.
    //
    int Days = Seconds / 86400;
    Seconds -= Days * 86400;

    int Hours = Seconds / 3600;
    Seconds -= Hours * 3600;

    int Minutes = Seconds / 60;

    if (Days > 0)
    {
        mux_sprintf(TimeBuffer80, sizeof(TimeBuffer80), T("%dd %02d:%02d"), Days, Hours, Minutes);
    }
    else
    {
        mux_sprintf(TimeBuffer80, sizeof(TimeBuffer80), T("%02d:%02d"), Hours, Minutes);
    }
    return TimeBuffer80;
}

// Show time in one of the following formats limited by a width of 8, 9, 10,
// or 11 places and depending on the value to display:
//
// Width:8
//         Z9:99            0 to         86,399
//      9d 99:99       86,400 to        863,999
//      ZZ9d 99h      864,000 to     86,396,459
//      ZZZ9w 9d   86,396,460 to  6,047,913,659
//
// Width:9
//         Z9:99            0 to         86,399
//     Z9d 99:99       86,400 to      8,639,999
//     ZZZ9d 99h    8,640,000 to    863,996,459
//      ZZZ9w 9d  863,996,460 to  6,047,913,659
//
// Width:10
//         Z9:99            0 to         86,399
//    ZZ9d 99:99       86,400 to     86,399,999
//    ZZZZ9d 99h   86,400,000 to  8,639,996,459
//
// Width:11
//         Z9:99            0 to         86,399
//   ZZZ9d 99:99       86,400 to    863,999,999
//   ZZZZZ9d 99h  864,000,000 to 86,399,996,459
//
static int tf1_width_table[4][3] =
{
    { 86399,    863999,  86396459, },
    { 86399,   8639999, 863996459, },
    { 86399,  86399999,   INT_MAX, },
    { 86399, 863999999,   INT_MAX, }
};

static struct
{
    const UTF8 *specs[4];
    int         div[3];
} tf1_case_table[4] =
{
    {
        { T("   %2d:%02d"), T("    %2d:%02d"), T("     %2d:%02d"), T("      %2d:%02d") },
        { 3600, 60, 1 }
    },
    {
        { T("%dd %02d:%02d"), T("%2dd %02d:%02d"), T("%3dd %02d:%02d"), T("%4dd %02d:%02d") },
        { 86400, 3600, 60 }
    },
    {
        { T("%3dd %02dh"), T("%4dd %02dh"), T("%5dd %02dh"), T("%6dd %02dh") },
        { 86400, 3600, 1 }
    },
    {
        { T("%4dw %d"), T("%4dw %d"), T(""), T("") },
        { 604800, 86400, 1 }
    }
};

const UTF8 *time_format_1(int Seconds, size_t maxWidth)
{
    if (Seconds < 0)
    {
        Seconds = 0;
    }

    if (  maxWidth < 8
       || 11 < maxWidth)
    {
        mux_strncpy(TimeBuffer80, T("???"), sizeof(TimeBuffer80)-1);
        return TimeBuffer80;
    }
    size_t iWidth = maxWidth - 8;

    int iCase = 0;
    while (  iCase < 3
          && tf1_width_table[iWidth][iCase] < Seconds)
    {
        iCase++;
    }

    int i, n[3];
    for (i = 0; i < 3; i++)
    {
        n[i] = Seconds / tf1_case_table[iCase].div[i];
        Seconds -= n[i] *tf1_case_table[iCase].div[i];
    }
    mux_sprintf(TimeBuffer80, sizeof(TimeBuffer80), tf1_case_table[iCase].specs[iWidth], n[0], n[1], n[2]);
    return TimeBuffer80;
}

// Show time in days, hours, minutes, or seconds.
//
const UTF8 *time_format_2(int Seconds)
{
    // 2^63/86400 is 1.07E14 which is at most 15 digits.
    // '(15)d\0' is at most 17 characters.
    //
    GeneralTimeConversion(TimeBuffer64, Seconds, IYEARS, ISECONDS, true, false);
    return TimeBuffer64;
}

// Del's added functions for dooferMUX ! :)
// D.Piper (del@doofer.org) 1997 & 2000
//

// expand_time - Written (short) time format.
//
static const UTF8 *expand_time(int Seconds)
{
    // 2^63/31556926 is 292277265436 which is at most 12 digits.
    // '(12)Y (2)M (1)w (1)d (2)h (2)m (2)s\0' is at most 34 characters.
    //
    GeneralTimeConversion(TimeBuffer64, Seconds, IYEARS, ISECONDS, false, false);
    return TimeBuffer64;
}

// write_time - Written (long) time format.
//
static const UTF8 *write_time(int Seconds)
{
    // 2^63/31556926 is 292277265436 which is at most 12 digits.
    // '(12) years (2) months (1) weeks (1) days (2) hours (2) minutes (2) seconds\0' is
    // at most 76 characters.
    //
    GeneralTimeConversion(TimeBuffer80, Seconds, IYEARS, ISECONDS, false, true);
    return TimeBuffer80;
}

// digittime - Digital format time ([(days)d]HH:MM) from given
// seconds. D.Piper - May 1997 & April 2000
//
static FUNCTION(fun_digittime)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int tt = mux_atol(fargs[0]);
    safe_str(digit_format(tt), buff, bufc);
}

// singletime - Single element time from given seconds.
// D.Piper - May 1997 & April 2000
//
static FUNCTION(fun_singletime)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int tt = mux_atol(fargs[0]);
    safe_str(time_format_2(tt), buff, bufc);
}

// exptime - Written (short) time from given seconds
// D.Piper - May 1997 & April 2000
//
static FUNCTION(fun_exptime)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int tt = mux_atol(fargs[0]);
    safe_str(expand_time(tt), buff, bufc);
}

// writetime - Written (long) time from given seconds
// D.Piper - May 1997 & April 2000
//
static FUNCTION(fun_writetime)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int tt = mux_atol(fargs[0]);
    safe_str(write_time(tt), buff, bufc);
}

// cmds - Return player command count (Wizard_Who OR Self ONLY)
// D.Piper - May 1997
//
static FUNCTION(fun_cmds)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    long nCmds = -1;
    if (is_rational(fargs[0]))
    {
        SOCKET s = mux_atol(fargs[0]);
        DESC *d = find_desc_by_socket(s);
        if (  nullptr != d
           && (  desc_player(d) == executor
              || Wizard_Who(executor)))
        {
            nCmds = desc_command_count(d);
        }
    }
    else
    {
        dbref target = lookup_player(executor, fargs[0], true);
        if (  Good_obj(target)
           && Connected(target)
           && (  Wizard_Who(executor)
              || Controls(executor, target)))
        {
            nCmds = fetch_cmds(target);
        }
    }
    safe_ltoa(nCmds, buff, bufc);
}

// startsecs - Time the MUX was started, in seconds
// D.Piper - May 1997 & April 2000
//
static FUNCTION(fun_startsecs)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str(mudstate.start_time.ReturnSecondsString(), buff, bufc);
}

// restartsecs - Time the MUX was @restarted in seconds or the the original
//               starttime.
//
static FUNCTION(fun_restartsecs)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    safe_str(mudstate.restart_time.ReturnSecondsString(), buff, bufc);
}


// conntotal - Return player's total online time to the MUX
// (including their current connection). D.Piper - May 1997
//
static FUNCTION(fun_conntotal)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref target = lookup_player(executor, fargs[0], true);
    if (Good_obj(target))
    {
        long TotalTime = fetch_totaltime(target);
        if (Connected(target))
        {
            TotalTime += fetch_connect(target);
        }
        safe_ltoa(TotalTime, buff, bufc);
    }
    else
    {
        safe_str(T("#-1 PLAYER NOT FOUND"), buff, bufc);
    }
}

// connmax - Return player's longest session to the MUX
// (including the current one). D.Piper - May 1997
//
static FUNCTION(fun_connmax)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref target = lookup_player(executor, fargs[0], true);
    if (Good_obj(target))
    {
        long Longest = fetch_longestconnect(target);
        long Current = fetch_connect(target);
        if (Longest < Current)
        {
            Longest = Current;
        }
        safe_ltoa(Longest, buff, bufc);
    }
    else
    {
        safe_str(T("#-1 PLAYER NOT FOUND"), buff, bufc);
    }
}

// connlast - Return player's last connection time to the MUX
// D.Piper - May 1997
//
static FUNCTION(fun_connlast)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref target = lookup_player(executor, fargs[0], true);
    if (Good_obj(target))
    {
        safe_ltoa(fetch_lastconnect(target), buff, bufc);
    }
    else
    {
        safe_str(T("#-1 PLAYER NOT FOUND"), buff, bufc);
    }
}

// connnum - Return the total number of sessions this player has had
// to the MUX (including any current ones). D.Piper - May 1997
//
static FUNCTION(fun_connnum)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref target = lookup_player(executor, fargs[0], true);
    if (Good_obj(target))
    {
        long NumConnections = fetch_numconnections(target);
        if (Connected(target))
        {
            NumConnections += fetch_session(target);
        }
        safe_ltoa(NumConnections, buff, bufc);
    }
    else
    {
        safe_str(T("#-1 PLAYER NOT FOUND"), buff, bufc);
    }
}

// connleft - Return when a player last logged off the MUX as
// UTC seconds. D.Piper - May 1997
//
static FUNCTION(fun_connleft)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref target = lookup_player(executor, fargs[0], true);
    if (Good_obj(target))
    {
        CLinearTimeAbsolute cl = fetch_logouttime(target);
        safe_str(cl.ReturnSecondsString(7), buff, bufc);
    }
    else
    {
        safe_str(T("#-1 PLAYER NOT FOUND"), buff, bufc);
    }
}

// lattrcmds - Output a list of all attributes containing $ commands.
// Altered from lattr(). D.Piper - May 1997 & April 2000
//
static FUNCTION(fun_lattrcmds)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Check for wildcard matching.  parse_attrib_wild checks for read
    // permission, so we don't have to.  Have p_a_w assume the
    // slash-star if it is missing.
    //
    olist_push();
    dbref thing;
    if (parse_attrib_wild(executor, fargs[0], &thing, false, false, true))
    {
        bool isFirst = true;
        UTF8 *buf = alloc_lbuf("fun_lattrcmds");
        for (int ca = olist_first(); ca != NOTHING; ca = olist_next())
        {
            ATTR *pattr = atr_num(ca);
            if (pattr)
            {
                dbref aowner;
                int   aflags;
                atr_get_str(buf, thing, pattr->number, &aowner, &aflags);
                if (buf[0] == '$')
                {
                    if (!isFirst)
                    {
                        safe_chr(' ', buff, bufc);
                    }
                    isFirst = false;
                    safe_str(pattr->name, buff, bufc);
                }
            }
        }
        free_lbuf(buf);
    }
    else
    {
        safe_nomatch(buff, bufc);
    }
    olist_pop();
}

// lcmds - Output a list of all $ commands on an object.
// Altered from MUX lattr(). D.Piper - May 1997 & April 2000
// Modified to handle spaced commands and ^-listens - July 2001 (Ash)
// Applied patch and code reviewed - February 2002 (Stephen)
//
static FUNCTION(fun_lcmds)
{
    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_NULL|DELIM_CRLF|DELIM_STRING))
    {
        return;
    }

    // Check to see what type of command matching we will do. '$' commands
    // or '^' listens.  We default with '$' commands.
    //
    UTF8 cmd_type = '$';
    if (  nfargs == 3
        && (  *fargs[2] == '$'
           || *fargs[2] == '^'))
    {
        cmd_type = *fargs[2];
    }

    // Check for wildcard matching.  parse_attrib_wild checks for read
    // permission, so we don't have to.  Have p_a_w assume the
    // slash-star if it is missing.
    //
    olist_push();
    dbref thing;
    if (parse_attrib_wild(executor, fargs[0], &thing, false, false, true))
    {
        bool isFirst = true;
        UTF8 *buf = alloc_lbuf("fun_lattrcmds");
        dbref aowner;
        int   aflags;
        for (int ca = olist_first(); ca != NOTHING; ca = olist_next())
        {
            ATTR *pattr = atr_num(ca);
            if (pattr)
            {
                atr_get_str(buf, thing, pattr->number, &aowner, &aflags);
                if (buf[0] == cmd_type)
                {
                    bool isFound = false;
                    UTF8 *c_ptr = buf+1;

                    // If there is no characters between the '$' or '^' and the
                    // ':' it's not a valid command, so skip it.
                    //
                    if (*c_ptr != ':')
                    {
                        int isEscaped = false;
                        while (*c_ptr && !isFound)
                        {
                            // We need to check if the ':' in the command is
                            // escaped.
                            //
                            if (*c_ptr == '\\')
                            {
                                isEscaped = !isEscaped;
                            }
                            else if (*c_ptr == ':' && !isEscaped)
                            {
                                isFound = true;
                                *c_ptr = '\0';
                            }
                            else if (*c_ptr != '\\' && isEscaped)
                            {
                                isEscaped = false;
                            }
                            c_ptr++;
                        }
                    }

                    // We don't want to bother printing out the $command
                    // if it doesn't have a matching ':'.  It isn't a valid
                    // command then.
                    //
                    if (isFound)
                    {
                        if (!isFirst)
                        {
                            print_sep(sep, buff, bufc);
                        }

                        size_t nCased;
                        UTF8  *pCased = mux_strlwr(buf, nCased);
                        safe_str(pCased+1, buff, bufc);

                        isFirst = false;
                    }
                }
            }
        }
        free_lbuf(buf);
    }
    else
    {
        safe_nomatch(buff, bufc);
    }
    olist_pop();
}

// lflags - List flags as names - (modified from 'flag_description()' and
// MUX flags(). D.Piper - May 1997 & May 2000
//
static FUNCTION(fun_lflags)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref target;
    ATTR  *pattr;
    if (parse_attrib(executor, fargs[0], &target, &pattr))
    {
        if (  pattr
           && See_attr(executor, target, pattr))
        {
            dbref aowner;
            int   aflags;
            atr_pget_info(target, pattr->number, &aowner, &aflags);
            decode_attr_flag_names(aflags, buff, bufc);
        }
    }
    else
    {
        target = match_thing_quiet(executor, fargs[0]);
        if (!Good_obj(target))
        {
            safe_match_result(target, buff, bufc);
            return;
        }
        bool bFirst = true;
        if (  mudconf.pub_flags
           || Examinable(executor, target))
        {
            FLAGNAMEENT *fp;
            for (fp = gen_flag_names; fp->flagname; fp++)
            {
                if (!fp->bPositive)
                {
                    continue;
                }
                FLAGBITENT *fbe = fp->fbe;
                if (db[target].fs.word[fbe->flagflag] & fbe->flagvalue)
                {
                    if (  (  (fbe->listperm & CA_STAFF)
                          && !Staff(executor))
                       || (  (fbe->listperm & CA_ADMIN)
                          && !WizRoy(executor))
                       || (  (fbe->listperm & CA_WIZARD)
                          && !Wizard(executor))
                       || (  (fbe->listperm & CA_GOD)
                          && !God(executor)))
                    {
                        continue;
                    }
                    if (  isPlayer(target)
                       && (fbe->flagvalue == CONNECTED)
                       && (fbe->flagflag == FLAG_WORD2)
                       && Hidden(target)
                       && !See_Hidden(executor))
                    {
                        continue;
                    }

                    if (!bFirst)
                    {
                        safe_chr(' ', buff, bufc);
                    }
                    bFirst = false;

                    safe_str(fp->flagname, buff, bufc);
                }
            }
        }
        else
        {
            safe_noperm(buff, bufc);
        }
    }
}

// ---------------------------------------------------------------------------
// fun_art:
//
// Accepts a single argument and returns the correct English indefinite
// article ("a" or "an") for the word.  The rules are hardcoded in
// art_scan.rl (compiled by Ragel -G2).
//

static FUNCTION(fun_art)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Drop the input string into lower case.
    size_t nCased;
    UTF8 *pCased = mux_strlwr(fargs[0], nCased);

    safe_str(art_should_use_an(pCased, nCased) ? T("an") : T("a"), buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_ord:
//
// Takes a single character and returns the corresponding ordinal of its
// position in the character set.
//
static FUNCTION(fun_ord)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    size_t nBytes = 0;
    UTF8 *p = strip_color(fargs[0], &nBytes, nullptr);
    if (0 == nBytes)
    {
        safe_str(T("#-1 FUNCTION EXPECTS ONE CHARACTER"), buff, bufc);
        return;
    }

    // Get the first grapheme cluster.
    //
    mux_cursor cluster = utf8_next_grapheme(p, nBytes);
    if (0 == cluster.m_byte)
    {
        safe_str(T("#-1 STRING IS INVALID"), buff, bufc);
        return;
    }

    // Verify there's exactly one cluster.
    //
    if (cluster.m_byte < nBytes)
    {
        mux_cursor second = utf8_next_grapheme(p + cluster.m_byte, nBytes - cluster.m_byte);
        if (0 < second.m_byte)
        {
            safe_str(T("#-1 FUNCTION EXPECTS ONE CHARACTER"), buff, bufc);
            return;
        }
        safe_str(T("#-1 STRING IS INVALID"), buff, bufc);
        return;
    }

    // Output space-separated code point values for the cluster.
    //
    const UTF8 *q = p;
    const UTF8 *qEnd = p + cluster.m_byte;
    bool bFirst = true;
    while (q < qEnd)
    {
        UTF32 ch = ConvertFromUTF8(q);
        if (UNI_EOF == ch)
        {
            safe_str(T("#-1 STRING IS INVALID"), buff, bufc);
            return;
        }
        if (!bFirst)
        {
            safe_chr(' ', buff, bufc);
        }
        safe_ltoa(static_cast<long>(ch), buff, bufc);
        bFirst = false;
        size_t nAdvance = utf8_FirstByte[static_cast<unsigned char>(*q)];
        if (nAdvance < 1 || nAdvance >= UTF8_CONTINUE)
        {
            nAdvance = 1;
        }
        q += nAdvance;
    }
}

// ---------------------------------------------------------------------------
// strdistance: Levenshtein edit distance between two strings.
//
// Operates on Unicode codepoints (not bytes).
//
static FUNCTION(fun_strdistance)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const UTF8 *s = fargs[0];
    const UTF8 *t = fargs[1];

    // Decode both strings to codepoint arrays.
    //
    UTF32 sCp[LBUF_SIZE], tCp[LBUF_SIZE];
    size_t sLen = 0, tLen = 0;

    while (*s && sLen < LBUF_SIZE - 1)
    {
        UTF32 ch = ConvertFromUTF8(s);
        if (UNI_EOF == ch) break;
        sCp[sLen++] = ch;
        s += utf8_FirstByte[static_cast<unsigned char>(*s)];
    }
    while (*t && tLen < LBUF_SIZE - 1)
    {
        UTF32 ch = ConvertFromUTF8(t);
        if (UNI_EOF == ch) break;
        tCp[tLen++] = ch;
        t += utf8_FirstByte[static_cast<unsigned char>(*t)];
    }

    // Single-row DP: prev[j] holds dist(s[0..i-1], t[0..j]).
    //
    if (sLen > 4000 || tLen > 4000)
    {
        safe_str(T("#-1 STRINGS TOO LONG"), buff, bufc);
        return;
    }

    std::vector<size_t> prev(tLen + 1);
    for (size_t j = 0; j <= tLen; j++)
    {
        prev[j] = j;
    }

    for (size_t i = 1; i <= sLen; i++)
    {
        size_t prevDiag = prev[0];
        prev[0] = i;
        for (size_t j = 1; j <= tLen; j++)
        {
            size_t cost = (sCp[i-1] == tCp[j-1]) ? 0 : 1;
            size_t val = prevDiag + cost;
            if (prev[j] + 1 < val) val = prev[j] + 1;
            if (prev[j-1] + 1 < val) val = prev[j-1] + 1;
            prevDiag = prev[j];
            prev[j] = val;
        }
    }

    safe_ltoa(static_cast<long>(prev[tLen]), buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_isalpha: Returns 1 if every codepoint in the string is a Unicode letter
// (General_Category L*), 0 otherwise.  Empty string returns 0.
//
static FUNCTION(fun_isalpha)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const UTF8 *p = fargs[0];
    if ('\0' == *p)
    {
        safe_chr('0', buff, bufc);
        return;
    }

    while ('\0' != *p)
    {
        if (!mux_isalpha_utf8(p))
        {
            safe_chr('0', buff, bufc);
            return;
        }
        p += utf8_FirstByte[static_cast<unsigned char>(*p)];
    }
    safe_chr('1', buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_isdigit: Returns 1 if every codepoint in the string is a Unicode
// decimal digit (General_Category Nd), 0 otherwise.  Empty string returns 0.
//
static FUNCTION(fun_isdigit)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const UTF8 *p = fargs[0];
    if ('\0' == *p)
    {
        safe_chr('0', buff, bufc);
        return;
    }

    while ('\0' != *p)
    {
        if (!mux_isdigit_utf8(p))
        {
            safe_chr('0', buff, bufc);
            return;
        }
        p += utf8_FirstByte[static_cast<unsigned char>(*p)];
    }
    safe_chr('1', buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_isalnum: Returns 1 if every codepoint in the string is a Unicode letter
// or decimal digit, 0 otherwise.  Empty string returns 0.
//
static FUNCTION(fun_isalnum)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const UTF8 *p = fargs[0];
    if ('\0' == *p)
    {
        safe_chr('0', buff, bufc);
        return;
    }

    while ('\0' != *p)
    {
        if (!mux_isalnum_utf8(p))
        {
            safe_chr('0', buff, bufc);
            return;
        }
        p += utf8_FirstByte[static_cast<unsigned char>(*p)];
    }
    safe_chr('1', buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_connlog: connlog(<player>[, <limit>])
//
// Returns connection log entries for <player>.  Each entry is a pipe-delimited
// record: id|player|connect_time|disconnect_time|host|ipaddr|reason
// Records are separated by newlines.  Default limit is 10, max 100.
//
static FUNCTION(fun_connlog)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref target = lookup_player(executor, fargs[0], true);
    if (!Good_obj(target))
    {
        safe_str(T("#-1 PLAYER NOT FOUND"), buff, bufc);
        return;
    }

    // Permission: must be the player themselves or a Wizard.
    //
    if (  executor != target
       && !Wizard(executor))
    {
        safe_noperm(buff, bufc);
        return;
    }

    int limit = 10;
    if (nfargs >= 2)
    {
        limit = mux_atol(fargs[1]);
        if (limit < 1) limit = 1;
        if (limit > 100) limit = 100;
    }

    bool bFirst = true;
    g_pSQLiteBackend->GetDB().ConnlogByPlayer(target, limit,
        [&](int64_t id, dbref player, int64_t connect_time,
            int64_t disconnect_time, const UTF8 *host,
            const UTF8 *ipaddr, const UTF8 *reason)
        {
            if (!bFirst)
            {
                safe_str(T("\r\n"), buff, bufc);
            }
            bFirst = false;

            safe_i64toa(id, buff, bufc);
            safe_chr('|', buff, bufc);
            safe_chr('#', buff, bufc);
            safe_ltoa(player, buff, bufc);
            safe_chr('|', buff, bufc);
            safe_i64toa(connect_time, buff, bufc);
            safe_chr('|', buff, bufc);
            safe_i64toa(disconnect_time, buff, bufc);
            safe_chr('|', buff, bufc);
            if (host) safe_str(host, buff, bufc);
            safe_chr('|', buff, bufc);
            if (ipaddr) safe_str(ipaddr, buff, bufc);
            safe_chr('|', buff, bufc);
            if (reason) safe_str(reason, buff, bufc);
        });
}

// ---------------------------------------------------------------------------
// fun_addrlog: addrlog(<ipaddr-pattern>[, <limit>])
//
// Returns connection log entries matching an IP address pattern (SQL LIKE).
// Wizard-only.  Each entry is pipe-delimited like connlog().
//
static FUNCTION(fun_addrlog)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (!Wizard(executor))
    {
        safe_noperm(buff, bufc);
        return;
    }

    int limit = 10;
    if (nfargs >= 2)
    {
        limit = mux_atol(fargs[1]);
        if (limit < 1) limit = 1;
        if (limit > 100) limit = 100;
    }

    bool bFirst = true;
    g_pSQLiteBackend->GetDB().ConnlogByAddr(fargs[0], limit,
        [&](int64_t id, dbref player, int64_t connect_time,
            int64_t disconnect_time, const UTF8 *host,
            const UTF8 *ipaddr, const UTF8 *reason)
        {
            if (!bFirst)
            {
                safe_str(T("\r\n"), buff, bufc);
            }
            bFirst = false;

            safe_i64toa(id, buff, bufc);
            safe_chr('|', buff, bufc);
            safe_chr('#', buff, bufc);
            safe_ltoa(player, buff, bufc);
            safe_chr('|', buff, bufc);
            safe_i64toa(connect_time, buff, bufc);
            safe_chr('|', buff, bufc);
            safe_i64toa(disconnect_time, buff, bufc);
            safe_chr('|', buff, bufc);
            if (host) safe_str(host, buff, bufc);
            safe_chr('|', buff, bufc);
            if (ipaddr) safe_str(ipaddr, buff, bufc);
            safe_chr('|', buff, bufc);
            if (reason) safe_str(reason, buff, bufc);
        });
}

// ---------------------------------------------------------------------------
// fun_chr:
//
// Takes an integer and returns the corresponding character from the character
// set.
//
static FUNCTION(fun_chr)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    // Build UTF-8 from space-separated code point integers.
    //
    UTF8 raw[LBUF_SIZE];
    UTF8 *pRaw = raw;
    const UTF8 *pEnd = raw + sizeof(raw) - 5;

    const UTF8 *pArg = fargs[0];
    bool bAny = false;
    while ('\0' != *pArg)
    {
        while (mux_isspace(*pArg))
        {
            pArg++;
        }
        if ('\0' == *pArg)
        {
            break;
        }

        bool bNegative = false;
        if ('-' == *pArg || '+' == *pArg)
        {
            bNegative = ('-' == *pArg);
            pArg++;
        }
        if (!mux_isdigit(*pArg))
        {
            safe_str(T("#-1 ARGUMENT MUST BE A NUMBER"), buff, bufc);
            return;
        }

        uint64_t uValue = 0;
        while (mux_isdigit(*pArg))
        {
            const uint64_t digit = static_cast<uint64_t>(*pArg - '0');
            if (uValue > (UINT64_MAX - digit) / 10ULL)
            {
                safe_str(T("#-1 ARGUMENT OUT OF RANGE"), buff, bufc);
                return;
            }
            uValue = 10ULL * uValue + digit;
            pArg++;
        }

        // Token separator must be whitespace or end of string.
        //
        if ('\0' != *pArg && !mux_isspace(*pArg))
        {
            safe_str(T("#-1 ARGUMENT MUST BE A NUMBER"), buff, bufc);
            return;
        }

        int64_t iValue = 0;
        if (bNegative)
        {
            if (uValue > (static_cast<uint64_t>(INT64_MAX) + 1ULL))
            {
                safe_str(T("#-1 ARGUMENT OUT OF RANGE"), buff, bufc);
                return;
            }
            if (uValue == (static_cast<uint64_t>(INT64_MAX) + 1ULL))
            {
                iValue = INT64_MIN;
            }
            else
            {
                iValue = -static_cast<int64_t>(uValue);
            }
        }
        else
        {
            if (uValue > static_cast<uint64_t>(INT64_MAX))
            {
                safe_str(T("#-1 ARGUMENT OUT OF RANGE"), buff, bufc);
                return;
            }
            iValue = static_cast<int64_t>(uValue);
        }

        if (  iValue < 0
           || iValue > static_cast<int64_t>(UNI_MAX_LEGAL_UTF32)
           || (  static_cast<UTF32>(iValue) >= UNI_SUR_HIGH_START
              && static_cast<UTF32>(iValue) <= UNI_SUR_LOW_END))
        {
            safe_str(T("#-1 ARGUMENT OUT OF RANGE"), buff, bufc);
            return;
        }

        UTF32 ch = static_cast<UTF32>(iValue);
        UTF8 *p = ConvertToUTF8(ch);
        if (!mux_isprint(p))
        {
            safe_str(T("#-1 UNPRINTABLE CHARACTER"), buff, bufc);
            return;
        }

        if (pRaw < pEnd)
        {
            size_t nCharBytes = strlen(reinterpret_cast<const char *>(p));
            if (pRaw + nCharBytes <= pEnd)
            {
                memcpy(pRaw, p, nCharBytes);
                pRaw += nCharBytes;
            }
        }
        bAny = true;
    }
    *pRaw = '\0';

    if (!bAny)
    {
        safe_str(T("#-1 ARGUMENT MUST BE A NUMBER"), buff, bufc);
        return;
    }

    // NFC normalize the result.
    //
    size_t nRaw = pRaw - raw;
    UTF8 nfc[LBUF_SIZE];
    size_t nNfc;
    utf8_normalize_nfc(raw, nRaw, nfc, sizeof(nfc) - 1, &nNfc);
    nfc[nNfc] = '\0';

    safe_str(nfc, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_stripaccents:
//
static FUNCTION(fun_stripaccents)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const UTF8 *p = reinterpret_cast<const UTF8 *>(ConvertToAscii(fargs[0]));
    safe_str(p, buff, bufc);
}

// Base Letter: !<>?ACDEGHIJKLNOPQRSTUWYZacdeghijklnopqrstuwyz
//
static const unsigned char AccentCombo1[128] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 3, 4,  // 3
    0, 5, 0, 6, 7, 8, 0, 9,10,11,12,13,14, 0,15,16,  // 4
   17,18,19,20,21,22, 0,23, 0,24,25, 0, 0, 0, 0, 0,  // 5
    0,26, 0,27,28,29, 0,30,31,32,33,34,35, 0,36,37,  // 6
   38,39,40,41,42,43, 0,44, 0,45,46, 0, 0, 0, 0, 0,  // 7
};

// Accent: "&',-./:BEGJ^`egjoquv|~
//
static const unsigned char AccentCombo2[128] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 1, 0, 0, 0, 2, 3, 0, 0, 0, 0, 4, 5, 6, 7,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0,  // 3
    0, 0, 9, 0, 0,10, 0,11, 0, 0,12, 0, 0, 0, 0, 0,  // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,13, 0,  // 5
   14, 0, 0, 0, 0,15, 0,16, 0, 0,17, 0, 0, 0, 0,18,  // 6
    0,19, 0, 0, 0,20,21, 0, 0, 0, 0, 0,22, 0,23, 0,  // 7
};

static const unsigned short AccentCombo3[46][24] =
{
    //  0     1     2     3     4     5     6     7     8     9    10    11    12    13    14    15    16    17    18    19    20    21    22    23
    //        "     &     '     ,     -     .     /     :     B     E     G     J     ^     `     e     g     j     o     q     u     v     |     ~
    //
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA1, 0x00, 0x00, 0x00 }, //  1 '!'
    {  0x00, 0xAB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, //  2 '<'
    {  0x00, 0xBB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, //  3 '>'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x00 }, //  4 '?'
    {  0x00, 0x00, 0x00, 0xC1,0x104,0x100, 0x00, 0x00, 0xC4, 0x00, 0xC6, 0x00, 0x00, 0xC2, 0xC0, 0x00, 0x00, 0x00, 0xC5, 0x00,0x102, 0x00, 0x00, 0xC3 }, //  5 'A'
    {  0x00, 0x00, 0x00,0x106, 0xC7, 0x00,0x10A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x108, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x10C, 0x00, 0x00 }, //  6 'C'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0xD0, 0x00,0x110, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x10E, 0x00, 0x00 }, //  7 'D'
    {  0x00, 0x00, 0x00, 0xC9,0x118,0x102,0x116, 0x00, 0xCB, 0x00, 0x00, 0x00, 0x00, 0xCA, 0xC8, 0x00, 0x00, 0x00, 0x00, 0x00,0x114,0x11A, 0x00, 0x00 }, //  8 'E'
    {  0x00, 0x00, 0x00, 0x00,0x122, 0x00,0x120, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x11C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x11E, 0x00, 0x00, 0x00 }, //  9 'G'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x126, 0x00, 0x00, 0x00, 0x00, 0x00,0x124, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 10 'H'
    {  0x00, 0x00, 0x00, 0xCD,0x12E,0x12A,0x130, 0x00, 0xCF, 0x00, 0x00, 0x00,0x132, 0xCE, 0xCC, 0x00, 0x00, 0x00, 0x00, 0x00,0x12C, 0x00, 0x00,0x128 }, // 11 'I'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x134, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 12 'J'
    {  0x00, 0x00, 0x00, 0x00,0x136, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x138, 0x00, 0x00, 0x00, 0x00 }, // 13 'K'
    {  0x00, 0x00, 0x00,0x139,0x13B, 0x00, 0x00,0x141, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x13D, 0x00, 0x00 }, // 14 'L'
    {  0x00, 0x00, 0x00,0x143,0x145, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x14A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x147, 0x00, 0xD1 }, // 15 'N'
    {  0x00,0x150, 0x00, 0xD3, 0x00,0x14C, 0x00, 0xD8, 0xD6, 0x00,0x152, 0x00, 0x00, 0xD4, 0xD2, 0x00, 0x00, 0x00, 0x00, 0x00,0x14E, 0x00, 0x00, 0xD5 }, // 16 'O'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xDE, 0x00 }, // 17 'P'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 18 'Q'
    {  0x00, 0x00, 0x00,0x154,0x156, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x158, 0x00, 0x00 }, // 19 'R'
    {  0x00, 0x00, 0x00,0x15A,0x15E, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x15C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x160, 0x00, 0x00 }, // 20 'S'
    {  0x00, 0x00, 0x00, 0x00,0x162, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x164, 0x00, 0x00 }, // 21 'T'
    {  0x00,0x170, 0x00, 0xDA,0x172,0x16A, 0x00, 0x00, 0xDC, 0x00, 0x00, 0x00, 0x00, 0xDB, 0xD9, 0x00, 0x00, 0x00,0x16E, 0x00,0x16C, 0x00, 0x00,0x168 }, // 22 'U'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x174, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 23 'W'
    {  0x00, 0x00, 0x00, 0xDD, 0x00, 0x00, 0x00, 0x00,0x178, 0x00, 0x00, 0x00, 0x00,0x176, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 24 'Y'
    {  0x00, 0x00, 0x00,0x179, 0x00, 0x00,0x17B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x17D, 0x00, 0x00 }, // 25 'Z'
    {  0x00, 0x00, 0x00, 0xE1,0x105,0x101, 0x00, 0x00, 0xE4, 0x00, 0x00, 0x00, 0x00, 0xE2, 0xE0, 0xE6, 0x00, 0x00, 0xE5, 0x00,0x103, 0x00, 0x00, 0xE3 }, // 26 'a'
    {  0x00, 0x00, 0x00,0x107, 0xE7, 0x00,0x10B, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x109, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x10D, 0x00, 0x00 }, // 27 'c'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x111, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x10F, 0x00, 0x00 }, // 28 'd'
    {  0x00, 0x00, 0x00, 0xE9,0x119,0x103,0x117, 0x00, 0xEB, 0x00, 0x00, 0x00, 0x00, 0xEA, 0xE8, 0x00, 0x00, 0x00, 0x00, 0x00,0x115,0x11B, 0x00, 0x00 }, // 29 'e'
    {  0x00, 0x00, 0x00, 0x00,0x123, 0x00,0x121, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x11D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x11F, 0x00, 0x00, 0x00 }, // 30 'g'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x127, 0x00, 0x00, 0x00, 0x00, 0x00,0x125, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 31 'h'
    {  0x00, 0x00, 0x00, 0xED,0x12F,0x12B,0x131, 0x00, 0xEF, 0x00, 0x00, 0x00, 0x00, 0xEE, 0xEC, 0x00, 0x00,0x133, 0x00, 0x00,0x12D, 0x00, 0x00,0x129 }, // 32 'i'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x135, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 33 'j'
    {  0x00, 0x00, 0x00, 0x00,0x137, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 34 'k'
    {  0x00, 0x00, 0x00,0x13A,0x13C, 0x00, 0x00,0x142, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x13E, 0x00, 0x00 }, // 35 'l'
    {  0x00, 0x00, 0x00,0x144,0x146, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x14B, 0x00, 0x00, 0x00, 0x00,0x148, 0x00, 0xF1 }, // 36 'n'
    {  0x00,0x151, 0xF0, 0xF3, 0x00,0x14D, 0x00, 0xF8, 0xF6, 0x00, 0x00, 0x00, 0x00, 0xF4, 0xF2,0x153, 0x00, 0x00, 0x00, 0x00,0x14F, 0x00, 0x00, 0xF5 }, // 37 'o'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x00 }, // 38 'p'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 39 'q'
    {  0x00, 0x00, 0x00,0x155,0x157, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x159, 0x00, 0x00 }, // 40 'r'
    {  0x00, 0x00, 0x00,0x15B,0x15F, 0x00, 0x00, 0x00, 0x00, 0xDF, 0x00, 0x00, 0x00,0x15D, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 41 's'
    {  0x00, 0x00, 0x00, 0x00,0x163, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x165, 0x00, 0x00 }, // 42 't'
    {  0x00,0x171, 0x00, 0xFA,0x173,0x16B, 0x00, 0x00, 0xFC, 0x00, 0x00, 0x00, 0x00, 0xFB, 0xF9, 0x00, 0x00, 0x00,0x16F, 0x00,0x16D, 0x00, 0x00,0x169 }, // 43 'u'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x175, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 44 'w'
    {  0x00, 0x00, 0x00, 0xFD, 0x00, 0x00, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00,0x177, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 45 'y'
    {  0x00, 0x00, 0x00,0x17A, 0x00, 0x00,0x17C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,0x17E, 0x00, 0x00 }, // 46 'z'
};

// ---------------------------------------------------------------------------
// fun_accent:
//
static FUNCTION(fun_accent)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    const UTF8 *p = fargs[0];
    const UTF8 *q = fargs[1];

    size_t n0, n1;
    if (  !utf8_strlen(p, n0)
       || !utf8_strlen(q, n1))
    {
        safe_str(T("#-1 STRINGS ARE INVALID"), buff, bufc);
        return;
    }

    if (n0 != n1)
    {
        safe_str(T("#-1 STRING LENGTHS MUST BE EQUAL"), buff, bufc);
        return;
    }

    while ('\0' != *p)
    {
        UTF32 ch = L'\0';
        UTF8 ch0 = *p;
        if (  UTF8_SIZE1 == utf8_FirstByte[ch0]
           && (0 < (ch0 = AccentCombo1[ch0])))
        {
            UTF8 ch1 = *q;
            if (  UTF8_SIZE1 == utf8_FirstByte[ch1]
               && (0 < (ch1 = AccentCombo2[ch1])))
            {
                ch  = AccentCombo3[ch0-1][ch1];
            }
        }

        if (L'\0' != ch)
        {
            UTF8 *t = ConvertToUTF8(ch);
            utf8_safe_chr(t, buff, bufc);
        }
        else
        {
            utf8_safe_chr(p, buff, bufc);
        }

        size_t nAdvanceP = utf8_FirstByte[static_cast<unsigned char>(*p)];
        if (nAdvanceP < 1 || nAdvanceP >= UTF8_CONTINUE)
        {
            nAdvanceP = 1;
        }
        else
        {
            for (size_t i = 1; i < nAdvanceP; i++)
            {
                if (  '\0' == p[i]
                   || UTF8_CONTINUE != utf8_FirstByte[static_cast<unsigned char>(p[i])])
                {
                    nAdvanceP = 1;
                    break;
                }
            }
        }

        size_t nAdvanceQ = utf8_FirstByte[static_cast<unsigned char>(*q)];
        if (nAdvanceQ < 1 || nAdvanceQ >= UTF8_CONTINUE)
        {
            nAdvanceQ = 1;
        }
        else
        {
            for (size_t i = 1; i < nAdvanceQ; i++)
            {
                if (  '\0' == q[i]
                   || UTF8_CONTINUE != utf8_FirstByte[static_cast<unsigned char>(q[i])])
                {
                    nAdvanceQ = 1;
                    break;
                }
            }
        }

        p += nAdvanceP;
        q += nAdvanceQ;
    }
}

static size_t expand_range(UTF8 *buf, size_t len)
{
    // Expand a-z, A-Z, 0-9 character ranges in-place.
    // Dashes without valid endpoints are treated literally.
    //
    UTF8 out[LBUF_SIZE];
    size_t wp = 0;

    for (size_t i = 0; i < len; i++)
    {
        if (  '-' == buf[i]
           && 0 < i
           && i + 1 < len)
        {
            UTF8 cBefore = out[wp - 1];
            UTF8 cAfter = buf[i + 1];

            if (  mux_isazAZ(cBefore)
               && mux_isazAZ(cAfter)
               && mux_islower_ascii(cBefore) == mux_islower_ascii(cAfter)
               && cBefore < cAfter)
            {
                // Same-case letter range: b through cAfter-1.
                //
                for (UTF8 c = cBefore + 1; c < cAfter && wp < LBUF_SIZE - 1; c++)
                {
                    out[wp++] = c;
                }
                i++; // skip cAfter, it will be copied normally next iteration
                // But we need to copy cAfter now.
                if (wp < LBUF_SIZE - 1) out[wp++] = cAfter;
            }
            else if (  mux_islower_ascii(cBefore)
                    && mux_isupper_ascii(cAfter))
            {
                // Cross-case range: cBefore+1..z, A..cAfter-1.
                //
                for (UTF8 c = cBefore + 1; c <= 'z' && wp < LBUF_SIZE - 1; c++)
                {
                    out[wp++] = c;
                }
                for (UTF8 c = 'A'; c < cAfter && wp < LBUF_SIZE - 1; c++)
                {
                    out[wp++] = c;
                }
                i++;
                if (wp < LBUF_SIZE - 1) out[wp++] = cAfter;
            }
            else if (  mux_isdigit(cBefore)
                    && mux_isdigit(cAfter)
                    && cBefore < cAfter)
            {
                // Numeric range.
                //
                for (UTF8 c = cBefore + 1; c < cAfter && wp < LBUF_SIZE - 1; c++)
                {
                    out[wp++] = c;
                }
                i++;
                if (wp < LBUF_SIZE - 1) out[wp++] = cAfter;
            }
            else
            {
                // Literal dash.
                //
                if (wp < LBUF_SIZE - 1) out[wp++] = '-';
            }
        }
        else
        {
            if (wp < LBUF_SIZE - 1) out[wp++] = buf[i];
        }
    }
    out[wp] = '\0';
    memcpy(buf, out, wp + 1);
    return wp;
}

static FUNCTION(fun_tr)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    size_t nStr = strlen(reinterpret_cast<const char *>(fargs[0]));
    if (0 != nStr)
    {
        // Expand character ranges (a-z, A-Z, 0-9) in from/to sets.
        //
        UTF8 fromBuf[LBUF_SIZE], toBuf[LBUF_SIZE];
        size_t fLen = strlen(reinterpret_cast<const char *>(fargs[1]));
        size_t tLen = strlen(reinterpret_cast<const char *>(fargs[2]));

        if (fLen >= LBUF_SIZE - 1) fLen = LBUF_SIZE - 2;
        if (tLen >= LBUF_SIZE - 1) tLen = LBUF_SIZE - 2;

        memcpy(fromBuf, fargs[1], fLen + 1);
        memcpy(toBuf,   fargs[2], tLen + 1);

        fLen = expand_range(fromBuf, fLen);
        tLen = expand_range(toBuf,   tLen);

        if (fLen != tLen)
        {
            safe_str(T("#-1 STRING LENGTHS MUST BE EQUAL"), buff, bufc);
        }
        else
        {
            unsigned char out[LBUF_SIZE];
            size_t nOut = co_transform(out,
                reinterpret_cast<const unsigned char *>(fargs[0]), nStr,
                reinterpret_cast<const unsigned char *>(fromBuf), fLen,
                reinterpret_cast<const unsigned char *>(toBuf), tLen);

            size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
            if (nOut > nMax) nOut = nMax;
            memcpy(*bufc, out, nOut);
            *bufc += nOut;
            **bufc = '\0';
        }
    }
}

// gmcp(<player>, <package>, <json>) - Send a GMCP message to a player.
// Requires Wizard or control over the target player.
//
static FUNCTION(fun_gmcp)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref target = lookup_player(executor, fargs[0], true);
    if (NOTHING == target)
    {
        safe_str(T("#-1 PLAYER NOT FOUND"), buff, bufc);
        return;
    }

    if (  executor != target
       && !Wizard(executor)
       && !Controls(executor, target))
    {
        safe_str(T("#-1 PERMISSION DENIED"), buff, bufc);
        return;
    }

    send_gmcp(target, fargs[1], (nfargs >= 3) ? fargs[2] : T(""));
}

// lua(<object>/<attr>[, <arg1>, <arg2>, ...]) - Execute a Lua script.
// The script is read from the named attribute, compiled, and executed
// in a sandboxed Lua 5.4 environment.  Arguments are passed via mux.args.
// Returns the Lua return value as a string, or #-1 LUA ERROR: <msg>.
//
static FUNCTION(fun_lua)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nullptr == mudstate.pILuaControl)
    {
        safe_str(T("#-1 LUA MODULE NOT LOADED"), buff, bufc);
        return;
    }

    // Parse obj/attr from first argument.
    //
    UTF8 *pSlash = (UTF8 *)strchr((const char *)fargs[0], '/');
    if (nullptr == pSlash)
    {
        safe_str(T("#-1 NO ATTRIBUTE SPECIFIED"), buff, bufc);
        return;
    }
    *pSlash = '\0';
    UTF8 *pAttrName = pSlash + 1;

    // Resolve the object.
    //
    dbref obj = lookup_player(executor, fargs[0], true);
    if (NOTHING == obj)
    {
        init_match(executor, fargs[0], NOTYPE);
        match_everything(MAT_EXIT_PARENTS);
        obj = match_result();
    }
    if (!Good_obj(obj))
    {
        safe_str(T("#-1 NO SUCH OBJECT"), buff, bufc);
        return;
    }

    // Build argument array (args 2..N).
    //
    const UTF8 *luaArgs[MAX_ARG];
    int nLuaArgs = nfargs - 1;
    if (nLuaArgs > MAX_ARG) nLuaArgs = MAX_ARG;
    for (int i = 0; i < nLuaArgs; i++)
    {
        luaArgs[i] = fargs[i + 1];
    }

    // Dispatch through the Lua module.
    //
    UTF8 result[8000];
    size_t nResult = 0;
    MUX_RESULT mr = mudstate.pILuaControl->CallAttr(
        executor, caller, enactor,
        obj, pAttrName, luaArgs, nLuaArgs,
        result, sizeof(result), &nResult);

    if (MUX_SUCCEEDED(mr))
    {
        safe_copy_buf(result, nResult, buff, bufc);
    }
    else
    {
        safe_str(T("#-1 LUA ERROR"), buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_bound: Clamp a value between min and optional max.
//   bound(value, min[, max])
// ---------------------------------------------------------------------------

static FUNCTION(fun_bound)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    double val = mux_atof(fargs[0], false);
    double lo  = mux_atof(fargs[1], false);

    if (val < lo)
    {
        val = lo;
    }

    if (nfargs >= 3)
    {
        double hi = mux_atof(fargs[2], false);
        if (val > hi)
        {
            val = hi;
        }
    }

    fval(buff, bufc, val);
}

// ---------------------------------------------------------------------------
// fun_mean: Arithmetic mean of arguments.
//   mean(number, number, ...)
// ---------------------------------------------------------------------------

static FUNCTION(fun_mean)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs < 1)
    {
        safe_str(T("#-1 TOO FEW ARGUMENTS"), buff, bufc);
        return;
    }

    double sum = 0.0;
    for (int i = 0; i < nfargs; i++)
    {
        sum += mux_atof(fargs[i], false);
    }
    fval(buff, bufc, sum / nfargs);
}

// ---------------------------------------------------------------------------
// fun_median: Median of arguments.
//   median(number, number, ...)
// ---------------------------------------------------------------------------

static FUNCTION(fun_median)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs < 1)
    {
        safe_str(T("#-1 TOO FEW ARGUMENTS"), buff, bufc);
        return;
    }

    double vals[MAX_ARG];
    for (int i = 0; i < nfargs; i++)
    {
        vals[i] = mux_atof(fargs[i], false);
    }

    // Simple insertion sort.
    //
    for (int i = 1; i < nfargs; i++)
    {
        double key = vals[i];
        int j = i - 1;
        while (j >= 0 && vals[j] > key)
        {
            vals[j + 1] = vals[j];
            j--;
        }
        vals[j + 1] = key;
    }

    double result;
    if (nfargs % 2 == 1)
    {
        result = vals[nfargs / 2];
    }
    else
    {
        result = (vals[nfargs / 2 - 1] + vals[nfargs / 2]) / 2.0;
    }
    fval(buff, bufc, result);
}

// ---------------------------------------------------------------------------
// fun_stddev: Sample standard deviation using Welford's online algorithm.
//   stddev(number, number, ...)
// ---------------------------------------------------------------------------

static FUNCTION(fun_stddev)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs < 2)
    {
        safe_str(T("0"), buff, bufc);
        return;
    }

    double mean = 0.0;
    double m2 = 0.0;
    for (int i = 0; i < nfargs; i++)
    {
        double x = mux_atof(fargs[i], false);
        double delta = x - mean;
        mean += delta / (i + 1);
        double delta2 = x - mean;
        m2 += delta * delta2;
    }

    fval(buff, bufc, sqrt(m2 / (nfargs - 1)));
}

// ---------------------------------------------------------------------------
// fun_unique: Remove duplicates from a list, keeping first occurrence.
//   unique(list[, sorttype[, sep[, osep]]])
// ---------------------------------------------------------------------------

static FUNCTION(fun_unique)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

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

    UTF8 *arr[LBUF_SIZE / 2];
    int nWords = list2arr(arr, LBUF_SIZE / 2, fargs[0], sep);

    // Track seen words with a simple linear scan (adequate for MUX lists).
    //
    bool bFirst = true;
    UTF8 *seen[LBUF_SIZE / 2];
    int nSeen = 0;

    for (int i = 0; i < nWords; i++)
    {
        bool bDup = false;
        for (int j = 0; j < nSeen; j++)
        {
            if (strcmp(reinterpret_cast<char *>(arr[i]),
                       reinterpret_cast<char *>(seen[j])) == 0)
            {
                bDup = true;
                break;
            }
        }
        if (!bDup)
        {
            if (!bFirst)
            {
                print_sep(osep, buff, bufc);
            }
            safe_str(arr[i], buff, bufc);
            bFirst = false;
            seen[nSeen++] = arr[i];
        }
    }
}

// ---------------------------------------------------------------------------
// fun_linsert: Insert an item at a position in a list.
//   linsert(list, position, item[, sep])
// ---------------------------------------------------------------------------

static FUNCTION(fun_linsert)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    UTF8 *arr[LBUF_SIZE / 2];
    int nWords = list2arr(arr, LBUF_SIZE / 2, fargs[0], sep);
    int pos = mux_atol(fargs[1]);

    if (pos < 0)
    {
        pos = 0;
    }
    if (pos > nWords)
    {
        pos = nWords;
    }

    bool bFirst = true;
    for (int i = 0; i <= nWords; i++)
    {
        if (i == pos)
        {
            if (!bFirst)
            {
                print_sep(sep, buff, bufc);
            }
            safe_str(fargs[2], buff, bufc);
            bFirst = false;
        }
        if (i < nWords)
        {
            if (!bFirst)
            {
                print_sep(sep, buff, bufc);
            }
            safe_str(arr[i], buff, bufc);
            bFirst = false;
        }
    }
}

// ---------------------------------------------------------------------------
// fun_strdelete: Delete count characters from string starting at start.
//   strdelete(string, start, count)
// ---------------------------------------------------------------------------

static FUNCTION(fun_strdelete)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int iStart = mux_atol(fargs[1]);
    int nDelete = mux_atol(fargs[2]);

    if (iStart < 0)
    {
        iStart = 0;
    }
    if (nDelete < 0)
    {
        nDelete = 0;
    }

    const unsigned char *p = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t slen = strlen(reinterpret_cast<const char *>(p));

    if (nDelete == 0 || static_cast<size_t>(iStart) >= slen)
    {
        safe_str(fargs[0], buff, bufc);
        return;
    }

    unsigned char out[LBUF_SIZE];
    size_t n = co_delete_cluster(out, p, slen,
        static_cast<size_t>(iStart), static_cast<size_t>(nDelete));

    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    if (n > nMax) n = nMax;
    memcpy(*bufc, out, n);
    *bufc += n;
    **bufc = '\0';
}

// ---------------------------------------------------------------------------
// fun_strinsert: Insert a string at a character position.
//   strinsert(string, position, insertion)
// ---------------------------------------------------------------------------

static FUNCTION(fun_strinsert)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int iPos = mux_atol(fargs[1]);
    if (iPos < 0)
    {
        iPos = 0;
    }

    const unsigned char *p = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t slen = strlen(reinterpret_cast<const char *>(p));
    size_t nClusters = co_cluster_count(p, slen);

    if (static_cast<size_t>(iPos) >= nClusters)
    {
        // Append.
        //
        safe_str(fargs[0], buff, bufc);
        safe_str(fargs[2], buff, bufc);
        return;
    }

    // Output: mid(str, 0, pos) + insertion + mid(str, pos, rest)
    //
    unsigned char left[LBUF_SIZE];
    size_t nLeft = co_mid_cluster(left, p, slen, 0, static_cast<size_t>(iPos));

    unsigned char right[LBUF_SIZE];
    size_t nRight = co_mid_cluster(right, p, slen,
        static_cast<size_t>(iPos), nClusters - static_cast<size_t>(iPos));

    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    size_t n = nLeft;
    if (n > nMax) n = nMax;
    memcpy(*bufc, left, n);
    *bufc += n;
    **bufc = '\0';

    safe_str(fargs[2], buff, bufc);

    nMax = buff + (LBUF_SIZE-1) - *bufc;
    n = nRight;
    if (n > nMax) n = nMax;
    memcpy(*bufc, right, n);
    *bufc += n;
    **bufc = '\0';
}

// ---------------------------------------------------------------------------
// fun_strreplace: Replace count chars starting at start with replacement.
//   strreplace(string, start, count, replacement)
// ---------------------------------------------------------------------------

static FUNCTION(fun_strreplace)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int iStart = mux_atol(fargs[1]);
    int nCount = mux_atol(fargs[2]);

    if (iStart < 0)
    {
        iStart = 0;
    }
    if (nCount < 0)
    {
        nCount = 0;
    }

    const unsigned char *p = reinterpret_cast<const unsigned char *>(fargs[0]);
    size_t slen = strlen(reinterpret_cast<const char *>(p));
    size_t nClusters = co_cluster_count(p, slen);

    if (static_cast<size_t>(iStart) >= nClusters)
    {
        // Start is past end — just append replacement.
        //
        safe_str(fargs[0], buff, bufc);
        safe_str(fargs[3], buff, bufc);
        return;
    }

    // Output: mid(str, 0, start) + replacement + mid(str, start+count, rest)
    //
    unsigned char left[LBUF_SIZE];
    size_t nLeft = co_mid_cluster(left, p, slen, 0, static_cast<size_t>(iStart));

    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    size_t n = nLeft;
    if (n > nMax) n = nMax;
    memcpy(*bufc, left, n);
    *bufc += n;
    **bufc = '\0';

    safe_str(fargs[3], buff, bufc);

    size_t rightStart = static_cast<size_t>(iStart) + static_cast<size_t>(nCount);
    if (rightStart < nClusters)
    {
        unsigned char right[LBUF_SIZE];
        size_t nRight = co_mid_cluster(right, p, slen,
            rightStart, nClusters - rightStart);

        nMax = buff + (LBUF_SIZE-1) - *bufc;
        n = nRight;
        if (n > nMax) n = nMax;
        memcpy(*bufc, right, n);
        *bufc += n;
        **bufc = '\0';
    }
}

// ---------------------------------------------------------------------------
// fun_unsetq: Clear specified q-registers, or all if no args.
//   unsetq([register, register, ...])
// ---------------------------------------------------------------------------

static FUNCTION(fun_unsetq)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs == 0)
    {
        // Clear all registers.
        //
        for (int i = 0; i < MAX_GLOBAL_REGS; i++)
        {
            if (mudstate.global_regs[i])
            {
                RegRelease(mudstate.global_regs[i]);
                mudstate.global_regs[i] = nullptr;
            }
        }
        return;
    }

    for (int i = 0; i < nfargs; i++)
    {
        int regnum;
        if (IsSingleCharReg(fargs[i], regnum))
        {
            if (mudstate.global_regs[regnum])
            {
                RegRelease(mudstate.global_regs[regnum]);
                mudstate.global_regs[regnum] = nullptr;
            }
        }
    }
}

// ---------------------------------------------------------------------------
// fun_listq: List all set q-registers.
//   listq()
// ---------------------------------------------------------------------------

static FUNCTION(fun_listq)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    bool bFirst = true;
    // Registers 0-9 map to characters '0'-'9', 10-35 map to 'A'-'Z'.
    //
    for (int i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        if (  mudstate.global_regs[i]
           && mudstate.global_regs[i]->reg_len > 0)
        {
            if (!bFirst)
            {
                safe_chr(' ', buff, bufc);
            }
            if (i < 10)
            {
                safe_chr(static_cast<UTF8>('0' + i), buff, bufc);
            }
            else
            {
                safe_chr(static_cast<UTF8>('A' + i - 10), buff, bufc);
            }
            bFirst = false;
        }
    }
}

// ---------------------------------------------------------------------------
// fun_ncon: Count of all contents in a location.
//   ncon(object)
// ---------------------------------------------------------------------------

static FUNCTION(fun_ncon)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    if (!Has_contents(it))
    {
        safe_ltoa(0, buff, bufc);
        return;
    }
    if (  !Examinable(executor, it)
       && Location(executor) != it
       && it != enactor)
    {
        safe_noperm(buff, bufc);
        return;
    }

    int count = 0;
    dbref thing;
    DOLIST(thing, Contents(it))
    {
        count++;
    }
    safe_ltoa(count, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_nexits: Count of exits from a location.
//   nexits(object)
// ---------------------------------------------------------------------------

static FUNCTION(fun_nexits)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    if (!Has_exits(it))
    {
        safe_ltoa(0, buff, bufc);
        return;
    }
    if (  !Examinable(executor, it)
       && Location(executor) != it
       && it != enactor)
    {
        safe_noperm(buff, bufc);
        return;
    }

    int count = 0;
    dbref thing;
    DOLIST(thing, Exits(it))
    {
        count++;
    }
    safe_ltoa(count, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_nplayers: Count of connected players in a location.
//   nplayers(object)
// ---------------------------------------------------------------------------

static FUNCTION(fun_nplayers)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    if (!Has_contents(it))
    {
        safe_ltoa(0, buff, bufc);
        return;
    }
    if (  !Examinable(executor, it)
       && Location(executor) != it
       && it != enactor)
    {
        safe_noperm(buff, bufc);
        return;
    }

    int count = 0;
    dbref thing;
    DOLIST(thing, Contents(it))
    {
        if (isPlayer(thing) && Connected(thing))
        {
            count++;
        }
    }
    safe_ltoa(count, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_nthings: Count of things in a location.
//   nthings(object)
// ---------------------------------------------------------------------------

static FUNCTION(fun_nthings)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    if (!Has_contents(it))
    {
        safe_ltoa(0, buff, bufc);
        return;
    }
    if (  !Examinable(executor, it)
       && Location(executor) != it
       && it != enactor)
    {
        safe_noperm(buff, bufc);
        return;
    }

    int count = 0;
    dbref thing;
    DOLIST(thing, Contents(it))
    {
        if (isThing(thing))
        {
            count++;
        }
    }
    safe_ltoa(count, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_lplayers: List dbrefs of connected players in a location.
//   lplayers(object)
// ---------------------------------------------------------------------------

static FUNCTION(fun_lplayers)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    if (!Has_contents(it))
    {
        safe_nothing(buff, bufc);
        return;
    }
    if (  !Examinable(executor, it)
       && Location(executor) != it
       && it != enactor)
    {
        safe_noperm(buff, bufc);
        return;
    }

    dbref thing;
    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    DOLIST(thing, Contents(it))
    {
        if (isPlayer(thing) && Connected(thing))
        {
            if (!ItemToList_AddInteger(&pContext, thing))
            {
                break;
            }
        }
    }
    ItemToList_Final(&pContext);
}

// ---------------------------------------------------------------------------
// fun_lthings: List dbrefs of things in a location.
//   lthings(object)
// ---------------------------------------------------------------------------

static FUNCTION(fun_lthings)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    if (!Has_contents(it))
    {
        safe_nothing(buff, bufc);
        return;
    }
    if (  !Examinable(executor, it)
       && Location(executor) != it
       && it != enactor)
    {
        safe_noperm(buff, bufc);
        return;
    }

    dbref thing;
    ITL pContext;
    ItemToList_Init(&pContext, buff, bufc, '#');
    DOLIST(thing, Contents(it))
    {
        if (isThing(thing))
        {
            if (!ItemToList_AddInteger(&pContext, thing))
            {
                break;
            }
        }
    }
    ItemToList_Final(&pContext);
}

// ---------------------------------------------------------------------------
// fun_lreplace: Replace element at a given position in a list.
//   lreplace(list, position, newvalue[, sep])
// ---------------------------------------------------------------------------

static FUNCTION(fun_lreplace)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    UTF8 *arr[LBUF_SIZE / 2];
    int nWords = list2arr(arr, LBUF_SIZE / 2, fargs[0], sep);
    int pos = mux_atol(fargs[1]);

    if (pos < 0 || pos >= nWords)
    {
        safe_str(T("#-1 POSITION OUT OF RANGE"), buff, bufc);
        return;
    }

    bool bFirst = true;
    for (int i = 0; i < nWords; i++)
    {
        if (!bFirst)
        {
            print_sep(sep, buff, bufc);
        }
        if (i == pos)
        {
            safe_str(fargs[2], buff, bufc);
        }
        else
        {
            safe_str(arr[i], buff, bufc);
        }
        bFirst = false;
    }
}

// benchmark(<expression>, <iterations>) - Time expression evaluation.
// Evaluates the expression N times and returns elapsed seconds.
//
static FUNCTION(fun_benchmark)
{
    UNUSED_PARAMETER(fp);
    UNUSED_PARAMETER(caller);

    int iterations = mux_atol(fargs[1]);
    if (iterations < 1)
    {
        safe_str(T("#-1 ITERATIONS MUST BE POSITIVE"), buff, bufc);
        return;
    }
    if (iterations > 10000) iterations = 10000;

    const UTF8 *expr = fargs[0];
    size_t nLen = strlen(reinterpret_cast<const char *>(expr));

#ifdef WIN32
    LARGE_INTEGER freq, pc0, pc1;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&pc0);
#else
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
#endif

    for (int i = 0; i < iterations; i++)
    {
        UTF8 temp[LBUF_SIZE];
        UTF8 *tp = temp;
        mux_exec(expr, nLen, temp, &tp, executor, caller, enactor,
                 eval | EV_STRIP_CURLY | EV_FCHECK | EV_EVAL,
                 cargs, ncargs);
        *tp = '\0';
    }

#ifdef WIN32
    QueryPerformanceCounter(&pc1);
    double elapsed = static_cast<double>(pc1.QuadPart - pc0.QuadPart)
                   / static_cast<double>(freq.QuadPart);
#else
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (t1.tv_sec - t0.tv_sec)
                   + (t1.tv_nsec - t0.tv_nsec) / 1e9;
#endif

    fval(buff, bufc, elapsed);
}

// ----------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Tier 3 u()-inlining helper functions.
//
// These are internal-only functions registered in the standard function
// table so the JIT compiler can call them via ECALL_CALL_INDEX with
// normal string marshaling.  No custom ECALL types or codegen changes.
//
// _CHECK_U_PERM(thing_dbref_str, attr_num_str)
//   Runtime permission guard for inlined u() bodies.
//   Returns "0" if the executor can read the attr and it's not NOEVAL.
//   Returns "1" if denied (visibility, object NOEVAL, or AF_NOEVAL).
//
static FUNCTION(fun__check_u_perm)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs < 2)
    {
        safe_chr('1', buff, bufc);
        return;
    }

    dbref thing = mux_atol(fargs[0]);
    int attr_num = mux_atol(fargs[1]);

    if (!Good_obj(thing))
    {
        safe_chr('1', buff, bufc);
        return;
    }

    ATTR *ap = atr_num(attr_num);
    if (!ap)
    {
        safe_chr('1', buff, bufc);
        return;
    }

    // Visibility check.
    if (!See_attr(executor, thing, ap))
    {
        safe_chr('1', buff, bufc);
        return;
    }

    // NoEval check (object flag).
    if (NoEval(thing))
    {
        safe_chr('1', buff, bufc);
        return;
    }

    // AF_NOEVAL check (attr flag).
    dbref aowner;
    int aflags;
    size_t nLen = 0;
    UTF8 *atext = atr_pget_LEN(thing, attr_num, &aowner, &aflags, &nLen);
    free_lbuf(atext);

    if (aflags & AF_NOEVAL)
    {
        safe_chr('1', buff, bufc);
        return;
    }

    safe_chr('0', buff, bufc);
}

// _SAVE_QREGS()
//   Save global %q registers for ulocal() inlining.
//   Returns a handle string (index into a static save stack).
//
static constexpr int MAX_QREG_SAVE_DEPTH = 16;
static struct {
    reg_ref **preserve;
    bool in_use;
} s_qreg_save_stack[MAX_QREG_SAVE_DEPTH];

static FUNCTION(fun__save_qregs)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    for (int i = 0; i < MAX_QREG_SAVE_DEPTH; i++)
    {
        if (!s_qreg_save_stack[i].in_use)
        {
            s_qreg_save_stack[i].preserve = PushRegisters(MAX_GLOBAL_REGS);
            save_global_regs(s_qreg_save_stack[i].preserve);
            s_qreg_save_stack[i].in_use = true;
            safe_ltoa(i, buff, bufc);
            return;
        }
    }
    // Stack full — return -1 (caller should fall back to ECALL fun_u).
    safe_str(T("-1"), buff, bufc);
}

// _RESTORE_QREGS(handle_str)
//   Restore global %q registers saved by _SAVE_QREGS.
//
static FUNCTION(fun__restore_qregs)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    if (nfargs < 1) return;

    int idx = mux_atol(fargs[0]);
    if (idx >= 0 && idx < MAX_QREG_SAVE_DEPTH
        && s_qreg_save_stack[idx].in_use)
    {
        restore_global_regs(s_qreg_save_stack[idx].preserve);
        PopRegisters(s_qreg_save_stack[idx].preserve, MAX_GLOBAL_REGS);
        s_qreg_save_stack[idx].in_use = false;
    }
}

// _SAVE_CARGS, _RESTORE_CARGS, _WRITE_CARG, _SET_NCARGS:
// defined in jit_compiler.cpp where s_current_ecall_ctx provides
// guest memory access.
//
extern "C++" {
    FUNCTION(fun__save_cargs);
    FUNCTION(fun__restore_cargs);
    FUNCTION(fun__write_carg);
    FUNCTION(fun__set_ncargs);
}

// flist: List of existing functions in alphabetical order.
//
//   Name          Handler      # of args   min #    max #   flags  permissions
//                               to parse  of args  of args
//
static FUN builtin_function_list[] =
{
    {T("@@"),          fun_null,             1, 1,       1, FN_NOEVAL, CA_PUBLIC},
    {T("_CHECK_U_PERM"),  fun__check_u_perm,  MAX_ARG, 2, 2, 0, CA_GOD},
    {T("_RESTORE_CARGS"), fun__restore_cargs, MAX_ARG, 1, 1, 0, CA_GOD},
    {T("_RESTORE_QREGS"), fun__restore_qregs, MAX_ARG, 1, 1, 0, CA_GOD},
    {T("_SAVE_CARGS"),    fun__save_cargs,    MAX_ARG, 0, 0, 0, CA_GOD},
    {T("_SAVE_QREGS"),    fun__save_qregs,    MAX_ARG, 0, 0, 0, CA_GOD},
    {T("_SET_NCARGS"),    fun__set_ncargs,    MAX_ARG, 1, 1, 0, CA_GOD},
    {T("_WRITE_CARG"),    fun__write_carg,    MAX_ARG, 2, 2, 0, CA_GOD},
    {T("ABS"),         fun_abs,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ACCENT"),      fun_accent,     MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("ACOS"),        fun_acos,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("ADD"),         fun_add,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("ADDRLOG"),     fun_addrlog,    MAX_ARG, 1,       2,         0, CA_WIZARD},
    {T("AFTER"),       fun_after,      MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("ALLOF"),       fun_allof,      MAX_ARG, 1, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("ALPHAMAX"),    fun_alphamax,   MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("ALPHAMIN"),    fun_alphamin,   MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("AND"),         fun_and,        MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("ANDBOOL"),     fun_andbool,    MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("ANDFLAGS"),    fun_andflags,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("ANSI"),        fun_ansi,       MAX_ARG, 2, MAX_ARG,         0, CA_PUBLIC},
    {T("APOSS"),       fun_aposs,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ART"),         fun_art,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ASIN"),        fun_asin,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("ATAN"),        fun_atan,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("ATAN2"),       fun_atan2,      MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("ASTBENCH"),    fun_astbench,   MAX_ARG, 2,       2, FN_NOEVAL, CA_WIZARD},
    {T("ASTEVAL"),     fun_asteval,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ATTRCNT"),     fun_attrcnt,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ATTRIB_SET"), fun_attrib_set, MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("BAND"),        fun_band,       MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("BASECONV"),    fun_baseconv,   MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("BEEP"),        fun_beep,       MAX_ARG, 0,       0,         0, CA_WIZARD},
    {T("BEFORE"),      fun_before,     MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("BENCHMARK"),   fun_benchmark,  MAX_ARG, 2,       2, FN_NOEVAL, CA_PUBLIC},
    {T("BETWEEN"),     fun_between,    MAX_ARG, 3,       4,         0, CA_PUBLIC},
    {T("BITTYPE"),     fun_bittype,    MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("BNAND"),       fun_bnand,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("BOR"),         fun_bor,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("BOUND"),       fun_bound,      MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("BXOR"),        fun_bxor,       MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("CAND"),        fun_cand,       MAX_ARG, 0, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("CANDBOOL"),    fun_candbool,   MAX_ARG, 0, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
#if defined(WOD_REALMS) || defined(REALITY_LVLS)
    {T("CANSEE"),      fun_cansee,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
#endif
    {T("CAPLIST"),     fun_caplist,    MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("CAPSTR"),      fun_capstr,           1, 1,       1,         0, CA_PUBLIC},
    {T("CASE"),        fun_case,       MAX_ARG, 2, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("CASEALL"),     fun_caseall,    MAX_ARG, 2, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("CAT"),         fun_cat,        MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("CEIL"),        fun_ceil,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CEMIT"),       fun_cemit,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("CENTER"),      fun_center,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("CHANFIND"),    fun_chanfind,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CHANINFO"),    fun_chaninfo,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("CHANNELS"),    fun_channels,   MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("CHANOBJ"),     fun_chanobj,    MAX_ARG, 1,       1,         0, CA_WIZARD},
    {T("CHANUSER"),    fun_chanuser,   MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("CHANUSERS"),   fun_chanusers,  MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("CBUFFER"),     fun_cbuffer,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CDESC"),       fun_cdesc,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CFLAGS"),      fun_cflags,     MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("CMOGRIFIER"),  fun_cmogrifier, MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CMSGS"),       fun_cmsgs,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("COWNER"),      fun_cowner,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CRECALL"),     fun_crecall,    MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("CSTATUS"),     fun_cstatus,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("CUSERS"),      fun_cusers,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CHILDREN"),    fun_children,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CHOOSE"),      fun_choose,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("CHR"),         fun_chr,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CITER"),       fun_citer,      MAX_ARG, 2,       3, FN_NOEVAL, CA_PUBLIC},
    {T("CMDS"),        fun_cmds,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("COLORDEPTH"),  fun_colordepth, MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("COLUMNS"),     fun_columns,    MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("COMALIAS"),    fun_comalias,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("COMP"),        fun_comp,       MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("COMTITLE"),    fun_comtitle,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("CON"),         fun_con,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CONFIG"),      fun_config,     MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("CONN"),        fun_conn,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CONNLAST"),    fun_connlast,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CONNLEFT"),    fun_connleft,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CONNLOG"),     fun_connlog,    MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("CONNMAX"),     fun_connmax,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CONNNUM"),     fun_connnum,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CONNRECORD"),  fun_connrecord, MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("CONNTOTAL"),   fun_conntotal,  MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CONTROLS"),    fun_controls,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("CONVSECS"),    fun_convsecs,   MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("CONVTIME"),    fun_convtime,   MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("COR"),         fun_cor,        MAX_ARG, 0, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("CORBOOL"),     fun_corbool,    MAX_ARG, 0, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("COS"),         fun_cos,        MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("CPAD"),        fun_cpad,       MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("CRC32"),       fun_crc32,      MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("CRC32OBJ"),    fun_crc32obj,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CLONE"),       fun_clone,      MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("CREATE"),      fun_create,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("CTIME"),       fun_ctime,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("CTU"),         fun_ctu,        MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("CWHO"),        fun_cwho,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("DEC"),         fun_dec,        MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("DECODE64"),    fun_decode64,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("DECRYPT"),     fun_decrypt,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("DEFAULT"),     fun_default,    MAX_ARG, 2,       2, FN_NOEVAL, CA_PUBLIC},
    {T("DELETE"),      fun_delete,     MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("DELEXTRACT"),  fun_delextract, MAX_ARG, 3,       4,         0, CA_PUBLIC},
    {T("DESTROY"),     fun_destroy,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("DIE"),         fun_die,        MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("DIGEST"),      fun_digest,           2, 1,       2,         0, CA_PUBLIC},
    {T("DIGITTIME"),   fun_digittime,  MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("DIST2D"),      fun_dist2d,     MAX_ARG, 4,       4,         0, CA_PUBLIC},
    {T("DIST3D"),      fun_dist3d,     MAX_ARG, 6,       6,         0, CA_PUBLIC},
    {T("DISTRIBUTE"),  fun_distribute, MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("DOING"),       fun_doing,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("DUMPING"),     fun_dumping,    MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("DYNHELP"),     fun_dynhelp,    MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("E"),           fun_e,          MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("EDEFAULT"),    fun_edefault,   MAX_ARG, 2,       2, FN_NOEVAL, CA_PUBLIC},
    {T("EDIT"),        fun_edit,       MAX_ARG, 3, MAX_ARG,         0, CA_PUBLIC},
    {T("ELEMENTS"),    fun_elements,   MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("ELOCK"),       fun_elock,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("EMIT"),        fun_emit,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ENCODE64"),    fun_encode64,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ENCRYPT"),     fun_encrypt,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("ENTRANCES"),   fun_entrances,  MAX_ARG, 0,       4,         0, CA_PUBLIC},
    {T("EQ"),          fun_eq,         MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("ERROR"),       fun_error,            1, 0,       1,         0, CA_PUBLIC},
    {T("ESCAPE"),      fun_escape,           1, 1,       1,         0, CA_PUBLIC},
    {T("ETIMEFMT"),    fun_etimefmt,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("EVAL"),        fun_eval,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("EXIT"),        fun_exit,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("EXP"),         fun_exp,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("EXPTIME"),     fun_exptime,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("EXTRACT"),     fun_extract,    MAX_ARG, 3,       5,         0, CA_PUBLIC},
    {T("FCOUNT"),      fun_fcount,     MAX_ARG, 0,       1, FN_NOEVAL, CA_PUBLIC},
    {T("FDEPTH"),      fun_fdepth,     MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("FDIV"),        fun_fdiv,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("FILTER"),      fun_filter,     MAX_ARG, 2,      13,         0, CA_PUBLIC},
    {T("FILTERBOOL"),  fun_filterbool, MAX_ARG, 2,      13,         0, CA_PUBLIC},
    {T("FINDABLE"),    fun_findable,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("FIRST"),       fun_first,      MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("FIRSTOF"),     fun_firstof,    MAX_ARG, 1, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("FLAGS"),       fun_flags,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("FLOOR"),       fun_floor,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("FLOORDIV"),    fun_floordiv,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("FMOD"),        fun_fmod,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("FOLD"),        fun_fold,       MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("FOREACH"),     fun_foreach,    MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("FULLNAME"),    fun_fullname,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("GET"),         fun_get,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("GET_EVAL"),    fun_get_eval,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("GMCP"),        fun_gmcp,       MAX_ARG, 2,       3,         0, CA_WIZARD},
    {T("GARBLE"),      fun_garble,     MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("GRAB"),        fun_grab,       MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("GRABALL"),     fun_graball,    MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("GREP"),        fun_grep,       MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("GREPI"),       fun_grepi,      MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("GT"),          fun_gt,         MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("GTE"),         fun_gte,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("HASATTR"),     fun_hasattr,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("HASATTRP"),    fun_hasattrp,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("HASFLAG"),     fun_hasflag,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("HASPOWER"),    fun_haspower,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("HASQUOTA"),    fun_hasquota,   MAX_ARG, 2,       3,         0, CA_PUBLIC},
#ifdef REALITY_LVLS
    {T("HASRXLEVEL"),  fun_hasrxlevel, MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("HASTXLEVEL"),  fun_hastxlevel, MAX_ARG, 2,       2,         0, CA_PUBLIC},
#endif // REALITY_LVLS
    {T("HASTYPE"),     fun_hastype,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("HEIGHT"),      fun_height,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("HMAC"),        fun_hmac,       MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("HOME"),        fun_home,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("HOST"),        fun_host,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("IABS"),        fun_iabs,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("IADD"),        fun_iadd,       MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("IDIV"),        fun_idiv,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("IDLE"),        fun_idle,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("IF"),          fun_ifelse,     MAX_ARG, 2,       3, FN_NOEVAL, CA_PUBLIC},
    {T("IFELSE"),      fun_ifelse,     MAX_ARG, 3,       3, FN_NOEVAL, CA_PUBLIC},
    {T("ILEV"),        fun_ilev,       MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("IMUL"),        fun_imul,       MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("INC"),         fun_inc,        MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("INDEX"),       fun_index,      MAX_ARG, 4,       4,         0, CA_PUBLIC},
    {T("INSERT"),      fun_insert,     MAX_ARG, 3,       5,         0, CA_PUBLIC},
    {T("INUM"),        fun_inum,       MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("INZONE"),      fun_inzone,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ISALNUM"),     fun_isalnum,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ISALPHA"),     fun_isalpha,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ISDBREF"),     fun_isdbref,    MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("ISDIGIT"),     fun_isdigit,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ISIGN"),       fun_isign,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ISINT"),       fun_isint,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ISNUM"),       fun_isnum,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ISOBJID"),     fun_isobjid,    MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("ISJSON"),      fun_isjson,     MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("ISRAT"),       fun_israt,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ISUB"),        fun_isub,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("ISWORD"),      fun_isword,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ITEMIZE"),     fun_itemize,    MAX_ARG, 1,       4,         0, CA_PUBLIC},
    {T("ITER"),        fun_iter,       MAX_ARG, 2,       4, FN_NOEVAL, CA_PUBLIC},
    {T("ITEXT"),       fun_itext,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
#ifdef TINYMUX_JIT
    {T("JITSTATS"),    fun_jitstats,   MAX_ARG, 0,       1,         0, CA_WIZARD},
#endif
    {T("JSON"),        fun_json,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("JSON_MOD"),    fun_json_mod,   MAX_ARG, 3,       4,         0, CA_PUBLIC},
    {T("JSON_QUERY"),  fun_json_query, MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("LADD"),        fun_ladd,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("LAND"),        fun_land,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("LAST"),        fun_last,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("LASTCREATE"),  fun_lastcreate, MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("LATTR"),       fun_lattr,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LATTRCMDS"),   fun_lattrcmds,  MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LATTRP"),      fun_lattrp,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
#ifdef REALITY_LVLS
    {T("LISTRLEVELS"), fun_listrlevels, MAX_ARG, 0,       0,         0, CA_PUBLIC},
#endif
    {T("LCMDS"),       fun_lcmds,      MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("LCON"),        fun_lcon,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("LCSTR"),       fun_lcstr,            1, 1,       1,         0, CA_PUBLIC},
    {T("LDELETE"),     fun_ldelete,    MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("LEDIT"),       fun_ledit,      MAX_ARG, 3,       5,         0, CA_PUBLIC},
    {T("LETQ"),        fun_letq,       MAX_ARG, 3, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("LEXITS"),      fun_lexits,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LFLAGS"),      fun_lflags,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LINK"),        fun_link,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("LINSERT"),     fun_linsert,    MAX_ARG, 3,       4,         0, CA_PUBLIC},
    {T("LIST"),        fun_list,       MAX_ARG, 2,       3, FN_NOEVAL, CA_PUBLIC},
    {T("LISTQ"),       fun_listq,      MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("LIT"),         fun_lit,              1, 1,       1, FN_NOEVAL, CA_PUBLIC},
    {T("LJUST"),       fun_ljust,      MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("LMAX"),        fun_lmax,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("LMATH"),       fun_lmath,      MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("LMIN"),        fun_lmin,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("LN"),          fun_ln,         MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LNUM"),        fun_lnum,       MAX_ARG, 0,       4,         0, CA_PUBLIC},
    {T("LOC"),         fun_loc,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LOCALIZE"),    fun_localize,   MAX_ARG, 1,       1, FN_NOEVAL, CA_PUBLIC},
    {T("LOCATE"),      fun_locate,     MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("LOCK"),        fun_lock,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("LOCKDECODE"),  fun_lockdecode, MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LOCKENCODE"),  fun_lockencode, MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LOG"),         fun_log,        MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("LOR"),         fun_lor,        MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("LPAD"),        fun_lpad,       MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("LPARENT"),     fun_lparent,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LPORTS"),      fun_lports,     MAX_ARG, 0,       0,         0, CA_WIZARD},
    {T("LPOS"),        fun_lpos,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("LPLAYERS"),    fun_lplayers,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LRAND"),       fun_lrand,      MAX_ARG, 3,       4,         0, CA_PUBLIC},
    {T("LREPLACE"),    fun_lreplace,   MAX_ARG, 3,       4,         0, CA_PUBLIC},
    {T("LREST"),       fun_lrest,      MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("LROOMS"),      fun_lrooms,     MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("LT"),          fun_lt,         MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("LTE"),         fun_lte,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("LTHINGS"),     fun_lthings,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LUA"),         fun_lua,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("LWHO"),        fun_lwho,       MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("MAIL"),        fun_mail,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("MAILCOUNT"),   fun_mailcount,  MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("MAILFLAGS"),   fun_mailflags,  MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("MAILFROM"),    fun_mailfrom,   MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("MAILINFO"),    fun_mailinfo,   MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("MAILLIST"),    fun_maillist,   MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("MAILREVIEW"),  fun_mailreview, MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("MAILSEND"),    fun_mailsend,   MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("MAILSIZE"),    fun_mailsize,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("MAILSTATS"),   fun_mailstats,  MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("MAILSUBJ"),    fun_mailsubj,   MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("MALIAS"),      fun_malias,     MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("MAP"),         fun_map,        MAX_ARG, 2,      13,         0, CA_PUBLIC},
#if defined(INLINESQL)
    {T("MAPSQL"),      fun_mapsql,     MAX_ARG, 2,       3,         0, CA_WIZARD},
#endif // INLINESQL
    {T("MATCH"),       fun_match,      MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("MATCHALL"),    fun_matchall,   MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("MAX"),         fun_max,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("MEAN"),        fun_mean,       MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("MEDIAN"),      fun_median,     MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("MEMBER"),      fun_member,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("MERGE"),       fun_merge,      MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("MID"),         fun_mid,        MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("MIN"),         fun_min,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("MIX"),         fun_mix,        MAX_ARG, 2,      12,         0, CA_PUBLIC},
    {T("MOD"),         fun_mod,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("MONEY"),       fun_money,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("MOON"),        fun_moon,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("MONIKER"),     fun_moniker,    MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("MOTD"),        fun_motd,       MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("MTIME"),       fun_mtime,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("MUDNAME"),     fun_mudname,    MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("MUL"),         fun_mul,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("MUNGE"),       fun_munge,      MAX_ARG, 3,       4,         0, CA_PUBLIC},
    {T("NAME"),        fun_name,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("NCON"),        fun_ncon,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("NEARBY"),      fun_nearby,     MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("NEQ"),         fun_neq,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("NEXITS"),      fun_nexits,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("NEXT"),        fun_next,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("NOT"),         fun_not,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("NPLAYERS"),    fun_nplayers,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("NTHINGS"),     fun_nthings,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("NSEMIT"),      fun_nsemit,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("NSOEMIT"),     fun_nsoemit,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("NSPEMIT"),     fun_nspemit,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("NSREMIT"),     fun_nsremit,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("NULL"),        fun_null,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("NUM"),         fun_num,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("OBJ"),         fun_obj,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("OBJEVAL"),     fun_objeval,    MAX_ARG, 2,       2, FN_NOEVAL, CA_PUBLIC},
    {T("OBJID"),       fun_objid,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("OBJMEM"),      fun_objmem,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("OEMIT"),       fun_oemit,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("OR"),          fun_or,         MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("ORBOOL"),      fun_orbool,     MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("ORD"),         fun_ord,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ORFLAGS"),     fun_orflags,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("OWNER"),       fun_owner,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("PACK"),        fun_pack,       MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("PARENT"),      fun_parent,     MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("PARENMATCH"), fun_parenmatch,       1, 1,       1, FN_NOEVAL, CA_PUBLIC},
    {T("PEMIT"),       fun_pemit,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("PFIND"),       fun_pfind,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("PI"),          fun_pi,         MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("PICKRAND"),    fun_pickrand,   MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("PLAYMEM"),     fun_playmem,    MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("PMATCH"),      fun_pmatch,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("POLL"),        fun_poll,       MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("PORTS"),       fun_ports,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("POS"),         fun_pos,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("PROMPT"),      fun_prompt,     MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("POSE"),        fun_pose,       MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("POSS"),        fun_poss,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("POWER"),       fun_power,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("POWERS"),      fun_powers,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("PRINTF"),      fun_printf,     MAX_ARG, 1,      11,         0, CA_PUBLIC},
    {T("R"),           fun_r,          MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("RAND"),        fun_rand,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("REGMATCH"),    fun_regmatch,   MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("REGREP"),      fun_regrep,     MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("REGREPI"),     fun_regrepi,    MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("REGMATCHI"),   fun_regmatchi,  MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("REGRAB"),      fun_regrab,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("REGRABALL"),   fun_regraball,  MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("REGRABALLI"),  fun_regraballi, MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("REGRABI"),     fun_regrabi,    MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("REGEDIT"),     fun_regedit,    MAX_ARG, 3, MAX_ARG,         0, CA_PUBLIC},
    {T("REGEDITI"),    fun_regediti,   MAX_ARG, 3, MAX_ARG,         0, CA_PUBLIC},
    {T("REGEDITALL"),  fun_regeditall, MAX_ARG, 3, MAX_ARG,         0, CA_PUBLIC},
    {T("REGEDITALLI"), fun_regeditalli,MAX_ARG, 3, MAX_ARG,         0, CA_PUBLIC},
    {T("REGLATTR"),    fun_reglattr,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("REGLATTRI"),   fun_reglattri,  MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("REGNATTR"),    fun_regnattr,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("REGNATTRI"),   fun_regnattri,  MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("REMAINDER"),   fun_remainder,  MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("REMIT"),       fun_remit,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("REMOVE"),      fun_remove,     MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("REPEAT"),      fun_repeat,     MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("REPLACE"),     fun_replace,    MAX_ARG, 3,       5,         0, CA_PUBLIC},
    {T("REST"),        fun_rest,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("RESTARTS"),    fun_restarts,   MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("RESTARTSECS"), fun_restartsecs, MAX_ARG, 0,      0,         0, CA_PUBLIC},
    {T("RESTARTTIME"), fun_restarttime, MAX_ARG, 0,      0,         0, CA_PUBLIC},
    {T("REVERSE"),     fun_reverse,          1, 1,       1,         0, CA_PUBLIC},
    {T("REVWORDS"),    fun_revwords,   MAX_ARG, 0,       3,         0, CA_PUBLIC},
    {T("RIGHT"),       fun_right,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("RJUST"),       fun_rjust,      MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("RLOC"),        fun_rloc,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("ROMAN"),       fun_roman,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ROOM"),        fun_room,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ROUND"),       fun_round,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("RPAD"),        fun_rpad,       MAX_ARG, 2,       3,         0, CA_PUBLIC},
#if defined(STUB_SLAVE)
    {T("RSERROR"),     fun_rserror,    MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("RSNEXT"),      fun_rsnext,     MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("RSRECNEXT"),   fun_rsrecnext,  MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("RSPREV"),      fun_rsprev,     MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("RSRECPREV"),   fun_rsrecprev,  MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("RSRELEASE"),   fun_rsrelease,  MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("RSROWS"),      fun_rsrows,     MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("RSREC"),       fun_rsrec,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
#endif
#ifdef REALITY_LVLS
    {T("RXLEVEL"),     fun_rxlevel,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
#endif // REALITY_LVLS
#if defined(TINYMUX_JIT)
    {T("RVBENCH"),     fun_rvbench,    MAX_ARG, 2,       2, FN_NOEVAL, CA_WIZARD},
#endif
    {T("S"),           fun_s,                1, 1,       1,         0, CA_PUBLIC},
    {T("SCRAMBLE"),    fun_scramble,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("SEARCH"),      fun_search,           1, 0,       1,         0, CA_PUBLIC},
    {T("SECS"),        fun_secs,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("SANDBOX"),     fun_sandbox,    MAX_ARG, 2,       3, FN_NOEVAL, CA_PUBLIC},
    {T("SECURE"),      fun_secure,           1, 1,       1,         0, CA_PUBLIC},
    {T("SET"),         fun_set,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("SETDIFF"),     fun_setdiff,    MAX_ARG, 2,       5,         0, CA_PUBLIC},
    {T("SETINTER"),    fun_setinter,   MAX_ARG, 2,       5,         0, CA_PUBLIC},
    {T("SETQ"),        fun_setq,       MAX_ARG, 2,       MAX_ARG,   0, CA_PUBLIC},
    {T("SETR"),        fun_setr,       MAX_ARG, 2,       MAX_ARG,   0, CA_PUBLIC},
    {T("SETUNION"),    fun_setunion,   MAX_ARG, 2,       5,         0, CA_PUBLIC},
    {T("SHA1"),        fun_sha1,             1, 0,       1,         0, CA_PUBLIC},
    {T("SHL"),         fun_shl,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("SHR"),         fun_shr,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("SHUFFLE"),     fun_shuffle,    MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("SIGN"),        fun_sign,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("SIN"),         fun_sin,        MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("SINGLETIME"),  fun_singletime, MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("SITEINFO"),    fun_siteinfo,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("SORT"),        fun_sort,       MAX_ARG, 1,       4,         0, CA_PUBLIC},
    {T("SORTBY"),      fun_sortby,     MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("SORTKEY"),     fun_sortkey,    MAX_ARG, 2,       5,         0, CA_PUBLIC},
    {T("SOUNDEX"),     fun_soundex,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("SOUNDLIKE"),   fun_soundlike,  MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("SUBNETMATCH"), fun_subnetmatch,MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("SPACE"),       fun_space,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("SPELLNUM"),    fun_spellnum,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("SPLICE"),      fun_splice,     MAX_ARG, 3,       5,         0, CA_PUBLIC},
#if defined(INLINESQL)
    {T("SQL"),         fun_sql,        MAX_ARG, 1,       3,         0, CA_WIZARD},
#endif // INLINESQL
    {T("SQRT"),        fun_sqrt,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("SQUISH"),      fun_squish,     MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("STARTSECS"),   fun_startsecs,  MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("STARTTIME"),   fun_starttime,  MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("STATS"),       fun_stats,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("STDDEV"),      fun_stddev,     MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("STEP"),        fun_step,       MAX_ARG, 3,       5,         0, CA_PUBLIC},
    {T("STRALLOF"),    fun_strallof,   MAX_ARG, 1, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("STRCAT"),      fun_strcat,     MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("STRDELETE"),   fun_strdelete,  MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("STRFIRSTOF"),  fun_strfirstof, MAX_ARG, 1, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("STRIP"),       fun_strip,      MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("STRIPACCENTS"),fun_stripaccents, MAX_ARG, 1,     1,         0, CA_PUBLIC},
    {T("STRIPANSI"),   fun_stripansi,  MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("STRINSERT"),   fun_strinsert,  MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("STRLEN"),      fun_strlen,           1, 0,       1,         0, CA_PUBLIC},
    {T("STRMATCH"),    fun_strmatch,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("STRMEM"),      fun_strmem,           1, 0,       1,         0, CA_PUBLIC},
    {T("STRDISTANCE"), fun_strdistance,MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("STRREPLACE"),  fun_strreplace, MAX_ARG, 4,       4,         0, CA_PUBLIC},
    {T("STRTRUNC"),    fun_strtrunc,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("SUB"),         fun_sub,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("SUBEVAL"),     fun_subeval,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("SUBJ"),        fun_subj,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("SUCCESSES"),   fun_successes,  MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("SWITCH"),      fun_switch,     MAX_ARG, 2, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("SWITCHALL"),   fun_switchall,  MAX_ARG, 2, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("T"),           fun_t,                1, 0,       1,         0, CA_PUBLIC},
    {T("TABLE"),       fun_table,      MAX_ARG, 1,       6,         0, CA_PUBLIC},
    {T("TAN"),         fun_tan,        MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("TEL"),         fun_tel,        MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("TERMINFO"),    fun_terminfo,         1, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("TEXTFILE"),    fun_textfile,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("TIME"),        fun_time,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("TIMEFMT"),     fun_timefmt,    MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("TR"),          fun_tr,         MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("TRACE"),       fun_trace,      MAX_ARG, 1,       1, FN_NOEVAL, CA_PUBLIC},
    {T("TRANSLATE"),   fun_translate,  MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("TRIGGER"),     fun_trigger,    MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("TRIM"),        fun_trim,       MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("TRUNC"),       fun_trunc,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
#ifdef REALITY_LVLS
    {T("TXLEVEL"),     fun_txlevel,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
#endif // REALITY_LVLS
    {T("TYPE"),        fun_type,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("U"),           fun_u,          MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("UCSTR"),       fun_ucstr,            1, 1,       1,         0, CA_PUBLIC},
    {T("UDEFAULT"),    fun_udefault,   MAX_ARG, 2, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("ULOCAL"),      fun_ulocal,     MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("UNIQUE"),      fun_unique,     MAX_ARG, 1,       4,         0, CA_PUBLIC},
    {T("UNPACK"),      fun_unpack,     MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("UNSETQ"),      fun_unsetq,     MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("URL_ESCAPE"),  fun_url_escape, MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("URL_UNESCAPE"),fun_url_unescape,MAX_ARG,1,       1,         0, CA_PUBLIC},
    {T("V"),           fun_v,          MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("VADD"),        fun_vadd,       MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("VALID"),       fun_valid,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("VCROSS"),      fun_vcross,     MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("VDIM"),        fun_words,      MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("VDOT"),        fun_vdot,       MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("VERB"),        fun_verb,       MAX_ARG, 1,       8,         0, CA_PUBLIC},
    {T("VERSION"),     fun_version,    MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("VISIBLE"),     fun_visible,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("VMAG"),        fun_vmag,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("VMUL"),        fun_vmul,       MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("VSUB"),        fun_vsub,       MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("VUNIT"),       fun_vunit,      MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("WHERE"),       fun_where,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("WIDTH"),       fun_width,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("WIPE"),        fun_wipe,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("WORDPOS"),     fun_wordpos,    MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("WORDS"),       fun_words,      MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("WHILE"),       fun_while,      MAX_ARG, 4,       6,         0, CA_PUBLIC},
    {T("WRAP"),        fun_wrap,       MAX_ARG, 1,       8,         0, CA_PUBLIC},
    {T("WRAPCOLUMNS"),fun_wrapcolumns,MAX_ARG, 3,       8,         0, CA_PUBLIC},
    {T("WRITETIME"),   fun_writetime,  MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("XGET"),        fun_xget,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("XOR"),         fun_xor,        MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("ZCHILDREN"),   fun_zchildren,  MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ZEXITS"),      fun_zexits,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ZFUN"),        fun_zfun,       MAX_ARG, 2,      11,         0, CA_PUBLIC},
    {T("ZONE"),        fun_zone,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ZROOMS"),      fun_zrooms,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ZTHINGS"),     fun_zthings,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ZWHO"),        fun_zwho,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {nullptr,   nullptr,       MAX_ARG, 0,       0,         0, 0}
};

void function_add(FUN *fp)
{
    size_t nCased;
    UTF8 *pCased = mux_strupr(fp->name, nCased);
    vector<UTF8> name(pCased, pCased + nCased);
    const auto it = mudstate.builtin_functions.find(name);
    if (it == mudstate.builtin_functions.end())
    {
        mudstate.builtin_functions.insert(make_pair(name, fp));
    }
}

void function_remove(FUN *fp)
{
    size_t nCased;
    UTF8 *pCased = mux_strupr(fp->name, nCased);
    vector<UTF8> name(pCased, pCased + nCased);
    const auto it = mudstate.builtin_functions.find(name);
    if (it != mudstate.builtin_functions.end())
    {
        mudstate.builtin_functions.erase(it);
    }
}

void functions_add(FUN funlist[])
{
    for (FUN *fp = funlist; fp->name; fp++)
    {
        function_add(fp);
    }
}

void init_functab(void)
{
    functions_add(builtin_function_list);
    ufun_list.clear();
    engine_api_init();
}

// ---------------------------------------------------------------
// Engine API: indexed function dispatch table.
// ---------------------------------------------------------------

FUN *engine_api_table[ENGINE_API_MAX_FUNCS];
int   engine_api_count = 0;

void engine_api_init()
{
    memset(engine_api_table, 0, sizeof(engine_api_table));
    engine_api_count = 1;  // index 0 is reserved (invalid)

    for (FUN *fp = builtin_function_list; fp->name; fp++) {
        if (engine_api_count >= ENGINE_API_MAX_FUNCS) break;
        engine_api_table[engine_api_count] = fp;
        engine_api_count++;
    }
}

int engine_api_lookup(const char *name)
{
    // Linear scan — called at compile time only, not in the hot path.
    for (int i = 1; i < engine_api_count; i++) {
        if (mux_stricmp((const UTF8 *)name,
                        engine_api_table[i]->name) == 0) {
            return i;
        }
    }
    return 0;
}

// MakeCanonicalUserFunctionName
//
// We truncate the name to a length of MAX_UFUN_NAME_LEN, if
// necessary. ANSI is stripped.
//
UTF8 *MakeCanonicalUserFunctionName(const UTF8 *pName, size_t *pnName, bool *pbValid)
{
    static UTF8 Buffer[MAX_UFUN_NAME_LEN+1];

    if (  nullptr == pName
       || '\0' == pName[0])
    {
        *pnName = 0;
        *pbValid = false;
        return nullptr;
    }

    size_t nLen = 0;
    UTF8 *pNameStripped = strip_color(pName, &nLen);
    UTF8 *pCased = mux_strupr(pNameStripped, nLen);

    // TODO: Fix truncation.
    //
    if (sizeof(Buffer)-1 < nLen)
    {
        nLen = sizeof(Buffer)-1;
    }
    memcpy(Buffer, pCased, nLen);
    Buffer[nLen] = '\0';

    *pnName = nLen;
    *pbValid = true;
    return Buffer;
}

void do_function
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   eval,
    int   key,
    int   nargs,
    UTF8 *fname,
    UTF8 *target,
    const UTF8 *cargs[],
    int   ncargs
)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    UFUN *ufp;
    ATTR *ap;

    if (  (key & FN_LIST)
       || nullptr == fname
       || '\0' == fname[0])
    {
        notify(executor, tprintf(T("%-28s   %-8s  %-30s Flgs"),
            "Function Name", "DBref#", "Attribute"));
        notify(executor, tprintf(T("%28s   %8s  %30s %5s"),
            "----------------------------", "--------",
            "------------------------------", "--- "));

        int count = 0;
        for (auto &ufn : ufun_list)
        {
            const UTF8 *pName = T("(WARNING: Bad Attribute Number)");
            ap = atr_num(ufn.atr);
            if (ap)
            {
                pName = ap->name;
            }
            notify(executor, tprintf(T("%-28.28s   #%-7d  %-30.30s  %c%c%c"),
                reinterpret_cast<const UTF8 *>(ufn.name.c_str()), ufn.obj,
                pName, ((ufn.flags & FN_PRIV) ? 'W' : '-'),
                ((ufn.flags & FN_PRES) ? 'p' : '-'),
                ((ufn.flags & FN_RESTRICT) ? 'R' : '-')));
            count++;
        }

        notify(executor, tprintf(T("%28s   %8s  %30s %5s"),
            "----------------------------", "--------",
            "------------------------------", "--- "));

        notify(executor, tprintf(T("Total User-Defined Functions: %d"), count));
        return;
    }

    ATTR *pattr;
    dbref obj;

    // Canonicalize function name.
    //
    size_t nLen;
    bool bValid;
    UTF8 *pName = MakeCanonicalUserFunctionName(fname, &nLen, &bValid);
    if (!bValid)
    {
        notify_quiet(executor, T("Function name is not valid."));
        return;
    }

    // Verify that the function doesn't exist in the builtin table.
    //
    vector<UTF8> name(pName, pName + nLen);
    const auto it = mudstate.builtin_functions.find(name);
    if (it != mudstate.builtin_functions.end())
    {
        notify_quiet(executor, T("Function already defined in builtin function table."));
        return;
    }

    // Check if we're removing a function.
    //
    if (  (key & FN_DELETE)
       || (  2 == nargs
          && '\0' == target[0]))
    {
        auto it_ufunc = mudstate.ufunc_htab.find(std::vector<UTF8>(pName, pName + nLen));
        ufp = (it_ufunc != mudstate.ufunc_htab.end()) ? static_cast<UFUN*>(it_ufunc->second) : nullptr;
        if (nullptr == ufp)
        {
            notify_quiet(executor, tprintf(T("Function %s not found."), pName));
        }
        else
        {
            mudstate.ufunc_htab.erase(std::vector<UTF8>(pName, pName + nLen));
            for (auto it = ufun_list.begin(); it != ufun_list.end(); ++it)
            {
                if (&*it == ufp)
                {
                    ufun_list.erase(it);
                    break;
                }
            }
            notify_quiet(executor, tprintf(T("Function %s deleted."), pName));
        }
        return;
    }

    // Make sure the target object exists.
    //
    if (!parse_attrib(executor, target, &obj, &pattr))
    {
        notify_quiet(executor, NOMATCH_MESSAGE);
        return;
    }

    // Make sure the attribute exists.
    //
    if (!pattr)
    {
        notify_quiet(executor, T("No such attribute."));
        return;
    }

    // Make sure attribute is readably by me.
    //
    if (!See_attr(executor, obj, pattr))
    {
        notify_quiet(executor, NOPERM_MESSAGE);
        return;
    }

    // Privileged and restricted functions require you control the obj.
    //
    if ((key & (FN_PRIV | FN_RESTRICT)) && !Controls(executor, obj))
    {
        notify_quiet(executor, NOPERM_MESSAGE);
        return;
    }

    // See if function already exists.  If so, redefine it.
    //
    auto it_ufunc2 = mudstate.ufunc_htab.find(std::vector<UTF8>(pName, pName + nLen));
    ufp = (it_ufunc2 != mudstate.ufunc_htab.end()) ? static_cast<UFUN*>(it_ufunc2->second) : nullptr;

    if (!ufp)
    {
        UFUN newufun;
        newufun.name.assign(reinterpret_cast<const char *>(pName), nLen);
        newufun.obj = obj;
        newufun.atr = pattr->number;
        newufun.perms = CA_PUBLIC;
        ufun_list.push_back(std::move(newufun));
        ufp = &ufun_list.back();
        mudstate.ufunc_htab.emplace(std::vector<UTF8>(pName, pName + nLen), ufp);
    }
    ufp->obj = obj;
    ufp->atr = pattr->number;
    ufp->flags = key;
    if (!Quiet(executor))
    {
        notify_quiet(executor, tprintf(T("Function %s defined."), pName));
    }
}

// ---------------------------------------------------------------------------
// list_functable: List available functions.
//
void list_functable(dbref player)
{
    UTF8 *buff = alloc_lbuf("list_functable");
    UTF8 *bp = buff;

    safe_str(T("Functions:"), buff, &bp);

    FUN *fp;
    for (fp = builtin_function_list; fp->name && bp < buff + (LBUF_SIZE-1); fp++)
    {
        if (check_access(player, fp->perms))
        {
            safe_chr(' ', buff, &bp);
            safe_str(fp->name, buff, &bp);
        }
    }
    *bp = '\0';
    notify(player, buff);

    bp = buff;
    safe_str(T("User-Functions:"), buff, &bp);

    for (auto &ufn : ufun_list)
    {
        if (bp >= buff + (LBUF_SIZE-1)) break;
        if (check_access(player, ufn.perms))
        {
            safe_chr(' ', buff, &bp);
            safe_str(reinterpret_cast<const UTF8 *>(ufn.name.c_str()), buff, &bp);
        }
    }
    *bp = '\0';
    notify(player, buff);
    free_lbuf(buff);
}


/* ---------------------------------------------------------------------------
 * cf_func_access: set access on functions
 */

CF_HAND(cf_func_access)
{
    UNUSED_PARAMETER(vp);

    // Find end of function name (first space or NUL).
    //
    UTF8 *ap;
    for (ap = str; *ap && !mux_isspace(*ap); ap++)
    {
        ; // Nothing.
    }
    size_t nOrigName = ap - str;

    // Uppercase the function name (Unicode-aware).
    //
    UTF8 TempName[LBUF_SIZE];
    memcpy(TempName, str, nOrigName);
    TempName[nOrigName] = '\0';
    size_t nUpper;
    UTF8 *pUpper = mux_strupr(TempName, nUpper);

    // Copy uppercased name back, shifting the rest of str if length changed.
    //
    if (nUpper != nOrigName)
    {
        size_t nTail = mux_strlen(ap);
        memmove(str + nUpper, ap, nTail + 1);
    }
    memcpy(str, pUpper, nUpper);
    ap = str + nUpper;
    size_t nstr = nUpper;

    if (*ap)
    {
        *ap++ = '\0';
    }

    vector<UTF8> name(str, str + nstr);
    const auto it = mudstate.builtin_functions.find(name);
    if (it != mudstate.builtin_functions.end())
    {
        FUN* fp = it->second;
        return cf_modify_bits(&fp->perms, ap, pExtra, nExtra, player, cmd);
    }
    auto it_ufunc = mudstate.ufunc_htab.find(std::vector<UTF8>(str, str + nstr));
    UFUN *ufp = (it_ufunc != mudstate.ufunc_htab.end()) ? static_cast<UFUN*>(it_ufunc->second) : nullptr;
    if (ufp)
    {
        return cf_modify_bits(&ufp->perms, ap, pExtra, nExtra, player, cmd);
    }
    cf_log_notfound(player, cmd, T("Function"), str);
    return -1;
}

// CFunctions component which is not directly accessible.
//

class CFunctions;

typedef struct FunctionsNode
{
    FUN                   fun;
    mux_IFunction         *pIFun;
    unsigned int          nKey;
    struct FunctionsNode *next;
} FunctionsNode;

class CFunctions : public mux_IFunctionsControl
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t     AddRef(void);
    virtual uint32_t     Release(void);

    // mux_IFunctionsControl
    //
    virtual MUX_RESULT Add(unsigned int nKey, const UTF8 *name, mux_IFunction *pIFun, int maxArgsParsed, int minArgs, int maxArgs, int flags, int perms);

    CFunctions(void);
    virtual ~CFunctions();

private:
    uint32_t          m_cRef;
    FunctionsNode  *m_pHead;
};

CFunctions::CFunctions(void) : m_cRef(1), m_pHead(nullptr)
{
}

CFunctions::~CFunctions()
{
    FunctionsNode *pfn;
    while (nullptr != m_pHead)
    {
        pfn = m_pHead;
        m_pHead = pfn->next;

        function_remove(&pfn->fun);

        pfn->fun.vp = nullptr;
        if (nullptr != pfn->pIFun)
        {
            pfn->pIFun->Release();
            pfn->pIFun = nullptr;
        }
        pfn->next = nullptr;

        delete pfn;
    }
}

MUX_RESULT CFunctions::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IFunctionsControl *>(this);
    }
    else if (IID_IFunctionsControl == iid)
    {
        *ppv = static_cast<mux_IFunctionsControl *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CFunctions::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CFunctions::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

static FUNCTION(fun_Functions)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(fargs);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    FunctionsNode *pfn = reinterpret_cast<FunctionsNode *>(fp->vp);
    if (nullptr != pfn)
    {
        mux_IFunction *pIFun = pfn->pIFun;
        if (nullptr != pIFun)
        {
            MUX_RESULT mr = pIFun->Call(pfn->nKey, buff, bufc, executor, caller, enactor, eval, fargs, nfargs, cargs, ncargs);
            UNUSED_PARAMETER(mr);
        }
    }
}

MUX_RESULT CFunctions::Add(unsigned int nKey, const UTF8 *name, mux_IFunction *pIFun, int maxArgsParsed, int minArgs, int maxArgs, int flags, int perms)
{
    if (  nullptr == name
       || nullptr == pIFun)
    {
        return MUX_E_INVALIDARG;
    }

    FunctionsNode *pfn = nullptr;
    try
    {
        pfn = new FunctionsNode;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pfn)
    {
        return MUX_E_OUTOFMEMORY;
    }

    pfn->fun.name = name;
    pfn->fun.fun = fun_Functions;
    pfn->fun.maxArgsParsed = maxArgsParsed;
    pfn->fun.minArgs = minArgs;
    pfn->fun.maxArgs = maxArgs;
    pfn->fun.flags = flags;
    pfn->fun.perms = perms;
    pfn->fun.vp = static_cast<void *>(pfn);

    pfn->pIFun = pIFun;
    pfn->pIFun->AddRef();

    pfn->nKey = nKey;

    // Add ourselves to the list.
    //
    pfn->next = m_pHead;
    m_pHead = pfn;

    function_add(&pfn->fun);

    return MUX_S_OK;
}

// Factory for CFunctions component which is not directly accessible.
//
CFunctionsFactory::CFunctionsFactory(void) : m_cRef(1)
{
}

CFunctionsFactory::~CFunctionsFactory()
{
}

MUX_RESULT CFunctionsFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else if (mux_IID_IClassFactory == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CFunctionsFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CFunctionsFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CFunctionsFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (nullptr != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CFunctions *pLog = nullptr;
    try
    {
        pLog = new CFunctions;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pLog)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pLog->QueryInterface(iid, ppv);
    pLog->Release();
    return mr;
}

MUX_RESULT CFunctionsFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}
