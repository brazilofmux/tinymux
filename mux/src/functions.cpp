/*! \file functions.cpp
 * \brief MUX function handlers
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "attrs.h"
#include "command.h"
#include "functions.h"
#include "funmath.h"
#include "interface.h"
#include "misc.h"
#include "mathutil.h"
#include "pcre.h"
#ifdef REALITY_LVLS
#include "levels.h"
#endif // REALITY_LVLS

#if defined(INLINESQL)
#include <mysql.h>

extern MYSQL *mush_database;
#endif // INLINESQL

UFUN *ufun_head;

SEP sepSpace = { 1, " " };

// Trim off leading and trailing spaces if the separator char is a
// space -- known length version.
//
UTF8 *trim_space_sep_LEN(UTF8 *str, size_t nStr, SEP *sep, size_t *nTrim)
{
    if (  sep->n != 1
       || sep->str[0] != ' ')
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
UTF8 *trim_space_sep(__in UTF8 *str, __in const SEP &sep)
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
UTF8 *next_token(__deref_inout UTF8 *str, const SEP &sep)
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
        UTF8 *p = (UTF8 *)strstr((char *)str, (char *)sep.str);
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
UTF8 *split_token(__deref_inout UTF8 **sp, const SEP &sep)
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
        UTF8 *p = (UTF8 *)strstr((char *)str, (char *)sep.str);
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
        UTF8 *p = ptrs[i];
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
        m_CouldBe = ASCII_LIST;
    }
}

int AutoDetect::GetType(void)
{
    return m_CouldBe;
}

int list2arr(__out_ecount(maxlen) UTF8 *arr[], int maxlen, __in UTF8 *list, __in const SEP &sep)
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

void arr2list(__in_ecount(alen) UTF8 *arr[], int alen, __inout UTF8 *list, __deref_inout UTF8 **bufc, __in const SEP &sep)
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
    __in UTF8 *buff, __deref_inout UTF8 **bufc,
    dbref executor, dbref caller, dbref enactor,
    int   eval,
    __in UTF8 *fargs[], int nfargs,
    __in const UTF8 *cargs[], int ncargs,
    int sep_arg, SEP *sep, int dflags
)
{
    bool bSuccess = true;
    if (sep_arg <= nfargs)
    {
        // First, we decide whether to evalute fargs[sep_arg-1] or not.
        //
        UTF8 *tstr = fargs[sep_arg-1];
        size_t tlen = strlen((char *)tstr);

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

int countwords(__in UTF8 *str, __in const SEP &sep)
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
               && (unsigned int)(higher-lower) <= INT32_MAX_VALUE)
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
    UTF8 *temp = ltaNow.ReturnDateString(nPrecision);
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
        UTF8 *temp = lta.ReturnDateString(nPrecision);
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
    while ((q = (UTF8 *)strchr((char *)p, '$')) != nullptr)
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
    while ((q = (UTF8 *)strchr((char *)p, '$')) != nullptr)
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

        // Handle modifiers
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

LBUF_OFFSET linewrap_general(const UTF8 *pStr,     LBUF_OFFSET nWidth,
                                   UTF8 *pBuffer,  size_t      nBuffer,
                             const UTF8 *pLeft,    LBUF_OFFSET nLeft,
                             const UTF8 *pRight,   LBUF_OFFSET nRight,
                                   int   iJustKey, LBUF_OFFSET nHanging,
                             const UTF8 *pOSep,    mux_cursor  curOSep,
                             LBUF_OFFSET nWidth0)
{
    mux_string *sStr = nullptr;
    try
    {
        sStr = new mux_string(pStr);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sStr)
    {
        return 0;
    }

    mux_cursor nStr = sStr->length_cursor();
    bool bFirst = true;
    mux_field fldLine, fldTemp, fldPad;
    mux_cursor curStr, curEnd, curTab, iPos, curNext;
    LBUF_OFFSET nLineWidth = (0 < nWidth0 ? nWidth0 : nWidth);

    while (curStr < nStr)
    {
        if (bFirst)
        {
            bFirst = false;
        }
        else if (nBuffer < static_cast<size_t>(fldLine.m_byte + curOSep.m_byte))
        {
            break;
        }
        else
        {
            mux_strncpy( pBuffer + fldLine.m_byte, pOSep,
                         nBuffer - fldLine.m_byte);
            fldLine( fldLine.m_byte + curOSep.m_byte,
                     fldLine.m_column + curOSep.m_point);
            if (0 < nHanging)
            {
                fldLine = PadField( pBuffer, nBuffer,
                                    fldLine.m_column + nHanging, fldLine);
            }
            nLineWidth = nWidth;
        }
        fldLine += StripTabsAndTruncate( pLeft, pBuffer + fldLine.m_byte,
                                         nBuffer - fldLine.m_byte, nLeft);

        if (!sStr->search(T("\r"), &iPos, curStr))
        {
            iPos = nStr;
        }

        sStr->cursor_from_point(curEnd, curStr.m_point + nLineWidth);
        if (iPos < curEnd)
        {
            curEnd = iPos;
            curNext = curEnd + curNewline;
        }
        else
        {
            curNext = curEnd;
        }

        while (sStr->search(T("\t"), &curTab, curStr, curEnd))
        {
            mux_string *sSpaces = nullptr;
            try
            {
                sSpaces = new mux_string(T("        "));
            }
            catch (...)
            {
                ; // Nothing.
            }

            if (nullptr == sSpaces)
            {
                ISOUTOFMEMORY(sSpaces);
                return 0;
            }

            LBUF_OFFSET nSpaces = 8 - ((curTab.m_point - curStr.m_point) % 8);
            mux_cursor curSpaces(nSpaces, nSpaces);
            sSpaces->truncate(curSpaces);
            sStr->replace_Chars(*sSpaces, curTab, curAscii);
            delete sSpaces;
            nStr = sStr->length_cursor();
            curNext = curTab + curSpaces;

            // We have to recalculate the end of the line and whether the
            // newline is within it now.
            //
            if (!sStr->search(T("\r"), &iPos, curStr))
            {
                iPos = nStr;
            }
            sStr->cursor_from_point(curEnd, curStr.m_point + nLineWidth);
            if (iPos < curEnd)
            {
                curEnd = iPos;
                curNext = curEnd + curNewline;
            }
            else
            {
                curNext = curEnd;
            }
            if (curNext < curEnd)
            {
                curNext = curEnd;
            }
        }

        if (  curEnd == nStr
           || mux_isspace(sStr->export_Char(curEnd.m_byte)))
        {
            // We already know where the line ends. Now we trim off trailing
            // spaces so that right and center justifications come out right.
            //
            mux_cursor curSpace = curEnd;
            if (' ' == sStr->export_Char(curEnd.m_byte))
            {
                sStr->cursor_next(curNext);
            }
            while (  mux_isspace(sStr->export_Char(curSpace.m_byte))
                  && curStr < curSpace)
            {
                curEnd = curSpace;
                sStr->cursor_prev(curSpace);
            }
        }
        else
        {
            // We want to backtrack to the last space, so that we can do a nice
            // line break between words.
            //
            mux_cursor curSpace = curEnd;
            while (  !mux_isspace(sStr->export_Char(curSpace.m_byte))
                  && curStr < curSpace)
            {
                sStr->cursor_prev(curSpace);
            }
            if (curStr < curSpace)
            {
                curNext = curSpace;
                sStr->cursor_next(curNext);
                while (  mux_isspace(sStr->export_Char(curSpace.m_byte))
                      && curStr < curSpace)
                {
                    curEnd = curSpace;
                    sStr->cursor_prev(curSpace);
                }
            }
        }

        fldTemp(curEnd.m_byte - curStr.m_byte, curEnd.m_point - curStr.m_point);
        if (  fldTemp.m_column < nLineWidth
           && CJC_LJUST != iJustKey)
        {
            LBUF_OFFSET nPadWidth;
            if (CJC_CENTER == iJustKey)
            {
                nPadWidth = fldLine.m_column + (nLineWidth - fldTemp.m_column)/2;
            }
            else // if (CJC_RJUST == iJustKey)
            {
                nPadWidth = fldLine.m_column + nLineWidth - fldTemp.m_column;
            }
            fldPad = PadField(pBuffer, nBuffer, nPadWidth, fldLine);
        }
        else
        {
            fldPad = fldLine;
        }
        LBUF_OFFSET nBytes = sStr->export_TextColor( pBuffer + fldPad.m_byte,
                                                     curStr, curEnd,
                                                     nBuffer - fldPad.m_byte);
        fldTemp(nBytes, fldTemp.m_column);
        if (CJC_RJUST == iJustKey)
        {
            fldLine = fldPad + fldTemp;
        }
        else
        {
            fldLine = PadField( pBuffer, nBuffer, fldLine.m_column + nLineWidth,
                                fldPad + fldTemp);
        }
        curStr = curNext;

        fldLine += StripTabsAndTruncate( pRight, pBuffer + fldLine.m_byte,
                                         nBuffer - fldLine.m_byte, nRight);
    }
    delete sStr;
    return fldLine.m_byte;
}

#if defined(FIRANMUX)

/*
 * ---------------------------------------------------------------------------
 * * fun_format: format a string (linewrap) with str, field, left, right
 */

FUNCTION(fun_format)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int fieldsize = mux_atol(fargs[1]);
    if (  fieldsize < 1
       || 80 < fieldsize)
    {
        safe_str(T("#-1 ILLEGAL FIELDSIZE"), buff, bufc);
        return;
    }

    size_t n2, n3;
    strip_color(fargs[2], nullptr, &n2);
    strip_color(fargs[3], nullptr, &n3);
    if (79 < fieldsize + n2 + n3)
    {
        safe_str(T("#-1 COMBINED FIELD TOO LARGE"), buff, bufc);
        return;
    }

    *bufc += linewrap_general( fargs[0], static_cast<LBUF_OFFSET>(fieldsize),
                               *bufc,    LBUF_SIZE - (*bufc - buff) - 1,
                               fargs[2], static_cast<LBUF_OFFSET>(n2),
                               fargs[3], static_cast<LBUF_OFFSET>(n3));
}

/*
 * ---------------------------------------------------------------------------
 * * text: return data from a file in game/text..
 */

FUNCTION(fun_text)
{
    FILE *textconf;
    if (!mux_fopen(&textconf, T("textfiles.conf"), T("r")))
    {
        // Can't open the file.
        //
        safe_str(T("#-1 TEXTFILES.CONF MISSING"), buff, bufc);
        return;
    }

    UTF8 mybuffer[80];
    while (fgets((char *)mybuffer, 80, textconf))
    {
        int index = 0;
        while (mybuffer[index])
        {
            if (mybuffer[index] == '\n')
            {
                mybuffer[index] = 0;
            }
            else
            {
                index++;
            }
        }

        /* Found the file listed, did I? */
        if (!strcmp((char *)mybuffer, (char *)fargs[0]))
        {
            FILE *myfile;
            if (!mux_fopen(&myfile, fargs[0], T("r")))
            {
                /* But not here!? */
                fclose(textconf);
                safe_str(T("#-1 FILE DOES NOT EXIST"),buff,bufc);
                return;
            }

            while (fgets((char *)mybuffer, 80, myfile))
            {
                index = 0;
                while (mybuffer[index])
                {
                    if (mybuffer[index] == '\n')
                    {
                        mybuffer[index] = 0;
                    }
                    else
                    {
                        index++;
                    }
                }

                if ('&' == mybuffer[0])
                {
                    if (!mux_stricmp(fargs[1]+strspn((char *)fargs[1], " "), mybuffer+2))
                    {
                        /* At this point I've found the file and the entry */
                        int thischar;
                        int lastchar = '\0';
                        while ((thischar = fgetc(myfile)) != EOF)
                        {
                            if ('&' == thischar)
                            {
                                if ('\n' == lastchar)
                                {
                                    fclose(textconf);
                                    fclose(myfile);
                                    return;
                                }
                            }
                            safe_chr(thischar, buff, bufc);
                            lastchar = thischar;
                        }
                        fclose(textconf);
                        fclose(myfile);
                        return;
                    }
                }
            }
            fclose(textconf);
            fclose(myfile);
            safe_str(T("#-1 ENTRY NOT FOUND"), buff, bufc);
            return;
        }
    }
    fclose(textconf);
    safe_str(T("#-1 FILE NOT LISTED"),buff,bufc);
}

#endif // FIRANMUX


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
        mux_exec(atr_gotten, nLen, buff, bufc, thing, executor, executor,
            AttrTrace(aflags, EV_FIGNORE|EV_EVAL), nullptr, 0);
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
    mux_exec(atext, LBUF_SIZE-1, buff, bufc, thing, executor, enactor,
        AttrTrace(aflags, EV_FCHECK|EV_EVAL),
        (const UTF8 **)&(fargs[1]), nfargs - 1);
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
    mux_string *sStr = new mux_string(fargs[0]);

    mux_cursor iCurStart, iCurEnd;
    sStr->cursor_from_point(iCurStart, (LBUF_OFFSET)iStart);
    sStr->cursor_from_point(iCurEnd, (LBUF_OFFSET)(iStart + nMid));

    if (iCurStart < iCurEnd)
    {
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        *bufc += sStr->export_TextColor(*bufc, iCurStart, iCurEnd, nMax);
    }

    delete sStr;
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

    mux_string *sStr = new mux_string(fargs[0]);
    mux_cursor iStart, iEnd;
    sStr->cursor_end(iEnd);

    if (nRight < iEnd.m_point)
    {
        sStr->cursor_from_point(iStart, (LBUF_OFFSET)(iEnd.m_point - nRight));
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        *bufc += sStr->export_TextColor(*bufc, iStart, iEnd, nMax);
    }
    else
    {
        safe_str(fargs[0], buff, bufc);
    }

    delete sStr;
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
        if ('\0' != *utf8_NextCodePoint(tbuf))
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

    mux_string *sStr = nullptr;
    mux_words *words = nullptr;
    try
    {
        sStr = new mux_string(trim_space_sep(fargs[0], sep));
        words = new mux_words(*sStr);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (  nullptr == sStr
       || nullptr == words)
    {
        delete sStr;
        delete words;
        return;
    }

    LBUF_OFFSET nWords = words->find_Words(sep.str);

    iFirstWord--;
    if (iFirstWord < nWords)
    {
        if (nWords < iFirstWord + nWordsToCopy)
        {
            nWordsToCopy = nWords - iFirstWord;
        }

        bool bFirst = true;

        for (LBUF_OFFSET i = static_cast<LBUF_OFFSET>(iFirstWord);
             i < iFirstWord + nWordsToCopy;
             i++)
        {
            if (!bFirst)
            {
                print_sep(osep, buff, bufc);
            }
            else
            {
                bFirst = false;
            }
            words->export_WordColor(i, buff, bufc);
        }
    }

    delete sStr;
    delete words;
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
        if ((s = (UTF8 *)strchr((char *)s, c)) != nullptr)
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
        if ((p = (UTF8 *)strchr((char *)p, c)) != nullptr)
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
        strip_color(fargs[0], 0, &n);
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
        n = strlen((char *)fargs[0]);
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
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int x;

    x = strcmp((char *)fargs[0], (char *)fargs[1]);
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
    { (UTF8 *)nullptr, lconAny }
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
#ifdef WOD_REALMS
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
#ifdef WOD_REALMS
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

    mux_string *sPat = nullptr;
    mux_string *sStr = nullptr;
    try
    {
        sPat = new mux_string(fargs[0]);
        sStr = new mux_string(fargs[1]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    mux_cursor nPat;
    if (  nullptr != sStr
       && nullptr != sPat
       && sStr->search(*sPat, &nPat))
    {
        safe_ltoa(static_cast<long>(nPat.m_point + 1), buff, bufc);
    }
    else
    {
        safe_nothing(buff, bufc);
    }

    delete sStr;
    delete sPat;
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

    mux_string *sStr = nullptr;
    try
    {
        sStr = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sStr)
    {
        return;
    }

    if (0 == sStr->length_byte())
    {
        delete sStr;
        return;
    }

    mux_string *sPat = nullptr;
    try
    {
        sPat = new mux_string(fargs[1]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sPat)
    {
        delete sStr;
        delete sPat;
        return;
    }

    if (0 == sPat->length_byte())
    {
        sPat->import(T(" "), 1);
    }

    mux_cursor nPat, nStart = CursorMin;
    bool bSucceeded = sStr->search(*sPat, &nPat);
    while (bSucceeded)
    {
        if (CursorMin < nStart)
        {
            safe_chr(' ', buff, bufc);
        }
        nStart = nPat;
        safe_ltoa(static_cast<long>(nStart.m_point), buff, bufc);
        sStr->cursor_next(nStart);

        bSucceeded = sStr->search(*sPat, &nPat, nStart);
    }

    delete sStr;
    delete sPat;
}

static int DCL_CDECL i_comp(const void *s1, const void *s2)
{
    if (*((int *)s1) > *((int *)s2))
    {
        return 1;
    }
    else if (*((int *)s1) < *((int *)s2))
    {
        return -1;
    }
    return 0;
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

static void do_itemfuns(__in UTF8 *buff, __deref_inout UTF8 **bufc, mux_string *sList,
   int nPositions, int aPositions[], mux_string *sWord, __in const SEP &sep,
   const SEP &osep, int flag)
{
    int j;
    if (nullptr == sList)
    {
        // Return an empty string if passed a null string.
        //
        return;
    }
    else if (0 == sList->length_byte())
    {
        if (IF_INSERT != flag)
        {
            // Return an empty string if performing a non-insert on an empty string.
            //
            return;
        }
        else
        {
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

    // Parse list into words
    //
    mux_words *words = nullptr;
    try
    {
        words = new mux_words(*sList);
    }
    catch (...)
    {
        ; // Nothing.
    }
    if (nullptr == words)
    {
        ISOUTOFMEMORY(words);
        return;
    }

    LBUF_OFFSET nWords = words->find_Words(sep.str, true);

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
                aPositions[j] += nWords + 1;
            }
            else
            {
                aPositions[j] += nWords;
            }
        }
        else
        {
            aPositions[j] -= 1;
        }

        if (  aPositions[j] < 0
           || (  nWords <= aPositions[j]
              && (  flag != IF_INSERT
                 || nWords < aPositions[j])))
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
    qsort(aPositions, nPositions, sizeof(int), i_comp);

    bool fFirst = true;
    LBUF_OFFSET i = 0;

    for (j = 0; j < nPositions; j++)
    {
        while (i < static_cast<LBUF_OFFSET>(aPositions[j]))
        {
            if (!fFirst)
            {
                print_sep(osep, buff, bufc);
            }
            else
            {
                fFirst = false;
            }
            words->export_WordColor(i++, buff, bufc);
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

            if (sWord)
            {
                size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
                *bufc += sWord->export_TextColor(*bufc, CursorMin, CursorMax, nMax);
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
        words->export_WordColor(i++, buff, bufc);
    }

    delete words;
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

    mux_string *sList = new mux_string(fargs[0]);
    int ai[MAX_WORDS];
    int nai = DecodeListOfIntegers(fargs[1], ai);

    // Delete a word at position X of a list.
    //
    do_itemfuns(buff, bufc, sList, nai, ai, nullptr, sep, osep, IF_DELETE);

    delete sList;
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

    mux_string *sList = nullptr;
    mux_string *sWord = nullptr;
    try
    {
        sList = new mux_string(fargs[0]);
        sWord = new mux_string(fargs[2]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    // Replace a word at position X of a list.
    //
    if (  nullptr != sList
       && nullptr != sWord)
    {
        int ai[MAX_WORDS];
        int nai = DecodeListOfIntegers(fargs[1], ai);
        do_itemfuns(buff, bufc, sList, nai, ai, sWord, sep, osep, IF_REPLACE);
    }

    delete sList;
    delete sWord;
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

    mux_string *sList = nullptr;
    try
    {
        sList = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sList)
    {
        return;
    }

    mux_string *sWord = nullptr;
    try
    {
        sWord = new mux_string(fargs[2]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sWord)
    {
        delete sList;
        return;
    }

    int ai[MAX_WORDS];
    int nai = DecodeListOfIntegers(fargs[1], ai);

    // Insert a word at position X of a list.
    //
    do_itemfuns(buff, bufc, sList, nai, ai, sWord, sep, osep, IF_INSERT);

    delete sList;
    delete sWord;
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

    mux_string *sWord = nullptr;
    try
    {
        sWord = new mux_string(fargs[1]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sWord)
    {
        return;
    }

    if (sWord->search(sep.str))
    {
        safe_str(T("#-1 CAN ONLY REMOVE ONE ELEMENT"), buff, bufc);
        delete sWord;
        return;
    }

    mux_string *sStr = nullptr;
    try
    {
        sStr = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sStr)
    {
        delete sWord;
        return;
    }

    mux_words *words = nullptr;
    try
    {
        words = new mux_words(*sStr);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == words)
    {
        delete sWord;
        delete sStr;
        ISOUTOFMEMORY(words);
        return;
    }

    LBUF_OFFSET nWords = words->find_Words(sep.str);
    mux_cursor iPos = CursorMin, iStart = CursorMin, iEnd = CursorMin;
    bool bSucceeded = sStr->search(*sWord, &iPos);

    // Walk through the string copying words until (if ever) we get to
    // one that matches the target word.
    //
    bool bFirst = true, bFound = false;
    for (LBUF_OFFSET i = 0; i < nWords; i++)
    {
        iStart = words->wordBegin(i);
        iEnd = words->wordEnd(i);

        if (  !bFound
           && bSucceeded
           && iPos < iStart)
        {
            bSucceeded = sStr->search(*sWord, &iPos, iStart);
        }

        if (  !bFound
           && sWord->length_cursor() == iEnd - iStart
           && (  (  bSucceeded
                 && iPos == iStart)
              || 0 == sWord->length_byte()))
        {
            bFound = true;
        }
        else
        {
            if (bFirst)
            {
                bFirst = false;
            }
            else
            {
                print_sep(osep, buff, bufc);
            }
            words->export_WordColor(i, buff, bufc);
        }
    }

    delete sWord;
    delete sStr;
    delete words;
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
        if (!strcmp((char *)fargs[1], (char *)r))
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

    mux_string *sStr = nullptr;
    try
    {
        sStr = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sStr)
    {
        return;
    }
    mux_cursor nLen = sStr->length_cursor();

    mux_cursor i = CursorMin;
    if (i < nLen)
    {
        mux_string *sTo = nullptr;
        try
        {
            sTo = new mux_string(T(" "));
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == sTo)
        {
            return;
        }

        do
        {
            UTF8 ch = sStr->export_Char(i.m_byte);
            if (mux_issecure(ch))
            {
                mux_cursor nReplace(utf8_FirstByte[ch], 1);
                sStr->replace_Chars(*sTo, i, nReplace);
            }
        } while (sStr->cursor_next(i));
        delete sTo;
    }

    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    *bufc += sStr->export_TextColor(*bufc, CursorMin, nLen, nMax);

    delete sStr;
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

    mux_string *sStr = nullptr;
    try
    {
        sStr = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sStr)
    {
        return;
    }

    mux_string *sOut = nullptr;
    try
    {
        sOut = new mux_string;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sOut)
    {
        delete sStr;
        return;
    }
    mux_cursor curStr;
    mux_cursor curOut;

    bool bBackslash = false;
    sOut->cursor_start(curOut);
    if (sStr->cursor_start(curStr))
    {
        for (;;)
        {
            if (  sStr->IsEscape(curStr)
               || !bBackslash)
            {
                sOut->append(T("\\"));
                sOut->set_Color(curOut.m_point, sStr->export_Color(curStr.m_point));
                sOut->cursor_next(curOut);
                bBackslash = true;
            }

            mux_cursor next = curStr;
            if (sStr->cursor_next(next))
            {
                sOut->append(*sStr, curStr, next);
                sOut->cursor_next(curOut);
                curStr = next;
            }
            else
            {
                break;
            }
        }
    }

    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    *bufc += sOut->export_TextColor(*bufc, CursorMin, CursorMax, nMax);

    delete sOut;
    delete sStr;
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
        cp = trim_space_sep_LEN(cp, ncp, &sep, &ncp_trimmed);
        UTF8 *xp = split_token(&cp, sep);

        int i;
        for (i = 1; xp; i++)
        {
            if (tp < xp + strlen((char *)xp))
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
    { T("no_name"),    AF_NONAME  },
    { T("no_parse"),   AF_NOPARSE },
    { T("regexp"),     AF_REGEXP  },
    { T("god"),        AF_GOD     },
    { T("visual"),     AF_VISUAL  },
    { T("no_inherit"), AF_PRIVATE },
    { T("const"),      AF_CONST   },
    { (UTF8 *)nullptr,      0     }
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
                strcat((char *)levelbuff, (char *)mudconf.reality_level[i].name);
                strcat((char *)levelbuff, " ");
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
                strcat((char *)levelbuff, (char *)mudconf.reality_level[i].name);
                strcat((char *)levelbuff, " ");
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
    mux_string *sStr = new mux_string(fargs[0]);

    mux_cursor iStartCur, iEnd;
    sStr->cursor_from_point(iStartCur, static_cast<LBUF_OFFSET>(iStart));
    sStr->cursor_from_point(iEnd, static_cast<LBUF_OFFSET>(iStartCur.m_point + nDelete));
    sStr->delete_Chars(iStartCur, iEnd);
    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    *bufc += sStr->export_TextColor(*bufc, CursorMin, CursorMax, nMax);

    delete sStr;
}

static FUNCTION(fun_lock)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
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

    process_sex(executor, fargs[0], (UTF8 *)"%o", buff, bufc);
}

static FUNCTION(fun_poss)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    process_sex(executor, fargs[0], (UTF8 *)"%p", buff, bufc);
}

static FUNCTION(fun_subj)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    process_sex(executor, fargs[0], (UTF8 *)"%s", buff, bufc);
}

static FUNCTION(fun_aposs)
{
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    process_sex(executor, fargs[0], (UTF8 *)"%a", buff, bufc);
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

    mux_string *sStr = new mux_string(fargs[0]);
    sStr->LowerCase();
    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    *bufc += sStr->export_TextColor(*bufc, CursorMin, CursorMax, nMax);

    delete sStr;
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

    mux_string *sStr = new mux_string(fargs[0]);
    sStr->UpperCase();
    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    *bufc += sStr->export_TextColor(*bufc, CursorMin, CursorMax, nMax);

    delete sStr;
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

    mux_string *sStr = new mux_string(fargs[0]);
    sStr->UpperCaseFirst();
    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    *bufc += sStr->export_TextColor(*bufc, CursorMin, CursorMax, nMax);

    delete sStr;
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

/*
 * ---------------------------------------------------------------------------
 * * fun_reverse, fun_revwords: Reverse things.
 */
#if 0
typedef void MEMXFORM(UTF8 *dest, UTF8 *src, size_t n);
static void ANSI_TransformTextReverseWithFunction
(
    UTF8 *buff,
    UTF8 **bufc,
    UTF8 *pString,
    MEMXFORM *pfMemXForm
)
{
    // Bounds checking.
    //
    size_t nString = strlen((char *)pString);
    UTF8 *pBuffer = *bufc;
    size_t nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    if (nString > nBufferAvailable)
    {
        nString = nBufferAvailable;
        pString[nString] = '\0';
    }

    // How it's done: We have a source string (pString, nString) and a
    // destination buffer (pBuffer, nString) of equal size.
    //
    // We recognize (ANSI,TEXT) phrases and place them at the end of
    // destination buffer working our way to the front. The ANSI part
    // is left inviolate, but the text part is reversed.
    //
    // In this way, (ANSI1,TEXT1)(ANSI2,TEXT2) is traslated into
    // (ANSI2,reverse(TEST2))(ANSI1,reverse(TEXT1)).
    //
    // TODO: Do not reverse the CRLF in the text part either.
    //
    size_t  nANSI = 0;
    UTF8 *pANSI = pString;
    pBuffer += nString;
    *bufc = pBuffer;
    **bufc = '\0';
    while (nString)
    {
        size_t nTokenLength0;
        size_t nTokenLength1;
        int iType = ANSI_lex(nString, pString, &nTokenLength0, &nTokenLength1);

        if (iType == TOKEN_TEXT_ANSI)
        {
            // (ANSI,TEXT) is given by (nANSI, nTokenLength0)
            //
            pBuffer -= nANSI + nTokenLength0;
            memcpy(pBuffer, pANSI, nANSI);
            pfMemXForm(pBuffer+nANSI, pString, nTokenLength0);

            // Adjust pointers and counts.
            //
            nString -= nTokenLength0;
            pString += nTokenLength0;
            pANSI = pString;
            nANSI = 0;

            nTokenLength0 = nTokenLength1;
        }
        // TOKEN_ANSI
        //
        nString -= nTokenLength0;
        pString += nTokenLength0;
        nANSI   += nTokenLength0;
    }

    // Copy the last ANSI part (if present). It will be overridden by
    // ANSI further on, but we need to fill up the space. Code
    // elsewhere will compact it before it's sent to the client.
    //
    pBuffer -= nANSI;
    memcpy(pBuffer, pANSI, nANSI);
}

static UTF8 ReverseWordsInText_Seperator;

static void ReverseWordsInText(UTF8 *dest, UTF8 *src, size_t n)
{
    UTF8 chSave = src[n];
    src[n] = '\0';
    dest += n;
    while (n)
    {
        UTF8 *pWord = (UTF8 *)strchr((char *)src, ReverseWordsInText_Seperator);
        size_t nLen;
        if (pWord)
        {
            nLen = (pWord - src);
        }
        else
        {
            nLen = n;
        }
        dest -= nLen;
        memcpy(dest, src, nLen);
        src += nLen;
        n -= nLen;
        if (pWord)
        {
            dest--;
            *dest = ReverseWordsInText_Seperator;
            src++;
            n--;
        }
    }
    src[n] = chSave;
}
#endif

static FUNCTION(fun_reverse)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    mux_string *sStr = new mux_string(fargs[0]);

    sStr->reverse();
    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    *bufc += sStr->export_TextColor(*bufc, CursorMin, CursorMax, nMax);

    delete sStr;
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

    mux_string *sStr = nullptr;
    mux_words *words = nullptr;
    try
    {
        sStr = new mux_string(fargs[0]);
        words = new mux_words(*sStr);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (  nullptr == sStr
       || nullptr == words)
    {
        delete sStr;
        delete words;
        return;
    }

    LBUF_OFFSET nWords = words->find_Words(sep.str);

    bool bFirst = true;
    for (LBUF_OFFSET i = 0; i < nWords; i++)
    {
        if (!bFirst)
        {
            print_sep(osep, buff, bufc);
        }
        else
        {
            bFirst = false;
        }
        words->export_WordColor(nWords-i-1, buff, bufc);
    }

    delete sStr;
    delete words;
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

    mux_string *sPat = nullptr;
    try
    {
        sPat = new mux_string;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sPat)
    {
        return;
    }

    // Sanity-check arg1 and arg2.
    //
    UTF8 *bp = fargs[0];
    if (nfargs > 1)
    {
        sPat->import(fargs[1]);
    }
    else
    {
        sPat->import(T(" "), 1);
    }
    mux_cursor nPat = sPat->length_cursor();

    if (  1 == nPat.m_byte
       && ' ' == sPat->export_Char(0))
    {
        bp = trim_space_sep(bp, sepSpace);
    }

    // Look for the target string.
    //
    mux_string *sStr = nullptr;
    try
    {
        sStr = new mux_string(bp);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sStr)
    {
        delete sPat;
        return;
    }
    mux_cursor i;

    bool bSucceeded = sStr->search(*sPat, &i);
    if (bSucceeded)
    {
        // Yup, return what follows.
        //
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        *bufc += sStr->export_TextColor(*bufc, i + nPat, CursorMax, nMax);
    }

    delete sStr;
    delete sPat;
}

static FUNCTION(fun_before)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    mux_string *sPat = nullptr;
    try
    {
        sPat = new mux_string;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sPat)
    {
        return;
    }
    size_t nPat;

    // Sanity-check arg1 and arg2.
    //
    UTF8 *bp = fargs[0];
    if (1 < nfargs)
    {
        sPat->import(fargs[1]);
        nPat = sPat->length_byte();
    }
    else
    {
        sPat->import(T(" "), 1);
        nPat = 1;
    }

    if (  1 == nPat
       && ' ' == sPat->export_Char(0))
    {
        bp = trim_space_sep(bp, sepSpace);
    }

    // Look for the target string.
    //
    mux_string *sStr = nullptr;
    try
    {
        sStr = new mux_string(bp);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sStr)
    {
        delete sPat;
        return;
    }
    mux_cursor i;

    bool bSucceeded = sStr->search(*sPat, &i);
    if (bSucceeded)
    {
        // Yup, return what follows.
        //
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        *bufc += sStr->export_TextColor(*bufc, CursorMin, i, nMax);
    }
    else
    {
        // Ran off the end without finding it.
        //
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        *bufc += sStr->export_TextColor(*bufc, CursorMin, CursorMax, nMax);
    }

    delete sStr;
    delete sPat;
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

    mux_string *sStrA = nullptr;
    mux_string *sStrB = nullptr;
    mux_string *sStrC = nullptr;
    try
    {
        sStrA = new mux_string(fargs[0]);
        sStrB = new mux_string(fargs[1]);
        sStrC = new mux_string(fargs[2]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (  nullptr != sStrA
       && nullptr != sStrB
       && nullptr != sStrC)
    {
        // Do length checks first.
        //
        mux_cursor nLenA = sStrA->length_cursor();
        mux_cursor nLenB = sStrB->length_cursor();
        if (nLenA.m_point != nLenB.m_point)
        {
            safe_str(T("#-1 STRING LENGTHS MUST BE EQUAL"), buff, bufc);
        }
        else
        {
            // Find the character to look for.  Null character is considered a
            // space.
            //
            if (0 == sStrC->length_point())
            {
                sStrC->import(T(" "));
            }

            mux_cursor iA, iB;
            if (  sStrA->cursor_start(iA)
               && sStrB->cursor_start(iB))
            {
                do
                {
                    if (sStrA->compare_Char(iA, *sStrC))
                    {
                        sStrA->replace_Char(iA, *sStrB, iB);
                        sStrA->set_Color(iA.m_point, sStrB->export_Color(iB.m_point));
                    }
                } while (  sStrA->cursor_next(iA)
                        && sStrB->cursor_next(iB));
            }

            size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
            *bufc += sStrA->export_TextColor(*bufc, CursorMin, CursorMax, nMax);
        }
    }
    delete sStrA;
    delete sStrB;
    delete sStrC;
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
    UTF8 *p1 = fargs[0];
    UTF8 *q1 = fargs[1];
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
        if (strcmp((char *)p2, (char *)fargs[2]) == 0)
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
        size_t len = strlen((char *)fargs[0]);
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
    UTF8 *cp = trim_space_sep_LEN(curr, dp-curr, &sep, &ncp);
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
        UTF8 *buff2 = replace_tokens(fargs[1], objstring, mux_ltoa_t(number),
            nullptr);
        mux_exec(buff2, LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        free_lbuf(buff2);
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
    UTF8 *cp = trim_space_sep_LEN(curr, dp-curr, &sep, &ncp);
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
        UTF8 *buff2 = replace_tokens(fargs[1], objstring, mux_ltoa_t(number),
            nullptr);
        dp = result = alloc_lbuf("fun_list.2");
        mux_exec(buff2, LBUF_SIZE-1, result, &dp, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        *dp = '\0';
        free_lbuf(buff2);
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

    PUTF8 *elems = nullptr;
    try
    {
        elems = new PUTF8[LBUF_SIZE/2];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == elems)
    {
        return;
    }

    PUTF8 *weights = nullptr;
    try
    {
        weights = new PUTF8[LBUF_SIZE/2];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == weights)
    {
        delete [] elems;
        return;
    }

    int n_elems   = list2arr(elems, LBUF_SIZE/2, fargs[0], isep);
    int n_weights = list2arr(weights, LBUF_SIZE/2, fargs[1], sepSpace);

    if (n_elems != n_weights)
    {
        safe_str(T("#-1 LISTS MUST BE OF EQUAL SIZE"), buff, bufc);
        delete [] elems;
        delete [] weights;
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
                delete [] elems;
                delete [] weights;
                return;
            }
            sum = sum_next;
            ip[i] = sum;
        }
    }

    INT32 num = RandomINT32(0, sum-1);

    for (i = 0; i < n_weights; i++)
    {
        if (  ip[i] != 0
           && num < ip[i])
        {
            safe_str(elems[i], buff, bufc);
            break;
        }
    }
    delete [] elems;
    delete [] weights;
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

    int *bin_array = nullptr;
    try
    {
        bin_array = new int[bins];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == bin_array)
    {
        safe_str(T("#-1 NOT ENOUGH MEMORY TO DISTRIBUTE"), buff, bufc);
    }
    else
    {
        // Initialize bins.
        //
        int current_bin;
        for (current_bin = 0; current_bin < bins; current_bin++)
        {
            bin_array[current_bin] = 0;
        }

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
        for (current_bin = 0; current_bin < bins; current_bin++)
        {
            if (!first)
            {
                print_sep(osep, buff, bufc);
            }
            first = false;
            safe_ltoa(bin_array[current_bin], buff, bufc);
        }
        delete [] bin_array;
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

    if (mysql_real_query(mush_database, (char *)cp, strlen((char *)cp)))
    {
        free_lbuf(curr);
        safe_str(T("#-1 QUERY ERROR"), buff, bufc);
        return;
    }

    MYSQL_RES *result = mysql_store_result(mush_database);
    if (!result)
    {
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
            safe_str((UTF8 *)row[loop], buff, bufc);
        }
        row = mysql_fetch_row(result);
        if (row)
        {
            print_sep(sepRow, buff, bufc);
        }
    }

    free_lbuf(curr);
    mysql_free_result(result);
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

static void filter_handler(__inout UTF8 *buff, __deref_inout UTF8 **bufc, dbref executor, dbref enactor,
                    __in_ecount(nfargs) UTF8 *fargs[], int nfargs, __in const SEP &sep, __in const SEP &osep, bool bBool)
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

    mux_string *sStr  = nullptr;
    mux_string *sFrom = nullptr;
    mux_string *sTo   = nullptr;

    try
    {
        sStr  = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr != sStr)
    {
        for (int i = 1; i + 1 < nfargs; i += 2)
        {
            try
            {
                sFrom = new mux_string(fargs[i]);
                sTo = new mux_string(fargs[i + 1]);
            }
            catch (...)
            {
                ; // Nothing.
            }
            if (nullptr != sFrom && nullptr != sTo)
            {
                sStr->edit(*sFrom, *sTo);
            }
            delete sFrom;
            delete sTo;
        }

        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        *bufc += sStr->export_TextColor(*bufc, CursorMin, CursorMax, nMax);
    }

    delete sStr;
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

    bool check_locks, verbose, multiple;
    dbref thing, what;
    UTF8 *cp;

    int pref_type = NOTYPE;
    check_locks = verbose = multiple = false;

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

        if (bSwitch ? wild_match(tbuff, mbuff) : strcmp((char *)tbuff, (char *)mbuff) == 0)
        {
            free_lbuf(tbuff);
            tbuff = replace_tokens(fargs[i+1], nullptr, nullptr, mbuff);
            free_lbuf(mbuff);
            mux_exec(tbuff, LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
                eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
            free_lbuf(tbuff);
            return;
        }
    }
    free_lbuf(tbuff);

    // Nope, return the default if there is one.
    //
    if (  i < nfargs
       && fargs[i])
    {
        tbuff = replace_tokens(fargs[i], nullptr, nullptr, mbuff);
        mux_exec(tbuff, LBUF_SIZE-1, buff, bufc, executor, caller, enactor,
            eval|EV_STRIP_CURLY|EV_FCHECK|EV_EVAL, cargs, ncargs);
        free_lbuf(tbuff);
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
        DESC *d;
        DESC_ITER_CONN(d)
        {
            if (d->socket == s)
            {
                nHeight = d->height;
                break;
            }
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
        DESC *d;
        DESC_ITER_CONN(d)
        {
            if (d->socket == s)
            {
                nWidth = d->width;
                break;
            }
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
        DESC *d;
        DESC_ITER_CONN(d)
        {
            if (d->socket == s)
            {
                target = d->player;
                break;
            }
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
        bool bFound = false;
        DESC *d;
        CLinearTimeAbsolute ltaNow;
        ltaNow.GetUTC();
        DESC_ITER_CONN(d)
        {
            if (d->socket == s)
            {
                bFound = true;
                break;
            }
        }
        if (  bFound
           && (  d->player == executor
              || Wizard_Who(executor)))
        {
            CLinearTimeDelta ltdResult = ltaNow - d->last_time;
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
        bool bFound = false;
        DESC *d;
        CLinearTimeAbsolute ltaNow;
        ltaNow.GetUTC();
        DESC_ITER_CONN(d)
        {
            if (d->socket == s)
            {
                bFound = true;
                break;
            }
        }
        if (  bFound
           && (  d->player == executor
              || Wizard_Who(executor)))
        {
            CLinearTimeDelta ltdResult = ltaNow - d->connected_at;
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
        CLinearTimeAbsolute ltaNow;
        ltaNow.GetUTC();
        DESC_ITER_CONN(d)
        {
            if (d->socket == s)
            {
                break;
            }
        }

        if (  nullptr != d
           && (  d->player != executor
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

        DESC_ITER_CONN(d)
        {
            if (d->player == target)
            {
                break;
            }
        }
    }

    if (nullptr == d)
    {
        safe_notconnected(buff, bufc);
        return;
    }

    if (d->ttype)
    {
        safe_str(d->ttype, buff, bufc);
        safe_str(T(" telnet"), buff, bufc);
    }
    else
    {
        safe_str(T("unknown"), buff, bufc);
        if (  d->nvt_him_state[TELNET_NAWS]
           || d->nvt_him_state[TELNET_SGA]
           || d->nvt_him_state[TELNET_EOR])
        {
            safe_str(T(" telnet"), buff, bufc);
        }
    }

    if (Html(d->player))
    {
        safe_str(T(" pueblo"), buff, bufc);
    }

    if (CHARSET_UTF8 == d->encoding)
    {
        safe_str(T(" unicode"), buff, bufc);
    }

#ifdef UNIX_SSL
    if (d->ssl_session)
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
        INT64  i64;
    } u;
    UTF8 *str;
} q_rec;

typedef int DCL_CDECL CompareFunction(const void *s1, const void *s2);

static int DCL_CDECL a_comp(const void *s1, const void *s2)
{
    return strcmp((char *)((q_rec *)s1)->str, (char *)((q_rec *)s2)->str);
}

static int DCL_CDECL a_casecomp(const void *s1, const void *s2)
{
    return mux_stricmp(((q_rec *)s1)->str, ((q_rec *)s2)->str);
}

static int DCL_CDECL f_comp(const void *s1, const void *s2)
{
    if (((q_rec *) s1)->u.d > ((q_rec *) s2)->u.d)
    {
        return 1;
    }
    else if (((q_rec *) s1)->u.d < ((q_rec *) s2)->u.d)
    {
        return -1;
    }
    return 0;
}

static int DCL_CDECL l_comp(const void *s1, const void *s2)
{
    if (((q_rec *) s1)->u.l > ((q_rec *) s2)->u.l)
    {
        return 1;
    }
    else if (((q_rec *) s1)->u.l < ((q_rec *) s2)->u.l)
    {
        return -1;
    }
    return 0;
}

static int DCL_CDECL i64_comp(const void *s1, const void *s2)
{
    if (((q_rec *) s1)->u.i64 > ((q_rec *) s2)->u.i64)
    {
        return 1;
    }
    else if (((q_rec *) s1)->u.i64 < ((q_rec *) s2)->u.i64)
    {
        return -1;
    }
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

    psc->m_ptrs = (q_rec *) MEMALLOC(n * sizeof(q_rec));
    if (nullptr != psc->m_ptrs)
    {
        switch (sort_type)
        {
        case ASCII_LIST:
            for (i = 0; i < n; i++)
            {
                psc->m_ptrs[i].str = s[i];
            }
            qsort(psc->m_ptrs, n, sizeof(q_rec), a_comp);
            break;

        case NUMERIC_LIST:
            for (i = 0; i < n; i++)
            {
                psc->m_ptrs[i].str = s[i];
                psc->m_ptrs[i].u.i64 = mux_atoi64(s[i]);
            }
            qsort(psc->m_ptrs, n, sizeof(q_rec), i64_comp);
            break;

        case DBREF_LIST:
            for (i = 0; i < n; i++)
            {
                psc->m_ptrs[i].str = s[i];
                psc->m_ptrs[i].u.l = dbnum(s[i]);
            }
            qsort(psc->m_ptrs, n, sizeof(q_rec), l_comp);
            break;

        case FLOAT_LIST:
            for (i = 0; i < n; i++)
            {
                psc->m_ptrs[i].str = s[i];
                psc->m_ptrs[i].u.d = mux_atof(s[i], false);
            }
            qsort(psc->m_ptrs, n, sizeof(q_rec), f_comp);
            break;

        case CI_ASCII_LIST:
            for (i = 0; i < n; i++)
            {
                psc->m_ptrs[i].str = s[i];
            }
            qsort(psc->m_ptrs, n, sizeof(q_rec), a_casecomp);
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

/* ---------------------------------------------------------------------------
 * fun_setunion, fun_setdiff, fun_setinter: Set management.
 */

#define SET_UNION     1
#define SET_INTERSECT 2
#define SET_DIFF      3

static void handle_sets
(
    int                  nfargs,
    __in UTF8           *fargs[],
    __in UTF8           *buff,
    __deref_inout UTF8 **bufc,
    int                  oper,
    __in const SEP      &sep,
    __in const SEP      &osep
)
{
    UTF8 **ptrs1 = nullptr;
    try
    {
        ptrs1 = new UTF8 *[LBUF_SIZE/2];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == ptrs1)
    {
        return;
    }

    UTF8 **ptrs2 = nullptr;
    try
    {
        ptrs2 = new UTF8 *[LBUF_SIZE/2];
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == ptrs2)
    {
        delete [] ptrs1;
        return;
    }

    int val;

    UTF8 *list1 = alloc_lbuf("fun_setunion.1");
    mux_strncpy(list1, fargs[0], LBUF_SIZE-1);
    int n1 = list2arr(ptrs1, LBUF_SIZE/2, list1, sep);

    UTF8 *list2 = alloc_lbuf("fun_setunion.2");
    mux_strncpy(list2, fargs[1], LBUF_SIZE-1);
    int n2 = list2arr(ptrs2, LBUF_SIZE/2, list2, sep);

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

        case '?':
        case '\0':
            {
                AutoDetect ad;
                ad.ExamineList(n1, ptrs1);
                ad.ExamineList(n2, ptrs2);
                sort_type = ad.GetType();
            }
            break;
        }
    }

    SortContext sc1;
    if (!do_asort_start(&sc1, n1, ptrs1, sort_type))
    {
        free_lbuf(list1);
        free_lbuf(list2);
        delete [] ptrs1;
        delete [] ptrs2;
        return;
    }

    SortContext sc2;
    if (!do_asort_start(&sc2, n2, ptrs2, sort_type))
    {
        do_asort_finish(&sc1);
        free_lbuf(list1);
        free_lbuf(list2);
        delete [] ptrs1;
        delete [] ptrs2;
        return;
    }

    CompareFunction *cf = nullptr;
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
           && cf(&sc1.m_ptrs[0], &sc2.m_ptrs[0]) == 0)
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
                      && cf(&sc1.m_ptrs[i1], oldp) == 0)
                {
                    i1++;
                }

                while (  i2 < n2
                      && oldp
                      && cf(&sc2.m_ptrs[i2], oldp) == 0)
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
                if (cf(&sc1.m_ptrs[i1], &sc2.m_ptrs[i2]) < 0)
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
               || cf(oldp, &sc1.m_ptrs[i1]) != 0)
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
               || cf(oldp, &sc2.m_ptrs[i2]) != 0)
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
            val = cf(&sc1.m_ptrs[i1], &sc2.m_ptrs[i2]);
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
                      && cf(&sc1.m_ptrs[i1], oldp) == 0)
                {
                    i1++;
                }
                while (  i2 < n2
                      && cf(&sc2.m_ptrs[i2], oldp) == 0)
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
            val = cf(&sc1.m_ptrs[i1], &sc2.m_ptrs[i2]);
            if (!val)
            {
                // Got a match, increment pointers.
                //
                oldp = &sc1.m_ptrs[i1];
                while (  i1 < n1
                      && cf(&sc1.m_ptrs[i1], oldp) == 0)
                {
                    i1++;
                }
                while (  i2 < n2
                      && cf(&sc2.m_ptrs[i2], oldp) == 0)
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
                      && cf(&sc1.m_ptrs[i1], oldp) == 0)
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
                      && cf(&sc2.m_ptrs[i2], oldp) == 0)
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
                  && cf(&sc1.m_ptrs[i1], oldp) == 0)
            {
                i1++;
            }
        }
    }

    do_asort_finish(&sc1);
    do_asort_finish(&sc2);
    free_lbuf(list1);
    free_lbuf(list2);
    delete [] ptrs1;
    delete [] ptrs2;
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
    LBUF_OFFSET nWidth = (LBUF_OFFSET)mux_atol(strip_color(fargs[1]));
    if (0 == nWidth)
    {
        return;
    }

    if (LBUF_SIZE <= nWidth)
    {
        safe_range(buff, bufc);
        return;
    }

    mux_string *sStr = nullptr;
    try
    {
        sStr = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sStr)
    {
        return;
    }

    mux_cursor nStr = sStr->length_cursor();

    // If there's no need to pad, then we are done.
    //
    if (nWidth <= nStr.m_point)
    {
        mux_cursor iEnd = nStr;
        if (bTrunc)
        {
            sStr->cursor_from_point(iEnd, nWidth);
        }
        size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
        *bufc += sStr->export_TextColor(*bufc, CursorMin, iEnd, nMax);
        delete sStr;
        return;
    }

    // Determine string to pad with.
    //
    mux_string *sPad = nullptr;
    try
    {
        sPad = new mux_string;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sPad)
    {
        delete sStr;
        return;
    }

    if (nfargs == 3 && *fargs[2])
    {
        sPad->import(fargs[2]);
        sPad->strip(T("\r\n\t"));
    }
    LBUF_OFFSET nPad = sPad->length_cursor().m_point;
    if (0 == nPad)
    {
        sPad->import(T(" "), 1);
        nPad = 1;
    }

    LBUF_OFFSET nLeading = 0;
    if (iType == CJC_CENTER)
    {
        nLeading = (nWidth - nStr.m_point)/2;
    }
    else if (iType == CJC_RJUST)
    {
        nLeading = nWidth - nStr.m_point;
    }
    LBUF_OFFSET nTrailing = nWidth - nLeading - nStr.m_point;
    LBUF_OFFSET nPos = 0;

    mux_string *s = nullptr;
    try
    {
        s = new mux_string;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == s)
    {
        delete sStr;
        delete sPad;
        return;
    }

    // Output leading padding.
    //
    while (nPos < nLeading)
    {
        mux_cursor iEnd;
        sPad->cursor_from_point(iEnd, nLeading - nPos);
        nPos = static_cast<LBUF_OFFSET>(nPos + nPad);
        s->append(*sPad, CursorMin, iEnd);
    }
    nPos = nLeading;

    // Output string.
    //
    s->append(*sStr, CursorMin, nStr);
    nPos = static_cast<LBUF_OFFSET>(nPos + nStr.m_point);

    // Output first part of trailing padding.
    //
    if (0 < nTrailing)
    {
        LBUF_OFFSET nPadStart = (LBUF_OFFSET)(nPos % nPad);
        LBUF_OFFSET nPadPart  = nWidth - nLeading - nStr.m_point;
        if (nPad - nPadStart < nPadPart)
        {
            nPadPart = nPad - nPadStart;
        }

        if (0 < nPadPart)
        {
            mux_cursor iStart, iEnd;
            sPad->cursor_from_point(iStart, nPadStart);
            sPad->cursor_from_point(iEnd, nPadStart+nPadPart);
            nPos = static_cast<LBUF_OFFSET>(nPos + nPadPart);
            s->append(*sPad, iStart, iEnd);
        }
    }

    // Output trailing padding.
    //
    while (nPos < nWidth)
    {
        mux_cursor iEnd;
        sPad->cursor_from_point(iEnd, nWidth-nPos);
        nPos = static_cast<LBUF_OFFSET>(nPos + nPad);
        s->append(*sPad, CursorMin, iEnd);
    }

    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    *bufc += s->export_TextColor(*bufc, CursorMin, CursorMax, nMax);

    delete sStr;
    delete sPad;
    delete s;
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

/* ---------------------------------------------------------------------------
 * setq, setr, r: set and read global registers.
 */

static FUNCTION(fun_setq)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int regnum = mux_RegisterSet[(unsigned char)fargs[0][0]];
    if (  regnum < 0
       || regnum >= MAX_GLOBAL_REGS
       || fargs[0][1] != '\0')
    {
        safe_str(T("#-1 INVALID GLOBAL REGISTER"), buff, bufc);
    }
    else
    {
        size_t n = strlen((char *)fargs[1]);
        RegAssign(&mudstate.global_regs[regnum], n, fargs[1]);
    }
}

static FUNCTION(fun_setr)
{
    UNUSED_PARAMETER(executor);
    UNUSED_PARAMETER(caller);
    UNUSED_PARAMETER(enactor);
    UNUSED_PARAMETER(eval);
    UNUSED_PARAMETER(nfargs);
    UNUSED_PARAMETER(cargs);
    UNUSED_PARAMETER(ncargs);

    int regnum = mux_RegisterSet[(unsigned char)fargs[0][0]];
    if (  regnum < 0
       || regnum >= MAX_GLOBAL_REGS
       || fargs[0][1] != '\0')
    {
        safe_str(T("#-1 INVALID GLOBAL REGISTER"), buff, bufc);
    }
    else
    {
        size_t n = strlen((char *)fargs[1]);
        RegAssign(&mudstate.global_regs[regnum], n, fargs[1]);
        safe_copy_buf(fargs[1], n, buff, bufc);
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

    int regnum = mux_RegisterSet[(unsigned char)fargs[0][0]];
    if (  regnum < 0
       || regnum >= MAX_GLOBAL_REGS
       || fargs[0][1] != '\0')
    {
        safe_str(T("#-1 INVALID GLOBAL REGISTER"), buff, bufc);
    }
    else if (mudstate.global_regs[regnum])
    {
        safe_copy_buf(mudstate.global_regs[regnum]->reg_ptr,
            mudstate.global_regs[regnum]->reg_len, buff, bufc);
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
        if (NUMBER_TOKEN == p[0])
        {
            p++;
            dbref dbitem = parse_dbref(p);
            bResult = Good_obj(dbitem);
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
            n = strlen((char *)p);
        }
    }

    mux_string *sStr = nullptr;
    try
    {
        sStr = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sStr)
    {
        return;
    }

    if (0 == n)
    {
        sStr->trim(' ', bLeft, bRight);
    }
    else
    {
        sStr->trim(p, n, bLeft, bRight);
    }

    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    *bufc += sStr->export_TextColor(*bufc, CursorMin, CursorMax, nMax);
    delete sStr;
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
    mux_string *sStr = nullptr;
    try
    {
        sStr = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sStr)
    {
        return;
    }

    if (  1 < nfargs
       && '\0' != fargs[1][0])
    {
        sStr->strip(strip_color(fargs[1]));
    }
    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
    *bufc += sStr->export_TextPlain(*bufc, CursorMin, CursorMax, nMax);

    delete sStr;
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
    mux_cursor curOSep(2, 2);
    if (  7 <= nfargs
       && '\0' != fargs[6][0])
    {
        if (!strcmp((char *)fargs[6], "@@"))
        {
            pOSep = T("");
        }
        else
        {
            pOSep = fargs[6];
        }
        utf8_strlen(pOSep, curOSep);
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
                               pOSep, curOSep, static_cast<LBUF_OFFSET>(nFirstWidth));
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
        bool bFound = false;
        DESC *d;
        DESC_ITER_CONN(d)
        {
            if (d->socket == s)
            {
                bFound = true;
                break;
            }
        }
        if (  bFound
           && (  d->player == executor
              || Wizard_Who(executor)))
        {
            nCmds = d->command_count;
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
// Accepts a single argument. Based on the rules specified in the config
// parameters article_regexp and article_exception it determines whether the
// word should use 'an' or 'a' as its article.
//
// By default if a word matches the regexp specified in article_regexp then
// this function will return 'an', otherwise it will return 'a'. If, however
// the word also matches one of the specified article_exception regexp's
// will return the given article.
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

    const int ovecsize = 33;
    int ovec[ovecsize];

    // Drop the input string into lower case.
    //
    size_t nCased;
    UTF8 *pCased = mux_strlwr(fargs[0], nCased);

    // Search for exceptions.
    //
    ArtRuleset *arRule = mudconf.art_rules;

    while (arRule != nullptr)
    {
        pcre* reRuleRegexp = (pcre *) arRule->m_pRegexp;
        pcre_extra* reRuleStudy = (pcre_extra *) arRule->m_pRegexpStudy;

        if (  !alarm_clock.alarmed
           && pcre_exec(reRuleRegexp, reRuleStudy, (char *)pCased, static_cast<int>(nCased),
                0, 0, ovec, ovecsize) > 0)
        {
            safe_str(arRule->m_bUseAn ? T("an") : T("a"), buff, bufc);
            return;
        }

        arRule = arRule->m_pNextRule;
    }

    // Default to 'a'.
    //
    safe_str(T("a"), buff, bufc);
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

    size_t n;
    UTF8 *p  = strip_color(fargs[0]);
    if (utf8_strlen(p, n))
    {
        if (1 == n)
        {
            UTF32 ch = ConvertFromUTF8(p);
            safe_ltoa(ch, buff, bufc);
        }
        else
        {
            safe_str(T("#-1 FUNCTION EXPECTS ONE CHARACTER"), buff, bufc);
        }
    }
    else
    {
        safe_str(T("#-1 STRING IS INVALID"), buff, bufc);
    }
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

    if (!is_integer(fargs[0], nullptr))
    {
        safe_str(T("#-1 ARGUMENT MUST BE A NUMBER"), buff, bufc);
        return;
    }

    int ch = mux_atol(fargs[0]);
    UTF8 *p = ConvertToUTF8(ch);
    if (mux_isprint(p))
    {
        utf8_safe_chr(p, buff, bufc);
    }
    else
    {
        safe_str(T("#-1 UNPRINTABLE CHARACTER"), buff, bufc);
    }
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

    const UTF8 *p = (UTF8 *)ConvertToAscii(fargs[0]);
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

        p = utf8_NextCodePoint(p);
        q = utf8_NextCodePoint(q);
    }
}

void transform_range(mux_string &sStr)
{
    // Look for a-z type character ranges. Dashes that don't have another
    // character on each end of them are treated literally.
    //
    mux_cursor nPos, nStart;
    UTF8 cBefore, cAfter;

    mux_string *sTemp = nullptr;
    try
    {
        sTemp = new mux_string;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == sTemp)
    {
        return;
    }

    sTemp->cursor_start(nStart);
    sTemp->cursor_next(nStart);

    bool bSucceeded = sStr.search(T("-"), &nPos, nStart);
    while (bSucceeded)
    {
        nStart = nPos;
        cBefore = sStr.export_Char(nStart.m_byte-1);
        cAfter = sStr.export_Char(nStart.m_byte+1);
        if ('\0' == cAfter)
        {
            break;
        }
        if (  mux_isazAZ(cBefore)
           && mux_isazAZ(cAfter))
        {
            // Character range.
            //
            sTemp->truncate(CursorMin);
            if (mux_islower_ascii(cBefore) == mux_islower_ascii(cAfter))
            {
                cBefore++;
                while (cBefore < cAfter)
                {
                    sTemp->append_TextPlain(&cBefore, 1);
                    cBefore++;
                }
                mux_cursor nReplace(1, 1);
                sStr.replace_Chars(*sTemp, nStart, nReplace);
            }
            else if (  mux_islower_ascii(cBefore)
                    && mux_isupper_ascii(cAfter))
            {
                cBefore++;
                while (cBefore <= 'z')
                {
                    sTemp->append_TextPlain(&cBefore, 1);
                    cBefore++;
                }
                cBefore = 'A';
                while (cBefore < cAfter)
                {
                    sTemp->append_TextPlain(&cBefore, 1);
                    cBefore++;
                }
                mux_cursor nReplace(1, 1);
                sStr.replace_Chars(*sTemp, nStart, nReplace);
            }
        }
        else if (  mux_isdigit(cBefore)
                && mux_isdigit(cAfter))
        {
            // Numeric range.
            //
            cBefore++;
            sTemp->truncate(CursorMin);
            while (cBefore < cAfter)
            {
                sTemp->append_TextPlain(&cBefore, 1);
                cBefore++;
            }
            mux_cursor nLen(1, 1);
            sStr.replace_Chars(*sTemp, nStart, nLen);
        }
        sStr.cursor_next(nStart);
        bSucceeded = sStr.search(T("-"), &nPos, nStart);
    }

    delete sTemp;
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

    mux_string *sStr = nullptr;
    try
    {
        sStr = new mux_string(fargs[0]);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr != sStr)
    {
        size_t nStr = sStr->length_byte();
        if (0 != nStr)
        {
            // Process character ranges.
            //
            mux_string *sFrom = nullptr;
            mux_string *sTo = nullptr;
            try
            {
                sFrom = new mux_string(fargs[1]);
                sTo   = new mux_string(fargs[2]);
            }
            catch (...)
            {
                ; // Nothing.
            }

            if (  nullptr != sFrom
               && nullptr != sTo)
            {
                transform_range(*sFrom);
                transform_range(*sTo);

                if (sFrom->length_cursor().m_point != sTo->length_cursor().m_point)
                {
                    safe_str(T("#-1 STRING LENGTHS MUST BE EQUAL"), buff, bufc);
                }
                else
                {
                    sStr->transform(*sFrom, *sTo);
                    size_t nMax = buff + (LBUF_SIZE-1) - *bufc;
                    *bufc += sStr->export_TextColor(*bufc, CursorMin, CursorMax, nMax);
                }
            }

            delete sFrom;
            delete sTo;
        }
    }
    delete sStr;
}

// ----------------------------------------------------------------------------
// flist: List of existing functions in alphabetical order.
//
//   Name          Handler      # of args   min #    max #   flags  permissions
//                               to parse  of args  of args
//
static FUN builtin_function_list[] =
{
    {T("@@"),          fun_null,             1, 1,       1, FN_NOEVAL, CA_PUBLIC},
    {T("ABS"),         fun_abs,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ACCENT"),      fun_accent,     MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("ACOS"),        fun_acos,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("ADD"),         fun_add,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("AFTER"),       fun_after,      MAX_ARG, 1,       2,         0, CA_PUBLIC},
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
    {T("ATTRCNT"),     fun_attrcnt,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("BAND"),        fun_band,       MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("BASECONV"),    fun_baseconv,   MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("BEEP"),        fun_beep,       MAX_ARG, 0,       0,         0, CA_WIZARD},
    {T("BEFORE"),      fun_before,     MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("BITTYPE"),     fun_bittype,    MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("BNAND"),       fun_bnand,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("BOR"),         fun_bor,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("BXOR"),        fun_bxor,       MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("CAND"),        fun_cand,       MAX_ARG, 0, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("CANDBOOL"),    fun_candbool,   MAX_ARG, 0, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
#if defined(WOD_REALMS) || defined(REALITY_LVLS)
    {T("CANSEE"),      fun_cansee,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
#endif
    {T("CAPSTR"),      fun_capstr,           1, 1,       1,         0, CA_PUBLIC},
    {T("CASE"),        fun_case,       MAX_ARG, 2, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("CAT"),         fun_cat,        MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("CEIL"),        fun_ceil,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CEMIT"),       fun_cemit,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("CENTER"),      fun_center,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("CHANNELS"),    fun_channels,   MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("CHANOBJ"),     fun_chanobj,    MAX_ARG, 1,       1,         0, CA_WIZARD},
    {T("CHILDREN"),    fun_children,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CHOOSE"),      fun_choose,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("CHR"),         fun_chr,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CMDS"),        fun_cmds,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("COLORDEPTH"),  fun_colordepth, MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("COLUMNS"),     fun_columns,    MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("COMALIAS"),    fun_comalias,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("COMP"),        fun_comp,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("COMTITLE"),    fun_comtitle,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("CON"),         fun_con,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CONFIG"),      fun_config,     MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("CONN"),        fun_conn,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CONNLAST"),    fun_connlast,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("CONNLEFT"),    fun_connleft,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
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
    {T("CREATE"),      fun_create,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("CTIME"),       fun_ctime,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("CTU"),         fun_ctu,        MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("CWHO"),        fun_cwho,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("DEC"),         fun_dec,        MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("DECRYPT"),     fun_decrypt,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("DEFAULT"),     fun_default,    MAX_ARG, 2,       2, FN_NOEVAL, CA_PUBLIC},
    {T("DELETE"),      fun_delete,     MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("DESTROY"),     fun_destroy,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("DIE"),         fun_die,        MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("DIGEST"),      fun_digest,           2, 1,       2,         0, CA_PUBLIC},
    {T("DIGITTIME"),   fun_digittime,  MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("DIST2D"),      fun_dist2d,     MAX_ARG, 4,       4,         0, CA_PUBLIC},
    {T("DIST3D"),      fun_dist3d,     MAX_ARG, 6,       6,         0, CA_PUBLIC},
    {T("DISTRIBUTE"),  fun_distribute, MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("DOING"),       fun_doing,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("DUMPING"),     fun_dumping,    MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("E"),           fun_e,          MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("EDEFAULT"),    fun_edefault,   MAX_ARG, 2,       2, FN_NOEVAL, CA_PUBLIC},
    {T("EDIT"),        fun_edit,       MAX_ARG, 3, MAX_ARG,         0, CA_PUBLIC},
    {T("ELEMENTS"),    fun_elements,   MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("ELOCK"),       fun_elock,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("EMIT"),        fun_emit,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
#ifdef DEPRECATED
    {T("EMPTY"),       fun_empty,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
#endif // DEPRECATED
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
    {T("FLAGS"),       fun_flags,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("FLOOR"),       fun_floor,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("FLOORDIV"),    fun_floordiv,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("FMOD"),        fun_fmod,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("FOLD"),        fun_fold,       MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("FOREACH"),     fun_foreach,    MAX_ARG, 2,       4,         0, CA_PUBLIC},
#if defined(FIRANMUX)
    {T("FORMAT"),      fun_format,     MAX_ARG, 4,       4,         0, CA_PUBLIC},
#endif // FIRANMUX
    {T("FULLNAME"),    fun_fullname,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("GET"),         fun_get,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("GET_EVAL"),    fun_get_eval,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
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
    {T("ISDBREF"),     fun_isdbref,    MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("ISIGN"),       fun_isign,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ISINT"),       fun_isint,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ISNUM"),       fun_isnum,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ISRAT"),       fun_israt,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ISUB"),        fun_isub,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("ISWORD"),      fun_isword,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ITEMIZE"),     fun_itemize,    MAX_ARG, 1,       4,         0, CA_PUBLIC},
#ifdef DEPRECATED
    {T("ITEMS"),       fun_items,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
#endif // DEPRECATED
    {T("ITER"),        fun_iter,       MAX_ARG, 2,       4, FN_NOEVAL, CA_PUBLIC},
    {T("ITEXT"),       fun_itext,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
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
    {T("LEXITS"),      fun_lexits,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LFLAGS"),      fun_lflags,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LINK"),        fun_link,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("LIST"),        fun_list,       MAX_ARG, 2,       3, FN_NOEVAL, CA_PUBLIC},
    {T("LIT"),         fun_lit,              1, 1,       1, FN_NOEVAL, CA_PUBLIC},
    {T("LJUST"),       fun_ljust,      MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("LMAX"),        fun_lmax,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("LMIN"),        fun_lmin,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("LN"),          fun_ln,         MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LNUM"),        fun_lnum,       MAX_ARG, 0,       4,         0, CA_PUBLIC},
    {T("LOC"),         fun_loc,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LOCALIZE"),    fun_localize,   MAX_ARG, 1,       1, FN_NOEVAL, CA_PUBLIC},
    {T("LOCATE"),      fun_locate,     MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("LOCK"),        fun_lock,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LOG"),         fun_log,        MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("LOR"),         fun_lor,        MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("LPAD"),        fun_lpad,       MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("LPARENT"),     fun_lparent,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("LPORTS"),      fun_lports,     MAX_ARG, 0,       0,         0, CA_WIZARD},
    {T("LPOS"),        fun_lpos,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("LRAND"),       fun_lrand,      MAX_ARG, 3,       4,         0, CA_PUBLIC},
    {T("LROOMS"),      fun_lrooms,     MAX_ARG, 1,       3,         0, CA_PUBLIC},
#ifdef DEPRECATED
    {T("LSTACK"),      fun_lstack,     MAX_ARG, 0,       1,         0, CA_PUBLIC},
#endif // DEPRECATED
    {T("LT"),          fun_lt,         MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("LTE"),         fun_lte,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("LWHO"),        fun_lwho,       MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("MAIL"),        fun_mail,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("MAILFROM"),    fun_mailfrom,   MAX_ARG, 1,       2,         0, CA_PUBLIC},
#if defined(FIRANMUX)
    {T("MAILJ"),       fun_mailj,      MAX_ARG, 0,       2,         0, CA_PUBLIC},
#endif // FIRANMUX
    {T("MAILSIZE"),    fun_mailsize,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("MAILSUBJ"),    fun_mailsubj,   MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("MAP"),         fun_map,        MAX_ARG, 2,      13,         0, CA_PUBLIC},
    {T("MATCH"),       fun_match,      MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("MATCHALL"),    fun_matchall,   MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("MAX"),         fun_max,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("MEMBER"),      fun_member,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("MERGE"),       fun_merge,      MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("MID"),         fun_mid,        MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {T("MIN"),         fun_min,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("MIX"),         fun_mix,        MAX_ARG, 2,      12,         0, CA_PUBLIC},
    {T("MOD"),         fun_mod,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("MONEY"),       fun_money,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("MONIKER"),     fun_moniker,    MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("MOTD"),        fun_motd,       MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("MTIME"),       fun_mtime,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("MUDNAME"),     fun_mudname,    MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("MUL"),         fun_mul,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {T("MUNGE"),       fun_munge,      MAX_ARG, 3,       4,         0, CA_PUBLIC},
    {T("NAME"),        fun_name,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("NEARBY"),      fun_nearby,     MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("NEQ"),         fun_neq,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("NEXT"),        fun_next,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("NOT"),         fun_not,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("NULL"),        fun_null,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("NUM"),         fun_num,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("OBJ"),         fun_obj,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("OBJEVAL"),     fun_objeval,    MAX_ARG, 2,       2, FN_NOEVAL, CA_PUBLIC},
    {T("OBJMEM"),      fun_objmem,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("OEMIT"),       fun_oemit,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("OR"),          fun_or,         MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("ORBOOL"),      fun_orbool,     MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("ORD"),         fun_ord,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ORFLAGS"),     fun_orflags,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("OWNER"),       fun_owner,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("PACK"),        fun_pack,       MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("PARENT"),      fun_parent,     MAX_ARG, 1,       2,         0, CA_PUBLIC},
#ifdef DEPRECATED
    {T("PEEK"),        fun_peek,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
#endif // DEPRECATED
    {T("PEMIT"),       fun_pemit,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("PFIND"),       fun_pfind,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("PI"),          fun_pi,         MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {T("PICKRAND"),    fun_pickrand,   MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("PLAYMEM"),     fun_playmem,    MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {T("PMATCH"),      fun_pmatch,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("POLL"),        fun_poll,       MAX_ARG, 0,       0,         0, CA_PUBLIC},
#ifdef DEPRECATED
    {T("POP"),         fun_pop,        MAX_ARG, 0,       2,         0, CA_PUBLIC},
#endif // DEPRECATED
    {T("PORTS"),       fun_ports,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("POS"),         fun_pos,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("POSS"),        fun_poss,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("POWER"),       fun_power,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("POWERS"),      fun_powers,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
#ifdef DEPRECATED
    {T("PUSH"),        fun_push,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
#endif // DEPRECATED
    {T("R"),           fun_r,          MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("RAND"),        fun_rand,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("REGMATCH"),    fun_regmatch,   MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("REGMATCHI"),   fun_regmatchi,  MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("REGRAB"),      fun_regrab,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("REGRABALL"),   fun_regraball,  MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("REGRABALLI"),  fun_regraballi, MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("REGRABI"),     fun_regrabi,    MAX_ARG, 2,       3,         0, CA_PUBLIC},
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
    {T("S"),           fun_s,                1, 1,       1,         0, CA_PUBLIC},
    {T("SCRAMBLE"),    fun_scramble,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("SEARCH"),      fun_search,           1, 0,       1,         0, CA_PUBLIC},
    {T("SECS"),        fun_secs,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("SECURE"),      fun_secure,           1, 1,       1,         0, CA_PUBLIC},
    {T("SET"),         fun_set,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("SETDIFF"),     fun_setdiff,    MAX_ARG, 2,       5,         0, CA_PUBLIC},
    {T("SETINTER"),    fun_setinter,   MAX_ARG, 2,       5,         0, CA_PUBLIC},
#if defined(FIRANMUX)
    {T("SETPARENT"),   fun_setparent,  MAX_ARG, 2,       2,         0, CA_PUBLIC},
#endif // FIRANMUX
    {T("SETQ"),        fun_setq,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("SETR"),        fun_setr,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
#if defined(FIRANMUX)
    {T("SETNAME"),     fun_setname,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
#endif // FIRANMUX
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
    {T("STEP"),        fun_step,       MAX_ARG, 3,       5,         0, CA_PUBLIC},
    {T("STRCAT"),      fun_strcat,     MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("STRIP"),       fun_strip,      MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("STRIPACCENTS"),fun_stripaccents, MAX_ARG, 1,     1,         0, CA_PUBLIC},
    {T("STRIPANSI"),   fun_stripansi,  MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("STRLEN"),      fun_strlen,           1, 0,       1,         0, CA_PUBLIC},
    {T("STRMATCH"),    fun_strmatch,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("STRMEM"),      fun_strmem,           1, 0,       1,         0, CA_PUBLIC},
    {T("STRTRUNC"),    fun_strtrunc,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("SUB"),         fun_sub,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("SUBEVAL"),     fun_subeval,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("SUBJ"),        fun_subj,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("SUCCESSES"),   fun_successes,  MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("SWITCH"),      fun_switch,     MAX_ARG, 2, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {T("T"),           fun_t,                1, 0,       1,         0, CA_PUBLIC},
    {T("TABLE"),       fun_table,      MAX_ARG, 1,       6,         0, CA_PUBLIC},
    {T("TAN"),         fun_tan,        MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {T("TEL"),         fun_tel,        MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {T("TERMINFO"),    fun_terminfo,         1, 1, MAX_ARG,         0, CA_PUBLIC},
#if defined(FIRANMUX)
    {T("TEXT"),        fun_text,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
#endif // FIRANMUX
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
    {T("UNPACK"),      fun_unpack,     MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {T("V"),           fun_v,          MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("VADD"),        fun_vadd,       MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("VALID"),       fun_valid,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("VCROSS"),      fun_vcross,     MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {T("VDIM"),        fun_words,      MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {T("VDOT"),        fun_vdot,       MAX_ARG, 2,       4,         0, CA_PUBLIC},
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
    {T("WRAP"),        fun_wrap,       MAX_ARG, 1,       8,         0, CA_PUBLIC},
    {T("WRITETIME"),   fun_writetime,  MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("XGET"),        fun_xget,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {T("XOR"),         fun_xor,        MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {T("ZFUN"),        fun_zfun,       MAX_ARG, 2,      11,         0, CA_PUBLIC},
    {T("ZONE"),        fun_zone,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {T("ZWHO"),        fun_zwho,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {(UTF8 *)nullptr,   nullptr,       MAX_ARG, 0,       0,         0, 0}
};

void function_add(FUN *fp)
{
    size_t nCased;
    UTF8 *pCased = mux_strupr(fp->name, nCased);
    if (nullptr == hashfindLEN(pCased, nCased, &mudstate.func_htab))
    {
        hashaddLEN(pCased, nCased, fp, &mudstate.func_htab);
    }
}

void function_remove(FUN *fp)
{
    size_t nCased;
    UTF8 *pCased = mux_strupr(fp->name, nCased);
    hashdeleteLEN(pCased, nCased, &mudstate.func_htab);
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
    ufun_head = nullptr;
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

    UFUN *ufp, *ufp2;
    ATTR *ap;

    if (  (key & FN_LIST)
       || nullptr == fname
       || '\0' == fname[0])
    {
        notify(executor, tprintf(T("%-28s   %-8s  %-30s Flgs"),
            "Function Name", "DBref#", "Attribute"));
        notify(executor, tprintf(T("%28s   %8s  %30s %4s"),
            "----------------------------", "--------",
            "------------------------------", " -- "));

        int count = 0;
        for (ufp2 = ufun_head; ufp2; ufp2 = ufp2->next)
        {
            const UTF8 *pName = T("(WARNING: Bad Attribute Number)");
            ap = atr_num(ufp2->atr);
            if (ap)
            {
                pName = ap->name;
            }
            notify(executor, tprintf(T("%-28.28s   #%-7d  %-30.30s  %c%c"),
                ufp2->name, ufp2->obj, pName, ((ufp2->flags & FN_PRIV) ? 'W' : '-'),
                ((ufp2->flags & FN_PRES) ? 'p' : '-')));
            count++;
        }

        notify(executor, tprintf(T("%28s   %8s  %30s %4s"),
            "----------------------------", "--------",
            "------------------------------", " -- "));

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
    if (hashfindLEN(pName, nLen, &mudstate.func_htab) != nullptr)
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
        ufp = (UFUN *) hashfindLEN(pName, nLen, &mudstate.ufunc_htab);
        if (nullptr == ufp)
        {
            notify_quiet(executor, tprintf(T("Function %s not found."), pName));
        }
        else
        {
            if (ufp == ufun_head)
            {
                ufun_head = ufun_head->next;
            }
            else
            {
                for (ufp2 = ufun_head; ufp2->next; ufp2 = ufp2->next)
                {
                    if (ufp2->next == ufp)
                    {
                        ufp2->next = ufp->next;
                        break;
                    }
                }
            }
            hashdeleteLEN(pName, nLen, &mudstate.ufunc_htab);
            delete ufp;
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

    // Privileged functions require you control the obj.
    //
    if ((key & FN_PRIV) && !Controls(executor, obj))
    {
        notify_quiet(executor, NOPERM_MESSAGE);
        return;
    }

    // See if function already exists.  If so, redefine it.
    //
    ufp = (UFUN *) hashfindLEN(pName, nLen, &mudstate.ufunc_htab);

    if (!ufp)
    {
        ufp = nullptr;
        try
        {
            ufp = new UFUN;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == ufp)
        {
            return;
        }

        ufp->name = StringCloneLen(pName, nLen);
        ufp->obj = obj;
        ufp->atr = pattr->number;
        ufp->perms = CA_PUBLIC;
        ufp->next = nullptr;
        if (!ufun_head)
        {
            ufun_head = ufp;
        }
        else
        {
            for (ufp2 = ufun_head; ufp2->next; ufp2 = ufp2->next)
            {
                // Nothing
                ;
            }
            ufp2->next = ufp;
        }
        hashaddLEN(pName, nLen, ufp, &mudstate.ufunc_htab);
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

    UFUN *ufp;
    for (ufp = ufun_head; ufp && bp < buff + (LBUF_SIZE-1); ufp = ufp->next)
    {
        if (check_access(player, ufp->perms))
        {
            safe_chr(' ', buff, &bp);
            safe_str(ufp->name, buff, &bp);
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

    UTF8 *ap;
    for (ap = str; *ap && !mux_isspace(*ap); ap++)
    {
        *ap = mux_toupper_ascii(*ap);
    }
    size_t nstr = ap - str;

    if (*ap)
    {
        *ap++ = '\0';
    }

    FUN *fp = (FUN *)hashfindLEN(str, nstr, &mudstate.func_htab);
    if (fp)
    {
        return cf_modify_bits(&fp->perms, ap, pExtra, nExtra, player, cmd);
    }
    UFUN *ufp = (UFUN *)hashfindLEN(str, nstr, &mudstate.ufunc_htab);
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
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IFunctionsControl
    //
    virtual MUX_RESULT Add(unsigned int nKey, const UTF8 *name, mux_IFunction *pIFun, int maxArgsParsed, int minArgs, int maxArgs, int flags, int perms);

    CFunctions(void);
    virtual ~CFunctions();

private:
    UINT32          m_cRef;
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

UINT32 CFunctions::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CFunctions::Release(void)
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

    FunctionsNode *pfn = (FunctionsNode *)(fp->vp);
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
    pfn->fun.vp = (void *)pfn;

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

UINT32 CFunctionsFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CFunctionsFactory::Release(void)
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
