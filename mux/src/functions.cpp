// functions.cpp -- MUX function handlers.
//
// $Id: functions.cpp,v 1.160 2005-12-29 01:10:35 sdennis Exp $
//
// MUX 2.4
// Copyright (C) 1998 through 2005 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "ansi.h"
#include "attrs.h"
#include "command.h"
#include "functions.h"
#include "funmath.h"
#include "interface.h"
#include "misc.h"
#include "pcre.h"
#ifdef REALITY_LVLS
#include "levels.h"
#endif /* REALITY_LVLS */

UFUN *ufun_head;

extern void cf_display(dbref, char *, char *, char **);
extern void cf_list(dbref, char *, char **);
extern bool parse_and_get_attrib(dbref, char *[], char **, dbref *, char *, char **);

// Function definitions from funceval.cpp
//

// In comsys.cpp
XFUNCTION(fun_channels);
XFUNCTION(fun_comalias);
XFUNCTION(fun_comtitle);
// In funceval.cpp
XFUNCTION(fun_alphamax);
XFUNCTION(fun_alphamin);
XFUNCTION(fun_andflags);
XFUNCTION(fun_ansi);
XFUNCTION(fun_beep);
XFUNCTION(fun_children);
XFUNCTION(fun_columns);
XFUNCTION(fun_cwho);
XFUNCTION(fun_decrypt);
XFUNCTION(fun_default);
XFUNCTION(fun_die);
XFUNCTION(fun_dumping);
XFUNCTION(fun_edefault);
XFUNCTION(fun_elements);
XFUNCTION(fun_empty);
XFUNCTION(fun_encrypt);
XFUNCTION(fun_entrances);
XFUNCTION(fun_fcount);
XFUNCTION(fun_fdepth);
XFUNCTION(fun_findable);
XFUNCTION(fun_foreach);
XFUNCTION(fun_grab);
XFUNCTION(fun_graball);
XFUNCTION(fun_grep);
XFUNCTION(fun_grepi);
XFUNCTION(fun_hasattr);
XFUNCTION(fun_hasattrp);
XFUNCTION(fun_hastype);
XFUNCTION(fun_ifelse);
XFUNCTION(fun_inzone);
XFUNCTION(fun_isword);
XFUNCTION(fun_items);
XFUNCTION(fun_last);
XFUNCTION(fun_lit);
XFUNCTION(fun_localize);
XFUNCTION(fun_lparent);
XFUNCTION(fun_lrand);
XFUNCTION(fun_lrooms);
XFUNCTION(fun_lstack);
XFUNCTION(fun_mail);
XFUNCTION(fun_mailfrom);
XFUNCTION(fun_matchall);
XFUNCTION(fun_mix);
XFUNCTION(fun_munge);
XFUNCTION(fun_null);
XFUNCTION(fun_objeval);
XFUNCTION(fun_objmem);
XFUNCTION(fun_orflags);
XFUNCTION(fun_pack);
XFUNCTION(fun_peek);
XFUNCTION(fun_pickrand);
XFUNCTION(fun_playmem);
XFUNCTION(fun_pop);
XFUNCTION(fun_ports);
XFUNCTION(fun_push);
XFUNCTION(fun_regmatch);
XFUNCTION(fun_regmatchi);
XFUNCTION(fun_regrab);
XFUNCTION(fun_regraball);
XFUNCTION(fun_regraballi);
XFUNCTION(fun_regrabi);
XFUNCTION(fun_scramble);
XFUNCTION(fun_shuffle);
XFUNCTION(fun_sortby);
XFUNCTION(fun_squish);
XFUNCTION(fun_strcat);
XFUNCTION(fun_stripansi);
XFUNCTION(fun_strtrunc);
XFUNCTION(fun_table);
XFUNCTION(fun_translate);
XFUNCTION(fun_udefault);
XFUNCTION(fun_unpack);
XFUNCTION(fun_valid);
XFUNCTION(fun_visible);
XFUNCTION(fun_zfun);
XFUNCTION(fun_zone);
XFUNCTION(fun_zwho);
#ifdef SIDE_EFFECT_FUNCTIONS
XFUNCTION(fun_create);
XFUNCTION(fun_emit);
XFUNCTION(fun_link);
XFUNCTION(fun_oemit);
XFUNCTION(fun_pemit);
XFUNCTION(fun_remit);
XFUNCTION(fun_cemit);
XFUNCTION(fun_set);
XFUNCTION(fun_tel);
XFUNCTION(fun_textfile);
#endif
// In netcommon.cpp
XFUNCTION(fun_doing);
XFUNCTION(fun_host);
XFUNCTION(fun_motd);
XFUNCTION(fun_poll);
// In quota.cpp
XFUNCTION(fun_hasquota);

SEP sepSpace = { 1, " " };

// Trim off leading and trailing spaces if the separator char is a
// space -- known length version.
//
char *trim_space_sep_LEN(char *str, int nStr, SEP *sep, int *nTrim)
{
    if (  sep->n != 1
       || sep->str[0] != ' ')
    {
        *nTrim = nStr;
        return str;
    }

    // Advance over leading spaces.
    //
    char *pBegin = str;
    char *pEnd = str + nStr - 1;
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
char *trim_space_sep(char *str, SEP *sep)
{
    if (  sep->n != 1
       || sep->str[0] != ' ')
    {
        return str;
    }
    while (*str == ' ')
    {
        str++;
    }
    char *p;
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

// next_token: Point at start of next token in string -- known length
// version.
//
char *next_token_LEN(char *str, int *nStr, SEP *psep)
{
    char *pBegin = str;
    if (psep->n == 1)
    {
        while (  *pBegin != '\0'
              && *pBegin != psep->str[0])
        {
            pBegin++;
        }
        if (!*pBegin)
        {
            *nStr = 0;
            return NULL;
        }
        pBegin++;
        if (psep->str[0] == ' ')
        {
            while (*pBegin == ' ')
            {
                pBegin++;
            }
        }
    }
    else
    {
        char *p = strstr(pBegin, psep->str);
        if (p)
        {
            pBegin = p + psep->n;
        }
        else
        {
            *nStr = 0;
            return NULL;
        }
    }
    *nStr -= pBegin - str;
    return pBegin;
}

// next_token: Point at start of next token in string
//
char *next_token(char *str, SEP *psep)
{
    if (psep->n == 1)
    {
        while (  *str != '\0'
              && *str != psep->str[0])
        {
            str++;
        }
        if (!*str)
        {
            return NULL;
        }
        str++;
        if (psep->str[0] == ' ')
        {
            while (*str == ' ')
            {
                str++;
            }
        }
    }
    else
    {
        char *p = strstr(str, psep->str);
        if (p)
        {
            str = p + psep->n;
        }
        else
        {
            return NULL;
        }
    }
    return str;
}

// split_token: Get next token from string as null-term string. String is
// destructively modified -- known length version.
//
char *split_token_LEN(char **sp, int *nStr, SEP *psep, int *nToken)
{
    char *str = *sp;
    char *save = str;
    if (!str)
    {
        *nStr = 0;
        *sp = NULL;
        *nToken = 0;
        return NULL;
    }

    if (psep->n == 1)
    {
        // Advance over token
        //
        while (  *str
              && *str != psep->str[0])
        {
            str++;
        }
        *nToken = str - save;

        if (*str)
        {
            *str++ = '\0';
            if (psep->str[0] == ' ')
            {
                while (*str == ' ')
                {
                    str++;
                }
            }
            *nStr -= str - save;
        }
        else
        {
            *nStr = 0;
            str = NULL;
        }
    }
    else
    {
        char *p = strstr(str, psep->str);
        if (p)
        {
            *p = '\0';
            str = p + psep->n;
        }
        else
        {
            str = NULL;
        }
    }
    *sp = str;
    return save;
}

// split_token: Get next token from string as null-term string.  String is
// destructively modified.
//
char *split_token(char **sp, SEP *psep)
{
    char *str = *sp;
    char *save = str;

    if (!str)
    {
        *sp = NULL;
        return NULL;
    }
    if (psep->n == 1)
    {
        while (  *str
              && *str != psep->str[0])
        {
            str++;
        }
        if (*str)
        {
            *str++ = '\0';
            if (psep->str[0] == ' ')
            {
                while (*str == ' ')
                {
                    str++;
                }
            }
        }
        else
        {
            str = NULL;
        }
    }
    else
    {
        char *p = strstr(str, psep->str);
        if (p)
        {
            *p = '\0';
            str = p + psep->n;
        }
        else
        {
            str = NULL;
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

static int autodetect_list(char *ptrs[], int nitems)
{
    int could_be = ALL_LIST;
    for (int i = 0; i < nitems; i++)
    {
        char *p = ptrs[i];
        if (p[0] != NUMBER_TOKEN)
        {
            could_be &= ~DBREF_LIST;
        }
        if (  (could_be & DBREF_LIST)
           && !is_integer(p+1, NULL))
        {
            could_be &= ~(DBREF_LIST|NUMERIC_LIST|FLOAT_LIST);
        }
        if (  (could_be & FLOAT_LIST)
           && !is_real(p))
        {
            could_be &= ~(NUMERIC_LIST|FLOAT_LIST);
        }
        if (  (could_be & NUMERIC_LIST)
           && !is_integer(p, NULL))
        {
            could_be &= ~NUMERIC_LIST;
        }

        if (could_be == ASCII_LIST)
        {
            return ASCII_LIST;
        }
    }
    if (could_be & NUMERIC_LIST)
    {
        return NUMERIC_LIST;
    }
    else if (could_be & FLOAT_LIST)
    {
        return FLOAT_LIST;
    }
    else if (could_be & DBREF_LIST)
    {
        return DBREF_LIST;
    }
    return ASCII_LIST;
}

static int get_list_type
(
    char *fargs[],
    int nfargs,
    int type_pos,
    char *ptrs[],
    int nitems
)
{
    if (nfargs >= type_pos)
    {
        switch (mux_tolower(*fargs[type_pos - 1]))
        {
        case 'd':
            return DBREF_LIST;
        case 'n':
            return NUMERIC_LIST;
        case 'f':
            return FLOAT_LIST;
        case 'i':
            return CI_ASCII_LIST;
        case '\0':
            return autodetect_list(ptrs, nitems);
        default:
            return ASCII_LIST;
        }
    }
    return autodetect_list(ptrs, nitems);
}

int list2arr(char *arr[], int maxlen, char *list, SEP *psep)
{
    list = trim_space_sep(list, psep);
    if (list[0] == '\0')
    {
        return 0;
    }
    char *p = split_token(&list, psep);
    int i;
    for (i = 0; p && i < maxlen; i++, p = split_token(&list, psep))
    {
        arr[i] = p;
    }
    return i;
}

void arr2list(char *arr[], int alen, char *list, char **bufc, SEP *psep)
{
    int i;
    for (i = 0; i < alen-1; i++)
    {
        safe_str(arr[i], list, bufc);
        print_sep(psep, list, bufc);
    }
    if (alen)
    {
        safe_str(arr[i], list, bufc);
    }
}

static int dbnum(char *dbr)
{
    if (dbr[0] != '#' || dbr[1] == '\0')
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

bool nearby_or_control(dbref player, dbref thing)
{
    if (!Good_obj(player) || !Good_obj(thing))
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
    char *buff, char **bufc,
    dbref executor, dbref caller, dbref enactor,
    char *fargs[], int nfargs,
    char *cargs[], int ncargs,
    int sep_arg, SEP *sep, int dflags
)
{
    bool bSuccess = true;
    if (sep_arg <= nfargs)
    {
        // First, we decide whether to evalute fargs[sep_arg-1] or not.
        //
        char *tstr = fargs[sep_arg-1];
        int tlen = strlen(tstr);

        if (tlen <= 1)
        {
            dflags &= ~DELIM_EVAL;
        }
        if (dflags & DELIM_EVAL)
        {
            char *str = tstr;
            char *bp = tstr = alloc_lbuf("delim_check");
            mux_exec(tstr, &bp, executor, caller, enactor,
                     EV_EVAL | EV_FCHECK, &str, cargs, ncargs);
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
                safe_str("#-1 SEPARATOR IS TOO LARGE", buff, bufc);
                bSuccess = false;
            }
        }
        else
        {
            safe_str("#-1 SEPARATOR MUST BE ONE CHARACTER", buff, bufc);
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

int countwords(char *str, SEP *psep)
{
    int n;

    str = trim_space_sep(str, psep);
    if (!*str)
    {
        return 0;
    }
    for (n = 0; str; str = next_token(str, psep), n++)
    {
        ; // Nothing.
    }
    return n;
}

FUNCTION(fun_words)
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

    safe_ltoa(countwords(strip_ansi(fargs[0]), &sep), buff, bufc);
}

/* ---------------------------------------------------------------------------
 * fun_flags: Returns the flags on an object or an object's attribute.
 * Because @switch is case-insensitive, not quite as useful as it could be.
 */

FUNCTION(fun_flags)
{
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
            char xbuf[11];
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
            char *buff2 = decode_flags(executor, &(db[it].fs));
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

FUNCTION(fun_rand)
{
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
            safe_str("#-1 ARGUMENT MUST BE INTEGER", buff, bufc);
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
            safe_str("#-1 ARGUMENT MUST BE INTEGER", buff, bufc);
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
FUNCTION(fun_time)
{
    CLinearTimeAbsolute ltaNow;
    if (  nfargs == 0
       || mux_stricmp("utc", fargs[0]) != 0)
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
    char *temp = ltaNow.ReturnDateString(nPrecision);
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
FUNCTION(fun_secs)
{
    CLinearTimeAbsolute ltaNow;
    if (  nfargs == 0
       || mux_stricmp("local", fargs[0]) != 0)
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
FUNCTION(fun_convsecs)
{
    CLinearTimeAbsolute lta;
    lta.SetSecondsString(fargs[0]);
    if (  nfargs == 1
       || mux_stricmp("utc", fargs[1]) != 0)
    {
        lta.UTC2Local();
    }
    int nPrecision = 0;
    if (nfargs == 3)
    {
        nPrecision = mux_atol(fargs[2]);
    }
    char *temp = lta.ReturnDateString(nPrecision);
    safe_str(temp, buff, bufc);
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
FUNCTION(fun_convtime)
{
    CLinearTimeAbsolute lta;
    bool bZoneSpecified = false;
    if (  lta.SetString(fargs[0])
       || ParseDate(lta, fargs[0], &bZoneSpecified))
    {
        if (  !bZoneSpecified
           && (  nfargs == 1
              || mux_stricmp("utc", fargs[1]) != 0))
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
        safe_str("#-1 INVALID DATE", buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_starttime: What time did this system last reboot?
 */

FUNCTION(fun_starttime)
{
    char *temp = mudstate.start_time.ReturnDateString();
    safe_str(temp, buff, bufc);
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
const char *DayOfWeekStringLong[7] =
{
    "Sunday",
    "Monday",
    "Tuesday",
    "Wednesday",
    "Thursday",
    "Friday",
    "Saturday"
};
extern char *DayOfWeekString[];
extern const char *monthtab[];
const char *MonthTableLong[] =
{
    "January",
    "February",
    "March",
    "April",
    "May",
    "June",
    "July",
    "August",
    "September",
    "October",
    "November",
    "December"
};

const int Map24to12[24] =
{
    12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
    12, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
};
FUNCTION(fun_timefmt)
{
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

    char *q;
    char *p = fargs[0];
    while ((q = strchr(p, '$')) != NULL)
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
            safe_str(DayOfWeekString[ft.iDayOfWeek], buff, bufc);
            break;

        case 'A': // $A - Full weekday name
            safe_str(DayOfWeekStringLong[ft.iDayOfWeek], buff, bufc);
            break;

        case 'b': // $b - Abbreviated month name
        case 'h':
            safe_str(monthtab[ft.iMonth-1], buff, bufc);
            break;

        case 'B': // $B - Full month name
            safe_str(MonthTableLong[ft.iMonth-1], buff, bufc);
            break;

        case 'c': // $c - Date and time
            if (iOption == '#')
            {
                // Long version.
                //
                safe_tprintf_str(buff, bufc, "%s, %s %d, %d, %02d:%02d:%02d",
                    DayOfWeekStringLong[ft.iDayOfWeek],
                    MonthTableLong[ft.iMonth-1],
                    ft.iDayOfMonth, ft.iYear, ft.iHour, ft.iMinute,
                    ft.iSecond);
            }
            else
            {
                safe_str(lta.ReturnDateString(7), buff, bufc);
            }
            break;

        case 'C': // $C - The century (year/100).
            safe_tprintf_str(buff, bufc, "%d", ft.iYear / 100);
            break;

        case 'd': // $d - Day of Month as decimal number (1-31)
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d",
                ft.iDayOfMonth);
            break;

        case 'x': // $x - Date
            if (iOption == '#')
            {
                safe_tprintf_str(buff, bufc, "%s, %s %d, %d",
                    DayOfWeekStringLong[ft.iDayOfWeek],
                    MonthTableLong[ft.iMonth-1],
                    ft.iDayOfMonth, ft.iYear);
                break;
            }

            // FALL THROUGH

        case 'D': // $D - Equivalent to %m/%d/%y
            safe_tprintf_str(buff, bufc, "%02d/%02d/%02d", ft.iMonth,
                ft.iDayOfMonth, ft.iYear % 100);
            break;

        case 'e': // $e - Like $d, the day of the month as a decimal number,
                  // but a leading zero is replaced with a space.
            safe_tprintf_str(buff, bufc, "%2d", ft.iDayOfMonth);
            break;

        case 'F': // $F - The ISO 8601 formated date.
            safe_tprintf_str(buff, bufc, "%d-%02d-%02d", ft.iYear, ft.iMonth,
                ft.iDayOfMonth);
            break;

        case 'g': // $g - Like $G, two-digit ISO 8601 year.
            safe_tprintf_str(buff, bufc, "%02d", iYearISO%100);
            break;

        case 'G': // $G - The ISO 8601 year.
            safe_tprintf_str(buff, bufc, "%04d", iYearISO);
            break;

        case 'H': // $H - Hour of the 24-hour day.
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d", ft.iHour);
            break;

        case 'I': // $I - Hour of the 12-hour day
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d", iHour12);
            break;

        case 'j': // $j - Day of the year.
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%03d",
                ft.iDayOfYear);
            break;

        case 'k': // $k - Hour of the 24-hour day. Pad with a space.
            safe_tprintf_str(buff, bufc, "%2d", ft.iHour);
            break;

        case 'l': // $l - Hour of the 12-hour clock. Pad with a space.
            safe_tprintf_str(buff, bufc, "%2d", iHour12);
            break;

        case 'm': // $m - Month of the year
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d",
                ft.iMonth);
            break;

        case 'M': // $M - Minutes after the hour
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d",
                ft.iMinute);
            break;

        case 'n': // $n - Newline.
            safe_str("\r\n", buff, bufc);
            break;

        case 'p': // $p - AM/PM
            safe_str((ft.iHour < 12)?"AM":"PM", buff, bufc);
            break;

        case 'P': // $p - am/pm
            safe_str((ft.iHour < 12)?"am":"pm", buff, bufc);
            break;

        case 'r': // $r - Equivalent to $I:$M:$S $p
            safe_tprintf_str(buff, bufc,
                (iOption=='#')?"%d:%02d:%02d %s":"%02d:%02d:%02d %s",
                iHour12, ft.iMinute, ft.iSecond,
                (ft.iHour<12)?"AM":"PM");
            break;

        case 'R': // $R - Equivalent to $H:$M
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d:%02d":"%02d:%02d",
                ft.iHour, ft.iMinute);
            break;

        case 's': // $s - Number of seconds since the epoch.
            safe_str(ltaUTC.ReturnSecondsString(7), buff, bufc);
            break;

        case 'S': // $S - Seconds after the minute
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d",
                ft.iSecond);
            break;

        case 't':
            safe_chr('\t', buff, bufc);
            break;

        case 'X': // $X - Time
        case 'T': // $T - Equivalent to $H:$M:$S
            safe_tprintf_str(buff, bufc,
                (iOption=='#')?"%d:%02d:%02d":"%02d:%02d:%02d",
                ft.iHour, ft.iMinute, ft.iSecond);
            break;

        case 'u': // $u - Day of the Week, range 1 to 7. Monday = 1.
            safe_ltoa(iDayOfWeekMonday, buff, bufc);
            break;

        case 'U': // $U - Week of the year from 1st Sunday
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d",
                iWeekOfYearSunday);
            break;

        case 'V': // $V - ISO 8601:1988 week number.
            safe_tprintf_str(buff, bufc, "%02d", iWeekISO);
            break;

        case 'w': // $w - Day of the week. 0 = Sunday
            safe_ltoa(ft.iDayOfWeek, buff, bufc);
            break;

        case 'W': // $W - Week of the year from 1st Monday
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d",
                iWeekOfYearMonday);
            break;

        case 'y': // $y - Two-digit year
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%02d",
                ft.iYear % 100);
            break;

        case 'Y': // $Y - All-digit year
            safe_tprintf_str(buff, bufc, (iOption=='#')?"%d":"%04d",
                ft.iYear);
            break;

        case 'z': // $z - Time zone
            safe_tprintf_str(buff, bufc, "%c%02d%02d", iTZSign, iTZHour,
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

/*
 * ---------------------------------------------------------------------------
 * * fun_get, fun_get_eval: Get attribute from object.
 */
#define GET_GET     1
#define GET_XGET    2
#define GET_EVAL    4
#define GET_GEVAL   8

void get_handler(char *buff, char **bufc, dbref executor, char *fargs[], int key)
{
    bool bFreeBuffer = false;
    char *pRefAttrib = fargs[0];

    if (  key == GET_XGET
       || key == GET_EVAL)
    {
        pRefAttrib = alloc_lbuf("get_handler");
        char *bufp = pRefAttrib;
        safe_tprintf_str(pRefAttrib, &bufp, "%s/%s", fargs[0], fargs[1]);
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
    if (!See_attr(executor, thing, pattr))
    {
        safe_noperm(buff, bufc);
        return;
    }

    dbref aowner;
    int   aflags;
    size_t nLen = 0;
    char *atr_gotten = atr_pget_LEN(thing, pattr->number, &aowner, &aflags, &nLen);

    if (  key == GET_EVAL
       || key == GET_GEVAL)
    {
        char *str = atr_gotten;
        mux_exec(buff, bufc, thing, executor, executor, EV_FIGNORE | EV_EVAL,
                    &str, (char **)NULL, 0);
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

FUNCTION(fun_get)
{
    get_handler(buff, bufc, executor, fargs, GET_GET);
}

FUNCTION(fun_xget)
{
    if (!*fargs[0] || !*fargs[1])
    {
        return;
    }

    get_handler(buff, bufc, executor, fargs, GET_XGET);
}


FUNCTION(fun_get_eval)
{
    get_handler(buff, bufc, executor, fargs, GET_GEVAL);
}

FUNCTION(fun_subeval)
{
    char *str = fargs[0];
    mux_exec(buff, bufc, executor, caller, enactor,
             EV_EVAL|EV_NO_LOCATION|EV_NOFCHECK|EV_FIGNORE|EV_NO_COMPRESS,
             &str, (char **)NULL, 0);
}

FUNCTION(fun_eval)
{
    if (nfargs == 1)
    {
        char *str = fargs[0];
        mux_exec(buff, bufc, executor, caller, enactor, EV_EVAL, &str,
                 (char **)NULL, 0);
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

static void do_ufun(char *buff, char **bufc, dbref executor, dbref caller,
            dbref enactor,
            char *fargs[], int nfargs,
            char *cargs[], int ncargs,
            bool is_local)
{
    char *atext;
    dbref thing;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, buff, bufc))
    {
        return;
    }

    // If we're evaluating locally, preserve the global registers.
    //
    char **preserve = NULL;
    int *preserve_len = NULL;
    if (is_local)
    {
        preserve = PushPointers(MAX_GLOBAL_REGS);
        preserve_len = PushIntegers(MAX_GLOBAL_REGS);
        save_global_regs("fun_ulocal_save", preserve, preserve_len);
    }

    // Evaluate it using the rest of the passed function args.
    //
    char *str = atext;
    mux_exec(buff, bufc, thing, executor, enactor, EV_FCHECK | EV_EVAL,
        &str, &(fargs[1]), nfargs - 1);
    free_lbuf(atext);

    // If we're evaluating locally, restore the preserved registers.
    //
    if (is_local)
    {
        restore_global_regs("fun_ulocal_restore", preserve, preserve_len);
        PopIntegers(preserve_len, MAX_GLOBAL_REGS);
        PopPointers(preserve, MAX_GLOBAL_REGS);
    }
}

FUNCTION(fun_u)
{
    do_ufun(buff, bufc, executor, caller, enactor, fargs, nfargs, cargs,
            ncargs, false);
}

FUNCTION(fun_ulocal)
{
    do_ufun(buff, bufc, executor, caller, enactor, fargs, nfargs, cargs,
            ncargs, true);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_parent: Get parent of object.
 */

FUNCTION(fun_parent)
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
        safe_tprintf_str(buff, bufc, "#%d", Parent(it));
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_mid: mid(foobar,2,3) returns oba
 */

FUNCTION(fun_mid)
{
    // Initial checks for iPosition0 [0,LBUF_SIZE), nLength [0,LBUF_SIZE),
    // and iPosition1 [0,LBUF_SIZE).
    //
    int iPosition0 = mux_atol(fargs[1]);
    int nLength = mux_atol(fargs[2]);
    if (nLength < 0)
    {
        iPosition0 += nLength;
        nLength = -nLength;
    }

    if (iPosition0 < 0)
    {
        iPosition0 = 0;
    }
    else if (LBUF_SIZE-1 < iPosition0)
    {
        iPosition0 = LBUF_SIZE-1;
    }

    // At this point, iPosition0, nLength are reasonable numbers which may
    // -still- not refer to valid data in the string.
    //
    struct ANSI_In_Context aic;
    ANSI_String_In_Init(&aic, fargs[0], ANSI_ENDGOAL_NORMAL);
    int nDone;
    ANSI_String_Skip(&aic, iPosition0, &nDone);
    if (nDone < iPosition0)
    {
        return;
    }

    struct ANSI_Out_Context aoc;
    int nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    ANSI_String_Out_Init(&aoc, *bufc, nBufferAvailable, nLength, ANSI_ENDGOAL_NORMAL);
    ANSI_String_Copy(&aoc, &aic, nLength);
    int nSize = ANSI_String_Finalize(&aoc, &nDone);
    *bufc += nSize;
}

// ---------------------------------------------------------------------------
// fun_right: right(foobar,2) returns ar
// ---------------------------------------------------------------------------

FUNCTION(fun_right)
{
    // nLength on [0,LBUF_SIZE).
    //
    long   lLength = mux_atol(fargs[1]);
    size_t nLength;
    if (lLength < 0)
    {
        safe_range(buff, bufc);
        return;
    }
    else if (LBUF_SIZE-1 < lLength)
    {
        nLength = LBUF_SIZE-1;
    }
    else
    {
        nLength = lLength;
    }

    // iPosition1 on [0,LBUF_SIZE)
    //
    size_t iPosition1 = strlen(strip_ansi(fargs[0]));

    // iPosition0 on [0,LBUF_SIZE)
    //
    size_t iPosition0;
    if (iPosition1 <= nLength)
    {
        iPosition0 = 0;
    }
    else
    {
        iPosition0 = iPosition1 - nLength;
    }

    // At this point, iPosition0, nLength, and iPosition1 are reasonable
    // numbers which may -still- not refer to valid data in the string.
    //
    struct ANSI_In_Context aic;
    ANSI_String_In_Init(&aic, fargs[0], ANSI_ENDGOAL_NORMAL);
    int nDone;
    ANSI_String_Skip(&aic, iPosition0, &nDone);
    if ((size_t)nDone < iPosition0)
    {
        return;
    }

    struct ANSI_Out_Context aoc;
    int nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    ANSI_String_Out_Init(&aoc, *bufc, nBufferAvailable, nLength, ANSI_ENDGOAL_NORMAL);
    ANSI_String_Copy(&aoc, &aic, nLength);
    int nSize = ANSI_String_Finalize(&aoc, &nDone);
    *bufc += nSize;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_first: Returns first word in a string
 */

FUNCTION(fun_first)
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

    char *s = trim_space_sep(fargs[0], &sep);
    char *first = split_token(&s, &sep);
    if (first)
    {
        safe_str(first, buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_rest: Returns all but the first word in a string
 */


FUNCTION(fun_rest)
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

    char *s = trim_space_sep(fargs[0], &sep);
    split_token(&s, &sep);
    if (s)
    {
        safe_str(s, buff, bufc);
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_v: Function form of %-substitution
 */

FUNCTION(fun_v)
{
    dbref aowner;
    int aflags;
    char *sbuf, *sbufc, *tbuf, *str;
    ATTR *ap;

    tbuf = fargs[0];
    if (mux_AttrNameInitialSet(tbuf[0]) && tbuf[1])
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
        return;
    }

    // Not an attribute, process as %<arg>
    //
    sbuf = alloc_sbuf("fun_v");
    sbufc = sbuf;
    safe_sb_chr('%', sbuf, &sbufc);
    safe_sb_str(fargs[0], sbuf, &sbufc);
    *sbufc = '\0';
    str = sbuf;
    mux_exec(buff, bufc, executor, caller, enactor, EV_EVAL|EV_FIGNORE, &str,
             cargs, ncargs);
    free_sbuf(sbuf);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_s: Force substitution to occur.
 */

FUNCTION(fun_s)
{
    char *str = fargs[0];
    mux_exec(buff, bufc, executor, caller, enactor, EV_FIGNORE | EV_EVAL, &str,
             cargs, ncargs);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_con: Returns first item in contents list of object/room
 */

FUNCTION(fun_con)
{
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
            safe_tprintf_str(buff, bufc, "#%d", Contents(it));
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

FUNCTION(fun_exit)
{
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
                safe_tprintf_str(buff, bufc, "#%d", exit);
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

FUNCTION(fun_next)
{
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
                safe_tprintf_str(buff, bufc, "#%d", Next(it));
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
                        safe_tprintf_str(buff, bufc, "#%d", exit);
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

FUNCTION(fun_loc)
{
    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
    }
    else if (locatable(executor, it, enactor))
    {
        safe_tprintf_str(buff, bufc, "#%d", Location(it));
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

FUNCTION(fun_where)
{
    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
    }
    else if (locatable(executor, it, enactor))
    {
        safe_tprintf_str(buff, bufc, "#%d", where_is(it));
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

FUNCTION(fun_rloc)
{
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
        safe_tprintf_str(buff, bufc, "#%d", it);
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

FUNCTION(fun_room)
{
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
                safe_tprintf_str(buff, bufc, "#%d", it);
                return;
            }
        }
        safe_nothing(buff, bufc);
    }
    else if (isRoom(it))
    {
        safe_tprintf_str(buff, bufc, "#%d", it);
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

FUNCTION(fun_owner)
{
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
    safe_tprintf_str(buff, bufc, "#%d", it);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_controls: Does x control y?
 */

FUNCTION(fun_controls)
{
    dbref x = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(x))
    {
        safe_match_result(x, buff, bufc);
        safe_str(" (ARG1)", buff, bufc);
        return;
    }
    dbref y = match_thing_quiet(executor, fargs[1]);
    if (!Good_obj(y))
    {
        safe_match_result(x, buff, bufc);
        safe_str(" (ARG2)", buff, bufc);
        return;
    }
    safe_bool(Controls(x,y), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_fullname: Return the fullname of an object (good for exits)
 */

FUNCTION(fun_fullname)
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
           && !isPlayer(it))
        {
            safe_str("#-1 TOO FAR AWAY TO SEE", buff, bufc);
            return;
        }
    }
    safe_str(Name(it), buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_name: Return the name of an object
 */

FUNCTION(fun_name)
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
            safe_str("#-1 TOO FAR AWAY TO SEE", buff, bufc);
            return;
        }
    }
    char *temp = *bufc;
    safe_str(Name(it), buff, bufc);
    if (isExit(it))
    {
        char *s;
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

/*
 * ---------------------------------------------------------------------------
 * * fun_match, fun_strmatch: Match arg2 against each word of arg1 returning
 * * index of first match, or against the whole string.
 */

FUNCTION(fun_match)
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
    char *s = trim_space_sep(fargs[0], &sep);
    do {
        char *r = split_token(&s, &sep);
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

FUNCTION(fun_strmatch)
{
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

FUNCTION(fun_extract)
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

    int start = mux_atol(fargs[1]);
    int len = mux_atol(fargs[2]);

    if (  start < 1
       || len < 1)
    {
        return;
    }

    // Skip to the start of the string to save.
    //
    start--;
    char *s = trim_space_sep(fargs[0], &sep);
    while (  start
          && s)
    {
        s = next_token(s, &sep);
        start--;
    }

    // If we ran of the end of the string, return nothing.
    //
    if (!s || !*s)
    {
        return;
    }

    // Count off the words in the string to save.
    //
    bool bFirst = true;
    while (  len
          && s)
    {
        char *t = split_token(&s, &sep);
        if (!bFirst)
        {
            print_sep(&osep, buff, bufc);
        }
        else
        {
            bFirst = false;
        }
        safe_str(t, buff, bufc);
        len--;
    }
}

// xlate() controls the subtle definition of a softcode boolean.
//
bool xlate(char *arg)
{
    const char *p = arg;
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

FUNCTION(fun_index)
{
    int start, end;
    char c, *s, *p;

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
        if ((s = strchr(s, c)) != NULL)
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
        if ((p = strchr(p, c)) != NULL)
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


FUNCTION(fun_cat)
{
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

FUNCTION(fun_version)
{
    safe_str(mudstate.version, buff, bufc);
}

FUNCTION(fun_strlen)
{
    size_t n = 0;
    if (nfargs >= 1)
    {
        strip_ansi(fargs[0], &n);
    }
    safe_ltoa(n, buff, bufc);
}

FUNCTION(fun_strmem)
{
   size_t n = 0;
   if (nfargs >= 1)
   {
       n = strlen(fargs[0]);
   }
   safe_ltoa(n, buff, bufc);
}

FUNCTION(fun_num)
{
    safe_tprintf_str(buff, bufc, "#%d", match_thing_quiet(executor, fargs[0]));
}

void internalPlayerFind
(
    char* buff,
    char** bufc,
    dbref player,
    char* name,
    int bVerifyPlayer
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
        char *nptr = name;
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


FUNCTION(fun_pmatch)
{
    internalPlayerFind(buff, bufc, executor, fargs[0], true);
}

FUNCTION(fun_pfind)
{
    internalPlayerFind(buff, bufc, executor, fargs[0], false);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_comp: string compare.
 */

FUNCTION(fun_comp)
{
    int x;

    x = strcmp(fargs[0], fargs[1]);
    if (x < 0)
    {
        safe_str("-1", buff, bufc);
    }
    else
    {
        safe_bool((x != 0), buff, bufc);
    }
}

#if defined(WOD_REALMS) || defined(REALITY_LVLS)
FUNCTION(fun_cansee)
{
    dbref looker = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(looker))
    {
        safe_match_result(looker, buff, bufc);
        safe_str(" (LOOKER)", buff, bufc);
        return;
    }
    dbref lookee = match_thing_quiet(executor, fargs[1]);
    if (!Good_obj(lookee))
    {
        safe_match_result(looker, buff, bufc);
        safe_str(" (LOOKEE)", buff, bufc);
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
#endif /* REALITY_LVLS */
    }
    safe_bool(bResult, buff, bufc);
}
#endif

/*
 * ---------------------------------------------------------------------------
 * * fun_lcon: Return a list of contents.
 */

FUNCTION(fun_lcon)
{
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

FUNCTION(fun_lexits)
{
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
    char *p;
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
            switch(mux_toupper(*p))
            {
            case 'A':
                find_ex = find_th = find_pl = find_rm = true;
                break;

            case 'E':
                find_ex = true;
                break;

            case 'T':
                find_th = true;
                break;

            case 'P':
                find_pl = true;
                break;

            case 'R':
                find_rm = true;
                break;

            default:
                safe_str("#-1 INVALID TYPE", buff, bufc);
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
        notify(executor, tprintf("You don't have enough %s.",
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

FUNCTION(fun_home)
{
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
        safe_tprintf_str(buff, bufc, "#%d", Home(it));
    }
    else if (Has_dropto(it))
    {
        safe_tprintf_str(buff, bufc, "#%d", Dropto(it));
    }
    else if (isExit(it))
    {
        safe_tprintf_str(buff, bufc, "#%d", where_is(it));
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

FUNCTION(fun_money)
{
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

FUNCTION(fun_pos)
{
    // Strip ANSI from pattern and save.
    //
    // Note: We need to save it because the next call to strip_ansi()
    // will overwrite the prior result.  Also, we save the pattern
    // instead of the source because the the pattern will tend to be
    // smaller (i.e., on average, fewer bytes to move).
    //
    size_t nPat = 0;
    char aPatBuf[LBUF_SIZE];
    char *pPatStrip = strip_ansi(fargs[0], &nPat);
    memcpy(aPatBuf, pPatStrip, nPat);

    // Strip ANSI from source.
    //
    size_t nSrc;
    char *pSrc = strip_ansi(fargs[1], &nSrc);

    // Search for pattern string inside source string.
    //
    int i = -1;
    if (nPat == 1)
    {
        // We can optimize the single-character case.
        //
        char *p = strchr(pSrc, aPatBuf[0]);
        if (p)
        {
            i = p - pSrc + 1;
        }
    }
    else if (nPat > 1)
    {
        // We have a multi-byte pattern.
        //
        i = BMH_StringSearch(nPat, aPatBuf, nSrc, pSrc)+1;
    }

    if (i > 0)
    {
        safe_ltoa(i, buff, bufc);
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

FUNCTION(fun_lpos)
{
    if (*fargs[0] == '\0')
    {
        return;
    }

    char c = *fargs[1];
    if (!c)
    {
        c = ' ';
    }

    int i;
    char *bb_p = *bufc;
    char *s = strip_ansi(fargs[0]);
    for (i = 0; *s; i++, s++)
    {
        if (*s == c)
        {
            if (*bufc != bb_p)
            {
                safe_chr(' ', buff, bufc);
            }
            safe_ltoa(i, buff, bufc);
        }
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

static void do_itemfuns(char *buff, char **bufc, char *str, int el,
                        char *word, SEP *psep, int flag)
{
    int ct;
    char *sptr, *iptr, *eptr;
    int slen = 0, ilen = 0, elen = 0;
    bool overrun;
    char nullb;

    // If passed a null string return an empty string, except that we
    // are allowed to append to a null string.
    //
    if (  (  !str
          || !*str)
       && (  flag != IF_INSERT
          || el != 1))
    {
        return;
    }
    int nStr = strlen(str);

    // We can't fiddle with anything before the first position.
    //
    if (el < 1)
    {
        safe_copy_buf(str, nStr, buff, bufc);
        return;
    }

    // Split the list up into 'before', 'target', and 'after' chunks
    // pointed to by sptr, iptr, and eptr respectively.
    //
    nullb = '\0';
    if (el == 1)
    {
        // No 'before' portion, just split off element 1
        //
        sptr = NULL;
        slen = 0;
        if (!str || !*str)
        {
            eptr = NULL;
            iptr = NULL;
        }
        else
        {
            eptr = trim_space_sep_LEN(str, nStr, psep, &elen);
            iptr = split_token_LEN(&eptr, &elen, psep, &ilen);
        }
    }
    else
    {
        // Break off 'before' portion.
        //
        sptr = eptr = trim_space_sep_LEN(str, nStr, psep, &elen);
        overrun = true;
        for (  ct = el;
               ct > 2 && eptr;
               eptr = next_token_LEN(eptr, &elen, psep), ct--)
        {
            // Nothing
        }
        if (eptr)
        {
            // Note: We are using (iptr,ilen) temporarily. It
            // doesn't represent the 'target' word, but the
            // the last token in the 'before' portion.
            //
            overrun = false;
            iptr = split_token_LEN(&eptr, &elen, psep, &ilen);
            slen = (iptr - sptr) + ilen;
        }

        // If we didn't make it to the target element, just return
        // the string. Insert is allowed to continue if we are exactly
        // at the end of the string, but replace and delete are not.
        //
        if (!(  eptr
             || (  flag == IF_INSERT
                && !overrun)))
        {
            safe_copy_buf(str, nStr, buff, bufc);
            return;
        }

        // Split the 'target' word from the 'after' portion.
        //
        if (eptr)
        {
            iptr = split_token_LEN(&eptr, &elen, psep, &ilen);
        }
        else
        {
            iptr = NULL;
            ilen = 0;
        }
    }

    switch (flag)
    {
    case IF_DELETE:

        // deletion
        //
        if (sptr)
        {
            safe_copy_buf(sptr, slen, buff, bufc);
            if (eptr)
            {
                safe_chr(psep->str[0], buff, bufc);
            }
        }
        if (eptr)
        {
            safe_copy_buf(eptr, elen, buff, bufc);
        }
        break;

    case IF_REPLACE:

        // replacing.
        //
        if (sptr)
        {
            safe_copy_buf(sptr, slen, buff, bufc);
            safe_chr(psep->str[0], buff, bufc);
        }
        safe_str(word, buff, bufc);
        if (eptr)
        {
            safe_chr(psep->str[0], buff, bufc);
            safe_copy_buf(eptr, elen, buff, bufc);
        }
        break;

    case IF_INSERT:

        // Insertion.
        //
        if (sptr)
        {
            safe_copy_buf(sptr, slen, buff, bufc);
            safe_chr(psep->str[0], buff, bufc);
        }
        safe_str(word, buff, bufc);
        if (iptr)
        {
            safe_chr(psep->str[0], buff, bufc);
            safe_copy_buf(iptr, ilen, buff, bufc);
        }
        if (eptr)
        {
            safe_chr(psep->str[0], buff, bufc);
            safe_copy_buf(eptr, elen, buff, bufc);
        }
        break;
    }
}


FUNCTION(fun_ldelete)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    // Delete a word at position X of a list.
    //
    do_itemfuns(buff, bufc, fargs[0], mux_atol(fargs[1]), NULL, &sep, IF_DELETE);
}

FUNCTION(fun_replace)
{
    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    // Replace a word at position X of a list.
    //
    do_itemfuns(buff, bufc, fargs[0], mux_atol(fargs[1]), fargs[2], &sep, IF_REPLACE);
}

FUNCTION(fun_insert)
{
    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    // Insert a word at position X of a list.
    //
    do_itemfuns(buff, bufc, fargs[0], mux_atol(fargs[1]), fargs[2], &sep, IF_INSERT);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_remove: Remove a word from a string
 */

FUNCTION(fun_remove)
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

    char *s, *sp, *word;
    bool first, found;

    if (strstr(fargs[1], sep.str))
    {
        safe_str("#-1 CAN ONLY REMOVE ONE ELEMENT", buff, bufc);
        return;
    }
    s = fargs[0];
    word = fargs[1];

    // Walk through the string copying words until (if ever) we get to
    // one that matches the target word.
    //
    sp = s;
    found = false;
    first = true;
    while (s)
    {
        sp = split_token(&s, &sep);
        if (  found
           || strcmp(sp, word) != 0)
        {
            if (!first)
            {
                print_sep(&osep, buff, bufc);
            }
            else
            {
                first = false;
            }
            safe_str(sp, buff, bufc);
        }
        else
        {
            found = true;
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_member: Is a word in a string
 */

FUNCTION(fun_member)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    int wcount;
    char *r, *s;

    wcount = 1;
    s = trim_space_sep(fargs[0], &sep);
    do
    {
        r = split_token(&s, &sep);
        if (!strcmp(fargs[1], r))
        {
            safe_ltoa(wcount, buff, bufc);
            return;
        }
        wcount++;
    } while (s);
    safe_chr('0', buff, bufc);
}

// fun_secure: This function replaces any character in the set
// '%$\[](){},;' with a space. It handles ANSI by not replacing
// the '[' character within an ANSI sequence.
//
FUNCTION(fun_secure)
{
    char *pString = fargs[0];
    int nString = strlen(pString);

    while (nString)
    {
        int nTokenLength0;
        int nTokenLength1;
        int iType = ANSI_lex(nString, pString, &nTokenLength0, &nTokenLength1);

        if (iType == TOKEN_TEXT_ANSI)
        {
            // Process TEXT portion (pString, nTokenLength0).
            //
            nString -= nTokenLength0;
            while (nTokenLength0--)
            {
                if (mux_issecure(*pString))
                {
                    safe_chr(' ', buff, bufc);
                }
                else
                {
                    safe_chr(*pString, buff, bufc);
                }
                pString++;
            }
            nTokenLength0 = nTokenLength1;
        }

        if (nTokenLength0)
        {
            // Process ANSI portion (pString, nTokenLength0).
            //
            safe_copy_buf(pString, nTokenLength0, buff, bufc);
            pString += nTokenLength0;
            nString -= nTokenLength0;
        }
    }
}

// fun_escape: This function prepends a '\' to the beginning of a
// string and before any character which occurs in the set '%\[]{};,()^$'.
// It handles ANSI by not treating the '[' character within an ANSI
// sequence as a special character.
//
FUNCTION(fun_escape)
{
    char *pString = fargs[0];
    int nString = strlen(pString);

    while (nString)
    {
        int nTokenLength0;
        int nTokenLength1;
        int iType = ANSI_lex(nString, pString, &nTokenLength0, &nTokenLength1);

        if (iType == TOKEN_TEXT_ANSI)
        {
            // Process TEXT portion (pString, nTokenLength0).
            //
            nString -= nTokenLength0;
            while (nTokenLength0--)
            {
                if (  mux_isescape(*pString)
                   || pString == fargs[0])
                {
                    safe_chr('\\', buff, bufc);
                }
                safe_chr(*pString, buff, bufc);
                pString++;
            }
            nTokenLength0 = nTokenLength1;
        }

        if (nTokenLength0)
        {
            // Process ANSI portion (pString, nTokenLength0).
            //
            safe_copy_buf(pString, nTokenLength0, buff, bufc);
            pString += nTokenLength0;
            nString -= nTokenLength0;
        }
    }
}

/*
 * Take a character position and return which word that char is in.
 * * wordpos(<string>, <charpos>)
 */
FUNCTION(fun_wordpos)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    unsigned int charpos = mux_atol(fargs[1]);
    char *cp = fargs[0];
    size_t ncp = strlen(cp);
    if (  charpos > 0
       && charpos <= ncp)
    {
        int ncp_trimmed;
        char *tp = &(cp[charpos - 1]);
        cp = trim_space_sep_LEN(cp, ncp, &sep, &ncp_trimmed);
        char *xp = split_token(&cp, &sep);

        int i;
        for (i = 1; xp; i++)
        {
            if (tp < xp + strlen(xp))
            {
                break;
            }
            xp = split_token(&cp, &sep);
        }
        safe_ltoa(i, buff, bufc);
        return;
    }
    safe_nothing(buff, bufc);
}

FUNCTION(fun_type)
{
    dbref it = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_match_result(it, buff, bufc);
        return;
    }
    switch (Typeof(it))
    {
    case TYPE_ROOM:
        safe_str("ROOM", buff, bufc);
        break;
    case TYPE_EXIT:
        safe_str("EXIT", buff, bufc);
        break;
    case TYPE_PLAYER:
        safe_str("PLAYER", buff, bufc);
        break;
    case TYPE_THING:
        safe_str("THING", buff, bufc);
        break;
    default:
        safe_str("#-1 ILLEGAL TYPE", buff, bufc);
    }
}

typedef struct
{
    const char *pName;
    int  iMask;
} ATR_HAS_FLAG_ENTRY;

ATR_HAS_FLAG_ENTRY atr_has_flag_table[] =
{
    { "dark",       AF_DARK    },
    { "wizard",     AF_WIZARD  },
    { "hidden",     AF_MDARK   },
    { "html",       AF_HTML    },
    { "locked",     AF_LOCK    },
    { "no_command", AF_NOPROG  },
    { "no_parse",   AF_NOPARSE },
    { "regexp",     AF_REGEXP  },
    { "god",        AF_GOD     },
    { "visual",     AF_VISUAL  },
    { "no_inherit", AF_PRIVATE },
    { "const",      AF_CONST   },
    { NULL,         0          }
};

static bool atr_has_flag
(
    dbref player,
    dbref thing,
    ATTR* pattr,
    dbref aowner,
    int   aflags,
    const char *flagname
)
{
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

FUNCTION(fun_hasflag)
{
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

FUNCTION(fun_haspower)
{
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
FUNCTION(fun_hasrxlevel)
{
    dbref player;
    dbref it;
    RLEVEL rl;

    it = match_thing(player, fargs[0]);
    if (!Good_obj(it)) {
        safe_str("#-1 NOT FOUND", buff, bufc);
        return;
    }
    rl = find_rlevel(fargs[1]);
    if (!rl) {
        safe_str("#-1 INVALID RLEVEL", buff, bufc);
        return;
    }
    if (Examinable(player, it)) {
        if ((RxLevel(it) & rl) == rl) {
            safe_chr('1', buff, bufc);
        } else {
            safe_chr('0', buff, bufc);
        }
   } else {
        safe_str("#-1 PERMISSION DENIED", buff, bufc);
   }
}

FUNCTION(fun_hastxlevel)
{
    dbref it;
    RLEVEL rl;

    it = match_thing(executor, fargs[0]);
    if (!Good_obj(it)) {
        safe_str("#-1 NOT FOUND", buff, bufc);
        return;
    }
    rl = find_rlevel(fargs[1]);
    if (!rl) {
        safe_str("#-1 INVALID RLEVEL", buff, bufc);
        return;
    }
    if (Examinable(executor, it)) {
        if ((TxLevel(it) & rl) == rl) {
            safe_chr('1', buff, bufc);
        } else {
             safe_chr('0', buff, bufc);
        }
   } else {
        safe_str("#-1 PERMISSION DENIED", buff, bufc);
   }
}

FUNCTION(fun_listrlevels)
{
   int i, add_space, cmp_x, cmp_y, cmp_z;

   cmp_x = sizeof(mudconf.reality_level);
   cmp_y = sizeof(mudconf.reality_level[0]);
   if ( cmp_y == 0 )
      cmp_z = 0;
   else
      cmp_z = cmp_x / cmp_y;
   if ( mudconf.no_levels < 1 ) {
      safe_str("#-1 NO REALITY LEVELS DEFINED", buff, bufc);
   } else {
      for (add_space = i = 0; (i < mudconf.no_levels) && (i < cmp_z); ++i) {
         if(add_space)
            safe_chr(' ', buff, bufc);
         safe_str(mudconf.reality_level[i].name, buff, bufc);
         add_space = 1;
      }
   }
}

FUNCTION(fun_rxlevel)
{
    dbref it;
    char levelbuff[2048];
    int i;
    RLEVEL lev;

    it = match_thing(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_str("#-1 NOT FOUND", buff, bufc);
        return;
    }
    if (Examinable(executor, it))
    {
        lev = RxLevel(it);
        levelbuff[0]='\0';
        for(i = 0; i < mudconf.no_levels; ++i)
            if((lev & mudconf.reality_level[i].value) == mudconf.reality_level[i].value)
            {
                strcat(levelbuff, mudconf.reality_level[i].name);
                strcat(levelbuff, " ");
            }
        safe_tprintf_str(buff, bufc, "%s", levelbuff);
    }
    else
        safe_str("#-1 PERMISSION DENIED", buff, bufc);
}

FUNCTION(fun_txlevel)
{
    dbref it;
    char levelbuff[2048];
    int i;
    RLEVEL lev;

    it = match_thing(executor, fargs[0]);
    if (!Good_obj(it))
    {
        safe_str("#-1 NOT FOUND", buff, bufc);
        return;
    }
    if (Examinable(executor, it))
    {
        lev = TxLevel(it);
        levelbuff[0]='\0';
        for(i = 0; i < mudconf.no_levels; ++i)
            if((lev & mudconf.reality_level[i].value) == mudconf.reality_level[i].value)
            {
                strcat(levelbuff, mudconf.reality_level[i].name);
                strcat(levelbuff, " ");
            }
        safe_tprintf_str(buff, bufc, "%s", levelbuff);
    }
    else
        safe_str("#-1 PERMISSION DENIED", buff, bufc);
}
#endif /* REALITY_LVLS */

FUNCTION(fun_powers)
{
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
        char *buf = powers_list(executor, it);
        safe_str(buf, buff, bufc);
        free_lbuf(buf);
    }
    else
    {
        safe_noperm(buff, bufc);
    }
}

FUNCTION(fun_delete)
{
    char *s = fargs[0];
    int iStart = mux_atol(fargs[1]);
    int nChars = mux_atol(fargs[2]);
    int nLen = strlen(s);

    int iEnd;
    if (0 <= nChars)
    {
        iEnd = iStart + nChars;
    }
    else
    {
        iEnd = iStart;
        iStart = iEnd + nChars;
    }

    // Are we deleting anything at all?
    //
    if (  iEnd <= 0
       || nLen <= iStart)
    {
        if (nLen)
        {
            safe_copy_buf(s, nLen, buff, bufc);
        }
        return;
    }

    if (iStart < 0) iStart = 0;
    if (nLen < iEnd) iEnd = nLen;

    // ASSERT: Now [iStart,iEnd) exist somewhere within the the string
    // [s,nLen).
    //
    if (iStart)
    {
        safe_copy_buf(s, iStart, buff, bufc);
    }
    if (iEnd < nLen)
    {
        safe_copy_buf(s + iEnd, nLen - iEnd, buff, bufc);
    }
}

FUNCTION(fun_lock)
{
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
    char *tbuf = atr_get(it, pattr->number, &aowner, &aflags);
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

FUNCTION(fun_elock)
{
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
        char *tbuf = atr_get(it, pattr->number, &aowner, &aflags);
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
        safe_str("#-1 TOO FAR AWAY", buff, bufc);
    }
}

/* ---------------------------------------------------------------------------
 * fun_lwho: Return list of connected users.
 */

FUNCTION(fun_lwho)
{
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

FUNCTION(fun_lports)
{
    make_port_ulist(executor, buff, bufc);
}

/* ---------------------------------------------------------------------------
 * fun_nearby: Return whether or not obj1 is near obj2.
 */

FUNCTION(fun_nearby)
{
    dbref obj1 = match_thing_quiet(executor, fargs[0]);
    if (!Good_obj(obj1))
    {
        safe_match_result(obj1, buff, bufc);
        safe_str(" (ARG1)", buff, bufc);
        return;
    }
    dbref obj2 = match_thing_quiet(executor, fargs[1]);
    if (!Good_obj(obj2))
    {
        safe_match_result(obj2, buff, bufc);
        safe_str(" (ARG2)", buff, bufc);
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

static void process_sex(dbref player, char *what, const char *token, char *buff, char **bufc)
{
    dbref it = match_thing_quiet(player, strip_ansi(what));
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
        char *str = (char *)token;
        mux_exec(buff, bufc, it, it, it, EV_EVAL, &str, (char **)NULL, 0);
    }
}

FUNCTION(fun_obj)
{
    process_sex(executor, fargs[0], "%o", buff, bufc);
}

FUNCTION(fun_poss)
{
    process_sex(executor, fargs[0], "%p", buff, bufc);
}

FUNCTION(fun_subj)
{
    process_sex(executor, fargs[0], "%s", buff, bufc);
}

FUNCTION(fun_aposs)
{
    process_sex(executor, fargs[0], "%a", buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_mudname: Return the name of the mud.
 */

FUNCTION(fun_mudname)
{
    safe_str(mudconf.mud_name, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_connrecord: Return the record number of connected players.
// ---------------------------------------------------------------------------

FUNCTION(fun_connrecord)
{
    safe_ltoa(mudstate.record_players, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_fcount: Return the current function invocation counter.
// ---------------------------------------------------------------------------

FUNCTION(fun_fcount)
{
    safe_ltoa(mudstate.func_invk_ctr, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_fdepth: Return the current function nesting depth.
// ---------------------------------------------------------------------------

FUNCTION(fun_fdepth)
{
    safe_ltoa(mudstate.func_nest_lev, buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_ctime: Return the value of an object's CREATED attribute.
// ---------------------------------------------------------------------------

FUNCTION(fun_ctime)
{
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

FUNCTION(fun_mtime)
{
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
FUNCTION(fun_moniker)
{
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

void ANSI_TransformTextWithTable
(
    char *buff,
    char **bufc,
    char *pString,
    const unsigned char xfrmTable[256])
{
    int   nString = strlen(pString);
    char *pBuffer = *bufc;
    int   nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    while (nString)
    {
        int nTokenLength0;
        int nTokenLength1;
        int iType = ANSI_lex(nString, pString, &nTokenLength0, &nTokenLength1);

        if (iType == TOKEN_TEXT_ANSI)
        {
            // Determine how much to move.
            //
            int nMove = nTokenLength0;
            if (nMove > nBufferAvailable)
            {
                nMove = nBufferAvailable;
            }
            nBufferAvailable -= nMove;

            // Update pointers and counts.
            //
            char *p = pString;
            nString -= nTokenLength0;
            pString += nTokenLength0;

            // Transform and Move text.
            //
            while (nMove--)
            {
                *pBuffer++ = xfrmTable[(unsigned char)*p++];
            }

            // Determine whether to move the ANSI part.
            //
            if (nTokenLength1)
            {
                if (nTokenLength1 <= nBufferAvailable)
                {
                    memcpy(pBuffer, pString, nTokenLength1);
                    pBuffer += nTokenLength1;
                    nBufferAvailable -= nTokenLength1;
                }
                nString -= nTokenLength1;
                pString += nTokenLength1;
            }
        }
        else
        {
            // TOKEN_ANSI
            //
            // Determine whether to move the ANSI part.
            //
            if (nTokenLength0 <= nBufferAvailable)
            {
                memcpy(pBuffer, pString, nTokenLength0);
                pBuffer += nTokenLength0;
                nBufferAvailable -= nTokenLength0;
            }
            nString -= nTokenLength0;
            pString += nTokenLength0;
        }
    }
    *pBuffer = '\0';
    *bufc = pBuffer;
}

/*
 * ---------------------------------------------------------------------------
 * * fun_lcstr, fun_ucstr, fun_capstr: Lowercase, uppercase, or capitalize str.
 */

FUNCTION(fun_lcstr)
{
    ANSI_TransformTextWithTable(buff, bufc, fargs[0], mux_tolower);
}

FUNCTION(fun_ucstr)
{
    ANSI_TransformTextWithTable(buff, bufc, fargs[0], mux_toupper);
}

FUNCTION(fun_capstr)
{
    char *pString = fargs[0];
    char *pBuffer = *bufc;
    int nString = strlen(pString);
    nString = safe_copy_buf(pString, nString, buff, bufc);

    // Find the first text character in (nString, pBuffer).
    //
    while (nString)
    {
        int nTokenLength0;
        int nTokenLength1;
        int iType = ANSI_lex(nString, pBuffer, &nTokenLength0, &nTokenLength1);
        if (iType == TOKEN_TEXT_ANSI)
        {
            *pBuffer = mux_toupper(*pBuffer);
            return;
        }
        else
        {
            // iType == TOKEN_ANSI
            //
            pBuffer += nTokenLength0;
            nString -= nTokenLength0;
        }
    }
}

/*
 * ---------------------------------------------------------------------------
 * * fun_lnum: Return a list of numbers.
 */
FUNCTION(fun_lnum)
{
    SEP sep;
    if (  nfargs == 0
       || !OPTIONAL_DELIM(3, sep, DELIM_NULL|DELIM_CRLF|DELIM_STRING))
    {
        return;
    }

    int bot = 0, top;
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
    }

    int i;
    if (bot == top)
    {
        safe_ltoa(bot, buff, bufc);
    }
    else if (bot < top)
    {
        safe_ltoa(bot, buff, bufc);
        for (i = bot+1; i <= top; i++)
        {
            print_sep(&sep, buff, bufc);
            char *p = *bufc;
            safe_ltoa(i, buff, bufc);
            if (p == *bufc) return;
        }
    }
    else if (top < bot)
    {
        safe_ltoa(bot, buff, bufc);
        for (i = bot-1; i >= top; i--)
        {
            print_sep(&sep, buff, bufc);
            char *p = *bufc;
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

void lattr_handler(char *buff, char **bufc, dbref executor, char *fargs[],
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

FUNCTION(fun_lattr)
{
    lattr_handler(buff, bufc, executor, fargs, false);
}

FUNCTION(fun_lattrp)
{
    lattr_handler(buff, bufc, executor, fargs, true);
}
// ---------------------------------------------------------------------------
// fun_attrcnt: Return number of attributes I can see on the object.
// ---------------------------------------------------------------------------

FUNCTION(fun_attrcnt)
{
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

static void mux_memrevcpy(char *dest, char *src, unsigned int n)
{
    dest += n - 1;
    while (n--)
    {
        *dest-- = *src++;
    }
}

typedef void MEMXFORM(char *dest, char *src, unsigned int n);
void ANSI_TransformTextReverseWithFunction
(
    char *buff,
    char **bufc,
    char *pString,
    MEMXFORM *pfMemXForm
)
{
    // Bounds checking.
    //
    unsigned int nString = strlen(pString);
    char *pBuffer = *bufc;
    unsigned int nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
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
    int  nANSI = 0;
    char *pANSI = pString;
    pBuffer += nString;
    *bufc = pBuffer;
    **bufc = '\0';
    while (nString)
    {
        int nTokenLength0;
        int nTokenLength1;
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

FUNCTION(fun_reverse)
{
    ANSI_TransformTextReverseWithFunction(buff, bufc, fargs[0], mux_memrevcpy);
}

char ReverseWordsInText_Seperator;
static void ReverseWordsInText(char *dest, char *src, unsigned int n)
{
    char chSave = src[n];
    src[n] = '\0';
    dest += n;
    while (n)
    {
        char *pWord = strchr(src, ReverseWordsInText_Seperator);
        int nLen;
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

FUNCTION(fun_revwords)
{
    // If we are passed an empty arglist return a null string.
    //
    if (nfargs == 0)
    {
        return;
    }
    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT))
    {
        return;
    }
    ReverseWordsInText_Seperator = sep.str[0];
    ANSI_TransformTextReverseWithFunction(buff, bufc, fargs[0], ReverseWordsInText);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_after, fun_before: Return substring after or before a specified string.
 */

FUNCTION(fun_after)
{
    char *mp;
    int mlen;

    // Sanity-check arg1 and arg2.
    //
    char *bp = fargs[0];
    if (nfargs > 1)
    {
        mp = fargs[1];
        mlen = strlen(mp);
    }
    else
    {
        mp = " ";
        mlen = 1;
    }

    if (  mlen == 1
       && *mp == ' ')
    {
        bp = trim_space_sep(bp, &sepSpace);
    }

    // Look for the target string.
    //
    int nText = strlen(bp);
    int i = BMH_StringSearch(mlen, mp, nText, bp);
    if (i >= 0)
    {
        // Yup, return what follows.
        //
        bp += i + mlen;
        safe_copy_buf(bp, nText-i-mlen, buff, bufc);
    }
    //
    // Ran off the end without finding it.
}

FUNCTION(fun_before)
{
    char *mp, *ip;
    int mlen;

    // Sanity-check arg1 and arg2.
    //
    char *bp = fargs[0];
    if (nfargs > 1)
    {
        mp = fargs[1];
        mlen = strlen(mp);
    }
    else
    {
        mp = " ";
        mlen = 1;
    }

    if (  mlen == 1
       && *mp == ' ')
    {
        bp = trim_space_sep(bp, &sepSpace);
    }

    ip = bp;

    // Look for the target string.
    //
    int i = BMH_StringSearch(mlen, mp, strlen(bp), bp);
    if (i >= 0)
    {
        // Yup, return what follows.
        //
        safe_copy_buf(ip, i, buff, bufc);
        return;
    }
    // Ran off the end without finding it.
    //
    safe_str(ip, buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_search: Search the db for things, returning a list of what matches
 */

FUNCTION(fun_search)
{
    char *pArg = NULL;
    if (nfargs != 0)
    {
        pArg = fargs[0];
    }

    // Set up for the search.  If any errors, abort.
    //
    SEARCH searchparm;
    if (!search_setup(executor, pArg, &searchparm))
    {
        safe_str("#-1 ERROR DURING SEARCH", buff, bufc);
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

FUNCTION(fun_stats)
{
    dbref who;

    if (  nfargs == 0
       || (!fargs[0])
       || !*fargs[0]
       || !string_compare(fargs[0], "all"))
    {
        who = NOTHING;
    }
    else
    {
        who = lookup_player(executor, fargs[0], true);
        if (who == NOTHING)
        {
            safe_str("#-1 PLAYER NOT FOUND", buff, bufc);
            return;
        }
    }
    STATS statinfo;
    if (!get_stats(executor, who, &statinfo))
    {
        safe_str("#-1 ERROR GETTING STATS", buff, bufc);
        return;
    }
    safe_tprintf_str(buff, bufc, "%d %d %d %d %d %d", statinfo.s_total, statinfo.s_rooms,
            statinfo.s_exits, statinfo.s_things, statinfo.s_players, statinfo.s_garbage);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_merge:  given two strings and a character, merge the two strings
 * *   by replacing characters in string1 that are the same as the given
 * *   character by the corresponding character in string2 (by position).
 * *   The strings must be of the same length.
 */

FUNCTION(fun_merge)
{
    char *str, *rep;
    char c;

    // Do length checks first.
    //
    if (strlen(fargs[0]) != strlen(fargs[1]))
    {
        safe_str("#-1 STRING LENGTHS MUST BE EQUAL", buff, bufc);
        return;
    }
    if (strlen(fargs[2]) > 1)
    {
        safe_str("#-1 TOO MANY CHARACTERS", buff, bufc);
        return;
    }

    // Find the character to look for. null character is considered a
    // space.
    //
    if (!*fargs[2])
        c = ' ';
    else
        c = *fargs[2];

    // Walk strings, copy from the appropriate string.
    //
    for (str = fargs[0], rep = fargs[1];
         *str && *rep && ((*bufc - buff) < (LBUF_SIZE-1));
         str++, rep++, (*bufc)++)
    {
        if (*str == c)
            **bufc = *rep;
        else
            **bufc = *str;
    }
    return;
}

/* ---------------------------------------------------------------------------
 * fun_splice: similar to MERGE(), eplaces by word instead of by character.
 */

FUNCTION(fun_splice)
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
    if (countwords(fargs[2], &sep) > 1)
    {
        safe_str("#-1 TOO MANY WORDS", buff, bufc);
        return;
    }
    int words = countwords(fargs[0], &sep);
    if (words != countwords(fargs[1], &sep))
    {
        safe_str("#-1 NUMBER OF WORDS MUST BE EQUAL", buff, bufc);
        return;
    }

    // Loop through the two lists.
    //
    char *p1 = fargs[0];
    char *q1 = fargs[1];
    char *p2, *q2;
    bool first = true;
    int i;
    for (i = 0; i < words; i++)
    {
        p2 = split_token(&p1, &sep);
        q2 = split_token(&q1, &sep);
        if (!first)
        {
            print_sep(&osep, buff, bufc);
        }
        if (strcmp(p2, fargs[2]) == 0)
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

FUNCTION(fun_repeat)
{
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
        int len = strlen(fargs[0]);
        if (len == 1)
        {
            // It turns into a memset.
            //
            safe_fill(buff, bufc, *fargs[0], times);
        }
        else
        {
            int nSize = len*times;
            if (  times > LBUF_SIZE - 1
               || nSize > LBUF_SIZE - 1)
            {
                safe_str("#-1 STRING TOO LONG", buff, bufc);
            }
            else
            {
                int nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
                if (nSize > nBufferAvailable)
                {
                    nSize = nBufferAvailable;
                }
                int nFullCopies = nSize / len;
                int nPartial = nSize - nFullCopies * len;
                while (nFullCopies--)
                {
                    memcpy(*bufc, fargs[0], len);
                    *bufc += len;
                }
                if (nPartial)
                {
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

FUNCTION(fun_iter)
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

    char *curr = alloc_lbuf("fun_iter");
    char *dp = curr;
    char *str = fargs[0];
    mux_exec(curr, &dp, executor, caller, enactor,
        EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    *dp = '\0';
    int ncp;
    char *cp = trim_space_sep_LEN(curr, dp-curr, &sep, &ncp);
    if (!*cp)
    {
        free_lbuf(curr);
        return;
    }
    bool first = true;
    int number = 0;
    mudstate.itext[mudstate.in_loop] = NULL;
    mudstate.inum[mudstate.in_loop] = number;
    mudstate.in_loop++;
    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim
          && !MuxAlarm.bAlarmed)
    {
        if (!first)
        {
            print_sep(&osep, buff, bufc);
        }
        first = false;
        number++;
        char *objstring = split_token(&cp, &sep);
        mudstate.itext[mudstate.in_loop-1] = objstring;
        mudstate.inum[mudstate.in_loop-1]  = number;
        char *buff2 = replace_tokens(fargs[1], objstring, mux_ltoa_t(number),
            NULL);
        str = buff2;
        mux_exec(buff, bufc, executor, caller, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        free_lbuf(buff2);
    }
    mudstate.in_loop--;
    mudstate.itext[mudstate.in_loop] = NULL;
    mudstate.inum[mudstate.in_loop] = 0;
    free_lbuf(curr);
}

void iter_value(char *buff, char **bufc, char *fargs[], int nfargs, bool bWhich)
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

FUNCTION(fun_itext)
{
    iter_value(buff, bufc, fargs, nfargs, false);
}

FUNCTION(fun_inum)
{
    iter_value(buff, bufc, fargs, nfargs, true);
}

FUNCTION(fun_list)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_EVAL|DELIM_STRING))
    {
        return;
    }

    char *objstring, *result, *str;

    char *curr = alloc_lbuf("fun_list");
    char *dp   = curr;
    str = fargs[0];
    mux_exec(curr, &dp, executor, caller, enactor,
        EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    int ncp;
    char *cp = trim_space_sep_LEN(curr, dp-curr, &sep, &ncp);
    if (!*cp)
    {
        free_lbuf(curr);
        return;
    }
    int number = 0;
    mudstate.itext[mudstate.in_loop] = NULL;
    mudstate.inum[mudstate.in_loop] = number;
    mudstate.in_loop++;
    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim
          && !MuxAlarm.bAlarmed)
    {
        number++;
        objstring = split_token(&cp, &sep);
        mudstate.itext[mudstate.in_loop-1] = objstring;
        mudstate.inum[mudstate.in_loop-1]  = number;
        char *buff2 = replace_tokens(fargs[1], objstring, mux_ltoa_t(number),
            NULL);
        dp = result = alloc_lbuf("fun_list.2");
        str = buff2;
        mux_exec(result, &dp, executor, caller, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        *dp = '\0';
        free_lbuf(buff2);
        notify(enactor, result);
        free_lbuf(result);
    }
    mudstate.in_loop--;
    mudstate.itext[mudstate.in_loop] = NULL;
    mudstate.inum[mudstate.in_loop] = 0;
    free_lbuf(curr);
}

FUNCTION(fun_ilev)
{
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

FUNCTION(fun_fold)
{
    SEP sep;
    if (!OPTIONAL_DELIM(4, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    char *atext;
    dbref thing;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, buff, bufc))
    {
        return;
    }

    // Evaluate it using the rest of the passed function args.
    //
    char *curr = fargs[1];
    char *cp = curr;
    char *atextbuf = alloc_lbuf("fun_fold");
    strcpy(atextbuf, atext);

    char *result, *bp, *str, *clist[2];

    // May as well handle first case now.
    //
    if ( nfargs >= 3
       && fargs[2])
    {
        clist[0] = fargs[2];
        clist[1] = split_token(&cp, &sep);
        result = bp = alloc_lbuf("fun_fold");
        str = atextbuf;
        mux_exec(result, &bp, thing, executor, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, clist, 2);
        *bp = '\0';
    }
    else
    {
        clist[0] = split_token(&cp, &sep);
        clist[1] = split_token(&cp, &sep);
        result = bp = alloc_lbuf("fun_fold");
        str = atextbuf;
        mux_exec(result, &bp, thing, executor, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, clist, 2);
        *bp = '\0';
    }

    char *rstore = result;
    result = alloc_lbuf("fun_fold");

    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim
          && !MuxAlarm.bAlarmed)
    {
        clist[0] = rstore;
        clist[1] = split_token(&cp, &sep);
        strcpy(atextbuf, atext);
        bp = result;
        str = atextbuf;
        mux_exec(result, &bp, thing, executor, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, clist, 2);
        *bp = '\0';
        strcpy(rstore, result);
    }
    free_lbuf(result);
    safe_str(rstore, buff, bufc);
    free_lbuf(rstore);
    free_lbuf(atext);
    free_lbuf(atextbuf);
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
FUNCTION(fun_itemize)
{
    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    const char *lconj = "and";
    if (nfargs > 2)
    {
        lconj = fargs[2];
    }
    const char *punc = ",";
    if (nfargs > 3)
    {
        punc = fargs[3];
    }

    int pos = 1;
    char *cp = trim_space_sep(fargs[0], &sep);
    char *word = split_token(&cp, &sep);
    while (cp && *cp)
    {
        pos++;
        safe_str(word, buff, bufc);
        char *nextword = split_token(&cp, &sep);

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
FUNCTION(fun_choose)
{
    SEP isep;
    if (!OPTIONAL_DELIM(3, isep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

    char *elems[LBUF_SIZE/2], *weights[LBUF_SIZE/2];
    int n_elems = list2arr(elems, LBUF_SIZE/2, fargs[0], &isep);
    int n_weights = list2arr(weights, LBUF_SIZE/2, fargs[1], &sepSpace);

    if (n_elems != n_weights)
    {
        safe_str("#-1 LISTS MUST BE OF EQUAL SIZE", buff, bufc);
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
                safe_str("#-1 OVERFLOW", buff, bufc);
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
}

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

void filter_handler(char *buff, char **bufc, dbref executor, dbref enactor,
                    char *fargs[], SEP *psep, SEP *posep, bool bBool)
{
    char *atext;
    dbref thing;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, buff, bufc))
    {
        return;
    }

    // Now iteratively eval the attrib with the argument list.
    //
    char *curr = trim_space_sep(fargs[1], psep);
    char *cp = curr;
    char *atextbuf = alloc_lbuf("fun_filter");
    char *result = alloc_lbuf("fun_filter");
    bool bFirst = true;
    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim
          && !MuxAlarm.bAlarmed)
    {
        char *objstring = split_token(&cp, psep);
        strcpy(atextbuf, atext);
        char *bp = result;
        char *str = atextbuf;
        mux_exec(result, &bp, thing, executor, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, &objstring, 1);
        *bp = '\0';

        if (  (  bBool
              && xlate(result))
           || (  !bBool
              && result[0] == '1'
              && result[1] == '\0'))
        {
            if (!bFirst)
            {
                print_sep(posep, buff, bufc);
            }
            safe_str(objstring, buff, bufc);
            bFirst = false;
        }
    }
    free_lbuf(result);
    free_lbuf(atext);
    free_lbuf(atextbuf);
}

FUNCTION(fun_filter)
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
    filter_handler(buff, bufc, executor, enactor, fargs, &sep, &osep, false);
}

FUNCTION(fun_filterbool)
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
    filter_handler(buff, bufc, executor, enactor, fargs, &sep, &osep, true);
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

FUNCTION(fun_map)
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

    char *atext;
    dbref thing;
    if (!parse_and_get_attrib(executor, fargs, &atext, &thing, buff, bufc))
    {
        return;
    }

    // Now process the list one element at a time.
    //
    char *cp = trim_space_sep(fargs[1], &sep);
    char *atextbuf = alloc_lbuf("fun_map");
    bool first = true;
    char *objstring, *str;
    while (  cp
          && mudstate.func_invk_ctr < mudconf.func_invk_lim
          && !MuxAlarm.bAlarmed)
    {
        if (!first)
        {
            print_sep(&osep, buff, bufc);
        }
        first = false;
        objstring = split_token(&cp, &sep);
        strcpy(atextbuf, atext);
        str = atextbuf;
        mux_exec(buff, bufc, thing, executor, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, &objstring, 1);
    }
    free_lbuf(atext);
    free_lbuf(atextbuf);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_edit: Edit text.
 */

FUNCTION(fun_edit)
{
    char *tstr;

    edit_string(strip_ansi(fargs[0]), &tstr, fargs[1], fargs[2]);
    safe_str(tstr, buff, bufc);
    free_lbuf(tstr);
}

/* ---------------------------------------------------------------------------
 * fun_locate: Search for things with the perspective of another obj.
 */

FUNCTION(fun_locate)
{
    bool check_locks, verbose, multiple;
    dbref thing, what;
    char *cp;

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

    safe_tprintf_str(buff, bufc, "#%d", what);
}

void switch_handler
(
    char *buff, char **bufc,
    dbref executor, dbref caller, dbref enactor,
    char *fargs[], int nfargs,
    char *cargs[], int ncargs,
    bool bSwitch
)
{
    // Evaluate the target in fargs[0].
    //
    char *mbuff = alloc_lbuf("fun_switch");
    char *bp = mbuff;
    char *str = fargs[0];
    mux_exec(mbuff, &bp, executor, caller, enactor,
        EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
    *bp = '\0';

    char *tbuff = alloc_lbuf("fun_switch.2");

    // Loop through the patterns looking for a match.
    //
    int i;
    for (i = 1;  i < nfargs-1
              && fargs[i]
              && fargs[i+1]
              && !MuxAlarm.bAlarmed; i += 2)
    {
        bp = tbuff;
        str = fargs[i];
        mux_exec(tbuff, &bp, executor, caller, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        *bp = '\0';

        if (bSwitch ? wild_match(tbuff, mbuff) : strcmp(tbuff, mbuff) == 0)
        {
            free_lbuf(tbuff);
            tbuff = replace_tokens(fargs[i+1], NULL, NULL, mbuff);
            free_lbuf(mbuff);
            str = tbuff;
            mux_exec(buff, bufc, executor, caller, enactor,
                EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
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
        tbuff = replace_tokens(fargs[i], NULL, NULL, mbuff);
        str = tbuff;
        mux_exec(buff, bufc, executor, caller, enactor,
            EV_STRIP_CURLY | EV_FCHECK | EV_EVAL, &str, cargs, ncargs);
        free_lbuf(tbuff);
    }
    free_lbuf(mbuff);
}

/* ---------------------------------------------------------------------------
 * fun_switch: Return value based on pattern matching (ala @switch)
 * NOTE: This function expects that its arguments have not been evaluated.
 */

FUNCTION(fun_switch)
{
    switch_handler
    (
        buff, bufc,
        executor, caller, enactor,
        fargs, nfargs,
        cargs, ncargs,
        true
    );
}

FUNCTION(fun_case)
{
    switch_handler
    (
        buff, bufc,
        executor, caller, enactor,
        fargs, nfargs,
        cargs, ncargs,
        false
    );
}

/*
 * ---------------------------------------------------------------------------
 * * fun_space: Make spaces.
 */

FUNCTION(fun_space)
{
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
            if (!is_integer(fargs[0], NULL))
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

FUNCTION(fun_height)
{
    long nHeight = 24;
    dbref target = NOTHING;
    if (is_rational(fargs[0]))
    {
        SOCKET s = mux_atol(fargs[0]);
        DESC *d;
        DESC_ITER_CONN(d)
        {
            if (d->descriptor == s)
            {
                target = d->player;
                break;
            }
        }
    }
    else
    {
        char *pTargetName = fargs[0];
        if (*pTargetName == '*')
        {
            pTargetName++;
        }
        target = lookup_player(executor, pTargetName, true);
        if (!Good_obj(target))
        {
            target = NOTHING;
        }
    }

    if (NOTHING != target)
    {
        if (  executor == target
           || See_All(executor))
        {
            nHeight = fetch_height(target);
        }
    }

    safe_ltoa(nHeight, buff, bufc);
}


FUNCTION(fun_width)
{
    long nWidth = 78;
    dbref target = NOTHING;
    if (is_rational(fargs[0]))
    {
        SOCKET s = mux_atol(fargs[0]);
        DESC *d;
        DESC_ITER_CONN(d)
        {
            if (d->descriptor == s)
            {
                target = d->player;
                break;
            }
        }
    }
    else
    {
        char *pTargetName = fargs[0];
        if (*pTargetName == '*')
        {
            pTargetName++;
        }
        target = lookup_player(executor, pTargetName, true);
        if (!Good_obj(target))
        {
            target = NOTHING;
        }
    }

    if (NOTHING != target)
    {
        if (  executor == target
           || See_All(executor))
        {
            nWidth = fetch_width(target);
        }
    }

    safe_ltoa(nWidth, buff, bufc);
}

/*
 * ---------------------------------------------------------------------------
 * * fun_idle, fun_conn: return seconds idle or connected.
 */

FUNCTION(fun_idle)
{
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
            if (d->descriptor == s)
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
        char *pTargetName = fargs[0];
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

FUNCTION(fun_conn)
{
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
            if (d->descriptor == s)
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
        char *pTargetName = fargs[0];
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

/*
 * ---------------------------------------------------------------------------
 * * fun_sort: Sort lists.
 */

typedef struct f_record
{
    double data;
    char *str;
} f_rec;

typedef struct i_record
{
    long data;
    char *str;
} i_rec;

typedef struct i64_record
{
    INT64 data;
    char *str;
} i64_rec;

static int DCL_CDECL a_comp(const void *s1, const void *s2)
{
    return strcmp(*(char **)s1, *(char **)s2);
}

static int DCL_CDECL a_casecomp(const void *s1, const void *s2)
{
    return mux_stricmp(*(char **)s1, *(char **)s2);
}

static int DCL_CDECL f_comp(const void *s1, const void *s2)
{
    if (((f_rec *) s1)->data > ((f_rec *) s2)->data)
    {
        return 1;
    }
    else if (((f_rec *) s1)->data < ((f_rec *) s2)->data)
    {
        return -1;
    }
    return 0;
}

static int DCL_CDECL i_comp(const void *s1, const void *s2)
{
    if (((i_rec *) s1)->data > ((i_rec *) s2)->data)
    {
        return 1;
    }
    else if (((i_rec *) s1)->data < ((i_rec *) s2)->data)
    {
        return -1;
    }
    return 0;
}

static int DCL_CDECL i64_comp(const void *s1, const void *s2)
{
    if (((i64_rec *) s1)->data > ((i64_rec *) s2)->data)
    {
        return 1;
    }
    else if (((i64_rec *) s1)->data < ((i64_rec *) s2)->data)
    {
        return -1;
    }
    return 0;
}

static void do_asort(char *s[], int n, int sort_type)
{
    int i;
    f_rec *fp;
    i_rec *ip;
    i64_rec *i64p;

    switch (sort_type)
    {
    case ASCII_LIST:
        qsort(s, n, sizeof(char *), a_comp);
        break;

    case NUMERIC_LIST:
        i64p = (i64_rec *) MEMALLOC(n * sizeof(i64_rec));
        ISOUTOFMEMORY(i64p);
        for (i = 0; i < n; i++)
        {
            i64p[i].str = s[i];
            i64p[i].data = mux_atoi64(s[i]);
        }
        qsort(i64p, n, sizeof(i64_rec), i64_comp);
        for (i = 0; i < n; i++)
        {
            s[i] = i64p[i].str;
        }
        MEMFREE(i64p);
        i64p = NULL;
        break;

    case DBREF_LIST:
        ip = (i_rec *) MEMALLOC(n * sizeof(i_rec));
        ISOUTOFMEMORY(ip);
        for (i = 0; i < n; i++)
        {
            ip[i].str = s[i];
            ip[i].data = dbnum(s[i]);
        }
        qsort(ip, n, sizeof(i_rec), i_comp);
        for (i = 0; i < n; i++)
        {
            s[i] = ip[i].str;
        }
        MEMFREE(ip);
        ip = NULL;
        break;

    case FLOAT_LIST:
        fp = (f_rec *) MEMALLOC(n * sizeof(f_rec));
        ISOUTOFMEMORY(fp);
        for (i = 0; i < n; i++)
        {
            fp[i].str = s[i];
            fp[i].data = mux_atof(s[i], false);
        }
        qsort(fp, n, sizeof(f_rec), f_comp);
        for (i = 0; i < n; i++)
        {
            s[i] = fp[i].str;
        }
        MEMFREE(fp);
        fp = NULL;
        break;

    case CI_ASCII_LIST:
        qsort(s, n, sizeof(char *), a_casecomp);
        break;
    }
}

FUNCTION(fun_sort)
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

    char *ptrs[LBUF_SIZE / 2];

    // Convert the list to an array.
    //
    char *list = alloc_lbuf("fun_sort");
    strcpy(list, fargs[0]);
    int nitems = list2arr(ptrs, LBUF_SIZE / 2, list, &sep);
    int sort_type = get_list_type(fargs, nfargs, 2, ptrs, nitems);
    do_asort(ptrs, nitems, sort_type);
    arr2list(ptrs, nitems, buff, bufc, &osep);
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
    char *fargs[],
    char *buff,
    char **bufc,
    int  oper,
    SEP  *psep,
    SEP  *posep
)
{
    char *ptrs1[LBUF_SIZE], *ptrs2[LBUF_SIZE];
    int val;

    char *list1 = alloc_lbuf("fun_setunion.1");
    strcpy(list1, fargs[0]);
    int n1 = list2arr(ptrs1, LBUF_SIZE, list1, psep);
    do_asort(ptrs1, n1, ASCII_LIST);

    char *list2 = alloc_lbuf("fun_setunion.2");
    strcpy(list2, fargs[1]);
    int n2 = list2arr(ptrs2, LBUF_SIZE, list2, psep);
    do_asort(ptrs2, n2, ASCII_LIST);

    int i1 = 0;
    int i2 = 0;
    char *oldp = NULL;
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
           && strcmp(ptrs1[0], ptrs2[0]) == 0)
        {
            safe_str(ptrs1[0], buff, bufc);
            break;
        }

        // Process until one list is empty.
        //
        while (  i1 < n1
              && i2 < n2)
        {
            // Skip over duplicates.
            //
            if (  i1 > 0
               || i2 > 0)
            {
                while (  i1 < n1
                      && oldp
                      && strcmp(ptrs1[i1], oldp) == 0)
                {
                    i1++;
                }
                while (  i2 < n2
                      && oldp
                      && strcmp(ptrs2[i2], oldp) == 0)
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
                    print_sep(posep, buff, bufc);
                }
                bFirst = false;
                if (strcmp(ptrs1[i1], ptrs2[i2]) < 0)
                {
                    oldp = ptrs1[i1];
                    safe_str(ptrs1[i1], buff, bufc);
                    i1++;
                }
                else
                {
                    oldp = ptrs2[i2];
                    safe_str(ptrs2[i2], buff, bufc);
                    i2++;
                }
            }
        }

        // Copy rest of remaining list, stripping duplicates.
        //
        for (; i1 < n1; i1++)
        {
            if (  !oldp
               || strcmp(oldp, ptrs1[i1]) != 0)
            {
                if (!bFirst)
                {
                    print_sep(posep, buff, bufc);
                }
                bFirst = false;
                oldp = ptrs1[i1];
                safe_str(ptrs1[i1], buff, bufc);
            }
        }
        for (; i2 < n2; i2++)
        {
            if (  !oldp
               || strcmp(oldp, ptrs2[i2]) != 0)
            {
                if (!bFirst)
                {
                    print_sep(posep, buff, bufc);
                }
                bFirst = false;
                oldp = ptrs2[i2];
                safe_str(ptrs2[i2], buff, bufc);
            }
        }
        break;

    case SET_INTERSECT:

        // Copy elements not in both lists.
        //
        while (  i1 < n1
              && i2 < n2)
        {
            val = strcmp(ptrs1[i1], ptrs2[i2]);
            if (!val)
            {
                // Got a match, copy it.
                //
                if (!bFirst)
                {
                    print_sep(posep, buff, bufc);
                }
                bFirst = false;
                oldp = ptrs1[i1];
                safe_str(ptrs1[i1], buff, bufc);
                i1++;
                i2++;
                while (  i1 < n1
                      && strcmp(ptrs1[i1], oldp) == 0)
                {
                    i1++;
                }
                while (  i2 < n2
                      && strcmp(ptrs2[i2], oldp) == 0)
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
            val = strcmp(ptrs1[i1], ptrs2[i2]);
            if (!val)
            {
                // Got a match, increment pointers.
                //
                oldp = ptrs1[i1];
                while (  i1 < n1
                      && strcmp(ptrs1[i1], oldp) == 0)
                {
                    i1++;
                }
                while (  i2 < n2
                      && strcmp(ptrs2[i2], oldp) == 0)
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
                    print_sep(posep, buff, bufc);
                }
                bFirst = false;
                safe_str(ptrs1[i1], buff, bufc);
                oldp = ptrs1[i1];
                i1++;
                while (  i1 < n1
                      && strcmp(ptrs1[i1], oldp) == 0)
                {
                    i1++;
                }
            }
            else
            {
                // Item in list2 but not in list1, discard.
                //
                oldp = ptrs2[i2];
                i2++;
                while (  i2 < n2
                      && strcmp(ptrs2[i2], oldp) == 0)
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
                print_sep(posep, buff, bufc);
            }
            bFirst = false;
            safe_str(ptrs1[i1], buff, bufc);
            oldp = ptrs1[i1];
            i1++;
            while (  i1 < n1
                  && strcmp(ptrs1[i1], oldp) == 0)
            {
                i1++;
            }
        }
    }
    free_lbuf(list1);
    free_lbuf(list2);
}

FUNCTION(fun_setunion)
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
    handle_sets(fargs, buff, bufc, SET_UNION, &sep, &osep);
}

FUNCTION(fun_setdiff)
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
    handle_sets(fargs, buff, bufc, SET_DIFF, &sep, &osep);
}

FUNCTION(fun_setinter)
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
    handle_sets(fargs, buff, bufc, SET_INTERSECT, &sep, &osep);
}

/* ---------------------------------------------------------------------------
 * rjust, ljust, center: Justify or center text, specifying fill character.
 */
#define CJC_CENTER 0
#define CJC_LJUST  1
#define CJC_RJUST  2

void centerjustcombo
(
    int iType,
    char *buff,
    char **bufc,
    char *fargs[],
    int nfargs
)
{
    // Width must be a number.
    //
    if (!is_integer(fargs[1], NULL))
    {
        return;
    }
    int width = mux_atol(fargs[1]);
    if (width <= 0 || LBUF_SIZE <= width)
    {
        safe_range(buff, bufc);
        return;
    }

    // Determine string to pad with.
    //
    int  vwPad = 0;
    int  nPad = -1;
    char aPad[SBUF_SIZE];
    struct ANSI_In_Context  aic;
    struct ANSI_Out_Context aoc;
    if (nfargs == 3 && *fargs[2])
    {
        char *p = RemoveSetOfCharacters(fargs[2], "\r\n\t");
        ANSI_String_In_Init(&aic, p, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Out_Init(&aoc, aPad, sizeof(aPad), sizeof(aPad), ANSI_ENDGOAL_LEAK);
        ANSI_String_Copy(&aoc, &aic, sizeof(aPad));
        nPad = ANSI_String_Finalize(&aoc, &vwPad);
    }
    if (nPad <= 0)
    {
        aPad[0] = ' ';
        aPad[1] = '\0';
        nPad    = 1;
        vwPad   = 1;
    }

    int  vwStr;
    char aStr[LBUF_SIZE];
    int nStr = ANSI_TruncateToField(fargs[0], sizeof(aStr), aStr,
        width, &vwStr, ANSI_ENDGOAL_NORMAL);

    // If the visual width of the text fits exactly into the field,
    // then we are done. ANSI_TruncateToField insures that it's
    // never larger.
    //
    if (vwStr == width)
    {
        safe_copy_buf(aStr, nStr, buff, bufc);
        return;
    }

    int vwLeading = 0;
    if (iType == CJC_CENTER)
    {
        vwLeading = (width - vwStr)/2;
    }
    else if (iType == CJC_RJUST)
    {
        vwLeading = width - vwStr;
    }
    int vwTrailing      = width - vwLeading - vwStr;

    // Shortcut this function if nPad == 1 (i.e., the padding is a single
    // character).
    //
    if (nPad == 1 && vwPad == 1)
    {
        safe_fill(buff, bufc, aPad[0], vwLeading);
        safe_copy_buf(aStr, nStr, buff, bufc);
        safe_fill(buff, bufc, aPad[0], vwTrailing);
        return;
    }


    // Calculate the necessary info about the leading padding.
    // The origin on the padding is at byte 0 at beginning of the
    // field (this may cause mis-syncronization on the screen if
    // the same background padding string is used on several lines
    // with each idented from column 0 by a different amount.
    // There is nothing center() can do about this issue. You are
    // on your own.
    //
    // Padding is repeated nLeadFull times and then a partial string
    // of vwLeadPartial visual width is tacked onto the end.
    //
    // vwLeading == nLeadFull * vwPad + vwLeadPartial
    //
    int nLeadFull     = 0;
    int vwLeadPartial = 0;
    if (vwLeading)
    {
        nLeadFull     = vwLeading / vwPad;
        vwLeadPartial = vwLeading - nLeadFull * vwPad;
    }

    // Calculate the necessary info about the trailing padding.
    //
    // vwTrailing == vwTrailPartial0 + nTrailFull * vwPad
    //             + vwTrailPartial1
    //
    int vwTrailSkip0    = 0;
    int vwTrailPartial0 = 0;
    int nTrailFull      = 0;
    int vwTrailPartial1 = 0;
    if (vwTrailing)
    {
        vwTrailSkip0    = (vwLeading + vwStr) % vwPad;
        vwTrailPartial0 = 0;
        if (vwTrailSkip0)
        {
            int n = vwPad - vwTrailSkip0;
            if (vwTrailing >= vwTrailPartial0)
            {
                vwTrailPartial0 = n;
                vwTrailing -= vwTrailPartial0;
            }
        }
        nTrailFull      = vwTrailing / vwPad;
        vwTrailPartial1 = vwTrailing - nTrailFull * vwPad;
    }

    int nBufferAvailable = LBUF_SIZE - (*bufc - buff) - 1;
    ANSI_String_Out_Init(&aoc, *bufc, nBufferAvailable,
        LBUF_SIZE-1, ANSI_ENDGOAL_NORMAL);
    int    vwDone;

    // Output the runs of full leading padding.
    //
    int i, n;
    for (i = 0; i < nLeadFull; i++)
    {
        ANSI_String_In_Init(&aic, aPad, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Copy(&aoc, &aic, vwPad);
    }

    // Output the partial leading padding segment.
    //
    if (vwLeadPartial > 0)
    {
        ANSI_String_In_Init(&aic, aPad, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Copy(&aoc, &aic, vwLeadPartial);
    }

    // Output the main string to be centered.
    //
    if (nStr > 0)
    {
        ANSI_String_In_Init(&aic, aStr, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Copy(&aoc, &aic, LBUF_SIZE-1);
    }

    // Output the first partial trailing padding segment.
    //
    if (vwTrailPartial0 > 0)
    {
        ANSI_String_In_Init(&aic, aPad, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Skip(&aic, vwTrailSkip0, &vwDone);
        ANSI_String_Copy(&aoc, &aic, LBUF_SIZE-1);
    }

    // Output the runs of full trailing padding.
    //
    for (i = 0; i < nTrailFull; i++)
    {
        ANSI_String_In_Init(&aic, aPad, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Copy(&aoc, &aic, vwPad);
    }

    // Output the second partial trailing padding segment.
    //
    if (vwTrailPartial1 > 0)
    {
        ANSI_String_In_Init(&aic, aPad, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Copy(&aoc, &aic, vwTrailPartial1);
    }

    n = ANSI_String_Finalize(&aoc, &vwDone);
    *bufc += n;
}

FUNCTION(fun_ljust)
{
    centerjustcombo(CJC_LJUST, buff, bufc, fargs, nfargs);
}

FUNCTION(fun_rjust)
{
    centerjustcombo(CJC_RJUST, buff, bufc, fargs, nfargs);
}

FUNCTION(fun_center)
{
    centerjustcombo(CJC_CENTER, buff, bufc, fargs, nfargs);
}

/* ---------------------------------------------------------------------------
 * setq, setr, r: set and read global registers.
 */

FUNCTION(fun_setq)
{
    int regnum = mux_RegisterSet[(unsigned char)fargs[0][0]];
    if (  regnum < 0
       || regnum >= MAX_GLOBAL_REGS
       || fargs[0][1] != '\0')
    {
        safe_str("#-1 INVALID GLOBAL REGISTER", buff, bufc);
    }
    else
    {
        if (!mudstate.global_regs[regnum])
        {
            mudstate.global_regs[regnum] = alloc_lbuf("fun_setq");
        }
        int n = strlen(fargs[1]);
        memcpy(mudstate.global_regs[regnum], fargs[1], n+1);
        mudstate.glob_reg_len[regnum] = n;
    }
}

FUNCTION(fun_setr)
{
    int regnum = mux_RegisterSet[(unsigned char)fargs[0][0]];
    if (  regnum < 0
       || regnum >= MAX_GLOBAL_REGS
       || fargs[0][1] != '\0')
    {
        safe_str("#-1 INVALID GLOBAL REGISTER", buff, bufc);
    }
    else
    {
        if (!mudstate.global_regs[regnum])
        {
            mudstate.global_regs[regnum] = alloc_lbuf("fun_setq");
        }
        int n = strlen(fargs[1]);
        memcpy(mudstate.global_regs[regnum], fargs[1], n+1);
        mudstate.glob_reg_len[regnum] = n;
        safe_copy_buf(fargs[1], n, buff, bufc);
    }
}

FUNCTION(fun_r)
{
    int regnum = mux_RegisterSet[(unsigned char)fargs[0][0]];
    if (  regnum < 0
       || regnum >= MAX_GLOBAL_REGS
       || fargs[0][1] != '\0')
    {
        safe_str("#-1 INVALID GLOBAL REGISTER", buff, bufc);
    }
    else if (mudstate.global_regs[regnum])
    {
        safe_copy_buf(mudstate.global_regs[regnum],
            mudstate.glob_reg_len[regnum], buff, bufc);
    }
}

/* ---------------------------------------------------------------------------
 * isdbref: is the argument a valid dbref?
 */

FUNCTION(fun_isdbref)
{
    bool bResult = false;

    char *p = fargs[0];
    if (NUMBER_TOKEN == p[0])
    {
        p++;
        dbref dbitem = parse_dbref(p);
        bResult = Good_obj(dbitem);
    }
    safe_bool(bResult, buff, bufc);
}

/* ---------------------------------------------------------------------------
 * trim: trim off unwanted white space.
 */

char* trim_fast_left(char* str, char delim)
{
    // We assume delim is never '\0'
    //
    while (*str == delim)
    {
        str++;
    }
    return str;
}

void trim_fast_right(char* str, char delim)
{
    // We assume delim is never '\0'
    //
    char* last = NULL;
    while (*str)
    {
        if (*str != delim)
        {
            last = str;
        }
        str++;
    }

    if (last == NULL)
    {
        return;
    }

    *(last+1) = '\0';
}

char* trim_left(char* str, SEP* sep)
{
    if (1 == sep->n)
    {
        return trim_fast_left(str, sep->str[0]);
    }
    int cycle = 0;
    int max = sep->n;
    char* base = str-1;
    for ( ; *str == sep->str[cycle]; str++)
    {
        if (max <= ++cycle)
        {
            cycle = 0;
            base = str;
        }
    }
    return base+1;
}

void trim_right(char* str, SEP* sep)
{
    if (1 == sep->n)
    {
        trim_fast_right(str,sep->str[0]);
        return;
    }

    int cycle = sep->n - 1;
    int max = sep->n - 1;
    int n = strlen(str);
    int base = n;
    n--;
    for ( ; n >= 0 && str[n] == sep->str[cycle]; n--)
    {
        if (--cycle < 0)
        {
            cycle = max;
            base = n;
        }
    }
    *(str+base) = '\0';
}

FUNCTION(fun_trim)
{
    SEP sep;
    if (!OPTIONAL_DELIM(3, sep, DELIM_DFLT|DELIM_STRING))
    {
        return;
    }

#define TRIM_LEFT  1
#define TRIM_RIGHT 2

    int trim;
    if (nfargs >= 2)
    {
        switch (mux_tolower(*fargs[1]))
        {
        case 'l':
            trim = TRIM_LEFT;
            break;

        case 'r':
            trim = TRIM_RIGHT;
            break;

        default:
            trim = TRIM_LEFT|TRIM_RIGHT;
            break;
        }
    }
    else
    {
        trim = TRIM_LEFT|TRIM_RIGHT;
    }

    char* str;
    if (trim & TRIM_LEFT)
    {
        str = trim_left(fargs[0],&sep);
    }
    else
    {
        str = fargs[0];
    }

    if (trim & TRIM_RIGHT)
    {
        trim_right(str,&sep);
    }
    safe_str(str,buff,bufc);
}

FUNCTION(fun_config)
{
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
int return_bit(dbref player)
{
   if (God(player))
      return 7;
   // 6 is Rhost Immortal. We don't have an equivalent (yet?).
   if (Wizard(player))
      return 5;
   if (Royalty(player))
      return 4;
   if (Staff(player) || Builder(player))
      return 3;
   if (Head(player) || Immortal(player))
      return 2;
   if (!(Uninspected(player) || Guest(player)))
      return 1;
   return 0;
}

FUNCTION(fun_bittype)
{
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

FUNCTION(fun_error)
{
    if (  Good_obj(mudconf.global_error_obj)
        && !Going(mudconf.global_error_obj) )
    {
        dbref aowner;
        int aflags;
        char *errtext = atr_get(mudconf.global_error_obj, A_VA, &aowner, &aflags);
        char *errbuff = alloc_lbuf("process_command.error_msg");
        char *errbufc = errbuff;
        char *str = errtext;
        if (nfargs == 1)
        {
            char *arg = fargs[0];
            mux_exec(errbuff, &errbufc, mudconf.global_error_obj, caller, enactor,
                EV_EVAL | EV_FCHECK | EV_STRIP_CURLY, &str, &arg, 1);
        }
        else
        {
            mux_exec(errbuff, &errbufc, mudconf.global_error_obj, caller, enactor,
                EV_EVAL | EV_FCHECK | EV_STRIP_CURLY, &str, (char **)NULL, 0);
        }
        safe_str(errbuff, buff, bufc);
        free_lbuf(errtext);
        free_lbuf(errbuff);
    }
    else
    {
        safe_str("Huh?  (Type \"help\" for help.)", buff, bufc);
    }
}

FUNCTION(fun_strip)
{
    if (fargs[0][0] == '\0')
    {
        return;
    }
    size_t n;
    char  *p = strip_ansi(fargs[0], &n);
    if (  nfargs < 2
       || fargs[1][0] == '\0')
    {
        safe_copy_buf(p, n, buff, bufc);
        return;
    }
    char *pInput = alloc_lbuf("fun_strip.1");
    memcpy(pInput, p, n+1);
    p = strip_ansi(fargs[1], &n);
    safe_str(RemoveSetOfCharacters(pInput, p), buff, bufc);
    free_lbuf(pInput);
}

#define DEFAULT_WIDTH 78
char *expand_tabs(const char *str)
{
    static char tbuf1[LBUF_SIZE];
    char *bp = tbuf1;

    if (str)
    {
        unsigned int n = 0;
        bool ansi = false;

        for (unsigned int i = 0; str[i]; i++)
        {
            switch (str[i])
            {
            case '\t':
                safe_fill(tbuf1, &bp, ' ', 8 - n % 8);
                continue;
            case '\r':
                // FALL THROUGH
            case '\n':
                n = 0;
                break;
            case ESC_CHAR:
                ansi = true;
                break;
            case ANSI_ATTR_CMD:
                if (ansi)
                {
                    ansi = false;
                }
                else
                {
                    n++;
                }
                break;
            case BEEP_CHAR:
                break;
            default:
                if (!ansi)
                {
                    n++;
                }
            }
            safe_chr(str[i], tbuf1, &bp);
        }
    }
    *bp = '\0';
    return tbuf1;
}

static int wraplen(char *str, const int nWidth, bool &newline)
{
    const int length = strlen(str);
    newline = false;
    if (length <= nWidth)
    {
        /* Find the first return char
        * so %r will not mess with any alignment
        * functions.
        */
        for (int i = 0; i < length; i++)
        {
            if (  str[i] == '\n'
               || str[i] == '\r')
            {
                newline = true;
                return i+2;
            }
        }
        return length;
    }

    /* Find the first return char
    * so %r will not mess with any alignment
    * functions.
    */
    for (int i = 0; i < nWidth; i++)
    {
        if (  str[i] == '\n'
           || str[i] == '\r')
        {
            newline = true;
            return i+2;
        }
    }

    /* No return char was found. Now
    * find the last space in str.
    */
    int maxlen = nWidth;
    while (str[maxlen] != ' ' && maxlen > 0)
    {
        maxlen--;
    }
    if (str[maxlen] != ' ')
    {
        maxlen = nWidth;
    }
    return (maxlen ? maxlen : -1);
}

FUNCTION(fun_wrap)
{
    // ARG 2: Width. Default: 78.
    //
    int nWidth = DEFAULT_WIDTH;
    if (  nfargs >= 2
       && fargs[1][0])
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
    if (  nfargs >= 3
       && fargs[2][0])
    {
        char cJust = mux_toupper(fargs[2][0]);
        switch (cJust)
        {
        case 'L':
            iJustKey = CJC_LJUST;
            break;
        case 'R':
            iJustKey = CJC_RJUST;
            break;
        case 'C':
            iJustKey = CJC_CENTER;
            break;
        default:
            safe_str("#-1 INVALID JUSTIFICATION SPECIFIED", buff, bufc);
            return;
        }
    }

    // ARG 4: Left padding. Default: blank.
    //
    char *pLeft = NULL;
    if (  nfargs >= 4
       && fargs[3][0])
    {
        pLeft = fargs[3];
    }

    // ARG 5: Right padding. Default: blank.
    //
    char *pRight = NULL;
    if (  nfargs >= 5
       && fargs[4][0])
    {
        pRight = fargs[4];
    }

    // ARG 6: Hanging indent. Default: 0.
    //
    int nHanging = 0;
    if (  nfargs >= 6
       && fargs[5][0])
    {
        nHanging = mux_atol(fargs[5]);
    }

    // ARG 7: Output separator. Default: line break.
    //
    char *pOSep = "\r\n";
    if (  nfargs >= 7
       && fargs[6][0])
    {
        if (!strcmp(fargs[6], "@@"))
        {
            pOSep = NULL;
        }
        else
        {
            pOSep = fargs[6];
        }
    }

    // ARG 8: First line width. Default: same as arg 2.
    int nFirstWidth = nWidth;
    if (  nfargs >= 8
       && fargs[7][0])
    {
        nFirstWidth = mux_atol(fargs[7]);
        if (  nFirstWidth < 1
           || nFirstWidth >= LBUF_SIZE)
        {
            safe_range(buff, bufc);
            return;
        }
    }

    char *str = alloc_lbuf("fun_mywrap.str");
    char *tstr = alloc_lbuf("fun_mywrap.str2");
    strcpy(tstr, expand_tabs(fargs[0]));
    strcpy(str,strip_ansi(tstr));
    int nLength = 0;
    bool newline = false;
    char *jargs[2];
    struct ANSI_In_Context aic;
    struct ANSI_Out_Context aoc;
    char *mbufc;
    char *mbuf = mbufc = alloc_lbuf("fun_mywrap.out");
    int nBufferAvailable, nSize;
    int nDone;
    int i = 0;

    while (str[i])
    {
        nLength = wraplen(str + i, i == 0 ? nFirstWidth : nWidth, newline);
        mbufc = mbuf;

        ANSI_String_In_Init(&aic, tstr, ANSI_ENDGOAL_NORMAL);
        ANSI_String_Skip(&aic, i, &nDone);
        if (nDone < i || nLength <= 0)
        {
            break;
        }
        if (i > 0)
        {
            safe_str(pOSep, buff, bufc);
            if (nHanging > 0)
            {
                safe_fill(buff, bufc, ' ', nHanging);
            }
        }
        nBufferAvailable = LBUF_SIZE - (mbufc - mbuf) - 1;
        ANSI_String_Out_Init(&aoc, mbufc, nBufferAvailable, nLength-(newline ? 2 : 0), ANSI_ENDGOAL_NORMAL);
        ANSI_String_Copy(&aoc, &aic, nLength-(newline ? 2 : 0));
        nSize = ANSI_String_Finalize(&aoc, &nDone);
        mbufc += nSize;

        jargs[0] = mbuf;
        jargs[1] = mux_ltoa_t(i == 0 ? nFirstWidth : nWidth);
        safe_str(pLeft,buff,bufc);
        centerjustcombo(iJustKey, buff, bufc, jargs, 2);
        safe_str(pRight, buff, bufc);

        i += nLength;
        if (str[i] == ' ' && str[i+1] != ' ')
        {
            i++;
        }
    }
    free_lbuf(mbuf);
    free_lbuf(str);
    free_lbuf(tstr);
}

typedef struct
{
    int  iBase;
    char chLetter;
    int  nName;
    char *pName;

} RADIX_ENTRY;

#define N_RADIX_ENTRIES 7
const RADIX_ENTRY reTable[N_RADIX_ENTRIES] =
{
    { 31556926, 'y', 4, "year"   },  // Average solar year.
    {  2629743, 'M', 5, "month"  },  // Average month.
    {   604800, 'w', 4, "week"   },  // 7 days.
    {    86400, 'd', 3, "day"    },
    {     3600, 'h', 4, "hour"   },
    {       60, 'm', 6, "minute" },
    {        1, 's', 6, "second" }
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
void GeneralTimeConversion
(
    char *Buffer,
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

    char *p = Buffer;
    int iValue;

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
//     expand_time   (33 bytes) uses TimeBuffer64,
//     write_time    (69 bytes) uses TimeBuffer80.
//
// time_format_1 and time_format_2 are called from within the same
// printf, so they must use different buffers.
//
// We pick 64 as a round number.
//
static char TimeBuffer64[64];
static char TimeBuffer80[80];

// Show time in days, hours, and minutes
//
// 2^63/86400 is 1.07E14 which is at most 15 digits.
// '(15)d (2):(2)\0' is at most 23 characters.
//
const char *digit_format(int Seconds)
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
        sprintf(TimeBuffer80, "%dd %02d:%02d", Days, Hours, Minutes);
    }
    else
    {
        sprintf(TimeBuffer80, "%02d:%02d", Hours, Minutes);
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
int tf1_width_table[4][3] =
{
    { 86399,    863999,  86396459, },
    { 86399,   8639999, 863996459, },
    { 86399,  86399999,   INT_MAX, },
    { 86399, 863999999,   INT_MAX, }
};

static struct
{
    char *specs[4];
    int  div[3];
} tf1_case_table[4] =
{
    {
        { "   %2d:%02d", "    %2d:%02d", "     %2d:%02d", "      %2d:%02d" },
        { 3600, 60, 1 }
    },
    {
        { "%dd %02d:%02d", "%2dd %02d:%02d", "%3dd %02d:%02d", "%4dd %02d:%02d" },
        { 86400, 3600, 60 }
    },
    {
        { "%3dd %02dh", "%4dd %02dh", "%5dd %02dh", "%6dd %02dh" },
        { 86400, 3600, 1 }
    },
    {
        { "%4dw %d", "%4dw %d", "", "" },
        { 604800, 86400, 1 }
    }
};

const char *time_format_1(int Seconds, size_t maxWidth)
{
    if (Seconds < 0)
    {
        Seconds = 0;
    }

    if (  maxWidth < 8
       || 12 < maxWidth)
    {
        strcpy(TimeBuffer80, "???");
        return TimeBuffer80;
    }
    int iWidth = maxWidth - 8;

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
    sprintf(TimeBuffer80, tf1_case_table[iCase].specs[iWidth], n[0], n[1], n[2]);
    return TimeBuffer80;
}

// Show time in days, hours, minutes, or seconds.
//
const char *time_format_2(int Seconds)
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
const char *expand_time(int Seconds)
{
    // 2^63/2592000 is 3558399705577 which is at most 13 digits.
    // '(13)M (1)w (1)d (2)h (2)m (2)s\0' is at most 33 characters.
    //
    GeneralTimeConversion(TimeBuffer64, Seconds, IMONTHS, ISECONDS, false, false);
    return TimeBuffer64;
}

// write_time - Written (long) time format.
//
const char *write_time(int Seconds)
{
    // 2^63/2592000 is 3558399705577 which is at most 13 digits.
    // '(13) months (1) weeks (1) days (2) hours (2) minutes (2) seconds\0' is
    // at most 69 characters.
    //
    GeneralTimeConversion(TimeBuffer80, Seconds, IMONTHS, ISECONDS, false, true);
    return TimeBuffer80;
}

// digittime - Digital format time ([(days)d]HH:MM) from given
// seconds. D.Piper - May 1997 & April 2000
//
FUNCTION(fun_digittime)
{
    int tt = mux_atol(fargs[0]);
    safe_str(digit_format(tt), buff, bufc);
}

// singletime - Single element time from given seconds.
// D.Piper - May 1997 & April 2000
//
FUNCTION(fun_singletime)
{
    int tt = mux_atol(fargs[0]);
    safe_str(time_format_2(tt), buff, bufc);
}

// exptime - Written (short) time from given seconds
// D.Piper - May 1997 & April 2000
//
FUNCTION(fun_exptime)
{
    int tt = mux_atol(fargs[0]);
    safe_str(expand_time(tt), buff, bufc);
}

// writetime - Written (long) time from given seconds
// D.Piper - May 1997 & April 2000
//
FUNCTION(fun_writetime)
{
    int tt = mux_atol(fargs[0]);
    safe_str(write_time(tt), buff, bufc);
}

// cmds - Return player command count (Wizard_Who OR Self ONLY)
// D.Piper - May 1997
//
FUNCTION(fun_cmds)
{
    long nCmds = -1;
    if (is_rational(fargs[0]))
    {
        SOCKET s = mux_atol(fargs[0]);
        bool bFound = false;
        DESC *d;
        DESC_ITER_CONN(d)
        {
            if (d->descriptor == s)
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
FUNCTION(fun_startsecs)
{
    CLinearTimeAbsolute lta;
    lta = mudstate.start_time;
    lta.Local2UTC();
    safe_str(lta.ReturnSecondsString(), buff, bufc);
}

// conntotal - Return player's total online time to the MUX
// (including their current connection). D.Piper - May 1997
//
FUNCTION(fun_conntotal)
{
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
        safe_str("#-1 PLAYER NOT FOUND", buff, bufc);
    }
}

// connmax - Return player's longest session to the MUX
// (including the current one). D.Piper - May 1997
//
FUNCTION(fun_connmax)
{
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
        safe_str("#-1 PLAYER NOT FOUND", buff, bufc);
    }
}

// connlast - Return player's last connection time to the MUX
// D.Piper - May 1997
//
FUNCTION(fun_connlast)
{
    dbref target = lookup_player(executor, fargs[0], true);
    if (Good_obj(target))
    {
        safe_ltoa(fetch_lastconnect(target), buff, bufc);
    }
    else
    {
        safe_str("#-1 PLAYER NOT FOUND", buff, bufc);
    }
}

// connnum - Return the total number of sessions this player has had
// to the MUX (including any current ones). D.Piper - May 1997
//
FUNCTION(fun_connnum)
{
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
        safe_str("#-1 PLAYER NOT FOUND", buff, bufc);
    }
}

// connleft - Return when a player last logged off the MUX as
// UTC seconds. D.Piper - May 1997
//
FUNCTION(fun_connleft)
{
    dbref target = lookup_player(executor, fargs[0], true);
    if (Good_obj(target))
    {
        CLinearTimeAbsolute cl = fetch_logouttime(target);
        safe_str(cl.ReturnSecondsString(7), buff, bufc);
    }
    else
    {
        safe_str("#-1 PLAYER NOT FOUND", buff, bufc);
    }
}

// lattrcmds - Output a list of all attributes containing $ commands.
// Altered from lattr(). D.Piper - May 1997 & April 2000
//
FUNCTION(fun_lattrcmds)
{
    // Check for wildcard matching.  parse_attrib_wild checks for read
    // permission, so we don't have to.  Have p_a_w assume the
    // slash-star if it is missing.
    //
    olist_push();
    dbref thing;
    if (parse_attrib_wild(executor, fargs[0], &thing, false, false, true))
    {
        bool isFirst = true;
        char *buf = alloc_lbuf("fun_lattrcmds");
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
FUNCTION(fun_lcmds)
{
    SEP sep;
    if (!OPTIONAL_DELIM(2, sep, DELIM_NULL|DELIM_CRLF|DELIM_STRING))
    {
        return;
    }

    // Check to see what type of command matching we will do. '$' commands
    // or '^' listens.  We default with '$' commands.
    //
    char cmd_type = '$';
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
        char *buf = alloc_lbuf("fun_lattrcmds");
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
                    char *c_ptr = buf+1;

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
                            print_sep(&sep, buff, bufc);
                        }

                        mux_strlwr(buf);
                        safe_str(buf+1, buff, bufc);

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

extern FLAGNAMEENT gen_flag_names[];

// lflags - List flags as names - (modified from 'flag_description()' and
// MUX flags(). D.Piper - May 1997 & May 2000
//
FUNCTION(fun_lflags)
{
    dbref target = match_thing_quiet(executor, fargs[0]);
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

FUNCTION(fun_art)
{
    const int ovecsize = 33;
    int ovec[ovecsize];

    // Drop the input string into lower case.
    //
    mux_strlwr(fargs[0]);

    // Search for exceptions.
    //
    ArtRuleset *arRule = mudconf.art_rules;

    while (arRule != NULL)
    {
        pcre* reRuleRegexp = (pcre *) arRule->m_pRegexp;
        pcre_extra* reRuleStudy = (pcre_extra *) arRule->m_pRegexpStudy;

        if (  !MuxAlarm.bAlarmed
           && pcre_exec(reRuleRegexp, reRuleStudy, fargs[0], strlen(fargs[0]),
                0, 0, ovec, ovecsize) > 0)
        {
            safe_str(arRule->m_bUseAn ? "an" : "a", buff, bufc);
            return;
        }

        arRule = arRule->m_pNextRule;
    }

    // Default to 'a'.
    //
    safe_str( "a", buff, bufc);
}

// ---------------------------------------------------------------------------
// fun_ord:
//
// Takes a single character and returns the corresponding ordinal of its
// position in the character set.
//
FUNCTION(fun_ord)
{
    size_t n;
    char *p  = strip_ansi(fargs[0], &n);
    if (n == 1)
    {
        unsigned char ch = p[0];
        safe_ltoa(ch, buff, bufc);
    }
    else
    {
        safe_str("#-1 FUNCTION EXPECTS ONE CHARACTER", buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_chr:
//
// Takes an integer and returns the corresponding character from the character
// set.
//
FUNCTION(fun_chr)
{
    if (!is_integer(fargs[0], NULL))
    {
        safe_str("#-1 ARGUMENT MUST BE A NUMBER", buff, bufc);
        return;
    }
    int ch = mux_atol(fargs[0]);
    if (  ch < 0
       || (int) UCHAR_MAX < ch)
    {
        safe_str("#-1 THIS ISN'T UNICODE", buff, bufc);
    }
    else if (mux_isprint(ch))
    {
        safe_chr(ch, buff, bufc);
    }
    else
    {
        safe_str("#-1 UNPRINTABLE CHARACTER", buff, bufc);
    }
}

// ---------------------------------------------------------------------------
// fun_stripaccents:
//
FUNCTION(fun_stripaccents)
{
    size_t nLen;
    char *p = strip_accents(fargs[0], &nLen);
    safe_copy_buf(p, nLen, buff, bufc);
}

// Base Letter: AaCcEeIiNnOoUuYy?!<>sPpD
//
static const unsigned char AccentCombo1[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0,18, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,19, 0,20,17,  // 3
    0, 1, 0, 3,24, 5, 0, 0, 0, 7, 0, 0, 0, 0, 9,11,  // 4
   22, 0, 0, 0, 0,13, 0, 0, 0,15, 0, 0, 0, 0, 0, 0,  // 5
    0, 2, 0, 4, 0, 6, 0, 0, 0, 8, 0, 0, 0, 0,10,12,  // 6
   23, 0, 0,21, 0,14, 0, 0, 0,16, 0, 0, 0, 0, 0, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

// Accent:      `'^~:o,u"B|-&Ee
//
static const unsigned char AccentCombo2[256] =
{
//  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F
//
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 0
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 1
    0, 0, 9, 0, 0, 0,13, 2, 0, 0, 0, 0, 7,12, 0, 0,  // 2
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 0, 0,  // 3
    0, 0,10, 0, 0,14, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 4
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 0,  // 5
    1, 0, 0, 0, 0,15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 6,  // 6
    0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0,11, 0, 4, 0,  // 7

    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 8
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // 9
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // A
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // B
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // C
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // D
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  // E
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   // F
};

static const unsigned char AccentCombo3[24][16] =
{
    //  0     1     2     3     4     5     6     7     8     9    10    11    12    13    14    15
    //        `     '     ^     ~     :     o     ,     u     "     B     |     -     &     E     e
    //
    {  0x00, 0xC0, 0xC1, 0xC2, 0xC3, 0xC4, 0xC5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC6, 0x00 }, //  1 'A'
    {  0x00, 0xE0, 0xE1, 0xE2, 0xE3, 0xE4, 0xE5, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE6 }, //  2 'a'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xC7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, //  3 'C'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xE7, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, //  4 'c'
    {  0x00, 0xC8, 0xC9, 0xCA, 0x00, 0xCB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, //  5 'E'
    {  0x00, 0xE8, 0xE9, 0xEA, 0x00, 0xEB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, //  6 'e'
    {  0x00, 0xCC, 0xCD, 0xCE, 0x00, 0xCF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, //  7 'I'
    {  0x00, 0xEC, 0xED, 0xEE, 0x00, 0xEF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, //  8 'i'

    {  0x00, 0x00, 0x00, 0x00, 0xD1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, //  9 'N'
    {  0x00, 0x00, 0x00, 0x00, 0xF1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 10 'n'
    {  0x00, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 11 'O'
    {  0x00, 0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x00, 0x00 }, // 12 'o'
    {  0x00, 0xD9, 0xDA, 0xDB, 0x00, 0xDC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 13 'U'
    {  0x00, 0xF9, 0xFA, 0xFB, 0x00, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 14 'u'
    {  0x00, 0x00, 0xDD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 15 'Y'
    {  0x00, 0x00, 0xFD, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 16 'y'

    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBF, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 17 '?'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 18 '!'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xAB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 19 '<'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xBB, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 20 '>'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xDF, 0x00, 0x00, 0x00, 0x00, 0x00 }, // 21 's'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xDE, 0x00, 0x00, 0x00, 0x00 }, // 22 'P'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFE, 0x00, 0x00, 0x00, 0x00 }, // 23 'p'
    {  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xD0, 0x00, 0x00, 0x00 }  // 24 'D'
};

// ---------------------------------------------------------------------------
// fun_accent:
//
FUNCTION(fun_accent)
{
    size_t n = strlen(fargs[0]);
    if (n != strlen(fargs[1]))
    {
        safe_str("#-1 STRING LENGTHS MUST BE EQUAL", buff, bufc);
        return;
    }

    const unsigned char *p = (unsigned char *)fargs[0];
    const unsigned char *q = (unsigned char *)fargs[1];

    while (*p)
    {
        unsigned char ch = '\0';
        unsigned char ch0 = AccentCombo1[*p];
        if (ch0)
        {
            unsigned char ch1 = AccentCombo2[*q];
            if (ch1)
            {
                ch  = AccentCombo3[ch0-1][ch1];
            }
        }
        if (!mux_isprint(ch))
        {
            ch = *p;
        }
        safe_chr(ch, buff, bufc);

        p++;
        q++;
    }
}

// ----------------------------------------------------------------------------
// flist: List of existing functions in alphabetical order.
//
//   Name          Handler      # of args   min #    max #   flags  permissions
//                               to parse  of args  of args
//
FUN builtin_function_list[] =
{
    {"@@",          fun_null,             1, 1,       1, FN_NOEVAL, CA_PUBLIC},
    {"ABS",         fun_abs,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"ACCENT",      fun_accent,     MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"ACOS",        fun_acos,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"ADD",         fun_add,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {"AFTER",       fun_after,      MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"ALPHAMAX",    fun_alphamax,   MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {"ALPHAMIN",    fun_alphamin,   MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {"AND",         fun_and,        MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {"ANDBOOL",     fun_andbool,    MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {"ANDFLAGS",    fun_andflags,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"ANSI",        fun_ansi,       MAX_ARG, 2, MAX_ARG,         0, CA_PUBLIC},
    {"APOSS",       fun_aposs,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"ART",         fun_art,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"ASIN",        fun_asin,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"ATAN",        fun_atan,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"ATTRCNT",     fun_attrcnt,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"BAND",        fun_band,       MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {"BEEP",        fun_beep,       MAX_ARG, 0,       0,         0, CA_WIZARD},
    {"BEFORE",      fun_before,     MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"BITTYPE",     fun_bittype,    MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"BNAND",       fun_bnand,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"BOR",         fun_bor,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {"BXOR",        fun_bxor,       MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {"CAND",        fun_cand,       MAX_ARG, 0, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {"CANDBOOL",    fun_candbool,   MAX_ARG, 0, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
#if defined(WOD_REALMS) || defined(REALITY_LVLS)
    {"CANSEE",      fun_cansee,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
#endif
    {"CAPSTR",      fun_capstr,           1, 1,       1,         0, CA_PUBLIC},
    {"CASE",        fun_case,       MAX_ARG, 2, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {"CAT",         fun_cat,        MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {"CEIL",        fun_ceil,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"CEMIT",       fun_cemit,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"CENTER",      fun_center,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"CHANNELS",    fun_channels,   MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"CHILDREN",    fun_children,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"CHOOSE",      fun_choose,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"CHR",         fun_chr,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"CMDS",        fun_cmds,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"COLUMNS",     fun_columns,    MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"COMALIAS",    fun_comalias,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"COMP",        fun_comp,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"COMTITLE",    fun_comtitle,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"CON",         fun_con,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"CONFIG",      fun_config,     MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"CONN",        fun_conn,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"CONNLAST",    fun_connlast,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"CONNLEFT",    fun_connleft,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"CONNMAX",     fun_connmax,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"CONNNUM",     fun_connnum,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"CONNRECORD",  fun_connrecord, MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {"CONNTOTAL",   fun_conntotal,  MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"CONTROLS",    fun_controls,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"CONVSECS",    fun_convsecs,   MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {"CONVTIME",    fun_convtime,   MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {"COR",         fun_cor,        MAX_ARG, 0, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {"CORBOOL",     fun_corbool,    MAX_ARG, 0, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {"COS",         fun_cos,        MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"CRC32",       fun_crc32,      MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {"CREATE",      fun_create,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"CTIME",       fun_ctime,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"CTU",         fun_ctu,        MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {"CWHO",        fun_cwho,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"DEC",         fun_dec,        MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"DECRYPT",     fun_decrypt,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"DEFAULT",     fun_default,    MAX_ARG, 2,       2, FN_NOEVAL, CA_PUBLIC},
    {"DELETE",      fun_delete,     MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {"DIE",         fun_die,        MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"DIGITTIME",   fun_digittime,  MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"DIST2D",      fun_dist2d,     MAX_ARG, 4,       4,         0, CA_PUBLIC},
    {"DIST3D",      fun_dist3d,     MAX_ARG, 6,       6,         0, CA_PUBLIC},
    {"DOING",       fun_doing,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"DUMPING",     fun_dumping,    MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {"E",           fun_e,          MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {"EDEFAULT",    fun_edefault,   MAX_ARG, 2,       2, FN_NOEVAL, CA_PUBLIC},
    {"EDIT",        fun_edit,       MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {"ELEMENTS",    fun_elements,   MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"ELOCK",       fun_elock,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"EMIT",        fun_emit,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"EMPTY",       fun_empty,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"ENCRYPT",     fun_encrypt,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"ENTRANCES",   fun_entrances,  MAX_ARG, 0,       4,         0, CA_PUBLIC},
    {"EQ",          fun_eq,         MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"ERROR",       fun_error,            1, 0,       1,         0, CA_PUBLIC},
    {"ESCAPE",      fun_escape,           1, 1,       1,         0, CA_PUBLIC},
    {"EVAL",        fun_eval,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"EXIT",        fun_exit,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"EXP",         fun_exp,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"EXPTIME",     fun_exptime,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"EXTRACT",     fun_extract,    MAX_ARG, 3,       5,         0, CA_PUBLIC},
    {"FCOUNT",      fun_fcount,     MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {"FDEPTH",      fun_fdepth,     MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {"FDIV",        fun_fdiv,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"FILTER",      fun_filter,     MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"FILTERBOOL",  fun_filterbool, MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"FINDABLE",    fun_findable,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"FIRST",       fun_first,      MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"FLAGS",       fun_flags,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"FLOOR",       fun_floor,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"FLOORDIV",    fun_floordiv,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"FMOD",        fun_fmod,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"FOLD",        fun_fold,       MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"FOREACH",     fun_foreach,    MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"FULLNAME",    fun_fullname,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"GET",         fun_get,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"GET_EVAL",    fun_get_eval,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"GRAB",        fun_grab,       MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"GRABALL",     fun_graball,    MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"GREP",        fun_grep,       MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {"GREPI",       fun_grepi,      MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {"GT",          fun_gt,         MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"GTE",         fun_gte,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"HASATTR",     fun_hasattr,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"HASATTRP",    fun_hasattrp,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"HASFLAG",     fun_hasflag,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"HASPOWER",    fun_haspower,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"HASQUOTA",    fun_hasquota,   MAX_ARG, 2,       3,         0, CA_PUBLIC},
#ifdef REALITY_LVLS
    {"HASRXLEVEL",  fun_hasrxlevel, MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"HASTXLEVEL",  fun_hastxlevel, MAX_ARG, 2,       2,         0, CA_PUBLIC},
#endif /* REALITY_LVLS */
    {"HASTYPE",     fun_hastype,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"HEIGHT",      fun_height,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"HOME",        fun_home,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"HOST",        fun_host,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"IABS",        fun_iabs,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"IADD",        fun_iadd,       MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {"IDIV",        fun_idiv,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"IDLE",        fun_idle,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"IF",          fun_ifelse,     MAX_ARG, 2,       3, FN_NOEVAL, CA_PUBLIC},
    {"IFELSE",      fun_ifelse,     MAX_ARG, 3,       3, FN_NOEVAL, CA_PUBLIC},
    {"ILEV",        fun_ilev,       MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {"IMUL",        fun_imul,       MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {"INC",         fun_inc,        MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"INDEX",       fun_index,      MAX_ARG, 4,       4,         0, CA_PUBLIC},
    {"INSERT",      fun_insert,     MAX_ARG, 3,       4,         0, CA_PUBLIC},
    {"INUM",        fun_inum,       MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"INZONE",      fun_inzone,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"ISDBREF",     fun_isdbref,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"ISIGN",       fun_isign,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"ISINT",       fun_isint,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"ISNUM",       fun_isnum,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"ISRAT",       fun_israt,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"ISUB",        fun_isub,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"ISWORD",      fun_isword,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"ITEMIZE",     fun_itemize,    MAX_ARG, 1,       4,         0, CA_PUBLIC},
    {"ITEMS",       fun_items,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"ITER",        fun_iter,       MAX_ARG, 2,       4, FN_NOEVAL, CA_PUBLIC},
    {"ITEXT",       fun_itext,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"LADD",        fun_ladd,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"LAND",        fun_land,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"LAST",        fun_last,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"LATTR",       fun_lattr,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"LATTRCMDS",   fun_lattrcmds,  MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"LATTRP",      fun_lattrp,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
#ifdef REALITY_LVLS
    {"LISTRLEVELS", fun_listrlevels, MAX_ARG, 0,       0,         0, CA_PUBLIC},
#endif
    {"LCMDS",       fun_lcmds,      MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {"LCON",        fun_lcon,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"LCSTR",       fun_lcstr,            1, 1,       1,         0, CA_PUBLIC},
    {"LDELETE",     fun_ldelete,    MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"LEXITS",      fun_lexits,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"LFLAGS",      fun_lflags,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"LINK",        fun_link,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"LIST",        fun_list,       MAX_ARG, 2,       3, FN_NOEVAL, CA_PUBLIC},
    {"LIT",         fun_lit,              1, 1,       1, FN_NOEVAL, CA_PUBLIC},
    {"LJUST",       fun_ljust,      MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"LN",          fun_ln,         MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"LNUM",        fun_lnum,       MAX_ARG, 0,       3,         0, CA_PUBLIC},
    {"LOC",         fun_loc,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"LOCALIZE",    fun_localize,   MAX_ARG, 1,       1, FN_NOEVAL, CA_PUBLIC},
    {"LOCATE",      fun_locate,     MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {"LOCK",        fun_lock,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"LOG",         fun_log,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"LOR",         fun_lor,        MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"LPARENT",     fun_lparent,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"LPORTS",      fun_lports,     MAX_ARG, 0,       0,         0, CA_WIZARD},
    {"LPOS",        fun_lpos,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"LRAND",       fun_lrand,      MAX_ARG, 3,       4,         0, CA_PUBLIC},
    {"LROOMS",      fun_lrooms,     MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {"LSTACK",      fun_lstack,     MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"LT",          fun_lt,         MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"LTE",         fun_lte,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"LWHO",        fun_lwho,       MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"MAIL",        fun_mail,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"MAILFROM",    fun_mailfrom,   MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"MAP",         fun_map,        MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"MATCH",       fun_match,      MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"MATCHALL",    fun_matchall,   MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"MAX",         fun_max,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {"MEMBER",      fun_member,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"MERGE",       fun_merge,      MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {"MID",         fun_mid,        MAX_ARG, 3,       3,         0, CA_PUBLIC},
    {"MIN",         fun_min,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {"MIX",         fun_mix,        MAX_ARG, 3,      12,         0, CA_PUBLIC},
    {"MOD",         fun_mod,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"MONEY",       fun_money,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"MONIKER",     fun_moniker,    MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"MOTD",        fun_motd,       MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {"MTIME",       fun_mtime,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"MUDNAME",     fun_mudname,    MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {"MUL",         fun_mul,        MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {"MUNGE",       fun_munge,      MAX_ARG, 3,       4,         0, CA_PUBLIC},
    {"NAME",        fun_name,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"NEARBY",      fun_nearby,     MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"NEQ",         fun_neq,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"NEXT",        fun_next,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"NOT",         fun_not,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"NULL",        fun_null,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"NUM",         fun_num,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"OBJ",         fun_obj,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"OBJEVAL",     fun_objeval,    MAX_ARG, 2,       2, FN_NOEVAL, CA_PUBLIC},
    {"OBJMEM",      fun_objmem,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"OEMIT",       fun_oemit,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"OR",          fun_or,         MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {"ORBOOL",      fun_orbool,     MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {"ORD",         fun_ord,        MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"ORFLAGS",     fun_orflags,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"OWNER",       fun_owner,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"PACK",        fun_pack,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"PARENT",      fun_parent,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"PARSE",       fun_iter,       MAX_ARG, 2,       4, FN_NOEVAL, CA_PUBLIC},
    {"PEEK",        fun_peek,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"PEMIT",       fun_pemit,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"PFIND",       fun_pfind,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"PI",          fun_pi,         MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {"PICKRAND",    fun_pickrand,   MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"PLAYMEM",     fun_playmem,    MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"PMATCH",      fun_pmatch,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"POLL",        fun_poll,       MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {"POP",         fun_pop,        MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"PORTS",       fun_ports,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"POS",         fun_pos,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"POSS",        fun_poss,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"POWER",       fun_power,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"POWERS",      fun_powers,     MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"PUSH",        fun_push,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"R",           fun_r,          MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"RAND",        fun_rand,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"REGMATCH",    fun_regmatch,   MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"REGMATCHI",   fun_regmatchi,  MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"REGRAB",      fun_regrab,     MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"REGRABALL",   fun_regraball,  MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"REGRABALLI",  fun_regraballi, MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"REGRABI",     fun_regrabi,    MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"REMAINDER",   fun_remainder,  MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"REMIT",       fun_remit,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"REMOVE",      fun_remove,     MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"REPEAT",      fun_repeat,     MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"REPLACE",     fun_replace,    MAX_ARG, 3,       4,         0, CA_PUBLIC},
    {"REST",        fun_rest,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"REVERSE",     fun_reverse,          1, 1,       1,         0, CA_PUBLIC},
    {"REVWORDS",    fun_revwords,   MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {"RIGHT",       fun_right,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"RJUST",       fun_rjust,      MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"RLOC",        fun_rloc,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"ROMAN",       fun_roman,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"ROOM",        fun_room,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"ROUND",       fun_round,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
#ifdef REALITY_LVLS
    {"RXLEVEL",     fun_rxlevel,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
#endif /* REALITY_LVLS */
    {"S",           fun_s,                1, 1,       1,         0, CA_PUBLIC},
    {"SCRAMBLE",    fun_scramble,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"SEARCH",      fun_search,           1, 0,       1,         0, CA_PUBLIC},
    {"SECS",        fun_secs,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"SECURE",      fun_secure,           1, 1,       1,         0, CA_PUBLIC},
    {"SET",         fun_set,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"SETDIFF",     fun_setdiff,    MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"SETINTER",    fun_setinter,   MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"SETQ",        fun_setq,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"SETR",        fun_setr,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"SETUNION",    fun_setunion,   MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"SHA1",        fun_sha1,             1, 0,       1,         0, CA_PUBLIC},
    {"SHL",         fun_shl,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"SHR",         fun_shr,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"SHUFFLE",     fun_shuffle,    MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {"SIGN",        fun_sign,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"SIN",         fun_sin,        MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"SINGLETIME",  fun_singletime, MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"SORT",        fun_sort,       MAX_ARG, 1,       4,         0, CA_PUBLIC},
    {"SORTBY",      fun_sortby,     MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"SPACE",       fun_space,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"SPELLNUM",    fun_spellnum,   MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"SPLICE",      fun_splice,     MAX_ARG, 3,       5,         0, CA_PUBLIC},
    {"SQRT",        fun_sqrt,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"SQUISH",      fun_squish,     MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"STARTSECS",   fun_startsecs,  MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {"STARTTIME",   fun_starttime,  MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {"STATS",       fun_stats,      MAX_ARG, 0,       1,         0, CA_PUBLIC},
    {"STRCAT",      fun_strcat,     MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {"STRIP",       fun_strip,      MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"STRIPACCENTS",fun_stripaccents, MAX_ARG, 1,     1,         0, CA_PUBLIC},
    {"STRIPANSI",   fun_stripansi,  MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"STRLEN",      fun_strlen,           1, 0,       1,         0, CA_PUBLIC},
    {"STRMATCH",    fun_strmatch,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"STRMEM",      fun_strmem,           1, 0,       1,         0, CA_PUBLIC},
    {"STRTRUNC",    fun_strtrunc,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"SUB",         fun_sub,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"SUBEVAL",     fun_subeval,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"SUBJ",        fun_subj,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"SWITCH",      fun_switch,     MAX_ARG, 2, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {"T",           fun_t,                1, 0,       1,         0, CA_PUBLIC},
    {"TABLE",       fun_table,      MAX_ARG, 1,       6,         0, CA_PUBLIC},
    {"TAN",         fun_tan,        MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"TEL",         fun_tel,        MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"TEXTFILE",    fun_textfile,   MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"TIME",        fun_time,       MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"TIMEFMT",     fun_timefmt,    MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"TRANSLATE",   fun_translate,  MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"TRIM",        fun_trim,       MAX_ARG, 1,       3,         0, CA_PUBLIC},
    {"TRUNC",       fun_trunc,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
#ifdef REALITY_LVLS
    {"TXLEVEL",     fun_txlevel,    MAX_ARG, 1,       1,         0, CA_PUBLIC},
#endif /* REALITY_LVLS */
    {"TYPE",        fun_type,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"U",           fun_u,          MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {"UCSTR",       fun_ucstr,            1, 1,       1,         0, CA_PUBLIC},
    {"UDEFAULT",    fun_udefault,   MAX_ARG, 2, MAX_ARG, FN_NOEVAL, CA_PUBLIC},
    {"ULOCAL",      fun_ulocal,     MAX_ARG, 1, MAX_ARG,         0, CA_PUBLIC},
    {"UNPACK",      fun_unpack,     MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"V",           fun_v,          MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"VADD",        fun_vadd,       MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"VALID",       fun_valid,      MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"VCROSS",      fun_vcross,     MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"VDIM",        fun_words,      MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"VDOT",        fun_vdot,       MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"VERSION",     fun_version,    MAX_ARG, 0,       0,         0, CA_PUBLIC},
    {"VISIBLE",     fun_visible,    MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"VMAG",        fun_vmag,       MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"VMUL",        fun_vmul,       MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"VSUB",        fun_vsub,       MAX_ARG, 2,       4,         0, CA_PUBLIC},
    {"VUNIT",       fun_vunit,      MAX_ARG, 1,       2,         0, CA_PUBLIC},
    {"WHERE",       fun_where,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"WIDTH",       fun_width,      MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"WORDPOS",     fun_wordpos,    MAX_ARG, 2,       3,         0, CA_PUBLIC},
    {"WORDS",       fun_words,      MAX_ARG, 0,       2,         0, CA_PUBLIC},
    {"WRAP",        fun_wrap,       MAX_ARG, 1,       8,         0, CA_PUBLIC},
    {"WRITETIME",   fun_writetime,  MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"XGET",        fun_xget,       MAX_ARG, 2,       2,         0, CA_PUBLIC},
    {"XOR",         fun_xor,        MAX_ARG, 0, MAX_ARG,         0, CA_PUBLIC},
    {"ZFUN",        fun_zfun,       MAX_ARG, 2,      11,         0, CA_PUBLIC},
    {"ZONE",        fun_zone,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {"ZWHO",        fun_zwho,       MAX_ARG, 1,       1,         0, CA_PUBLIC},
    {NULL,          NULL,           MAX_ARG, 0,       0,         0, 0}
};

void function_add(FUN *fp)
{
    char *buff = alloc_sbuf("init_functab");
    char *bp = buff;
    safe_sb_str(fp->name, buff, &bp);
    *bp = '\0';
    mux_strlwr(buff);
    hashaddLEN(buff, strlen(buff), fp, &mudstate.func_htab);
    free_sbuf(buff);
}

void functions_add(FUN funlist[])
{
    char *buff = alloc_sbuf("init_functab");
    for (FUN *fp = funlist; fp->name; fp++)
    {
        char *bp = buff;
        safe_sb_str(fp->name, buff, &bp);
        *bp = '\0';
        mux_strlwr(buff);
        hashaddLEN(buff, strlen(buff), fp, &mudstate.func_htab);
    }
    free_sbuf(buff);
}

void init_functab(void)
{
    functions_add(builtin_function_list);
    ufun_head = NULL;
}

void do_function
(
    dbref executor,
    dbref caller,
    dbref enactor,
    int   key,
    int   nargs,
    char *fname,
    char *target
)
{
    UFUN *ufp, *ufp2;
    ATTR *ap;

    if ((key & FN_LIST) || !fname || *fname == '\0')
    {
        notify(executor, tprintf("%-28s   %-8s  %-30s Flgs",
            "Function Name", "DBref#", "Attribute"));
        notify(executor, tprintf("%28s   %8s  %30s %4s",
            "----------------------------", "--------",
            "------------------------------", " -- "));

        int count = 0;
        for (ufp2 = ufun_head; ufp2; ufp2 = ufp2->next)
        {
            const char *pName = "(WARNING: Bad Attribute Number)";
            ap = atr_num(ufp2->atr);
            if (ap)
            {
                pName = ap->name;
            }
            notify(executor, tprintf("%-28.28s   #%-7d  %-30.30s  %c%c",
                ufp2->name, ufp2->obj, pName, ((ufp2->flags & FN_PRIV) ? 'W' : '-'),
                ((ufp2->flags & FN_PRES) ? 'p' : '-')));
            count++;
        }

        notify(executor, tprintf("%28s   %8s  %30s %4s",
            "----------------------------", "--------",
            "------------------------------", " -- "));

        notify(executor, tprintf("Total User-Defined Functions: %d", count));
        return;
    }

    char *np, *bp;
    ATTR *pattr;
    dbref obj;

    // Make a local uppercase copy of the function name.
    //
    bp = np = alloc_sbuf("add_user_func");
    safe_sb_str(fname, np, &bp);
    *bp = '\0';
    mux_strlwr(np);

    // Verify that the function doesn't exist in the builtin table.
    //
    if (hashfindLEN(np, strlen(np), &mudstate.func_htab) != NULL)
    {
        notify_quiet(executor, "Function already defined in builtin function table.");
        free_sbuf(np);
        return;
    }

    // Make sure the target object exists.
    //
    if (!parse_attrib(executor, target, &obj, &pattr))
    {
        notify_quiet(executor, NOMATCH_MESSAGE);
        free_sbuf(np);
        return;
    }


    // Make sure the attribute exists.
    //
    if (!pattr)
    {
        notify_quiet(executor, "No such attribute.");
        free_sbuf(np);
        return;
    }

    // Make sure attribute is readably by me.
    //
    if (!See_attr(executor, obj, pattr))
    {
        notify_quiet(executor, NOPERM_MESSAGE);
        free_sbuf(np);
        return;
    }

    // Privileged functions require you control the obj.
    //
    if ((key & FN_PRIV) && !Controls(executor, obj))
    {
        notify_quiet(executor, NOPERM_MESSAGE);
        free_sbuf(np);
        return;
    }

    // See if function already exists.  If so, redefine it.
    //
    ufp = (UFUN *) hashfindLEN(np, strlen(np), &mudstate.ufunc_htab);

    if (!ufp)
    {
        ufp = (UFUN *) MEMALLOC(sizeof(UFUN));
        ISOUTOFMEMORY(ufp);
        ufp->name = StringClone(np);
        mux_strupr(ufp->name);
        ufp->obj = obj;
        ufp->atr = pattr->number;
        ufp->perms = CA_PUBLIC;
        ufp->next = NULL;
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
        hashaddLEN(np, strlen(np), ufp, &mudstate.ufunc_htab);
    }
    ufp->obj = obj;
    ufp->atr = pattr->number;
    ufp->flags = key;
    free_sbuf(np);
    if (!Quiet(executor))
    {
        notify_quiet(executor, tprintf("Function %s defined.", fname));
    }
}

// ---------------------------------------------------------------------------
// list_functable: List available functions.
//
void list_functable(dbref player)
{
    char *buff = alloc_lbuf("list_functable");
    char *bp = buff;

    safe_str("Functions:", buff, &bp);

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
    safe_str("User-Functions:", buff, &bp);

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
    char *ap;
    for (ap = str; *ap && !mux_isspace(*ap); ap++)
    {
        *ap = mux_tolower(*ap); // Nothing.
    }
    int nstr = ap - str;

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
    cf_log_notfound(player, cmd, "Function", str);
    return -1;
}
