/*! \file timeparser.cpp
 * \brief General Date Parser.
 *
 * This file contains code related to parsing date strings.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

enum class TokenType {
    Symbol = 1,
    NumericUnsigned = 2,
    Spaces = 3,
    Text = 4,
    NumericSigned = 5
};

enum class NodeFlag : unsigned {
    Nothing              = 0x00000000,
    TimeFieldSeparator   = 0x00000001,
    DateFieldSeparator   = 0x00000002,
    Whitespace           = 0x00000004,
    DayOfMonthSuffix     = 0x00000008,
    Sign                 = 0x00000010,
    SecondsDecimal       = 0x00000020,
    Removeable           = 0x00000040,
    Year                 = 0x00000080,
    Month                = 0x00000100,
    DayOfMonth           = 0x00000200,
    DayOfWeek            = 0x00000400,
    WeekOfYear           = 0x00000800,
    DayOfYear            = 0x00001000,
    YD                   = 0x00002000,
    YMD                  = 0x00004000,
    MDY                  = 0x00008000,
    DMY                  = 0x00010000,
    DateTimeSeparator    = 0x00020000,
    TimeZone             = 0x00040000,
    WeekOfYearPrefix     = 0x00080000,
    Meridian             = 0x00100000,
    Minute               = 0x00200000,
    Second               = 0x00400000,
    Subsecond            = 0x00800000,
    HourTime             = 0x01000000,
    HMSTime              = 0x02000000,
    HourTimeZone         = 0x04000000,
    HMSTimeZone          = 0x08000000
};

// Allows for combining of individual flags
inline NodeFlag operator|(NodeFlag a, NodeFlag b) {
    return static_cast<NodeFlag>(
        static_cast<unsigned>(a) | static_cast<unsigned>(b));
}

// tests whether any flag in the set is present
inline bool hasAnyFlag(NodeFlag value, NodeFlag flags) {
    return (static_cast<unsigned>(value) & static_cast<unsigned>(flags)) != 0;
}

// tests that all flags in the set are present
inline bool hasAllFlags(NodeFlag value, NodeFlag flags) {
    return (static_cast<unsigned>(value) & static_cast<unsigned>(flags))
           == static_cast<unsigned>(flags);
}

// tests that all flags are in the set and no others
inline bool hasOnlyFlags(NodeFlag value, NodeFlag flags) {
    return (static_cast<unsigned>(value) & ~static_cast<unsigned>(flags)) == 0;
}

// tests that other flags besides those in the set are present
inline bool hasAnyFlagsNotIn(NodeFlag value, NodeFlag flags) {
    return (static_cast<unsigned>(value) & ~static_cast<unsigned>(flags)) != 0;
}

// allows combining sets of flags
inline NodeFlag& addFlags(NodeFlag& value, NodeFlag flags) {
    value = static_cast<NodeFlag>(
        static_cast<unsigned>(value) | static_cast<unsigned>(flags));
    return value;
}

// retains only flags from the set
inline NodeFlag& keepOnlyFlags(NodeFlag& value, NodeFlag flags) {
    value = static_cast<NodeFlag>(
        static_cast<unsigned>(value) & static_cast<unsigned>(flags));
    return value;
}

// removes flags from the set
inline NodeFlag removeFlags(NodeFlag& value, NodeFlag bitsToRemove) {
    value = static_cast<NodeFlag>(
        static_cast<unsigned>(value) & ~static_cast<unsigned>(bitsToRemove));
    return value;
}

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
    NodeFlag  uCouldBe;

    // These fields deal with token things and we avoid mixing
    // them up in higher meanings. This is the lowest level.
    //
    // Further Notes:
    //
    // TokenType::Symbol is always a -single- (nToken==1) character.
    //
    // uTokenType must be one of the PDTT_* values.
    //
    // TokenType::NumericSigned, TokenType::NumericaUnsigned, and
    // TokenType::Text types may have an iToken value associated with
    // them.
    //
    TokenType uTokenType;
    UTF8     *pToken;
    size_t    nToken;
    int       iToken;

    // Link to previous and next node.
    //
    struct tag_pd_node *pNextNode;
    struct tag_pd_node *pPrevNode;

} PD_Node;

static PD_Node *PD_NewNode(void);
static void PD_AppendNode(PD_Node *pNode);
static void PD_InsertAfter(PD_Node *pWhere, PD_Node *pNode);
typedef void BREAK_DOWN_FUNC(PD_Node *pNode);
typedef struct tag_pd_breakdown
{
    NodeFlag mask;
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

static void SplitLastTwoDigits(PD_Node *pNode, NodeFlag mask)
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

static void SplitLastThreeDigits(PD_Node *pNode, NodeFlag mask)
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
    if (hasAnyFlag(pNode->uCouldBe, NodeFlag::HMSTime))
    {
        pNode->uCouldBe = NodeFlag::HourTime;
    }
    else
    {
        pNode->uCouldBe = NodeFlag::HourTimeZone;
    }
    switch (pNode->nToken)
    {
    case 5:
    case 6:
        SplitLastTwoDigits(pNode, NodeFlag::Second);

    case 3:
    case 4:
        SplitLastTwoDigits(pNode, NodeFlag::Minute);
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
    pNode->uCouldBe = NodeFlag::Year;
    SplitLastTwoDigits(pNode, NodeFlag::DayOfMonth);
    SplitLastTwoDigits(pNode, NodeFlag::Month);
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
    pNode->uCouldBe = NodeFlag::Month;
    SplitLastTwoDigits(pNode, NodeFlag::Year);
    SplitLastTwoDigits(pNode, NodeFlag::DayOfMonth);
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
    pNode->uCouldBe = NodeFlag::DayOfMonth;
    SplitLastTwoDigits(pNode, NodeFlag::Year);
    SplitLastTwoDigits(pNode, NodeFlag::Month);
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
    pNode->uCouldBe = NodeFlag::Year;
    SplitLastThreeDigits(pNode, NodeFlag::DayOfYear);
}

static const NodeFlag InitialCouldBe[9] =
{
    NodeFlag::Year|NodeFlag::Month|NodeFlag::DayOfMonth|NodeFlag::DayOfWeek|NodeFlag::HMSTime|NodeFlag::HMSTimeZone|NodeFlag::HourTime|NodeFlag::HourTimeZone|NodeFlag::Subsecond,  // 1
    NodeFlag::Year|NodeFlag::Month|NodeFlag::DayOfMonth|NodeFlag::WeekOfYear|NodeFlag::HMSTime|NodeFlag::HMSTimeZone|NodeFlag::HourTime|NodeFlag::HourTimeZone|NodeFlag::Minute|NodeFlag::Second|NodeFlag::Subsecond, // 2
    NodeFlag::Year|NodeFlag::HMSTime|NodeFlag::HMSTimeZone|NodeFlag::DayOfYear|NodeFlag::Subsecond, // 3
    NodeFlag::Year|NodeFlag::HMSTime|NodeFlag::HMSTimeZone|NodeFlag::YD|NodeFlag::Subsecond, // 4
    NodeFlag::Year|NodeFlag::HMSTime|NodeFlag::HMSTimeZone|NodeFlag::YD|NodeFlag::Subsecond, // 5
    NodeFlag::HMSTime|NodeFlag::HMSTimeZone|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY|NodeFlag::YD|NodeFlag::Subsecond, // 6
    NodeFlag::YMD|NodeFlag::YD|NodeFlag::Subsecond, // 7
    NodeFlag::YMD|NodeFlag::YD, // 8
    NodeFlag::YMD  // 9
};

typedef bool PVALIDFUNC(size_t nStr, UTF8 *pStr, int iValue);

typedef struct tag_pd_numeric_valid
{
    NodeFlag mask;
    PVALIDFUNC *fnValid;
} NUMERIC_VALID_RECORD;

const NUMERIC_VALID_RECORD NumericSet[] =
{
    { NodeFlag::Year,         isValidYear       },
    { NodeFlag::Month,        isValidMonth      },
    { NodeFlag::DayOfMonth,   isValidDayOfMonth },
    { NodeFlag::DayOfYear,    isValidDayOfYear  },
    { NodeFlag::WeekOfYear,   isValidWeekOfYear },
    { NodeFlag::DayOfWeek,    isValidDayOfWeek  },
    { NodeFlag::HMSTime|NodeFlag::HMSTimeZone, isValidHMS },
    { NodeFlag::YMD,          isValidYMD        },
    { NodeFlag::MDY,          isValidMDY        },
    { NodeFlag::DMY,          isValidDMY        },
    { NodeFlag::YD,           isValidYD         },
    { NodeFlag::HourTime|NodeFlag::HourTimeZone, isValidHour },
    { NodeFlag::Minute,       isValidMinute     },
    { NodeFlag::Second,       isValidSecond     },
    { NodeFlag::Subsecond,    isValidSubSecond  },
    { NodeFlag::Nothing, 0},
};

// This function looks at the numeric token and assigns the initial set
// of possibilities.
//
static void ClassifyNumericToken(PD_Node *pNode)
{
    size_t nToken = pNode->nToken;
    UTF8  *pToken = pNode->pToken;
    int    iToken = pNode->iToken;

    NodeFlag uCouldBe = InitialCouldBe[nToken-1];

    int i = 0;
    NodeFlag mask = NodeFlag::Nothing;
    while ((mask = NumericSet[i].mask) != NodeFlag::Nothing)
    {
        if (hasAnyFlag(uCouldBe, mask) && !(NumericSet[i].fnValid(nToken, pToken, iToken)))
        {
            removeFlags(uCouldBe, mask);
        }
        i++;
    }
    pNode->uCouldBe = uCouldBe;
}

typedef struct
{
    const UTF8  *szText;
    NodeFlag    uCouldBe;
    int         iValue;
} PD_TEXT_ENTRY;

const PD_TEXT_ENTRY PD_TextTable[] =
{
    {T("sun"),       NodeFlag::DayOfWeek,   7 },
    {T("mon"),       NodeFlag::DayOfWeek,   1 },
    {T("tue"),       NodeFlag::DayOfWeek,   2 },
    {T("wed"),       NodeFlag::DayOfWeek,   3 },
    {T("thu"),       NodeFlag::DayOfWeek,   4 },
    {T("fri"),       NodeFlag::DayOfWeek,   5 },
    {T("sat"),       NodeFlag::DayOfWeek,   6 },
    {T("jan"),       NodeFlag::Month,         1 },
    {T("feb"),       NodeFlag::Month,         2 },
    {T("mar"),       NodeFlag::Month,         3 },
    {T("apr"),       NodeFlag::Month,         4 },
    {T("may"),       NodeFlag::Month,         5 },
    {T("jun"),       NodeFlag::Month,         6 },
    {T("jul"),       NodeFlag::Month,         7 },
    {T("aug"),       NodeFlag::Month,         8 },
    {T("sep"),       NodeFlag::Month,         9 },
    {T("oct"),       NodeFlag::Month,        10 },
    {T("nov"),       NodeFlag::Month,        11 },
    {T("dec"),       NodeFlag::Month,        12 },
    {T("january"),   NodeFlag::Month,         1 },
    {T("february"),  NodeFlag::Month,         2 },
    {T("march"),     NodeFlag::Month,         3 },
    {T("april"),     NodeFlag::Month,         4 },
    {T("may"),       NodeFlag::Month,         5 },
    {T("june"),      NodeFlag::Month,         6 },
    {T("july"),      NodeFlag::Month,         7 },
    {T("august"),    NodeFlag::Month,         8 },
    {T("september"), NodeFlag::Month,         9 },
    {T("october"),   NodeFlag::Month,        10 },
    {T("november"),  NodeFlag::Month,        11 },
    {T("december"),  NodeFlag::Month,        12 },
    {T("sunday"),    NodeFlag::DayOfWeek,   7 },
    {T("monday"),    NodeFlag::DayOfWeek,   1 },
    {T("tuesday"),   NodeFlag::DayOfWeek,   2 },
    {T("wednesday"), NodeFlag::DayOfWeek,   3 },
    {T("thursday"),  NodeFlag::DayOfWeek,   4 },
    {T("friday"),    NodeFlag::DayOfWeek,   5 },
    {T("saturday"),  NodeFlag::DayOfWeek,   6 },
    {T("a"),         NodeFlag::TimeZone,    100 },
    {T("b"),         NodeFlag::TimeZone,    200 },
    {T("c"),         NodeFlag::TimeZone,    300 },
    {T("d"),         NodeFlag::TimeZone,    400 },
    {T("e"),         NodeFlag::TimeZone,    500 },
    {T("f"),         NodeFlag::TimeZone,    600 },
    {T("g"),         NodeFlag::TimeZone,    700 },
    {T("h"),         NodeFlag::TimeZone,    800 },
    {T("i"),         NodeFlag::TimeZone,    900 },
    {T("k"),         NodeFlag::TimeZone,   1000 },
    {T("l"),         NodeFlag::TimeZone,   1100 },
    {T("m"),         NodeFlag::TimeZone,   1200 },
    {T("n"),         NodeFlag::TimeZone,   -100 },
    {T("o"),         NodeFlag::TimeZone,   -200 },
    {T("p"),         NodeFlag::TimeZone,   -300 },
    {T("q"),         NodeFlag::TimeZone,   -400 },
    {T("r"),         NodeFlag::TimeZone,   -500 },
    {T("s"),         NodeFlag::TimeZone,   -600 },
    {T("t"),         NodeFlag::DateTimeSeparator|NodeFlag::TimeZone, -700},
    {T("u"),         NodeFlag::TimeZone,   -800 },
    {T("v"),         NodeFlag::TimeZone,   -900 },
    {T("w"),         NodeFlag::WeekOfYearPrefix|NodeFlag::TimeZone,  -1000 },
    {T("x"),         NodeFlag::TimeZone,  -1100 },
    {T("y"),         NodeFlag::TimeZone,  -1200 },
    {T("z"),         NodeFlag::TimeZone,      0 },
    {T("hst"),       NodeFlag::TimeZone,  -1000 },
    {T("akst"),      NodeFlag::TimeZone,   -900 },
    {T("pst"),       NodeFlag::TimeZone,   -800 },
    {T("mst"),       NodeFlag::TimeZone,   -700 },
    {T("cst"),       NodeFlag::TimeZone,   -600 },
    {T("est"),       NodeFlag::TimeZone,   -500 },
    {T("ast"),       NodeFlag::TimeZone,   -400 },
    {T("akdt"),      NodeFlag::TimeZone,   -800 },
    {T("pdt"),       NodeFlag::TimeZone,   -700 },
    {T("mdt"),       NodeFlag::TimeZone,   -600 },
    {T("cdt"),       NodeFlag::TimeZone,   -500 },
    {T("edt"),       NodeFlag::TimeZone,   -400 },
    {T("adt"),       NodeFlag::TimeZone,   -300 },
    {T("bst"),       NodeFlag::TimeZone,    100 },
    {T("ist"),       NodeFlag::TimeZone,    100 },
    {T("cet"),       NodeFlag::TimeZone,    100 },
    {T("cest"),      NodeFlag::TimeZone,    200 },
    {T("eet"),       NodeFlag::TimeZone,    200 },
    {T("eest"),      NodeFlag::TimeZone,    300 },
    {T("aest"),      NodeFlag::TimeZone,   1000 },
    {T("gmt"),       NodeFlag::TimeZone,      0 },
    {T("ut"),        NodeFlag::TimeZone,      0 },
    {T("utc"),       NodeFlag::TimeZone,      0 },
    {T("st"),        NodeFlag::DayOfMonthSuffix, 0 },
    {T("nd"),        NodeFlag::DayOfMonthSuffix, 0 },
    {T("rd"),        NodeFlag::DayOfMonthSuffix, 0 },
    {T("th"),        NodeFlag::DayOfMonthSuffix, 0 },
    {T("am"),        NodeFlag::Meridian,      0 },
    {T("pm"),        NodeFlag::Meridian,     12 },
    {(UTF8 *)0,      NodeFlag::Nothing,       0 }
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
        pNode->uTokenType = TokenType::Symbol;
        if (ch == ':')
        {
            pNode->uCouldBe = NodeFlag::TimeFieldSeparator;
        }
        else if (ch == '-')
        {
            pNode->uCouldBe = NodeFlag::DateFieldSeparator|NodeFlag::Sign;
        }
        else if (ch == '+')
        {
            pNode->uCouldBe = NodeFlag::Sign;
        }
        else if (ch == '/')
        {
            pNode->uCouldBe = NodeFlag::DateFieldSeparator;
        }
        else if (ch == '.')
        {
            pNode->uCouldBe = NodeFlag::DateFieldSeparator|NodeFlag::SecondsDecimal;
        }
        else if (ch == ',')
        {
            pNode->uCouldBe = NodeFlag::Removeable|NodeFlag::SecondsDecimal|NodeFlag::DayOfMonthSuffix;
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
        pNode->uCouldBe = NodeFlag::Nothing;

        if (iType == PD_LEX_DIGIT)
        {
            pNode->uTokenType = TokenType::NumericUnsigned;

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
            pNode->uTokenType = TokenType::Spaces;
            pNode->uCouldBe = NodeFlag::Whitespace;
        }
        else if (iType == PD_LEX_ALPHA)
        {
            pNode->uTokenType = TokenType::Text;

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
    { NodeFlag::HMSTime, BreakDownHMS },
    { NodeFlag::HMSTimeZone, BreakDownHMS },
    { NodeFlag::YD,  BreakDownYD },
    { NodeFlag::YMD, BreakDownYMD },
    { NodeFlag::MDY, BreakDownMDY },
    { NodeFlag::DMY, BreakDownDMY },
    { NodeFlag::Nothing, 0 }
};

static void PD_Pass2(void)
{
    PD_Node *pNode = PD_FirstNode();
    while (pNode)
    {
        PD_Node *pPrev = PD_PrevNode(pNode);
        PD_Node *pNext = PD_NextNode(pNode);

        // Absorb information from NodeFlag::TimeFieldSeparator.
        //
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::TimeFieldSeparator))
        {
            if (pPrev && pNext)
            {
                if (  hasAnyFlag(pPrev->uCouldBe, NodeFlag::HourTime|NodeFlag::HourTimeZone)
                   && hasAnyFlag(pNext->uCouldBe, NodeFlag::Minute))
                {
                    keepOnlyFlags(pPrev->uCouldBe, NodeFlag::HourTime|NodeFlag::HourTimeZone);
                    pNext->uCouldBe = NodeFlag::Minute;
                }
                else if (  hasAnyFlag(pPrev->uCouldBe, NodeFlag::Minute)
                        && hasAnyFlag(pNext->uCouldBe, NodeFlag::Second))
                {
                    pPrev->uCouldBe = NodeFlag::Minute;
                    pNext->uCouldBe = NodeFlag::Second;
                }
            }
            pNode->uCouldBe = NodeFlag::Removeable;
        }

        // Absorb information from NodeFlag::SecondsDecimal.
        //
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::SecondsDecimal))
        {
            if (  pPrev
               && pNext
               && pPrev->uCouldBe == NodeFlag::Second
               && hasAnyFlag(pNext->uCouldBe, NodeFlag::Subsecond))
            {
                pNode->uCouldBe = NodeFlag::SecondsDecimal;
                pNext->uCouldBe = NodeFlag::Subsecond;
            }
            else
            {
                removeFlags(pNode->uCouldBe, NodeFlag::SecondsDecimal);
            }
            pNode->uCouldBe = NodeFlag::Removeable;
        }

        // Absorb information from NodeFlag::Subsecond
        //
        if (pNode->uCouldBe != NodeFlag::Subsecond)
        {
            removeFlags(pNode->uCouldBe, NodeFlag::Subsecond);
        }

        // Absorb information from NodeFlag::DateFieldSeparator.
        //
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::DateFieldSeparator))
        {
            removeFlags(pNode->uCouldBe, NodeFlag::DateFieldSeparator);

            const NodeFlag Separators = NodeFlag::Year|NodeFlag::Month|NodeFlag::DayOfMonth|NodeFlag::DayOfYear|NodeFlag::WeekOfYear|NodeFlag::DayOfWeek;
            if (  pPrev
               && pNext
               && hasAnyFlag(pPrev->uCouldBe, Separators)
               && hasAnyFlag(pNext->uCouldBe, Separators))
            {
                keepOnlyFlags(pPrev->uCouldBe, Separators);
                keepOnlyFlags(pNext->uCouldBe, Separators);
                pNode->uCouldBe = NodeFlag::Removeable;
            }
        }


        // Process NodeFlag::DayOfMonthSuffix
        //
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::DayOfMonthSuffix))
        {
            pNode->uCouldBe = NodeFlag::Removeable;
            if (  pPrev
               && hasAnyFlag(pPrev->uCouldBe, NodeFlag::DayOfMonth))
            {
                pPrev->uCouldBe = NodeFlag::DayOfMonth;
            }
        }

        // Absorb semantic meaning of NodeFlag::Sign.
        //
        if (pNode->uCouldBe == NodeFlag::Sign)
        {
            const NodeFlag SignablePositive = NodeFlag::HMSTime|NodeFlag::HMSTimeZone;
            const NodeFlag SignableNegative = NodeFlag::Year|NodeFlag::YD|SignablePositive|NodeFlag::YMD;
            NodeFlag Signable;
            if (pNode->pToken[0] == '-')
            {
                Signable = SignableNegative;
            }
            else
            {
                Signable = SignablePositive;
            }
            if (  pNext
               && hasAnyFlag(pNext->uCouldBe, Signable))
            {
                keepOnlyFlags(pNext->uCouldBe, Signable);
            }
            else
            {
                pNode->uCouldBe = NodeFlag::Removeable;
            }
        }

        // A timezone HOUR or HMS requires a leading sign.
        //
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::HMSTimeZone|NodeFlag::HourTimeZone))
        {
            if (  !pPrev
               || pPrev->uCouldBe != NodeFlag::Sign)
            {
                removeFlags(pNode->uCouldBe, NodeFlag::HMSTimeZone|NodeFlag::HourTimeZone);
            }
        }

        // Likewise, a NodeFlag::HourTime or NodeFlag::HMSTime cannot have a
        // leading sign.
        //
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::HMSTime|NodeFlag::HourTime))
        {
            if (  pPrev
               && pPrev->uCouldBe == NodeFlag::Sign)
            {
                removeFlags(pNode->uCouldBe, NodeFlag::HMSTime|NodeFlag::HourTime);
            }
        }

        // Remove NodeFlag::Whitespace.
        //
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::Whitespace|NodeFlag::Removeable))
        {
            PD_RemoveNode(pNode);
        }
        pNode = pNext;
    }
}

typedef struct tag_pd_cantbe
{
    NodeFlag mask;
    NodeFlag cantbe;
} PD_CANTBE;

const PD_CANTBE CantBeTable[] =
{
    { NodeFlag::Year,         NodeFlag::Year|NodeFlag::YD|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY },
    { NodeFlag::Month,        NodeFlag::Month|NodeFlag::WeekOfYear|NodeFlag::DayOfYear|NodeFlag::YD|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY|NodeFlag::WeekOfYearPrefix },
    { NodeFlag::DayOfMonth, NodeFlag::DayOfMonth|NodeFlag::WeekOfYear|NodeFlag::DayOfYear|NodeFlag::YD|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY|NodeFlag::WeekOfYearPrefix },
    { NodeFlag::DayOfWeek,  NodeFlag::DayOfWeek },
    { NodeFlag::WeekOfYear, NodeFlag::WeekOfYear|NodeFlag::Month|NodeFlag::DayOfMonth|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY },
    { NodeFlag::DayOfYear,  NodeFlag::DayOfYear|NodeFlag::Month|NodeFlag::DayOfMonth|NodeFlag::WeekOfYear|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY|NodeFlag::WeekOfYearPrefix },
    { NodeFlag::YD,           NodeFlag::Year|NodeFlag::YD|NodeFlag::Month|NodeFlag::DayOfMonth|NodeFlag::WeekOfYear|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY|NodeFlag::WeekOfYearPrefix },
    { NodeFlag::YMD,          NodeFlag::Year|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY|NodeFlag::Month|NodeFlag::DayOfMonth|NodeFlag::WeekOfYearPrefix|NodeFlag::WeekOfYear|NodeFlag::YD|NodeFlag::DayOfYear },
    { NodeFlag::MDY,          NodeFlag::Year|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY|NodeFlag::Month|NodeFlag::DayOfMonth|NodeFlag::WeekOfYearPrefix|NodeFlag::WeekOfYear|NodeFlag::YD|NodeFlag::DayOfYear },
    { NodeFlag::DMY,          NodeFlag::Year|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY|NodeFlag::Month|NodeFlag::DayOfMonth|NodeFlag::WeekOfYearPrefix|NodeFlag::WeekOfYear|NodeFlag::YD|NodeFlag::DayOfYear },
    { NodeFlag::YMD|NodeFlag::MDY, NodeFlag::Year|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY|NodeFlag::Month|NodeFlag::DayOfMonth|NodeFlag::WeekOfYearPrefix|NodeFlag::WeekOfYear|NodeFlag::YD|NodeFlag::DayOfYear },
    { NodeFlag::MDY|NodeFlag::DMY, NodeFlag::Year|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY|NodeFlag::Month|NodeFlag::DayOfMonth|NodeFlag::WeekOfYearPrefix|NodeFlag::WeekOfYear|NodeFlag::YD|NodeFlag::DayOfYear },
    { NodeFlag::YMD|NodeFlag::DMY, NodeFlag::Year|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY|NodeFlag::Month|NodeFlag::DayOfMonth|NodeFlag::WeekOfYearPrefix|NodeFlag::WeekOfYear|NodeFlag::YD|NodeFlag::DayOfYear },
    { NodeFlag::YMD|NodeFlag::DMY|NodeFlag::MDY, NodeFlag::Year|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY|NodeFlag::Month|NodeFlag::DayOfMonth|NodeFlag::WeekOfYearPrefix|NodeFlag::WeekOfYear|NodeFlag::YD|NodeFlag::DayOfYear },
    { NodeFlag::TimeZone, NodeFlag::TimeZone|NodeFlag::HMSTimeZone|NodeFlag::HourTimeZone },
    { NodeFlag::HourTime, NodeFlag::HMSTime|NodeFlag::HourTime },
    { NodeFlag::HourTimeZone, NodeFlag::TimeZone|NodeFlag::HMSTimeZone|NodeFlag::HourTimeZone },
    { NodeFlag::HMSTime, NodeFlag::HMSTime|NodeFlag::HourTime },
    { NodeFlag::HMSTimeZone, NodeFlag::TimeZone|NodeFlag::HMSTimeZone|NodeFlag::HourTimeZone },
    { NodeFlag::Nothing, NodeFlag::Nothing }
};

static void PD_Deduction(void)
{
    PD_Node *pNode = PD_FirstNode();
    while (pNode)
    {
        int j =0;
        while (CantBeTable[j].mask != NodeFlag::Nothing)
        {
            if (pNode->uCouldBe == CantBeTable[j].mask)
            {
                PD_Node *pNodeInner = PD_FirstNode();
                while (pNodeInner)
                {
                    removeFlags(pNodeInner->uCouldBe, CantBeTable[j].cantbe);
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
        while (BreakDownTable[j].mask != NodeFlag::Nothing)
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

        // If all that is left is NodeFlag::HMSTime and NodeFlag::HourTime, then
        // it's NodeFlag::HourTime.
        //
        if (pNode->uCouldBe == (NodeFlag::HMSTime|NodeFlag::HourTime))
        {
            pNode->uCouldBe = NodeFlag::HourTime;
        }
        if (pNode->uCouldBe == (NodeFlag::HMSTimeZone|NodeFlag::HourTimeZone))
        {
            pNode->uCouldBe = NodeFlag::HourTimeZone;
        }

        // NodeFlag::Minute must follow an NodeFlag::HourTime or NodeFlag::HourTimeZone.
        //
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::Minute))
        {
            if (  !pPrev
               || !hasAnyFlag(pPrev->uCouldBe, NodeFlag::HourTime|NodeFlag::HourTimeZone))
            {
                removeFlags(pNode->uCouldBe, NodeFlag::Minute);
            }
        }

        // NodeFlag::Second must follow an NodeFlag::Minute.
        //
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::Second))
        {
            if (  !pPrev
               || !hasAnyFlag(pPrev->uCouldBe, NodeFlag::Minute))
            {
                removeFlags(pNode->uCouldBe, NodeFlag::Second);
            }
        }

        // YMD MDY DMY
        //
        // NodeFlag::DayOfMonth cannot follow NodeFlag::Year.
        //
        if (  hasAnyFlag(pNode->uCouldBe, NodeFlag::DayOfMonth)
           && pPrev
           && pPrev->uCouldBe == NodeFlag::Year)
        {
            removeFlags(pNode->uCouldBe, NodeFlag::DayOfMonth);
        }

        // Timezone cannot occur before the time.
        //
        if (  hasAnyFlag(pNode->uCouldBe, NodeFlag::TimeZone)
           && !bMightHaveSeenTimeHour)
        {
            removeFlags(pNode->uCouldBe, NodeFlag::TimeZone);
        }

        // TimeDateSeparator cannot occur after the time.
        //
        if (  hasAnyFlag(pNode->uCouldBe, NodeFlag::DateTimeSeparator)
           && bHaveSeenTimeHour)
        {
            removeFlags(pNode->uCouldBe, NodeFlag::DateTimeSeparator);
        }

        if (pNode->uCouldBe == NodeFlag::DateTimeSeparator)
        {
            PD_Node *pNodeInner = PD_FirstNode();
            while (pNodeInner && pNodeInner != pNode)
            {
                removeFlags(pNodeInner->uCouldBe, NodeFlag::TimeZone|NodeFlag::HourTime|NodeFlag::HourTimeZone|NodeFlag::Minute|NodeFlag::Second|NodeFlag::Subsecond|NodeFlag::Meridian|NodeFlag::HMSTime|NodeFlag::HMSTimeZone);
                pNodeInner = PD_NextNode(pNodeInner);
            }
            pNodeInner = pNext;
            while (pNodeInner)
            {
                removeFlags(pNodeInner->uCouldBe, NodeFlag::WeekOfYearPrefix|NodeFlag::YD|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY|NodeFlag::Year|NodeFlag::Month|NodeFlag::DayOfMonth|NodeFlag::DayOfWeek|NodeFlag::WeekOfYear|NodeFlag::DayOfYear);
                pNodeInner = PD_NextNode(pNodeInner);
            }
            pNode->uCouldBe = NodeFlag::Removeable;
        }

        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::WeekOfYearPrefix))
        {
            if (  pNext
               && hasAnyFlag(pNext->uCouldBe, NodeFlag::WeekOfYear))
            {
                pNext->uCouldBe = NodeFlag::WeekOfYear;
                pNode->uCouldBe = NodeFlag::Removeable;
            }
            else if (pNode->uCouldBe == NodeFlag::WeekOfYearPrefix)
            {
                pNode->uCouldBe = NodeFlag::Removeable;
            }
        }

        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::HourTime|NodeFlag::HMSTime))
        {
            if (hasOnlyFlags(pNode->uCouldBe, NodeFlag::HourTime|NodeFlag::HMSTime))
            {
                bHaveSeenTimeHour = true;
            }
            bMightHaveSeenTimeHour = true;
        }

        // Remove NodeFlag::Removeable.
        //
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::Removeable))
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
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::HMSTime|NodeFlag::HourTime))
        {
            cTime++;
        }
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::WeekOfYear))
        {
            cWeekOfYear++;
        }
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::Year|NodeFlag::YD|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY))
        {
            cYear++;
        }
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::Month|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY))
        {
            cMonth++;
        }
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::DayOfMonth|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY))
        {
            cDayOfMonth++;
        }
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::DayOfWeek))
        {
            cDayOfWeek++;
        }
        if (hasAnyFlag(pNode->uCouldBe, NodeFlag::DayOfYear|NodeFlag::YD))
        {
            cDayOfYear++;
        }
        pNode = PD_NextNode(pNode);
    }

    NodeFlag OnlyOneMask = NodeFlag::Nothing;
    NodeFlag CantBeMask = NodeFlag::Nothing;
    if (cYear == 1)
    {
        addFlags(OnlyOneMask, NodeFlag::Year|NodeFlag::YD|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY);
    }
    if (cTime == 1)
    {
        addFlags(OnlyOneMask, NodeFlag::HMSTime|NodeFlag::HourTime);
    }
    if (cMonth == 0 || cDayOfMonth == 0)
    {
        addFlags(CantBeMask, NodeFlag::Month|NodeFlag::DayOfMonth);
    }
    if (cDayOfWeek == 0)
    {
        addFlags(CantBeMask, NodeFlag::WeekOfYear);
    }
    if (  cMonth == 1 && cDayOfMonth == 1
       && (cWeekOfYear != 1 || cDayOfWeek != 1)
       && cDayOfYear != 1)
    {
        addFlags(OnlyOneMask, NodeFlag::Month|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY);
        addFlags(OnlyOneMask, NodeFlag::DayOfMonth);
        addFlags(CantBeMask, NodeFlag::WeekOfYear|NodeFlag::YD);
    }
    else if (cDayOfYear == 1 && (cWeekOfYear != 1 || cDayOfWeek != 1))
    {
        addFlags(OnlyOneMask, NodeFlag::DayOfYear|NodeFlag::YD);
        addFlags(CantBeMask, NodeFlag::WeekOfYear|NodeFlag::Month|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY);
        addFlags(CantBeMask, NodeFlag::DayOfMonth);
    }
    else if (cWeekOfYear == 1 && cDayOfWeek == 1)
    {
        addFlags(OnlyOneMask, NodeFlag::WeekOfYear);
        addFlags(OnlyOneMask, NodeFlag::DayOfWeek);
        addFlags(CantBeMask, NodeFlag::YD|NodeFlag::Month|NodeFlag::YMD|NodeFlag::MDY|NodeFlag::DMY);
        addFlags(CantBeMask, NodeFlag::DayOfMonth);
    }

    // Also, if we match OnlyOneMask, then force only something in
    // OnlyOneMask.
    //
    pNode = PD_FirstNode();
    while (pNode)
    {
        if (hasAnyFlag(pNode->uCouldBe, OnlyOneMask))
        {
            keepOnlyFlags(pNode->uCouldBe, OnlyOneMask);
        }
        if (hasAnyFlagsNotIn(pNode->uCouldBe, CantBeMask))
        {
            removeFlags(pNode->uCouldBe, CantBeMask);
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
        if (pNode->uCouldBe == NodeFlag::Year)
        {
            paf->iYear = pNode->iToken;
            PD_Node *pPrev = PD_PrevNode(pNode);
            if (  pPrev
               && pPrev->uCouldBe == NodeFlag::Sign
               && pPrev->pToken[0] == '-')
            {
                paf->iYear = -paf->iYear;
            }
        }
        else if (pNode->uCouldBe == NodeFlag::DayOfYear)
        {
            paf->iDayOfYear = pNode->iToken;
        }
        else if (pNode->uCouldBe == NodeFlag::Month)
        {
            paf->iMonthOfYear = pNode->iToken;
        }
        else if (pNode->uCouldBe == NodeFlag::DayOfMonth)
        {
            paf->iDayOfMonth = pNode->iToken;
        }
        else if (pNode->uCouldBe == NodeFlag::WeekOfYear)
        {
            paf->iWeekOfYear = pNode->iToken;
        }
        else if (pNode->uCouldBe == NodeFlag::DayOfWeek)
        {
            paf->iDayOfWeek = pNode->iToken;
        }
        else if (pNode->uCouldBe == NodeFlag::HourTime)
        {
            paf->iHourTime = pNode->iToken;
            pNode = PD_NextNode(pNode);
            if (  pNode
               && pNode->uCouldBe == NodeFlag::Minute)
            {
                paf->iMinuteTime = pNode->iToken;
                pNode = PD_NextNode(pNode);
                if (  pNode
                   && pNode->uCouldBe == NodeFlag::Second)
                {
                    paf->iSecondTime = pNode->iToken;
                    pNode = PD_NextNode(pNode);
                    if (  pNode
                       && pNode->uCouldBe == NodeFlag::Subsecond)
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
               && pNode->uCouldBe == NodeFlag::Meridian)
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
        else if (pNode->uCouldBe == NodeFlag::HourTimeZone)
        {
            paf->iMinuteTimeZone = pNode->iToken * 60;
            PD_Node *pPrev = PD_PrevNode(pNode);
            if (  pPrev
               && pPrev->uCouldBe == NodeFlag::Sign
               && pPrev->pToken[0] == '-')
            {
                paf->iMinuteTimeZone = -paf->iMinuteTimeZone;
            }
            pNode = PD_NextNode(pNode);
            if (  pNode
               && pNode->uCouldBe == NodeFlag::Minute)
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
        else if (pNode->uCouldBe == NodeFlag::TimeZone)
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
        else if (hasAnyFlag(pNode->uCouldBe, NodeFlag::Sign|NodeFlag::DateTimeSeparator))
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
