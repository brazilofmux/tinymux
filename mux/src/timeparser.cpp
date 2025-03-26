/*! \file timeparser.cpp
 * \brief General Date Parser.
 *
 * This file contains code related to parsing date strings.
 */

#include <list>
#include <limits>
#include <algorithm>

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

enum class NodeType {
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

constexpr NodeFlag operator|(NodeFlag a, NodeFlag b) {
    return static_cast<NodeFlag>(
        static_cast<std::underlying_type_t<NodeFlag>>(a) |
        static_cast<std::underlying_type_t<NodeFlag>>(b)
        );
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

// --- Basic C++14 compatible string_view ---
class string_view {
private:
    const UTF8* ptr_ = nullptr;
    size_t len_ = 0;

public:
    // Define npos similar to std::string
    static constexpr size_t npos = static_cast<size_t>(-1);

    constexpr string_view() noexcept = default;
    constexpr string_view(const UTF8* s, size_t count) noexcept : ptr_(s), len_(count) {}
    constexpr size_t size() const noexcept { return len_; }
    constexpr const UTF8* data() const noexcept { return ptr_; }
    constexpr const UTF8 operator[](size_t pos) const noexcept { return ptr_[pos]; }
    constexpr bool empty() const noexcept { return len_ == 0; }

    string_view substr(size_t pos, size_t count = npos) const {
        if (pos >= len_) return string_view();
        size_t rcount = min(count, len_ - pos);
        return string_view(ptr_ + pos, rcount);
    }
};

template <typename T> using Optional = T;
constexpr int kFieldNotPresent = INT_MIN;

// We must deal with several levels at once. That is, a single
// character is overlapped by layers and layers of meaning from 'digit'
// to 'the second digit of the hours field of the timezone'.
//
struct PD_Node
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
    NodeFlag  uCouldBe = NodeFlag::Nothing;

    // These fields deal with token things and we avoid mixing
    // them up in higher meanings. This is the lowest level.
    //
    // Further Notes:
    //
    // NodeType::Symbol is always a -single- (nToken==1) character.
    //
    // uNodeType must be one of the PDTT_* values.
    //
    // NodeType::NumericSigned, NodeType::NumericaUnsigned, and
    // NodeType::Text types may have an iToken value associated with
    // them.
    //
    NodeType uNodeType;
    string_view tokenView;
    int       iToken;

    PD_Node() = default;

    PD_Node(NodeType type, string_view view, NodeFlag flags = NodeFlag::Nothing, int value = 0)
        : uCouldBe(flags), uNodeType(type), tokenView(view), iToken(value) {
    }
};

typedef void BREAK_DOWN_FUNC(std::list<PD_Node>& nodes, std::list<PD_Node>::iterator iter);
struct PD_BREAKDOWN
{
    NodeFlag mask;
    BREAK_DOWN_FUNC *fpBreakDown;
};

struct ALLFIELDS
{
    Optional<int> iYear = kFieldNotPresent;
    Optional<int> iDayOfYear = kFieldNotPresent;
    Optional<int> iMonthOfYear = kFieldNotPresent;
    Optional<int> iDayOfMonth = kFieldNotPresent;
    Optional<int> iWeekOfYear = kFieldNotPresent;
    Optional<int> iDayOfWeek = kFieldNotPresent;
    Optional<int> iHourTime = kFieldNotPresent;
    Optional<int> iMinuteTime = kFieldNotPresent;
    Optional<int> iSecondTime = kFieldNotPresent;
    Optional<int> iMillisecondTime = kFieldNotPresent;
    Optional<int> iMicrosecondTime = kFieldNotPresent;
    Optional<int> iNanosecondTime = kFieldNotPresent;
    Optional<int> iMinuteTimeZone = kFieldNotPresent;
};

// isValidYear assumes numeric string.
//
static bool isValidYear(string_view view, int iValue)
{
    // Year may be Y, YY, YYY, YYYY, or YYYYY.
    // Negative and zero years are permitted in general, but we aren't
    // give the leading sign.
    //
    return (1 <= view.size() && view.size() <= 5);
}

static bool isValidMonth(string_view view, int iValue)
{
    // Month may be 1 through 9, 01 through 09, 10, 11, or 12.
    //
    if (  1 <= view.size() && view.size() <= 2
       && 1 <= iValue && iValue <= 12)
    {
        return true;
    }
    return false;
}

static bool isValidDayOfMonth(string_view view, int iValue)
{
    // Day Of Month may be 1 through 9, 01 through 09, 10 through 19,
    // 20 through 29, 30, and 31.
    //
    if (  1 <= view.size() && view.size() <= 2
       && 1 <= iValue && iValue <= 31)
    {
        return true;
    }
    return false;
}

static bool isValidDayOfWeek(string_view view, int iValue)
{
    // Day Of Week may be 1 through 7.
    //
    if (  1 == view.size()
       && 1 <= iValue && iValue <= 7)
    {
        return true;
    }
    return false;
}

static bool isValidDayOfYear(string_view view, int iValue)
{
    // Day Of Year 001 through 366
    //
    if (  3 == view.size()
       && 1 <= iValue && iValue <= 366)
    {
        return true;
    }
    return false;
}

static bool isValidWeekOfYear(string_view view, int iValue)
{
    // Week Of Year may be 01 through 53.
    //
    if (  2 == view.size()
       && 1 <= iValue && iValue <= 53)
    {
        return true;
    }
    return false;
}

static bool isValidHour(string_view view, int iValue)
{
    // Hour may be 0 through 9, 00 through 09, 10 through 19, 20 through 24.
    //
    if (  1 <= view.size() && view.size() <= 2
       && 0 <= iValue && iValue <= 24)
    {
        return true;
    }
    return false;
}

static bool isValidMinute(string_view view, int iValue)
{
    // Minute may be 00 through 59.
    //
    if (  2 == view.size()
       && 0 <= iValue && iValue <= 59)
    {
        return true;
    }
    return false;
}

static bool isValidSecond(string_view view, int iValue)
{
    // Second may be 00 through 59. Leap seconds represented
    // by '60' are not dealt with.
    //
    if (  2 == view.size()
       && 0 <= iValue && iValue <= 59)
    {
        return true;
    }
    return false;
}

static bool isValidSubSecond(string_view view, int iValue)
{
    // Sub seconds can really be anything, but we limit
    // it's precision to 100 ns.
    //
    if (view.size() <= 7)
    {
        return true;
    }
    return false;
}

// This function handles H, HH, HMM, HHMM, HMMSS, HHMMSS
//
static bool isValidHMS(string_view view, int iValue)
{
    int iHour, iMinutes, iSeconds;
    switch (view.size())
    {
    case 1:
    case 2:
        return isValidHour(view, iValue);
    case 3:
    case 4:
        iHour = iValue / 100; iValue -= iHour * 100;
        iMinutes = iValue;
        if (  isValidHour(view.substr(0, view.size() - 2), iHour)
           && isValidMinute(view.substr(view.size() - 2, 2), iMinutes))
        {
            return true;
        }
        break;
    case 5:
    case 6:
        iHour = iValue / 10000;  iValue -= iHour * 10000;
        iMinutes = iValue / 100;    iValue -= iMinutes * 100;
        iSeconds = iValue;
        if (  isValidHour(view.substr(0, view.size() - 4), iHour)
           && isValidMinute(view.substr(view.size() - 4, 2), iMinutes)
           && isValidSecond(view.substr(view.size() - 2, 2), iSeconds))
        {
            return true;
        }
        break;
    }
    return false;
}

static std::list<PD_Node>::iterator PD_InsertAfter(std::list<PD_Node>& nodes, std::list<PD_Node>::iterator pos, const PD_Node& node)
{
    return nodes.insert(std::next(pos), node);
}

static void SplitLastTwoDigits(std::list<PD_Node>& nodes, std::list<PD_Node>::iterator iter, NodeFlag mask)
{
    PD_Node newNode = *iter;
    newNode.uCouldBe = mask;
    newNode.tokenView = iter->tokenView.substr(iter->tokenView.size() - 2, 2);
    newNode.iToken = iter->iToken % 100;

    iter->tokenView = iter->tokenView.substr(0, iter->tokenView.size() - 2);
    iter->iToken /= 100;

    PD_InsertAfter(nodes, iter, newNode);
}

static void SplitLastThreeDigits(std::list<PD_Node>& nodes, std::list<PD_Node>::iterator iter, NodeFlag mask)
{
    PD_Node newNode = *iter;
    newNode.uCouldBe = mask;
    newNode.tokenView = iter->tokenView.substr(iter->tokenView.size() - 3, 3);
    newNode.iToken = iter->iToken % 1000;

    iter->tokenView = iter->tokenView.substr(0, iter->tokenView.size() - 3);
    iter->iToken /= 1000;

    PD_InsertAfter(nodes, iter, newNode);
}

// This function breaks down H, HH, HMM, HHMM, HMMSS, HHMMSS
//
static void BreakDownHMS(std::list<PD_Node>& nodes, std::list<PD_Node>::iterator iter)
{
    if (hasAnyFlag(iter->uCouldBe, NodeFlag::HMSTime))
    {
        iter->uCouldBe = NodeFlag::HourTime;
    }
    else
    {
        iter->uCouldBe = NodeFlag::HourTimeZone;
    }
    switch (iter->tokenView.size())
    {
    case 5:
    case 6:
        SplitLastTwoDigits(nodes, iter, NodeFlag::Second);
        // Fall through intentional
    case 3:
    case 4:
        SplitLastTwoDigits(nodes, iter, NodeFlag::Minute);
        // Hour remains in iter
    }
}

// This function handles YYMMDD, YYYMMDD, YYYYMMDD, YYYYYMMDD
//
static bool isValidYMD(string_view view, int iValue)
{
    int iYear = iValue / 10000;
    iValue -= 10000 * iYear;
    int iMonth = iValue / 100;
    iValue -= 100 * iMonth;
    int iDay = iValue;

    if (  isValidMonth(view.substr(view.size() - 4, 2), iMonth)
       && isValidDayOfMonth(view.substr(view.size() - 2, 2), iDay)
       && isValidYear(view.substr(0, view.size() - 4), iYear))
    {
        return true;
    }
    return false;
}

// This function breaks down YYMMDD, YYYMMDD, YYYYMMDD, YYYYYMMDD
//
static void BreakDownYMD(std::list<PD_Node>& nodes, std::list<PD_Node>::iterator iter)
{
    iter->uCouldBe = NodeFlag::Year;
    SplitLastTwoDigits(nodes, iter, NodeFlag::DayOfMonth);
    SplitLastTwoDigits(nodes, iter, NodeFlag::Month);
}

// This function handles MMDDYY
//
static bool isValidMDY(string_view view, int iValue)
{
    int iMonth = iValue / 10000;
    iValue -= 10000 * iMonth;
    int iDay = iValue / 100;
    iValue -= 100 * iDay;
    int iYear = iValue;

    if (  view.size() == 6
       && isValidMonth(view.substr(0, 2), iMonth)
       && isValidDayOfMonth(view.substr(2, 2), iDay)
       && isValidYear(view.substr(4, 2), iYear))
    {
        return true;
    }
    return false;
}

// This function breaks down MMDDYY
//
static void BreakDownMDY(std::list<PD_Node>& nodes, std::list<PD_Node>::iterator iter)
{
    iter->uCouldBe = NodeFlag::Month;
    SplitLastTwoDigits(nodes, iter, NodeFlag::Year);
    SplitLastTwoDigits(nodes, iter, NodeFlag::DayOfMonth);
}

// This function handles DDMMYY
//
static bool isValidDMY(string_view view, int iValue)
{
    int iDay = iValue / 10000;
    iValue -= 10000 * iDay;
    int iMonth = iValue / 100;
    iValue -= 100 * iMonth;
    int iYear = iValue;

    if (  view.size() == 6
       && isValidMonth(view.substr(2, 2), iMonth)
       && isValidDayOfMonth(view.substr(0, 2), iDay)
       && isValidYear(view.substr(4, 2), iYear))
    {
        return true;
    }
    return false;
}

// This function breaks down DDMMYY
//
static void BreakDownDMY(std::list<PD_Node>& nodes, std::list<PD_Node>::iterator iter)
{
    iter->uCouldBe = NodeFlag::DayOfMonth;
    SplitLastTwoDigits(nodes, iter, NodeFlag::Year);
    SplitLastTwoDigits(nodes, iter, NodeFlag::Month);
}

// This function handles YDDD, YYDDD, YYYDDD, YYYYDDD, YYYYYDDD
//
static bool isValidYD(string_view view, int iValue)
{
    int iYear = iValue / 1000;
    iValue -= 1000 * iYear;
    int iDay = iValue;

    if (  4 <= view.size() && view.size() <= 8
       && isValidDayOfYear(view.substr(view.size() - 3, 3), iDay)
       && isValidYear(view.substr(0, view.size() - 3), iYear))
    {
        return true;
    }
    return false;
}

// This function breaks down YDDD, YYDDD, YYYDDD, YYYYDDD, YYYYYDDD
//
static void BreakDownYD(std::list<PD_Node>& nodes, std::list<PD_Node>::iterator iter)
{
    iter->uCouldBe = NodeFlag::Year;
    SplitLastThreeDigits(nodes, iter, NodeFlag::DayOfYear);
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

typedef bool PVALIDFUNC(string_view tokenView, int iValue);

struct NUMERIC_VALID_RECORD
{
    NodeFlag mask;
    PVALIDFUNC *fnValid;
};

const std::vector<NUMERIC_VALID_RECORD> NumericSet =
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
};

// This function looks at the numeric token and assigns the initial set
// of possibilities.
//
static void ClassifyNumericToken(PD_Node& node)
{
    int    iToken = node.iToken;
    NodeFlag uCouldBe = InitialCouldBe[node.tokenView.size() - 1];
    for (int i = 0; i < NumericSet.size(); ++i)
    {
        auto mask = NumericSet[i].mask;
        if (hasAnyFlag(uCouldBe, mask) && !(NumericSet[i].fnValid(node.tokenView, iToken)))
        {
            removeFlags(uCouldBe, mask);
        }
    }
    node.uCouldBe = uCouldBe;
}

typedef struct
{
    const UTF8  *szText;
    NodeFlag    uCouldBe;
    int         iValue;
} PD_TEXT_ENTRY;

const std::vector<PD_TEXT_ENTRY> PD_TextTable =
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
    {T("pm"),        NodeFlag::Meridian,     12 }
};

#define PD_LEX_INVALID 0
#define PD_LEX_SYMBOL  1
#define PD_LEX_DIGIT   2
#define PD_LEX_SPACE   3
#define PD_LEX_ALPHA   4
#define PD_LEX_EOS     5

constexpr std::array<char, 256> LexTable =
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

using NodeList = std::list<PD_Node>;

bool case_insensitive_equals(string_view s1, string_view s2) {
    // Fast path: if lengths differ, strings can't be equal
    if (s1.size() != s2.size()) {
        return false;
    }

    // Compare characters
    for (size_t i = 0; i < s1.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(s1[i])) !=
            std::tolower(static_cast<unsigned char>(s2[i]))) {
            return false;
        }
    }

    return true;
}

static std::list<PD_Node>::iterator PD_ScanNextToken(string_view& remainingInput, NodeList& nodes)
{
    if (remainingInput.empty()) {
        return nodes.end();
    }

    unsigned char ch = static_cast<unsigned char>(remainingInput[0]);
    int iType = LexTable[ch];

    if (iType == PD_LEX_EOS || iType == PD_LEX_INVALID) {
        return nodes.end();
    }

    PD_Node node{};
    size_t consumed = 0;

    if (iType == PD_LEX_SYMBOL) {
        // Single character symbol
        consumed = 1;
        node.tokenView = remainingInput.substr(0, 1);
        node.uNodeType = NodeType::Symbol;
        node.iToken = 0;

        // Set appropriate flags based on the symbol
        if (ch == ':') {
            node.uCouldBe = NodeFlag::TimeFieldSeparator;
        }
        else if (ch == '-') {
            node.uCouldBe = NodeFlag::DateFieldSeparator | NodeFlag::Sign;
        }
        else if (ch == '+') {
            node.uCouldBe = NodeFlag::Sign;
        }
        else if (ch == '/') {
            node.uCouldBe = NodeFlag::DateFieldSeparator;
        }
        else if (ch == '.') {
            node.uCouldBe = NodeFlag::DateFieldSeparator | NodeFlag::SecondsDecimal;
        }
        else if (ch == ',') {
            node.uCouldBe = NodeFlag::Removeable | NodeFlag::SecondsDecimal | NodeFlag::DayOfMonthSuffix;
        }
    }
    else {
        // Find the length of the token (all characters of the same type)
        size_t len = 1;
        while (len < remainingInput.size() &&
            LexTable[static_cast<unsigned char>(remainingInput[len])] == iType) {
            len++;
        }
        consumed = len;
        node.tokenView = remainingInput.substr(0, len);

        if (iType == PD_LEX_DIGIT) {
            node.uNodeType = NodeType::NumericUnsigned;
            node.iToken = 0;

            if (1 <= len && len <= 9) {
                // Convert digit string to integer
                // We should use our own safe_atol here
                char buff[10];
                for (size_t i = 0; i < len && i < 9; i++) {
                    buff[i] = remainingInput[i];
                }
                buff[len < 9 ? len : 9] = '\0';

                node.iToken = std::atol(buff);
                ClassifyNumericToken(node);
            }
        }
        else if (iType == PD_LEX_SPACE) {
            node.uNodeType = NodeType::Spaces;
            node.uCouldBe = NodeFlag::Whitespace;
        }
        else if (iType == PD_LEX_ALPHA) {
            node.uNodeType = NodeType::Text;
            node.iToken = 0;

            // Match text against known text entries
            bool bFound = false;
            for (const auto& entry : PD_TextTable) {
                if (!entry.szText) break; // End of table

                string_view entry_sv(entry.szText, mux_strlen(entry.szText));

                if (node.tokenView.size() == entry_sv.size() &&
                    case_insensitive_equals(node.tokenView, entry_sv)) {
                    node.uCouldBe = entry.uCouldBe;
                    node.iToken = entry.iValue;
                    bFound = true;
                    break;
                }
            }

            if (!bFound) {
                return nodes.end();
            }
        }
    }

    // Consume the processed characters
    remainingInput = remainingInput.substr(consumed);

    // Add the node to the list
    nodes.push_back(node);
    return std::prev(nodes.end());
}

static const std::vector<PD_BREAKDOWN> BreakDownTable =
{
    { NodeFlag::HMSTime, BreakDownHMS },
    { NodeFlag::HMSTimeZone, BreakDownHMS },
    { NodeFlag::YD,  BreakDownYD },
    { NodeFlag::YMD, BreakDownYMD },
    { NodeFlag::MDY, BreakDownMDY },
    { NodeFlag::DMY, BreakDownDMY },
};

static void PD_Pass2(NodeList& nodes)
{
    auto iter = nodes.begin();
    while (iter != nodes.end())
    {
        auto iterNext = std::next(iter);
        auto iterPrev = (iter == nodes.begin()) ? nodes.end() : std::prev(iter);

        // Absorb information from NodeFlag::TimeFieldSeparator.
        if (hasAnyFlag(iter->uCouldBe, NodeFlag::TimeFieldSeparator))
        {
            if (iterPrev != nodes.end() && iterNext != nodes.end())
            {
                if (  hasAnyFlag(iterPrev->uCouldBe, NodeFlag::HourTime | NodeFlag::HourTimeZone)
                   && hasAnyFlag(iterNext->uCouldBe, NodeFlag::Minute))
                {
                    keepOnlyFlags(iterPrev->uCouldBe, NodeFlag::HourTime | NodeFlag::HourTimeZone);
                    iterNext->uCouldBe = NodeFlag::Minute;
                }
                else if (  hasAnyFlag(iterPrev->uCouldBe, NodeFlag::Minute)
                        && hasAnyFlag(iterNext->uCouldBe, NodeFlag::Second))
                {
                    iterPrev->uCouldBe = NodeFlag::Minute;
                    iterNext->uCouldBe = NodeFlag::Second;
                }
            }
            iter->uCouldBe = NodeFlag::Removeable;
        }

        // Absorb information from NodeFlag::SecondsDecimal.
        if (hasAnyFlag(iter->uCouldBe, NodeFlag::SecondsDecimal))
        {
            if (  iterPrev != nodes.end() && iterNext != nodes.end()
               && iterPrev->uCouldBe == NodeFlag::Second
               && hasAnyFlag(iterNext->uCouldBe, NodeFlag::Subsecond))
            {
                iter->uCouldBe = NodeFlag::SecondsDecimal;
                iterNext->uCouldBe = NodeFlag::Subsecond;
            }
            else
            {
                removeFlags(iter->uCouldBe, NodeFlag::SecondsDecimal);
            }
            iter->uCouldBe = NodeFlag::Removeable;
        }

        // Absorb information from NodeFlag::Subsecond
        if (iter->uCouldBe != NodeFlag::Subsecond)
        {
            removeFlags(iter->uCouldBe, NodeFlag::Subsecond);
        }

        // Absorb information from NodeFlag::DateFieldSeparator.
        if (hasAnyFlag(iter->uCouldBe, NodeFlag::DateFieldSeparator))
        {
            removeFlags(iter->uCouldBe, NodeFlag::DateFieldSeparator);
            const NodeFlag Separators = NodeFlag::Year | NodeFlag::Month | NodeFlag::DayOfMonth |
                NodeFlag::DayOfYear | NodeFlag::WeekOfYear | NodeFlag::DayOfWeek;
            if (  iterPrev != nodes.end() && iterNext != nodes.end()
               && hasAnyFlag(iterPrev->uCouldBe, Separators)
               && hasAnyFlag(iterNext->uCouldBe, Separators))
            {
                keepOnlyFlags(iterPrev->uCouldBe, Separators);
                keepOnlyFlags(iterNext->uCouldBe, Separators);
                iter->uCouldBe = NodeFlag::Removeable;
            }
        }

        // Process NodeFlag::DayOfMonthSuffix
        if (hasAnyFlag(iter->uCouldBe, NodeFlag::DayOfMonthSuffix))
        {
            iter->uCouldBe = NodeFlag::Removeable;
            if (  iterPrev != nodes.end()
               && hasAnyFlag(iterPrev->uCouldBe, NodeFlag::DayOfMonth))
            {
                iterPrev->uCouldBe = NodeFlag::DayOfMonth;
            }
        }

        // Absorb semantic meaning of NodeFlag::Sign.
        if (iter->uCouldBe == NodeFlag::Sign)
        {
            const NodeFlag SignablePositive = NodeFlag::HMSTime | NodeFlag::HMSTimeZone;
            const NodeFlag SignableNegative = NodeFlag::Year | NodeFlag::YD | SignablePositive | NodeFlag::YMD;
            NodeFlag Signable;

            // Check the first character of the token view
            if (iter->tokenView.size() > 0 && iter->tokenView[0] == '-')
            {
                Signable = SignableNegative;
            }
            else
            {
                Signable = SignablePositive;
            }

            if (  iterNext != nodes.end()
               && hasAnyFlag(iterNext->uCouldBe, Signable))
            {
                keepOnlyFlags(iterNext->uCouldBe, Signable);
            }
            else
            {
                iter->uCouldBe = NodeFlag::Removeable;
            }
        }

        // A timezone HOUR or HMS requires a leading sign.
        if (hasAnyFlag(iter->uCouldBe, NodeFlag::HMSTimeZone | NodeFlag::HourTimeZone))
        {
            if (  iterPrev == nodes.end()
               || iterPrev->uCouldBe != NodeFlag::Sign)
            {
                removeFlags(iter->uCouldBe, NodeFlag::HMSTimeZone | NodeFlag::HourTimeZone);
            }
        }

        // Likewise, a NodeFlag::HourTime or NodeFlag::HMSTime cannot have a
        // leading sign.
        if (hasAnyFlag(iter->uCouldBe, NodeFlag::HMSTime | NodeFlag::HourTime))
        {
            if (  iterPrev != nodes.end()
               && iterPrev->uCouldBe == NodeFlag::Sign)
            {
                removeFlags(iter->uCouldBe, NodeFlag::HMSTime | NodeFlag::HourTime);
            }
        }

        iter = iterNext;
    }

    // Remove whitespace and removeable nodes
    iter = nodes.begin();
    while (iter != nodes.end())
    {
        if (hasAnyFlag(iter->uCouldBe, NodeFlag::Whitespace | NodeFlag::Removeable))
        {
            iter = nodes.erase(iter);  // Use standard erase instead of PD_RemoveNode
        }
        else
        {
            ++iter;
        }
    }
}

struct PD_CANTBE
{
    NodeFlag mask;
    NodeFlag cantbe;
};

const std::vector<PD_CANTBE> CantBeTable =
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
};

static void PD_Deduction(NodeList& nodes)
{
    for (auto& node : nodes)
    {
        for (const auto& cantBe : CantBeTable)
        {
            if (node.uCouldBe == cantBe.mask)
            {
                for (auto& nodeInner : nodes)
                {
                    if (&nodeInner != &node)
                    {
                        removeFlags(nodeInner.uCouldBe, cantBe.cantbe);
                    }
                }
                node.uCouldBe = cantBe.mask;
                break;
            }
        }
    }
}

static void PD_BreakItDown(NodeList& nodes)
{
    for (auto iter = nodes.begin(); iter != nodes.end(); /* no increment here */)
    {
        bool foundBreakDown = false;
        for (const auto& breakDown : BreakDownTable)
        {
            if (iter->uCouldBe == breakDown.mask)
            {
                breakDown.fpBreakDown(nodes, iter);
                foundBreakDown = true;
                break;  // Break after finding a match
            }
        }

        // Only increment if we're sure the iterator is still valid
        // Some break down functions might invalidate the iterator by inserting nodes
        if (foundBreakDown)
        {
            // Skip the increment and re-process this position in case the node changed
            // This ensures we process newly modified nodes properly
        }
        else
        {
            ++iter;  // Only increment if no breakdown was applied
        }
    }
}

static void PD_Pass5(NodeList& nodes)
{
    bool bHaveSeenTimeHour = false;
    bool bMightHaveSeenTimeHour = false;

    auto iter = nodes.begin();
    while (iter != nodes.end())
    {
        auto iterPrev = (iter == nodes.begin()) ? nodes.end() : std::prev(iter);
        auto iterNext = (std::next(iter) == nodes.end()) ? nodes.end() : std::next(iter);

        // If all that is left is NodeFlag::HMSTime and NodeFlag::HourTime, then
        // it's NodeFlag::HourTime.
        if (iter->uCouldBe == (NodeFlag::HMSTime | NodeFlag::HourTime))
        {
            iter->uCouldBe = NodeFlag::HourTime;
        }
        if (iter->uCouldBe == (NodeFlag::HMSTimeZone | NodeFlag::HourTimeZone))
        {
            iter->uCouldBe = NodeFlag::HourTimeZone;
        }

        // NodeFlag::Minute must follow an NodeFlag::HourTime or NodeFlag::HourTimeZone.
        if (hasAnyFlag(iter->uCouldBe, NodeFlag::Minute))
        {
            if (  iterPrev == nodes.end()
               || !hasAnyFlag(iterPrev->uCouldBe, NodeFlag::HourTime | NodeFlag::HourTimeZone))
            {
                removeFlags(iter->uCouldBe, NodeFlag::Minute);
            }
        }

        // NodeFlag::Second must follow an NodeFlag::Minute.
        if (hasAnyFlag(iter->uCouldBe, NodeFlag::Second))
        {
            if (  iterPrev == nodes.end()
               || !hasAnyFlag(iterPrev->uCouldBe, NodeFlag::Minute))
            {
                removeFlags(iter->uCouldBe, NodeFlag::Second);
            }
        }

        // YMD MDY DMY
        // NodeFlag::DayOfMonth cannot follow NodeFlag::Year.
        if (  hasAnyFlag(iter->uCouldBe, NodeFlag::DayOfMonth)
           && iterPrev != nodes.end()
           && iterPrev->uCouldBe == NodeFlag::Year)
        {
            removeFlags(iter->uCouldBe, NodeFlag::DayOfMonth);
        }

        // Timezone cannot occur before the time.
        if (  hasAnyFlag(iter->uCouldBe, NodeFlag::TimeZone)
           && !bMightHaveSeenTimeHour)
        {
            removeFlags(iter->uCouldBe, NodeFlag::TimeZone);
        }

        // TimeDateSeparator cannot occur after the time.
        if (  hasAnyFlag(iter->uCouldBe, NodeFlag::DateTimeSeparator)
           && bHaveSeenTimeHour)
        {
            removeFlags(iter->uCouldBe, NodeFlag::DateTimeSeparator);
        }

        if (iter->uCouldBe == NodeFlag::DateTimeSeparator)
        {
            auto iterInner = nodes.begin();
            while (iterInner != nodes.end() && iterInner != iter)
            {
                removeFlags(iterInner->uCouldBe, NodeFlag::TimeZone | NodeFlag::HourTime |
                    NodeFlag::HourTimeZone | NodeFlag::Minute | NodeFlag::Second |
                    NodeFlag::Subsecond | NodeFlag::Meridian | NodeFlag::HMSTime |
                    NodeFlag::HMSTimeZone);
                ++iterInner;
            }

            iterInner = iterNext;
            while (iterInner != nodes.end())
            {
                removeFlags(iterInner->uCouldBe, NodeFlag::WeekOfYearPrefix | NodeFlag::YD |
                    NodeFlag::YMD | NodeFlag::MDY | NodeFlag::DMY | NodeFlag::Year |
                    NodeFlag::Month | NodeFlag::DayOfMonth | NodeFlag::DayOfWeek |
                    NodeFlag::WeekOfYear | NodeFlag::DayOfYear);
                ++iterInner;
            }

            iter->uCouldBe = NodeFlag::Removeable;
        }

        if (hasAnyFlag(iter->uCouldBe, NodeFlag::WeekOfYearPrefix))
        {
            if (  iterNext != nodes.end()
               && hasAnyFlag(iterNext->uCouldBe, NodeFlag::WeekOfYear))
            {
                iterNext->uCouldBe = NodeFlag::WeekOfYear;
                iter->uCouldBe = NodeFlag::Removeable;
            }
            else if (iter->uCouldBe == NodeFlag::WeekOfYearPrefix)
            {
                iter->uCouldBe = NodeFlag::Removeable;
            }
        }

        if (hasAnyFlag(iter->uCouldBe, NodeFlag::HourTime | NodeFlag::HMSTime))
        {
            if (hasOnlyFlags(iter->uCouldBe, NodeFlag::HourTime | NodeFlag::HMSTime))
            {
                bHaveSeenTimeHour = true;
            }
            bMightHaveSeenTimeHour = true;
        }

        // Remove NodeFlag::Removeable.
        if (hasAnyFlag(iter->uCouldBe, NodeFlag::Removeable))
        {
            iter = nodes.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
}

static void PD_Pass6(NodeList& nodes)
{
    int cYear = 0;
    int cMonth = 0;
    int cDayOfMonth = 0;
    int cWeekOfYear = 0;
    int cDayOfYear = 0;
    int cDayOfWeek = 0;
    int cTime = 0;

    // First pass: count the types of nodes
    for (const auto& node : nodes)
    {
        if (hasAnyFlag(node.uCouldBe, NodeFlag::HMSTime | NodeFlag::HourTime))
        {
            cTime++;
        }
        if (hasAnyFlag(node.uCouldBe, NodeFlag::WeekOfYear))
        {
            cWeekOfYear++;
        }
        if (hasAnyFlag(node.uCouldBe, NodeFlag::Year | NodeFlag::YD | NodeFlag::YMD | NodeFlag::MDY | NodeFlag::DMY))
        {
            cYear++;
        }
        if (hasAnyFlag(node.uCouldBe, NodeFlag::Month | NodeFlag::YMD | NodeFlag::MDY | NodeFlag::DMY))
        {
            cMonth++;
        }
        if (hasAnyFlag(node.uCouldBe, NodeFlag::DayOfMonth | NodeFlag::YMD | NodeFlag::MDY | NodeFlag::DMY))
        {
            cDayOfMonth++;
        }
        if (hasAnyFlag(node.uCouldBe, NodeFlag::DayOfWeek))
        {
            cDayOfWeek++;
        }
        if (hasAnyFlag(node.uCouldBe, NodeFlag::DayOfYear | NodeFlag::YD))
        {
            cDayOfYear++;
        }
    }

    // Determine masks based on the counts
    NodeFlag OnlyOneMask = NodeFlag::Nothing;
    NodeFlag CantBeMask = NodeFlag::Nothing;

    if (cYear == 1)
    {
        addFlags(OnlyOneMask, NodeFlag::Year | NodeFlag::YD | NodeFlag::YMD | NodeFlag::MDY | NodeFlag::DMY);
    }
    if (cTime == 1)
    {
        addFlags(OnlyOneMask, NodeFlag::HMSTime | NodeFlag::HourTime);
    }
    if (cMonth == 0 || cDayOfMonth == 0)
    {
        addFlags(CantBeMask, NodeFlag::Month | NodeFlag::DayOfMonth);
    }
    if (cDayOfWeek == 0)
    {
        addFlags(CantBeMask, NodeFlag::WeekOfYear);
    }

    if (cMonth == 1 && cDayOfMonth == 1 &&
        (cWeekOfYear != 1 || cDayOfWeek != 1) &&
        cDayOfYear != 1)
    {
        addFlags(OnlyOneMask, NodeFlag::Month | NodeFlag::YMD | NodeFlag::MDY | NodeFlag::DMY);
        addFlags(OnlyOneMask, NodeFlag::DayOfMonth);
        addFlags(CantBeMask, NodeFlag::WeekOfYear | NodeFlag::YD);
    }
    else if (cDayOfYear == 1 && (cWeekOfYear != 1 || cDayOfWeek != 1))
    {
        addFlags(OnlyOneMask, NodeFlag::DayOfYear | NodeFlag::YD);
        addFlags(CantBeMask, NodeFlag::WeekOfYear | NodeFlag::Month | NodeFlag::YMD | NodeFlag::MDY | NodeFlag::DMY);
        addFlags(CantBeMask, NodeFlag::DayOfMonth);
    }
    else if (cWeekOfYear == 1 && cDayOfWeek == 1)
    {
        addFlags(OnlyOneMask, NodeFlag::WeekOfYear);
        addFlags(OnlyOneMask, NodeFlag::DayOfWeek);
        addFlags(CantBeMask, NodeFlag::YD | NodeFlag::Month | NodeFlag::YMD | NodeFlag::MDY | NodeFlag::DMY);
        addFlags(CantBeMask, NodeFlag::DayOfMonth);
    }

    // Second pass: apply masks to each node
    for (auto& node : nodes)
    {
        if (hasAnyFlag(node.uCouldBe, OnlyOneMask))
        {
            keepOnlyFlags(node.uCouldBe, OnlyOneMask);
        }
        if (hasAnyFlagsNotIn(node.uCouldBe, CantBeMask))
        {
            removeFlags(node.uCouldBe, CantBeMask);
        }
    }
}

static bool PD_GetFields(ALLFIELDS* paf, NodeList& nodes)
{
    for (auto iter = nodes.begin(); iter != nodes.end(); /* no increment here */)
    {
        if (iter->uCouldBe == NodeFlag::Year)
        {
            paf->iYear = iter->iToken;
            // Check for negative year (preceding minus sign)
            if (iter != nodes.begin())
            {
                auto iterPrev = std::prev(iter);
                if (iterPrev != nodes.end() &&
                    iterPrev->uCouldBe == NodeFlag::Sign &&
                    iterPrev->tokenView.size() > 0 &&
                    iterPrev->tokenView[0] == '-')
                {
                    paf->iYear = -paf->iYear;
                }
            }
            ++iter;
        }
        else if (iter->uCouldBe == NodeFlag::DayOfYear)
        {
            paf->iDayOfYear = iter->iToken;
            ++iter;
        }
        else if (iter->uCouldBe == NodeFlag::Month)
        {
            paf->iMonthOfYear = iter->iToken;
            ++iter;
        }
        else if (iter->uCouldBe == NodeFlag::DayOfMonth)
        {
            paf->iDayOfMonth = iter->iToken;
            ++iter;
        }
        else if (iter->uCouldBe == NodeFlag::WeekOfYear)
        {
            paf->iWeekOfYear = iter->iToken;
            ++iter;
        }
        else if (iter->uCouldBe == NodeFlag::DayOfWeek)
        {
            paf->iDayOfWeek = iter->iToken;
            ++iter;
        }
        else if (iter->uCouldBe == NodeFlag::HourTime)
        {
            paf->iHourTime = iter->iToken;
            ++iter;

            // Check for minute
            if (iter != nodes.end() && iter->uCouldBe == NodeFlag::Minute)
            {
                paf->iMinuteTime = iter->iToken;
                ++iter;

                // Check for second
                if (iter != nodes.end() && iter->uCouldBe == NodeFlag::Second)
                {
                    paf->iSecondTime = iter->iToken;
                    ++iter;

                    // Check for subsecond
                    if (iter != nodes.end() && iter->uCouldBe == NodeFlag::Subsecond)
                    {
                        // Need to parse subsecond from string_view
                        unsigned short ms = 0, us = 0, ns = 0;

                        // Convert this to use string_view and parse appropriately
                        // This might need a separate implementation to work with string_view
                        if (iter->tokenView.size() <= 7) {
                            // Parse as decimal: convert to string and use standard function
                            // or directly parse from the string_view
                            char buff[8];
                            size_t len = min(iter->tokenView.size(), size_t(7));
                            for (size_t i = 0; i < len; i++) {
                                buff[i] = iter->tokenView[i];
                            }
                            buff[len] = '\0';

                            // Simple parsing directly in this function
                            long val = std::atol(buff);

                            // Scale the value to get ms/us/ns parts
                            size_t digits = len;
                            if (digits >= 3) {
                                ms = (val / 10000) % 1000;
                                us = (val / 10) % 1000;
                                ns = (val % 10) * 100;
                            }
                            else if (digits == 2) {
                                ms = (val / 10) % 1000;
                                us = (val % 10) * 100;
                            }
                            else if (digits == 1) {
                                ms = val % 1000;
                            }
                        }

                        paf->iMillisecondTime = ms;
                        paf->iMicrosecondTime = us;
                        paf->iNanosecondTime = ns;
                        ++iter;
                    }
                }
            }

            // Check for meridian (AM/PM)
            if (iter != nodes.end() && iter->uCouldBe == NodeFlag::Meridian)
            {
                if (paf->iHourTime == 12)
                {
                    paf->iHourTime = 0;
                }
                paf->iHourTime += iter->iToken;
                ++iter;
            }

            continue;  // Already incremented
        }
        else if (iter->uCouldBe == NodeFlag::HourTimeZone)
        {
            paf->iMinuteTimeZone = iter->iToken * 60;

            // Check for sign
            if (iter != nodes.begin())
            {
                auto iterPrev = std::prev(iter);
                if (iterPrev != nodes.end() &&
                    iterPrev->uCouldBe == NodeFlag::Sign &&
                    iterPrev->tokenView.size() > 0 &&
                    iterPrev->tokenView[0] == '-')
                {
                    paf->iMinuteTimeZone = -paf->iMinuteTimeZone;
                }
            }

            ++iter;

            // Check for minute component of timezone
            if (iter != nodes.end() && iter->uCouldBe == NodeFlag::Minute)
            {
                if (paf->iMinuteTimeZone < 0)
                {
                    paf->iMinuteTimeZone -= iter->iToken;
                }
                else
                {
                    paf->iMinuteTimeZone += iter->iToken;
                }
                ++iter;
            }

            continue;  // Already incremented
        }
        else if (iter->uCouldBe == NodeFlag::TimeZone)
        {
            if (iter->iToken < 0)
            {
                paf->iMinuteTimeZone = (iter->iToken / 100) * 60
                    - ((-iter->iToken) % 100);
            }
            else
            {
                paf->iMinuteTimeZone = (iter->iToken / 100) * 60
                    + iter->iToken % 100;
            }
            ++iter;
        }
        else if (hasAnyFlag(iter->uCouldBe, NodeFlag::Sign | NodeFlag::DateTimeSeparator))
        {
            ++iter; // Just skip these
        }
        else
        {
            return false;  // Unrecognized node type
        }
    }

    return true;
}

static bool ConvertAllFieldsToLinearTime(CLinearTimeAbsolute &lta, ALLFIELDS *paf)
{
    FIELDEDTIME ft;
    memset(&ft, 0, sizeof(ft));

    int iExtraDays = 0;
    if (paf->iYear == kFieldNotPresent)
    {
        return false;
    }
    ft.iYear = static_cast<short>(paf->iYear);

    if (paf->iMonthOfYear != kFieldNotPresent && paf->iDayOfMonth != kFieldNotPresent)
    {
        ft.iMonth = static_cast<unsigned short>(paf->iMonthOfYear);
        ft.iDayOfMonth = static_cast<unsigned short>(paf->iDayOfMonth);
    }
    else if (paf->iDayOfYear != kFieldNotPresent)
    {
        iExtraDays = paf->iDayOfYear - 1;
        ft.iMonth = 1;
        ft.iDayOfMonth = 1;
    }
    else if (paf->iWeekOfYear != kFieldNotPresent && paf->iDayOfWeek != kFieldNotPresent)
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

    if (paf->iHourTime != kFieldNotPresent)
    {
        ft.iHour = static_cast<unsigned short>(paf->iHourTime);
        if (paf->iMinuteTime != kFieldNotPresent)
        {
            ft.iMinute = static_cast<unsigned short>(paf->iMinuteTime);
            if (paf->iSecondTime != kFieldNotPresent)
            {
                ft.iSecond = static_cast<unsigned short>(paf->iSecondTime);
                if (paf->iMillisecondTime != kFieldNotPresent)
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
        if (paf->iMinuteTimeZone != kFieldNotPresent)
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
    NodeList nodes;
    string_view inputView(pDateString, mux_strlen(pDateString));

    while (!inputView.empty())
    {
        auto iter = PD_ScanNextToken(inputView, nodes);
        if (iter == nodes.end() && !inputView.empty()) {
            // Scanning encountered an error before reaching end of string
            return false;
        }
    }

    PD_Pass2(nodes);

    PD_Deduction(nodes);
    PD_BreakItDown(nodes);
    PD_Pass5(nodes);
    PD_Pass6(nodes);

    PD_Deduction(nodes);
    PD_BreakItDown(nodes);
    PD_Pass5(nodes);
    PD_Pass6(nodes);

    ALLFIELDS af;
    if (  PD_GetFields(&af, nodes)
       && ConvertAllFieldsToLinearTime(lt, &af))
    {
        *pbZoneSpecified = (af.iMinuteTimeZone != kFieldNotPresent);
        return true;
    }
    return false;
}
