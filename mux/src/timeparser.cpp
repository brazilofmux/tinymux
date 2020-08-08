/*! \file timeparser.cpp
 * \brief General Date Parser.
 *
 * This file contains code related to parsing date strings.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "mathutil.h"

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
    UTF8     *pToken;
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

static PD_Node *PD_NewNode(void);
static void PD_AppendNode(PD_Node *pNode);
static void PD_InsertAfter(PD_Node *pWhere, PD_Node *pNode);
typedef void BREAK_DOWN_FUNC(PD_Node *pNode);
typedef struct tag_pd_breakdown
{
    unsigned int mask;
    BREAK_DOWN_FUNC *fpBreakDown;
} PD_BREAKDOWN;

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
static bool isValidYear(size_t nStr, UTF8 *pStr, int iValue)
{
    UNUSED_PARAMETER(pStr);
    UNUSED_PARAMETER(iValue);

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

static bool isValidMonth(size_t nStr, UTF8 *pStr, int iValue)
{
    UNUSED_PARAMETER(pStr);

    // Month may be 1 through 9, 01 through 09, 10, 11, or 12.
    //
    if (  1 <= nStr && nStr <= 2
       && 1 <= iValue && iValue <= 12)
    {
        return true;
    }
    return false;
}

static bool isValidDayOfMonth(size_t nStr, UTF8 *pStr, int iValue)
{
    UNUSED_PARAMETER(pStr);

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

static bool isValidDayOfWeek(size_t nStr, UTF8 *pStr, int iValue)
{
    UNUSED_PARAMETER(pStr);

    // Day Of Week may be 1 through 7.
    //
    if (  1 == nStr
       && 1 <= iValue && iValue <= 7)
    {
        return true;
    }
    return false;
}

static bool isValidDayOfYear(size_t nStr, UTF8 *pStr, int iValue)
{
    UNUSED_PARAMETER(pStr);

    // Day Of Year 001 through 366
    //
    if (  3 == nStr
       && 1 <= iValue && iValue <= 366)
    {
        return true;
    }
    return false;
}

static bool isValidWeekOfYear(size_t nStr, UTF8 *pStr, int iValue)
{
    UNUSED_PARAMETER(pStr);

    // Week Of Year may be 01 through 53.
    //
    if (  2 == nStr
       && 1 <= iValue && iValue <= 53)
    {
        return true;
    }
    return false;
}

static bool isValidHour(size_t nStr, UTF8 *pStr, int iValue)
{
    UNUSED_PARAMETER(pStr);

    // Hour may be 0 through 9, 00 through 09, 10 through 19, 20 through 24.
    //
    if (  1 <= nStr && nStr <= 2
       && 0 <= iValue && iValue <= 24)
    {
        return true;
    }
    return false;
}

static bool isValidMinute(size_t nStr, UTF8 *pStr, int iValue)
{
    UNUSED_PARAMETER(pStr);

    // Minute may be 00 through 59.
    //
    if (  2 == nStr
       && 0 <= iValue && iValue <= 59)
    {
        return true;
    }
    return false;
}

static bool isValidSecond(size_t nStr, UTF8 *pStr, int iValue)
{
    UNUSED_PARAMETER(pStr);

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

static bool isValidSubSecond(size_t nStr, UTF8 *pStr, int iValue)
{
    UNUSED_PARAMETER(pStr);
    UNUSED_PARAMETER(iValue);

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
static bool isValidHMS(size_t nStr, UTF8 *pStr, int iValue)
{
    int iHour, iMinutes, iSeconds;
    switch (nStr)
    {
    case 1:
    case 2:
        return isValidHour(nStr, pStr, iValue);

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

static void SplitLastTwoDigits(PD_Node *pNode, unsigned mask)
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

static void SplitLastThreeDigits(PD_Node *pNode, unsigned mask)
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
static void BreakDownHMS(PD_Node *pNode)
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
static bool isValidYMD(size_t nStr, UTF8 *pStr, int iValue)
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
static void BreakDownYMD(PD_Node *pNode)
{
    pNode->uCouldBe = PDCB_YEAR;
    SplitLastTwoDigits(pNode, PDCB_DAY_OF_MONTH);
    SplitLastTwoDigits(pNode, PDCB_MONTH);
}

// This function handles MMDDYY
//
static bool isValidMDY(size_t nStr, UTF8 *pStr, int iValue)
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
static void BreakDownMDY(PD_Node *pNode)
{
    pNode->uCouldBe = PDCB_MONTH;
    SplitLastTwoDigits(pNode, PDCB_YEAR);
    SplitLastTwoDigits(pNode, PDCB_DAY_OF_MONTH);
}

// This function handles DDMMYY
//
static bool isValidDMY(size_t nStr, UTF8 *pStr, int iValue)
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
static void BreakDownDMY(PD_Node *pNode)
{
    pNode->uCouldBe = PDCB_DAY_OF_MONTH;
    SplitLastTwoDigits(pNode, PDCB_YEAR);
    SplitLastTwoDigits(pNode, PDCB_MONTH);
}

// This function handles YDDD, YYDDD, YYYDDD, YYYYDDD, YYYYYDDD
//
static bool isValidYD(size_t nStr, UTF8 *pStr, int iValue)
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
static void BreakDownYD(PD_Node *pNode)
{
    pNode->uCouldBe = PDCB_YEAR;
    SplitLastThreeDigits(pNode, PDCB_DAY_OF_YEAR);
}

static const int InitialCouldBe[9] =
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

typedef bool PVALIDFUNC(size_t nStr, UTF8 *pStr, int iValue);

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
static void ClassifyNumericToken(PD_Node *pNode)
{
    size_t nToken = pNode->nToken;
    UTF8  *pToken = pNode->pToken;
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
    const UTF8  *szText;
    unsigned int uCouldBe;
    int          iValue;
} PD_TEXT_ENTRY;

const PD_TEXT_ENTRY PD_TextTable[] =
{
    {T("sun"),       PDCB_DAY_OF_WEEK,   7 },
    {T("mon"),       PDCB_DAY_OF_WEEK,   1 },
    {T("tue"),       PDCB_DAY_OF_WEEK,   2 },
    {T("wed"),       PDCB_DAY_OF_WEEK,   3 },
    {T("thu"),       PDCB_DAY_OF_WEEK,   4 },
    {T("fri"),       PDCB_DAY_OF_WEEK,   5 },
    {T("sat"),       PDCB_DAY_OF_WEEK,   6 },
    {T("jan"),       PDCB_MONTH,         1 },
    {T("feb"),       PDCB_MONTH,         2 },
    {T("mar"),       PDCB_MONTH,         3 },
    {T("apr"),       PDCB_MONTH,         4 },
    {T("may"),       PDCB_MONTH,         5 },
    {T("jun"),       PDCB_MONTH,         6 },
    {T("jul"),       PDCB_MONTH,         7 },
    {T("aug"),       PDCB_MONTH,         8 },
    {T("sep"),       PDCB_MONTH,         9 },
    {T("oct"),       PDCB_MONTH,        10 },
    {T("nov"),       PDCB_MONTH,        11 },
    {T("dec"),       PDCB_MONTH,        12 },
    {T("january"),   PDCB_MONTH,         1 },
    {T("february"),  PDCB_MONTH,         2 },
    {T("march"),     PDCB_MONTH,         3 },
    {T("april"),     PDCB_MONTH,         4 },
    {T("may"),       PDCB_MONTH,         5 },
    {T("june"),      PDCB_MONTH,         6 },
    {T("july"),      PDCB_MONTH,         7 },
    {T("august"),    PDCB_MONTH,         8 },
    {T("september"), PDCB_MONTH,         9 },
    {T("october"),   PDCB_MONTH,        10 },
    {T("november"),  PDCB_MONTH,        11 },
    {T("december"),  PDCB_MONTH,        12 },
    {T("sunday"),    PDCB_DAY_OF_WEEK,   7 },
    {T("monday"),    PDCB_DAY_OF_WEEK,   1 },
    {T("tuesday"),   PDCB_DAY_OF_WEEK,   2 },
    {T("wednesday"), PDCB_DAY_OF_WEEK,   3 },
    {T("thursday"),  PDCB_DAY_OF_WEEK,   4 },
    {T("friday"),    PDCB_DAY_OF_WEEK,   5 },
    {T("saturday"),  PDCB_DAY_OF_WEEK,   6 },
    {T("a"),         PDCB_TIMEZONE,    100 },
    {T("b"),         PDCB_TIMEZONE,    200 },
    {T("c"),         PDCB_TIMEZONE,    300 },
    {T("d"),         PDCB_TIMEZONE,    400 },
    {T("e"),         PDCB_TIMEZONE,    500 },
    {T("f"),         PDCB_TIMEZONE,    600 },
    {T("g"),         PDCB_TIMEZONE,    700 },
    {T("h"),         PDCB_TIMEZONE,    800 },
    {T("i"),         PDCB_TIMEZONE,    900 },
    {T("k"),         PDCB_TIMEZONE,   1000 },
    {T("l"),         PDCB_TIMEZONE,   1100 },
    {T("m"),         PDCB_TIMEZONE,   1200 },
    {T("n"),         PDCB_TIMEZONE,   -100 },
    {T("o"),         PDCB_TIMEZONE,   -200 },
    {T("p"),         PDCB_TIMEZONE,   -300 },
    {T("q"),         PDCB_TIMEZONE,   -400 },
    {T("r"),         PDCB_TIMEZONE,   -500 },
    {T("s"),         PDCB_TIMEZONE,   -600 },
    {T("t"),         PDCB_DATE_TIME_SEPARATOR|PDCB_TIMEZONE, -700},
    {T("u"),         PDCB_TIMEZONE,   -800 },
    {T("v"),         PDCB_TIMEZONE,   -900 },
    {T("w"),         PDCB_WEEK_OF_YEAR_PREFIX|PDCB_TIMEZONE,  -1000 },
    {T("x"),         PDCB_TIMEZONE,  -1100 },
    {T("y"),         PDCB_TIMEZONE,  -1200 },
    {T("z"),         PDCB_TIMEZONE,      0 },
    {T("hst"),       PDCB_TIMEZONE,  -1000 },
    {T("akst"),      PDCB_TIMEZONE,   -900 },
    {T("pst"),       PDCB_TIMEZONE,   -800 },
    {T("mst"),       PDCB_TIMEZONE,   -700 },
    {T("cst"),       PDCB_TIMEZONE,   -600 },
    {T("est"),       PDCB_TIMEZONE,   -500 },
    {T("ast"),       PDCB_TIMEZONE,   -400 },
    {T("akdt"),      PDCB_TIMEZONE,   -800 },
    {T("pdt"),       PDCB_TIMEZONE,   -700 },
    {T("mdt"),       PDCB_TIMEZONE,   -600 },
    {T("cdt"),       PDCB_TIMEZONE,   -500 },
    {T("edt"),       PDCB_TIMEZONE,   -400 },
    {T("adt"),       PDCB_TIMEZONE,   -300 },
    {T("bst"),       PDCB_TIMEZONE,    100 },
    {T("ist"),       PDCB_TIMEZONE,    100 },
    {T("cet"),       PDCB_TIMEZONE,    100 },
    {T("cest"),      PDCB_TIMEZONE,    200 },
    {T("eet"),       PDCB_TIMEZONE,    200 },
    {T("eest"),      PDCB_TIMEZONE,    300 },
    {T("aest"),      PDCB_TIMEZONE,   1000 },
    {T("gmt"),       PDCB_TIMEZONE,      0 },
    {T("ut"),        PDCB_TIMEZONE,      0 },
    {T("utc"),       PDCB_TIMEZONE,      0 },
    {T("st"),        PDCB_DAY_OF_MONTH_SUFFIX, 0 },
    {T("nd"),        PDCB_DAY_OF_MONTH_SUFFIX, 0 },
    {T("rd"),        PDCB_DAY_OF_MONTH_SUFFIX, 0 },
    {T("th"),        PDCB_DAY_OF_MONTH_SUFFIX, 0 },
    {T("am"),        PDCB_MERIDIAN,      0 },
    {T("pm"),        PDCB_MERIDIAN,     12 },
    {(UTF8 *) 0, 0, 0}
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

static int nNodes = 0;
#define MAX_NODES 200
static PD_Node Nodes[MAX_NODES];

static PD_Node *PD_Head = 0;
static PD_Node *PD_Tail = 0;

static void PD_Reset(void)
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
    return nullptr;
}

static PD_Node *PD_FirstNode(void)
{
    return PD_Head;
}

#if 0
static PD_Node *PD_LastNode(void)
{
    return PD_Tail;
}
#endif

static PD_Node *PD_NextNode(PD_Node *pNode)
{
    return pNode->pNextNode;
}

static PD_Node *PD_PrevNode(PD_Node *pNode)
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

static void PD_RemoveNode(PD_Node *pNode)
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

static PD_Node *PD_ScanNextToken(UTF8 **ppString)
{
    UTF8 *p = *ppString;
    int ch = *p;
    if (ch == 0)
    {
        return nullptr;
    }
    PD_Node *pNode;
    int iType = LexTable[ch];
    if (iType == PD_LEX_EOS || iType == PD_LEX_INVALID)
    {
        return nullptr;
    }
    else if (iType == PD_LEX_SYMBOL)
    {
        pNode = PD_NewNode();
        if (!pNode)
        {
            return nullptr;
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
        UTF8 *pSave = p;
        do
        {
            p++;
            ch = *p;
        } while (iType == LexTable[ch]);

        pNode = PD_NewNode();
        if (!pNode)
        {
            return nullptr;
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
                UTF8 buff[10];
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
                if (  strlen((char *)PD_TextTable[j].szText) == nLen
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
                return nullptr;
            }
        }
    }
    *ppString = p;
    return pNode;
}

static const PD_BREAKDOWN BreakDownTable[] =
{
    { PDCB_HMS_TIME, BreakDownHMS },
    { PDCB_HMS_TIMEZONE, BreakDownHMS },
    { PDCB_YD,  BreakDownYD },
    { PDCB_YMD, BreakDownYMD },
    { PDCB_MDY, BreakDownMDY },
    { PDCB_DMY, BreakDownDMY },
    { 0, 0 }
};

static void PD_Pass2(void)
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

static void PD_Deduction(void)
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

static void PD_BreakItDown(void)
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

static void PD_Pass5(void)
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

static void PD_Pass6(void)
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

static bool PD_GetFields(ALLFIELDS *paf)
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

static bool ConvertAllFieldsToLinearTime(CLinearTimeAbsolute &lta, ALLFIELDS *paf)
{
    FIELDEDTIME ft;
    memset(&ft, 0, sizeof(ft));

    int iExtraDays = 0;
    if (paf->iYear == NOT_PRESENT)
    {
        return false;
    }
    ft.iYear = static_cast<short>(paf->iYear);

    if (paf->iMonthOfYear != NOT_PRESENT && paf->iDayOfMonth != NOT_PRESENT)
    {
        ft.iMonth = static_cast<unsigned short>(paf->iMonthOfYear);
        ft.iDayOfMonth = static_cast<unsigned short>(paf->iDayOfMonth);
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
        ftWD.iYear = static_cast<short>(paf->iYear - 1);
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
        ft.iHour = static_cast<unsigned short>(paf->iHourTime);
        if (paf->iMinuteTime != NOT_PRESENT)
        {
            ft.iMinute = static_cast<unsigned short>(paf->iMinuteTime);
            if (paf->iSecondTime != NOT_PRESENT)
            {
                ft.iSecond = static_cast<unsigned short>(paf->iSecondTime);
                if (paf->iMillisecondTime != NOT_PRESENT)
                {
                    ft.iMillisecond = static_cast<unsigned short>(paf->iMillisecondTime);
                    ft.iMicrosecond = static_cast<unsigned short>(paf->iMicrosecondTime);
                    ft.iNanosecond  = static_cast<unsigned short>(paf->iNanosecondTime);
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
    UTF8 *pDateString,
    bool *pbZoneSpecified
)
{
    PD_Reset();

    UTF8 *p = pDateString;
    PD_Node *pNode;
    for (  pNode = PD_ScanNextToken(&p);
           pNode;
           pNode = PD_ScanNextToken(&p))
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
