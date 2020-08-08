/*! \file timeutil.cpp
 * \brief CLinearTimeAbsolute module.
 *
 * CLinearTimeAbsolute deals with civil time from a fixed Epoch.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

int CLinearTimeAbsolute::m_nCount = 0;
UTF8 CLinearTimeAbsolute::m_Buffer[I64BUF_SIZE*2];

bool operator<(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb)
{
    return lta.m_tAbsolute < ltb.m_tAbsolute;
}

bool operator>(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb)
{
    return lta.m_tAbsolute > ltb.m_tAbsolute;
}

bool operator==(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb)
{
    return lta.m_tAbsolute == ltb.m_tAbsolute;
}

bool operator<=(const CLinearTimeAbsolute& lta, const CLinearTimeAbsolute& ltb)
{
    return lta.m_tAbsolute <= ltb.m_tAbsolute;
}

CLinearTimeAbsolute operator-(const CLinearTimeAbsolute& lta, const CLinearTimeDelta& ltd)
{
    CLinearTimeAbsolute t;
    t.m_tAbsolute = lta.m_tAbsolute - ltd.m_tDelta;
    return t;
}

CLinearTimeAbsolute::CLinearTimeAbsolute(const CLinearTimeAbsolute& ltaOrigin, const CLinearTimeDelta& ltdOffset)
{
    m_tAbsolute = ltaOrigin.m_tAbsolute + ltdOffset.m_tDelta;
}

UTF8 *CLinearTimeAbsolute::ReturnSecondsString(int nFracDigits)
{
    ConvertToSecondsString(m_Buffer, m_tAbsolute - EPOCH_OFFSET, nFracDigits);
    return m_Buffer;
}

CLinearTimeAbsolute::CLinearTimeAbsolute(const CLinearTimeAbsolute &lta)
{
    m_tAbsolute = lta.m_tAbsolute;
}

CLinearTimeAbsolute::CLinearTimeAbsolute(void)
{
    m_tAbsolute = 0;
}

void CLinearTimeAbsolute::Set100ns(INT64 arg_t100ns)
{
    m_tAbsolute = arg_t100ns;
}

INT64 CLinearTimeAbsolute::Return100ns(void)
{
    return m_tAbsolute;
}

void CLinearTimeAbsolute::SetSeconds(INT64 arg_tSeconds)
{
    m_tAbsolute = arg_tSeconds;
    m_tAbsolute *= FACTOR_100NS_PER_SECOND;

    // Epoch difference between (00:00:00 UTC, January 1, 1970) and
    // (00:00:00 UTC, January 1, 1601).
    //
    m_tAbsolute += EPOCH_OFFSET;
}

INT64 CLinearTimeAbsolute::ReturnSeconds(void)
{
    // INT64 is in hundreds of nanoseconds.
    // And the Epoch is 0:00 1/1/1601 instead of 0:00 1/1/1970
    //
    return i64FloorDivision(m_tAbsolute - EPOCH_OFFSET, FACTOR_100NS_PER_SECOND);
}

bool CLinearTimeAbsolute::ReturnFields(FIELDEDTIME *arg_tStruct)
{
    return LinearTimeToFieldedTime(m_tAbsolute, arg_tStruct);
}

bool CLinearTimeAbsolute::SetString(const UTF8 *arg_tBuffer)
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

CLinearTimeAbsolute operator+(const CLinearTimeAbsolute& ltaA, const CLinearTimeDelta& ltdB)
{
    CLinearTimeAbsolute lta;
    lta.m_tAbsolute = ltaA.m_tAbsolute + ltdB.m_tDelta;
    return lta;
}

void CLinearTimeAbsolute::operator=(const CLinearTimeAbsolute& lta)
{
    m_tAbsolute = lta.m_tAbsolute;
}

void CLinearTimeAbsolute::operator-=(const CLinearTimeDelta& ltd)
{
    m_tAbsolute -= ltd.m_tDelta;
}

void CLinearTimeAbsolute::operator+=(const CLinearTimeDelta& ltd)
{
    m_tAbsolute += ltd.m_tDelta;
}

bool CLinearTimeAbsolute::SetFields(FIELDEDTIME *arg_tStruct)
{
    m_tAbsolute = 0;
    return FieldedTimeToLinearTime(arg_tStruct, &m_tAbsolute);
}

void CLinearTimeAbsolute::ReturnUniqueString(UTF8 *buffer, size_t nBuffer)
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

UTF8 *CLinearTimeAbsolute::ReturnDateString(int nFracDigits)
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
        if (  0 < nFracDigits
           && (  ft.iMillisecond != 0
              || ft.iMicrosecond != 0
              || ft.iNanosecond != 0))
        {
            mux_sprintf(buffer, sizeof(buffer), T(".%03d%03d%03d"),
                ft.iMillisecond, ft.iMicrosecond, ft.iNanosecond);

            // Remove trailing zeros.
            //
            UTF8 *p = (buffer + 1) + (nFracDigits - 1);
            while (*p == '0')
            {
                p--;
            }
            p++;
            *p = '\0';
        }
        mux_sprintf(m_Buffer, sizeof(m_Buffer),
            T("%s %s %02d %02d:%02d:%02d%s %04d"),
            DayOfWeekString[ft.iDayOfWeek], monthtab[ft.iMonth-1],
            ft.iDayOfMonth, ft.iHour, ft.iMinute, ft.iSecond, buffer,
            ft.iYear);
    }
    else
    {
        m_Buffer[0] = '\0';
    }
    return m_Buffer;
}

void CLinearTimeAbsolute::GetUTC(void)
{
    GetUTCLinearTime(&m_tAbsolute);
}

void CLinearTimeAbsolute::GetLocal(void)
{
    GetUTCLinearTime(&m_tAbsolute);
    UTC2Local();
}

bool CLinearTimeAbsolute::SetSecondsString(UTF8 *arg_szSeconds)
{
    INT64 t;
    const INT64 tEarliest = EARLIEST_VALID_DATE;
    const INT64 tLatest   = LATEST_VALID_DATE;
    ParseFractionalSecondsString(t, arg_szSeconds);
    t += EPOCH_OFFSET;
    if (  tEarliest <= t
       && t <= tLatest)
    {
        m_tAbsolute = t;
        return true;
    }
    return false;
}

void CLinearTimeAbsolute::UTC2Local(void)
{
    bool bDST;
    CLinearTimeDelta ltd = QueryLocalOffsetAtUTC(*this, &bDST);
    m_tAbsolute += ltd.m_tDelta;
}

void CLinearTimeAbsolute::Local2UTC(void)
{
    bool bDST1, bDST2;
    CLinearTimeDelta ltdOffset1 = QueryLocalOffsetAtUTC(*this, &bDST1);
    CLinearTimeAbsolute ltaUTC2 = *this - ltdOffset1;
    CLinearTimeDelta ltdOffset2 = QueryLocalOffsetAtUTC(ltaUTC2, &bDST2);

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
