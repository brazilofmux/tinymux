/*! \file timeutil.h
 * \brief CLinearTimeAbsolute and CLinearTimeDelta modules.
 *
 * Date/Time code based on algorithms presented in "Calendrical Calculations",
 * Cambridge Press, 1998.
 */

#ifndef TIMEUTIL_H
#define TIMEUTIL_H

#include <chrono>
#include <memory>
#include <atomic>
#include <cstdint>
#include <thread>
#include <mutex>
#include <condition_variable>

using UnderlyingTickType = std::int64_t;
using HectoNanoseconds = std::chrono::duration<UnderlyingTickType, std::ratio<1, 10'000'000>>;

typedef struct
{
             short iYear;       // 1900 would be stored as 1900.
    unsigned short iMonth;      // January is 1. December is 12.
    unsigned short iDayOfWeek;  // 0 is Sunday, 1 is Monday, etc.
    unsigned short iDayOfMonth; // Day of Month, 1..31
    unsigned short iDayOfYear;  // January 1st is 1, etc.
    unsigned short iHour;
    unsigned short iMinute;
    unsigned short iSecond;
    unsigned short iMillisecond; // Milliseconds less than a second.
    unsigned short iMicrosecond; // Microseconds less than a Millisecond.
    unsigned short iNanosecond;  // Nanoseconds less than a Microsecond.
} FIELDEDTIME;

class CLinearTimeDelta;

class LIBMUX_API CLinearTimeAbsolute
{
    friend class CLinearTimeDelta;
    friend LIBMUX_API bool operator<(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept;
    friend LIBMUX_API bool operator>(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept;
    friend LIBMUX_API bool operator==(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept;
    friend LIBMUX_API bool operator<=(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept;
    friend LIBMUX_API bool operator>=(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept;
    friend LIBMUX_API CLinearTimeAbsolute operator+(const CLinearTimeAbsolute& lta, const CLinearTimeDelta& ltd) noexcept;
    friend LIBMUX_API CLinearTimeAbsolute operator-(const CLinearTimeAbsolute& lta, const CLinearTimeDelta& ltd) noexcept;
    friend LIBMUX_API CLinearTimeDelta operator-(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb);

private:
    UnderlyingTickType m_tAbsolute;
    static int m_nCount;
    static UTF8 m_Buffer[I64BUF_SIZE * 2];

public:
    //! Default constructor - initializes to epoch (zero)
    CLinearTimeAbsolute() noexcept;

    //! Constructor from origin time plus offset
    CLinearTimeAbsolute(const CLinearTimeAbsolute& ltaOrigin, const CLinearTimeDelta& ltdOffset) noexcept;

    //! Copy constructor
    CLinearTimeAbsolute(const CLinearTimeAbsolute& lta) noexcept;

    //! Assignment operator
    CLinearTimeAbsolute& operator=(const CLinearTimeAbsolute& lta) noexcept;

    //! Add time delta to this time
    CLinearTimeAbsolute& operator+=(const CLinearTimeDelta& ltdOffset) noexcept;

    //! Subtract time delta from this time
    CLinearTimeAbsolute& operator-=(const CLinearTimeDelta& ltdOffset) noexcept;

    //! Get UTC time
    void GetUTC();

    //! Get local time
    void GetLocal();

    //! Return a unique string representation
    void ReturnUniqueString(UTF8* buffer, size_t nBuffer) const;

    //! Return date as string with optional fractional digits
    const UTF8* ReturnDateString(int nFracDigits = 0) const;

    //! Convert time to fielded representation
    bool ReturnFields(FIELDEDTIME* arg_tStruct) const;

    //! Get seconds since Unix epoch
    UnderlyingTickType ReturnSeconds() const noexcept;

    //! Get seconds as string with optional fractional digits
    const UTF8* ReturnSecondsString(int nFracDigits = 0) const;

    //! Get raw 100ns ticks
    UnderlyingTickType Return100ns() const noexcept;

    //! Set time from seconds since Unix epoch
    void SetSeconds(UnderlyingTickType arg_tSeconds) noexcept;

    //! Set time from seconds string
    bool SetSecondsString(UTF8* arg_szSeconds);

    //! Set time from fielded time
    bool SetFields(FIELDEDTIME* arg_tStruct);

    //! Set time from string representation
    bool SetString(const UTF8* arg_tBuffer);

    //! Set raw 100ns ticks
    void Set100ns(UnderlyingTickType arg_t100ns) noexcept;

    //! Convert UTC time to local time
    void UTC2Local();

    //! Convert local time to UTC
    void Local2UTC();
};

//! Less than comparison
LIBMUX_API bool operator<(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept;

//! Greater than comparison
LIBMUX_API bool operator>(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept;

//! Equality comparison
LIBMUX_API bool operator==(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept;

//! Less than or equal comparison
LIBMUX_API bool operator<=(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept;

//! Greater than or equal comparison
LIBMUX_API bool operator>=(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept;

//! Add time delta to absolute time
LIBMUX_API CLinearTimeAbsolute operator+(const CLinearTimeAbsolute& lta, const CLinearTimeDelta& ltd) noexcept;

//! Subtract time delta from absolute time
LIBMUX_API CLinearTimeAbsolute operator-(const CLinearTimeAbsolute& lta, const CLinearTimeDelta& ltd) noexcept;

LIBMUX_API bool FieldedTimeToLinearTime(FIELDEDTIME *ft, int64_t *plt);
LIBMUX_API bool LinearTimeToFieldedTime(int64_t lt, FIELDEDTIME *ft);

class LIBMUX_API CLinearTimeDelta {
public:
    // --- Chrono Integration ---
    // Define the equivalent chrono duration type publicly
    using HectoNanoseconds = std::chrono::duration<UnderlyingTickType, std::ratio<1, 10'000'000>>;

    // Constructor from chrono duration (explicit to avoid accidental conversions)
    explicit CLinearTimeDelta(const HectoNanoseconds& duration) noexcept;

    // Conversion function to chrono duration
    HectoNanoseconds ToChronoDuration() const noexcept;

    // Optional: Conversion operator (allows implicit conversion TO chrono)
    // operator HectoNanoseconds() const noexcept;

    // Optional: Setter from chrono duration
    void SetChronoDuration(const HectoNanoseconds& duration) noexcept;

    // --- Existing Interface (Keep for compatibility) ---

    // Static buffer - still recommend changing this eventually for thread safety
    // For now, keep the original signature
    UTF8* ReturnSecondsString(int nFracDigits = 7); // Default precision matches 100ns

    // Constructors
    CLinearTimeDelta() noexcept;
    CLinearTimeDelta(UnderlyingTickType arg_t100ns) noexcept; // Use standard type
    CLinearTimeDelta(CLinearTimeAbsolute t0, CLinearTimeAbsolute t1);

    // Conversions (Consider adding const)
    void ReturnTimeValueStruct(struct timeval* tv) const;
#ifdef HAVE_NANOSLEEP
    void ReturnTimeSpecStruct(struct timespec* ts) const;
#endif // HAVE_NANOSLEEP

    // Setters
    void SetTimeValueStruct(const struct timeval* tv); // Pass const*
    void SetMilliseconds(long arg_dwMilliseconds); // Or std::int64_t?
    void SetSecondsString(const UTF8* arg_szSeconds); // Use const*
    void SetSeconds(UnderlyingTickType arg_tSeconds); // Use standard type
    void Set100ns(UnderlyingTickType arg_t100ns) noexcept; // Use standard type

    // Getters (Mark const)
    long ReturnMilliseconds() const; // Return type may truncate
    UnderlyingTickType ReturnMicroseconds() const; // Return type fits
    UnderlyingTickType Return100ns() const noexcept;
    long ReturnDays() const; // Return type may truncate
    long ReturnSeconds() const; // Return type may truncate

    // Operators
    CLinearTimeDelta& operator+=(const CLinearTimeDelta& ltd) noexcept;
    CLinearTimeDelta& operator-=(const CLinearTimeDelta& ltd) noexcept;

    // Comparison operators (declare as friends or non-members)
    friend LIBMUX_API bool operator==(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept;
    friend LIBMUX_API bool operator!=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept;
    friend LIBMUX_API bool operator<=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept;
    friend LIBMUX_API bool operator<(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept;
    friend LIBMUX_API bool operator>=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept;
    friend LIBMUX_API bool operator>(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept;

private:
    UnderlyingTickType m_tDelta; // The core data: count of 100ns intervals

    // Still unsafe static buffer - keep for now
    static UTF8 m_Buffer[I64BUF_SIZE * 2];

    // Make relevant conversion factors accessible if needed internally
    // static constexpr UnderlyingTickType FACTOR_100NS_PER_SECOND = 10'000'000; // etc.
};

// Non-member operators (use UnderlyingTickType, check division by zero)
LIBMUX_API CLinearTimeDelta operator-(const CLinearTimeAbsolute& ltaA, const CLinearTimeAbsolute& ltaB);
LIBMUX_API CLinearTimeDelta operator-(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept;
LIBMUX_API CLinearTimeDelta operator*(const CLinearTimeDelta& ltd, int Scale);
LIBMUX_API UnderlyingTickType operator/(const CLinearTimeDelta& ltdA, const CLinearTimeDelta& ltdB);
LIBMUX_API bool operator==(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept;
LIBMUX_API bool operator!=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept;
LIBMUX_API bool operator<=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept;
LIBMUX_API bool operator<(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept;
LIBMUX_API bool operator>=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept;
LIBMUX_API bool operator>(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept;

class LIBMUX_API mux_alarm
{
private:
    bool alarm_set_{};
    std::thread alarm_thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_{};
    std::chrono::milliseconds alarm_period_{0};
    bool wake_{};                    // spurious-wakeup guard

    void alarm_proc();

public:
    std::atomic<bool> alarmed{};
    mux_alarm();
    ~mux_alarm();
    static void sleep(CLinearTimeDelta sleep_period);
    static void surrender_slice();
    void set(CLinearTimeDelta alarm_period);
    void clear();
};

extern LIBMUX_API mux_alarm alarm_clock;

#define FACTOR_NANOSECONDS_PER_100NS 100
#define FACTOR_100NS_PER_MICROSECOND 10
#define FACTOR_100NS_PER_MILLISECOND 10000
#define EPOCH_OFFSET              INT64_C(116444736000000000)
#define EARLIEST_VALID_DATE       INT64_C(-9106391088000000000)
#define LATEST_VALID_DATE         INT64_C(9222834959999999999)
#define TIMEUTIL_TIME_T_MIN_VALUE INT64_C(-922283539200)
#define TIMEUTIL_TIME_T_MAX_VALUE INT64_C(910638979199)
extern LIBMUX_API const int64_t FACTOR_MS_PER_SECOND;
extern LIBMUX_API const int64_t FACTOR_US_PER_SECOND;
extern LIBMUX_API const int64_t FACTOR_100NS_PER_SECOND;
extern LIBMUX_API const int64_t FACTOR_100NS_PER_MINUTE;
extern LIBMUX_API const int64_t FACTOR_100NS_PER_HOUR;
extern LIBMUX_API const int64_t FACTOR_100NS_PER_DAY;
extern LIBMUX_API const int64_t FACTOR_100NS_PER_WEEK;

const CLinearTimeDelta time_200us = 200*FACTOR_100NS_PER_MICROSECOND;
const CLinearTimeDelta time_5ms   = 5*FACTOR_100NS_PER_MILLISECOND;
const CLinearTimeDelta time_250ms = 250*FACTOR_100NS_PER_MILLISECOND;
const CLinearTimeDelta time_1s    = FACTOR_100NS_PER_SECOND;
const CLinearTimeDelta time_5s    = 5*FACTOR_100NS_PER_SECOND;
const CLinearTimeDelta time_15s   = 15*FACTOR_100NS_PER_SECOND;
const CLinearTimeDelta time_30s   = 30*FACTOR_100NS_PER_SECOND;
const CLinearTimeDelta time_45s   = 45*FACTOR_100NS_PER_SECOND;
const CLinearTimeDelta time_15m   = 15*FACTOR_100NS_PER_MINUTE;
const CLinearTimeDelta time_30m   = 30*FACTOR_100NS_PER_MINUTE;
const CLinearTimeDelta time_1w    = FACTOR_100NS_PER_WEEK;

namespace TimezoneCache {
    LIBMUX_API void initialize(void);
    LIBMUX_API CLinearTimeDelta queryLocalOffsetAtUTC(const CLinearTimeAbsolute& utc_lta, bool* is_dst);
    LIBMUX_API CLinearTimeDelta getCurrentLocalOffset(bool* is_dst);
}

#ifdef SMALLEST_INT_GTE_NEG_QUOTIENT
LIBMUX_API int64_t i64Mod(int64_t x, int64_t y);
LIBMUX_API int64_t i64FloorDivision(int64_t x, int64_t y);
inline int64_t i64Division(int64_t x, int64_t y) { return x / y; }
inline int64_t i64Remainder(int64_t x, int64_t y) { return x % y; }
LIBMUX_API int iFloorDivisionMod(int x, int y, int *piMod);
#else // SMALLEST_INT_GTE_NEG_QUOTIENT
inline int64_t i64Mod(int64_t x, int64_t y) { return x % y; }
inline int64_t i64FloorDivision(int64_t x, int64_t y) { return x / y; }
LIBMUX_API int64_t i64Division(int64_t x, int64_t y);
LIBMUX_API int64_t i64Remainder(int64_t x, int64_t y);
inline int iFloorDivisionMod(int x, int y, int *piMod) \
{                   \
    *piMod = x % y; \
    return x / y;   \
}
#endif // SMALLEST_INT_GTE_NEG_QUOTIENT

LIBMUX_API int iMod(int x, int y);
LIBMUX_API int iFloorDivision(int x, int y);
LIBMUX_API int64_t i64FloorDivisionMod(int64_t x, int64_t y, int64_t *piMod);
LIBMUX_API bool ParseDate(CLinearTimeAbsolute &lta, UTF8 *pDateString, bool *pbZoneSpecified);
LIBMUX_API void ParseDecimalSeconds(size_t n, const UTF8 *p, unsigned short *iMilli,
                         unsigned short *iMicro, unsigned short *iNano);
LIBMUX_API bool isLeapYear(long iYear);
LIBMUX_API void ConvertToSecondsString(UTF8 *buffer, int64_t n64, int nFracDigits);
LIBMUX_API bool ParseFractionalSecondsString(int64_t &i64, const UTF8 *str);
LIBMUX_API void GetUTCLinearTime(int64_t *plt);
LIBMUX_API bool do_convtime(const UTF8 *str, FIELDEDTIME *ft);
LIBMUX_API CLinearTimeDelta QueryLocalOffsetAtUTC
(
    const CLinearTimeAbsolute &lta,
    bool *pisDST
);

extern LIBMUX_API const UTF8 *monthtab[12];
extern LIBMUX_API const char daystab[12];
extern LIBMUX_API const UTF8 *DayOfWeekString[7];

#endif // TIMEUTIL_H
