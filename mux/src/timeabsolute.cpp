/*! \file timeabsolute.cpp
 * \brief CLinearTimeAbsolute module.
 *
 * CLinearTimeAbsolute deals with civil time from a fixed Epoch.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <memory>
#include <string>
#include <algorithm>

int CLinearTimeAbsolute::m_nCount = 0;
UTF8 CLinearTimeAbsolute::m_Buffer[I64BUF_SIZE * 2];

bool operator<(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept
{
    return lta.m_tAbsolute < ltb.m_tAbsolute;
}

bool operator>(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept
{
    return lta.m_tAbsolute > ltb.m_tAbsolute;
}

bool operator==(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept
{
    return lta.m_tAbsolute == ltb.m_tAbsolute;
}

bool operator<=(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept
{
    return lta.m_tAbsolute <= ltb.m_tAbsolute;
}

bool operator>=(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb) noexcept
{
    return lta.m_tAbsolute >= ltb.m_tAbsolute;
}

CLinearTimeAbsolute operator-(const CLinearTimeAbsolute& lta, const CLinearTimeDelta& ltd) noexcept
{
    CLinearTimeAbsolute t;
    t.m_tAbsolute = lta.m_tAbsolute - ltd.Return100ns();
    return t;
}

CLinearTimeAbsolute::CLinearTimeAbsolute(const CLinearTimeAbsolute& ltaOrigin, const CLinearTimeDelta& ltdOffset) noexcept
{
    m_tAbsolute = ltaOrigin.m_tAbsolute + ltdOffset.Return100ns();
}

const UTF8* CLinearTimeAbsolute::ReturnSecondsString(int nFracDigits) const
{
    ConvertToSecondsString(m_Buffer, m_tAbsolute - EPOCH_OFFSET, nFracDigits);
    return m_Buffer;
}

CLinearTimeAbsolute::CLinearTimeAbsolute(const CLinearTimeAbsolute& lta) noexcept
{
    m_tAbsolute = lta.m_tAbsolute;
}

CLinearTimeAbsolute::CLinearTimeAbsolute() noexcept
{
    m_tAbsolute = 0;
}

void CLinearTimeAbsolute::Set100ns(UnderlyingTickType arg_t100ns) noexcept
{
    m_tAbsolute = arg_t100ns;
}

UnderlyingTickType CLinearTimeAbsolute::Return100ns() const noexcept
{
    return m_tAbsolute;
}

void CLinearTimeAbsolute::SetSeconds(UnderlyingTickType arg_tSeconds) noexcept
{
    m_tAbsolute = arg_tSeconds;
    m_tAbsolute *= FACTOR_100NS_PER_SECOND;

    // Epoch difference between (00:00:00 UTC, January 1, 1970) and
    // (00:00:00 UTC, January 1, 1601).
    //
    m_tAbsolute += EPOCH_OFFSET;
}

UnderlyingTickType CLinearTimeAbsolute::ReturnSeconds() const noexcept
{
    // UnderlyingTickType is in hundreds of nanoseconds.
    // And the Epoch is 0:00 1/1/1601 instead of 0:00 1/1/1970
    //
    return i64FloorDivision(m_tAbsolute - EPOCH_OFFSET, FACTOR_100NS_PER_SECOND);
}

bool CLinearTimeAbsolute::ReturnFields(FIELDEDTIME* arg_tStruct) const
{
    return LinearTimeToFieldedTime(m_tAbsolute, arg_tStruct);
}

bool CLinearTimeAbsolute::SetString(const UTF8* arg_tBuffer)
{
    FIELDEDTIME ft;
    if (do_convtime(arg_tBuffer, &ft))
    {
        if (FieldedTimeToLinearTime(&ft, &m_tAbsolute))
        {
            return true;
        }
    }
    m_tAbsolute = 0;
    return false;
}

CLinearTimeAbsolute operator+(const CLinearTimeAbsolute& ltaA, const CLinearTimeDelta& ltdB) noexcept
{
    CLinearTimeAbsolute lta;
    lta.m_tAbsolute = ltaA.m_tAbsolute + ltdB.Return100ns();
    return lta;
}

CLinearTimeAbsolute& CLinearTimeAbsolute::operator=(const CLinearTimeAbsolute& lta) noexcept
{
    m_tAbsolute = lta.m_tAbsolute;
    return *this;
}

CLinearTimeAbsolute& CLinearTimeAbsolute::operator-=(const CLinearTimeDelta& ltd) noexcept
{
    m_tAbsolute -= ltd.Return100ns();
    return *this;
}

CLinearTimeAbsolute& CLinearTimeAbsolute::operator+=(const CLinearTimeDelta& ltd) noexcept
{
    m_tAbsolute += ltd.Return100ns();
    return *this;
}

bool CLinearTimeAbsolute::SetFields(FIELDEDTIME* arg_tStruct)
{
    m_tAbsolute = 0;
    return FieldedTimeToLinearTime(arg_tStruct, &m_tAbsolute);
}

void CLinearTimeAbsolute::ReturnUniqueString(UTF8* buffer, size_t nBuffer) const
{
    FIELDEDTIME ft;
    if (LinearTimeToFieldedTime(m_tAbsolute, &ft))
    {
        mux_sprintf(buffer, nBuffer, T("%04d%02d%02d-%02d%02d%02d"), ft.iYear, ft.iMonth,
            ft.iDayOfMonth, ft.iHour, ft.iMinute, ft.iSecond);
    }
    else
    {
        mux_sprintf(buffer, nBuffer, T("%03d"), m_nCount++);
    }
}

const UTF8* CLinearTimeAbsolute::ReturnDateString(int nFracDigits) const
{
    FIELDEDTIME ft;
    if (LinearTimeToFieldedTime(m_tAbsolute, &ft))
    {
        // Sanitize Precision Request.
        //
        const int maxFracDigits = 7;
        const int minFracDigits = 0;
        if (nFracDigits < minFracDigits)
        {
            nFracDigits = minFracDigits;
        }
        else if (maxFracDigits < nFracDigits)
        {
            nFracDigits = maxFracDigits;
        }

        UTF8 buffer[11];
        buffer[0] = '\0';
        if (0 < nFracDigits
            && (ft.iMillisecond != 0
                || ft.iMicrosecond != 0
                || ft.iNanosecond != 0))
        {
            mux_sprintf(buffer, sizeof(buffer), T(".%03d%03d%03d"),
                ft.iMillisecond, ft.iMicrosecond, ft.iNanosecond);

            // Remove trailing zeros.
            //
            UTF8* p = (buffer + 1) + (nFracDigits - 1);
            while (*p == '0')
            {
                p--;
            }
            p++;
            *p = '\0';
        }
        mux_sprintf(m_Buffer, sizeof(m_Buffer),
            T("%s %s %02d %02d:%02d:%02d%s %04d"),
            DayOfWeekString[ft.iDayOfWeek], monthtab[ft.iMonth - 1],
            ft.iDayOfMonth, ft.iHour, ft.iMinute, ft.iSecond, buffer,
            ft.iYear);
    }
    else
    {
        m_Buffer[0] = '\0';
    }
    return m_Buffer;
}

void CLinearTimeAbsolute::GetUTC()
{
    GetUTCLinearTime(&m_tAbsolute);
}

void CLinearTimeAbsolute::GetLocal()
{
    GetUTCLinearTime(&m_tAbsolute);
    UTC2Local();
}

bool CLinearTimeAbsolute::SetSecondsString(UTF8* arg_szSeconds)
{
    UnderlyingTickType t;
    const UnderlyingTickType tEarliest = EARLIEST_VALID_DATE;
    const UnderlyingTickType tLatest = LATEST_VALID_DATE;
    if (!ParseFractionalSecondsString(t, arg_szSeconds))
    {
        return false;
    }

    t += EPOCH_OFFSET;
    if (tEarliest <= t && t <= tLatest)
    {
        m_tAbsolute = t;
        return true;
    }
    return false;
}

void CLinearTimeAbsolute::UTC2Local()
{
    bool bDST;
    CLinearTimeDelta ltd = TimezoneCache::queryLocalOffsetAtUTC(*this, &bDST);
    m_tAbsolute += ltd.Return100ns();
}

void CLinearTimeAbsolute::Local2UTC()
{
    bool bDST1, bDST2;
    CLinearTimeDelta ltdOffset1 = TimezoneCache::queryLocalOffsetAtUTC(*this, &bDST1);
    CLinearTimeAbsolute ltaUTC2 = *this - ltdOffset1;
    CLinearTimeDelta ltdOffset2 = TimezoneCache::queryLocalOffsetAtUTC(ltaUTC2, &bDST2);

    CLinearTimeAbsolute ltaLocalGuess = ltaUTC2 + ltdOffset2;
    if (ltaLocalGuess == *this)
    {
        // We found an offset, UTC, local time combination that
        // works.
        //
        m_tAbsolute = ltaUTC2.m_tAbsolute;
    }
    else
    {
        CLinearTimeAbsolute ltaUTC1 = *this - ltdOffset2;
        m_tAbsolute = ltaUTC1.m_tAbsolute;
    }
}
