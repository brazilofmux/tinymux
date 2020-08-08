/*! \file timeutil.h
 * \brief CLinearTimeAbsolute and CLinearTimeDelta modules.
 *
 * Date/Time code based on algorithms presented in "Calendrical Calculations",
 * Cambridge Press, 1998.
 */

#ifndef TIMEUTIL_H
#define TIMEUTIL_H

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

class CLinearTimeAbsolute
{
    friend class CLinearTimeDelta;
    friend bool operator<(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb);
    friend bool operator>(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb);
    friend bool operator==(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb);
    friend bool operator<=(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb);
    friend CLinearTimeAbsolute operator+(const CLinearTimeAbsolute& lta, const CLinearTimeDelta& ltd);
    friend CLinearTimeAbsolute operator-(const CLinearTimeAbsolute& lta, const CLinearTimeDelta& ltd);
    friend CLinearTimeDelta operator-(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb);

private:
    INT64  m_tAbsolute;
    static int m_nCount;
    static UTF8 m_Buffer[I64BUF_SIZE*2];

public:
    //CLinearTimeAbsolute(int tInitial);
    CLinearTimeAbsolute(void);
    CLinearTimeAbsolute(const CLinearTimeAbsolute& ltaOrigin, const CLinearTimeDelta& ltdOffset);
    CLinearTimeAbsolute(const CLinearTimeAbsolute& lta);
    void operator=(const CLinearTimeAbsolute& lta);
    void operator+=(const CLinearTimeDelta& ltdOffset);
    void operator-=(const CLinearTimeDelta& ltdOffset);

    void GetUTC(void);
    void GetLocal(void);

    void  ReturnUniqueString(UTF8 *buffer, size_t nBuffer);
    UTF8 *ReturnDateString(int nFracDigits = 0);
    bool  ReturnFields(FIELDEDTIME *arg_tStruct);
    INT64 ReturnSeconds(void);
    UTF8 *ReturnSecondsString(int nFracDigits = 0);
    INT64 Return100ns(void);

    void SetSeconds(INT64 arg_tSeconds);
    bool SetSecondsString(UTF8 *arg_szSeconds);
    bool SetFields(FIELDEDTIME *arg_tStruct);
    bool SetString(const UTF8 *arg_tBuffer);
    void Set100ns(INT64 arg_t100ns);

    void UTC2Local(void);
    void Local2UTC(void);
};

bool FieldedTimeToLinearTime(FIELDEDTIME *ft, INT64 *plt);
bool LinearTimeToFieldedTime(INT64 lt, FIELDEDTIME *ft);

class CLinearTimeDelta
{
    friend class CLinearTimeAbsolute;
    friend bool operator<(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb);
    friend bool operator>(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb);
    friend bool operator==(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb);
    friend bool operator<=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb);
    friend bool operator!=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb);
    friend CLinearTimeDelta operator-(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb);
    friend CLinearTimeDelta operator-(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb);
    friend int operator/(const CLinearTimeDelta& ltdA, const CLinearTimeDelta& ltdB);
    friend CLinearTimeDelta operator*(const CLinearTimeDelta& ltdA, int nScaler);
    friend CLinearTimeAbsolute operator+(const CLinearTimeAbsolute& ltdA, const CLinearTimeDelta& ltdB);
    friend CLinearTimeAbsolute operator-(const CLinearTimeAbsolute& lta, const CLinearTimeDelta& ltd);

private:
    INT64 m_tDelta;
    static UTF8 m_Buffer[I64BUF_SIZE*2];

public:
    CLinearTimeDelta(void);
    CLinearTimeDelta(INT64 arg_t100ns);
    CLinearTimeDelta(CLinearTimeAbsolute, CLinearTimeAbsolute);

    void ReturnTimeValueStruct(struct timeval *tv);
#ifdef HAVE_NANOSLEEP
    void ReturnTimeSpecStruct(struct timespec *ts);
#endif
    long ReturnMilliseconds(void);
    INT64 ReturnMicroseconds(void);
    long ReturnDays(void);
    long ReturnSeconds(void);
    UTF8 *ReturnSecondsString(int nFracDigits = 0);
    INT64 Return100ns(void);

    void SetTimeValueStruct(struct timeval *tv);
    void SetMilliseconds(unsigned long arg_dwMilliseconds);
    void SetSeconds(INT64 arg_tSeconds);
    void SetSecondsString(UTF8 *arg_szSeconds);
    void Set100ns(INT64 arg_t100ns);

    void operator+=(const CLinearTimeDelta& ltd);
};

class mux_alarm
{
private:
    bool alarm_set_{};
#if defined(WINDOWS_THREADS)
    HANDLE thread_handle_;
volatile HANDLE semaphore_handle_;
volatile DWORD  alarm_period_in_milliseconds_{};
static DWORD WINAPI alarm_proc(LPVOID parameter);
#endif // WINDOWS_THREADS

public:
    bool alarmed{};

#if defined(WINDOWS_THREADS)
    ~mux_alarm();
#endif // WINDOWS_THREADS

    mux_alarm();
    static void sleep(CLinearTimeDelta sleep_period);
    static void surrender_slice();

    void set(CLinearTimeDelta alarm_period);
    void clear();

#if defined(UNIX_SIGNALS)
    void signal();
#endif // UNIX_SIGNALS
};

extern mux_alarm alarm_clock;

#define FACTOR_NANOSECONDS_PER_100NS 100
#define FACTOR_100NS_PER_MICROSECOND 10
#define FACTOR_100NS_PER_MILLISECOND 10000
#define EPOCH_OFFSET              INT64_C(116444736000000000)
#define EARLIEST_VALID_DATE       INT64_C(-9106391088000000000)
#define LATEST_VALID_DATE         INT64_C(9222834959999999999)
#define TIMEUTIL_TIME_T_MIN_VALUE INT64_C(-922283539200)
#define TIMEUTIL_TIME_T_MAX_VALUE INT64_C(910638979199)
extern const INT64 FACTOR_MS_PER_SECOND;
extern const INT64 FACTOR_US_PER_SECOND;
extern const INT64 FACTOR_100NS_PER_SECOND;
extern const INT64 FACTOR_100NS_PER_MINUTE;
extern const INT64 FACTOR_100NS_PER_HOUR;
extern const INT64 FACTOR_100NS_PER_DAY;
extern const INT64 FACTOR_100NS_PER_WEEK;

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

void TIME_Initialize(void);

#ifdef SMALLEST_INT_GTE_NEG_QUOTIENT
INT64 i64Mod(INT64 x, INT64 y);
INT64 i64FloorDivision(INT64 x, INT64 y);
inline INT64 i64Division(INT64 x, INT64 y) { return x / y; }
inline INT64 i64Remainder(INT64 x, INT64 y) { return x % y; }
int iFloorDivisionMod(int x, int y, int *piMod);
#else // SMALLEST_INT_GTE_NEG_QUOTIENT
inline INT64 i64Mod(INT64 x, INT64 y) { return x % y; }
inline INT64 i64FloorDivision(INT64 x, INT64 y) { return x / y; }
INT64 i64Division(INT64 x, INT64 y);
INT64 i64Remainder(INT64 x, INT64 y);
inline int iFloorDivisionMod(int x, int y, int *piMod) \
{                   \
    *piMod = x % y; \
    return x / y;   \
}
#endif // SMALLEST_INT_GTE_NEG_QUOTIENT

int iMod(int x, int y);
int iFloorDivision(int x, int y);
INT64 i64FloorDivisionMod(INT64 x, INT64 y, INT64 *piMod);
bool ParseDate(CLinearTimeAbsolute &lta, UTF8 *pDateString, bool *pbZoneSpecified);
void ParseDecimalSeconds(size_t n, const UTF8 *p, unsigned short *iMilli,
                         unsigned short *iMicro, unsigned short *iNano);
bool isLeapYear(long iYear);
void ConvertToSecondsString(UTF8 *buffer, INT64 n64, int nFracDigits);
bool ParseFractionalSecondsString(INT64 &i64, UTF8 *str);
void GetUTCLinearTime(INT64 *plt);
bool do_convtime(const UTF8 *str, FIELDEDTIME *ft);
CLinearTimeDelta QueryLocalOffsetAtUTC
(
    const CLinearTimeAbsolute &lta,
    bool *pisDST
);

extern const UTF8 *monthtab[12];
extern const char daystab[12];
extern const UTF8 *DayOfWeekString[7];

#endif // TIMEUTIL_H
