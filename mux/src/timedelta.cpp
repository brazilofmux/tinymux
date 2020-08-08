/*! \file timedelta.cpp
 * \brief CLinearTimeDelta module.
 *
 * CLinearTimeDelta deals with time differences.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

UTF8 CLinearTimeDelta::m_Buffer[I64BUF_SIZE*2];

bool operator==(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb)
{
    return lta.m_tDelta == ltb.m_tDelta;
}

bool operator!=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb)
{
    return lta.m_tDelta != ltb.m_tDelta;
}

bool operator<=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb)
{
    return lta.m_tDelta <= ltb.m_tDelta;
}

UTF8 *CLinearTimeDelta::ReturnSecondsString(int nFracDigits)
{
    ConvertToSecondsString(m_Buffer, m_tDelta, nFracDigits);
    return m_Buffer;
}

CLinearTimeDelta::CLinearTimeDelta(void)
{
    m_tDelta = 0;
}

CLinearTimeDelta::CLinearTimeDelta(INT64 arg_t100ns)
{
    m_tDelta = arg_t100ns;
}

void CLinearTimeDelta::ReturnTimeValueStruct(struct timeval *tv)
{
    INT64 Leftover;
    tv->tv_sec = static_cast<long>(i64FloorDivisionMod(m_tDelta, FACTOR_100NS_PER_SECOND, &Leftover));
    tv->tv_usec = static_cast<long>(i64FloorDivision(Leftover, FACTOR_100NS_PER_MICROSECOND));
}

#ifdef HAVE_NANOSLEEP
void CLinearTimeDelta::ReturnTimeSpecStruct(struct timespec *ts)
{
    INT64 Leftover;
    ts->tv_sec = static_cast<long>(i64FloorDivisionMod(m_tDelta, FACTOR_100NS_PER_SECOND, &Leftover));
    ts->tv_nsec = static_cast<long>(Leftover*FACTOR_NANOSECONDS_PER_100NS);
}
#endif // HAVE_NANOSLEEP

void CLinearTimeDelta::SetTimeValueStruct(struct timeval *tv)
{
    m_tDelta = FACTOR_100NS_PER_SECOND * tv->tv_sec
             + FACTOR_100NS_PER_MICROSECOND * tv->tv_usec;
}

void CLinearTimeDelta::SetMilliseconds(unsigned long arg_dwMilliseconds)
{
    m_tDelta = arg_dwMilliseconds * FACTOR_100NS_PER_MILLISECOND;
}

long CLinearTimeDelta::ReturnMilliseconds(void)
{
    return static_cast<long>(m_tDelta/FACTOR_100NS_PER_MILLISECOND);
}

INT64 CLinearTimeDelta::ReturnMicroseconds(void)
{
    return m_tDelta/FACTOR_100NS_PER_MICROSECOND;
}

void CLinearTimeDelta::SetSecondsString(UTF8 *arg_szSeconds)
{
    ParseFractionalSecondsString(m_tDelta, arg_szSeconds);
}

void CLinearTimeDelta::SetSeconds(INT64 arg_tSeconds)
{
    m_tDelta = arg_tSeconds * FACTOR_100NS_PER_SECOND;
}

void CLinearTimeDelta::Set100ns(INT64 arg_t100ns)
{
    m_tDelta = arg_t100ns;
}

INT64 CLinearTimeDelta::Return100ns(void)
{
    return m_tDelta;
}

CLinearTimeDelta::CLinearTimeDelta(CLinearTimeAbsolute t0, CLinearTimeAbsolute t1)
{
    m_tDelta = t1.m_tAbsolute - t0.m_tAbsolute;
}

long CLinearTimeDelta::ReturnDays(void)
{
    return static_cast<long>(m_tDelta/FACTOR_100NS_PER_DAY);
}

long CLinearTimeDelta::ReturnSeconds(void)
{
    return static_cast<long>(m_tDelta/FACTOR_100NS_PER_SECOND);
}

void CLinearTimeDelta::operator+=(const CLinearTimeDelta& ltd)
{
    m_tDelta += ltd.m_tDelta;
}

CLinearTimeDelta operator-(const CLinearTimeAbsolute& ltaA, const CLinearTimeAbsolute& ltaB)
{
    CLinearTimeDelta ltd;
    ltd.m_tDelta = ltaA.m_tAbsolute - ltaB.m_tAbsolute;
    return ltd;
}

CLinearTimeDelta operator-(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb)
{
    CLinearTimeDelta ltd;
    ltd.m_tDelta = lta.m_tDelta - ltb.m_tDelta;
    return ltd;
}

CLinearTimeDelta operator*(const CLinearTimeDelta& ltd, int Scale)
{
    CLinearTimeDelta ltdResult;
    ltdResult.m_tDelta = ltd.m_tDelta * Scale;
    return ltdResult;
}

int operator/(const CLinearTimeDelta& ltdA, const CLinearTimeDelta& ltdB)
{
    int iResult = static_cast<int>(ltdA.m_tDelta / ltdB.m_tDelta);
    return iResult;
}

bool operator<(const CLinearTimeDelta& ltdA, const CLinearTimeDelta& ltdB)
{
    return ltdA.m_tDelta < ltdB.m_tDelta;
}

bool operator>(const CLinearTimeDelta& ltdA, const CLinearTimeDelta& ltdB)
{
    return ltdA.m_tDelta > ltdB.m_tDelta;
}

