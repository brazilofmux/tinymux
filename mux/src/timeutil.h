// timeutil.cpp -- CLinearTimeAbsolute, and CLinearTimeDelta modules.
//
// $Id: timeutil.h,v 1.13 2004-05-16 08:14:26 sdennis Exp $
//
// MUX 2.4
// Copyright (C) 1998 through 2004 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.  
//
// Date/Time code based on algorithms presented in "Calendrical Calculations",
// Cambridge Press, 1998.
//
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
    static char m_Buffer[204];

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

    void  ReturnUniqueString(char *buffer);
    char *ReturnDateString(int nFracDigits = 0);
    bool  ReturnFields(FIELDEDTIME *arg_tStruct);
    INT64 ReturnSeconds(void);
    char *ReturnSecondsString(int nFracDigits = 0);
    INT64 Return100ns(void);

    void SetSeconds(INT64 arg_tSeconds);
    void SetSecondsString(char *arg_szSeconds);
    bool SetFields(FIELDEDTIME *arg_tStruct);
    bool SetString(const char *arg_tBuffer);
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
    static char m_Buffer[204];

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
    char *ReturnSecondsString(int nFracDigits = 0);
    INT64 Return100ns(void);

    void SetTimeValueStruct(struct timeval *tv);
    void SetMilliseconds(unsigned long arg_dwMilliseconds);
    void SetSeconds(INT64 arg_tSeconds);
    void SetSecondsString(char *arg_szSeconds);
    void Set100ns(INT64 arg_t100ns);

    void operator+=(const CLinearTimeDelta& ltd);
};

class CMuxAlarm
{
private:
    bool bAlarmSet;
#ifdef WIN32
    HANDLE hThread;
#endif

public:
    bool bAlarmed;

#ifdef WIN32
    volatile HANDLE hSemAlarm;
    volatile DWORD  dwWait;
    ~CMuxAlarm();
#endif

    CMuxAlarm(void);
    void Sleep(CLinearTimeDelta ltd);
    void SurrenderSlice(void);

    void Set(CLinearTimeDelta ltdDeadline);
    void Clear(void);

#ifndef WIN32
    void Signal(void);
#endif
};

extern CMuxAlarm MuxAlarm;

#define FACTOR_NANOSECONDS_PER_100NS 100
#define FACTOR_100NS_PER_MICROSECOND 10
#define FACTOR_100NS_PER_MILLISECOND 10000
#ifdef WIN32
#define EPOCH_OFFSET 116444736000000000i64
#else // WIN32
#define EPOCH_OFFSET 116444736000000000ull
#endif // WIN32
extern const INT64 FACTOR_100NS_PER_SECOND;
extern const INT64 FACTOR_100NS_PER_MINUTE;
extern const INT64 FACTOR_100NS_PER_HOUR;
extern const INT64 FACTOR_100NS_PER_DAY;
extern const INT64 FACTOR_100NS_PER_WEEK;

const CLinearTimeDelta time_250ms = 250*FACTOR_100NS_PER_MILLISECOND;
const CLinearTimeDelta time_1s    = FACTOR_100NS_PER_SECOND;
const CLinearTimeDelta time_15s   = 15*FACTOR_100NS_PER_SECOND;
const CLinearTimeDelta time_30s   = 30*FACTOR_100NS_PER_SECOND;
const CLinearTimeDelta time_45s   = 45*FACTOR_100NS_PER_SECOND;
const CLinearTimeDelta time_15m   = 15*FACTOR_100NS_PER_MINUTE;
const CLinearTimeDelta time_30m   = 30*FACTOR_100NS_PER_MINUTE;
const CLinearTimeDelta time_1w    = FACTOR_100NS_PER_WEEK;

extern void TIME_Initialize(void);
#ifdef WIN32
extern BOOL CalibrateQueryPerformance(void);
#endif

#ifdef SMALLEST_INT_GTE_NEG_QUOTIENT
INT64 i64Mod(INT64 x, INT64 y);
INT64 i64FloorDivision(INT64 x, INT64 y);
DCL_INLINE INT64 i64Division(INT64 x, INT64 y) { return x / y; }
DCL_INLINE INT64 i64Remainder(INT64 x, INT64 y) { return x % y; }
int iFloorDivisionMod(int x, int y, int *piMod);
#else // SMALLEST_INT_GTE_NEG_QUOTIENT
DCL_INLINE INT64 i64Mod(INT64 x, INT64 y) { return x % y; }
DCL_INLINE INT64 i64FloorDivision(INT64 x, INT64 y) { return x / y; }
INT64 i64Division(INT64 x, INT64 y);
INT64 i64Remainder(INT64 x, INT64 y);
int DCL_INLINE iFloorDivisionMod(int x, int y, int *piMod) \
{                   \
    *piMod = x % y; \
    return x / y;   \
}

#endif // SMALLEST_INT_GTE_NEG_QUOTIENT

extern bool ParseDate(CLinearTimeAbsolute &lta, char *pDateString, bool *pbZoneSpecified);
extern bool isLeapYear(long iYear);

#endif // TIMEUTIL_H
