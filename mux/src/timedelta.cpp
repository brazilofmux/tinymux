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

#include <chrono>

UTF8 CLinearTimeDelta::m_Buffer[I64BUF_SIZE*2];

// --- Chrono Integration Implementations ---

// Constructor from chrono duration
CLinearTimeDelta::CLinearTimeDelta(const CLinearTimeDelta::HectoNanoseconds& duration) noexcept
    : m_tDelta(duration.count()) // Directly use the count from chrono duration
{
}

// Conversion function to chrono duration
CLinearTimeDelta::HectoNanoseconds CLinearTimeDelta::ToChronoDuration() const noexcept
{
    // Create a HectoNanoseconds duration from our internal tick count
    return HectoNanoseconds(m_tDelta);
}

/* // Optional: Conversion operator implementation
CLinearTimeDelta::operator CLinearTimeDelta::HectoNanoseconds() const noexcept
{
    return HectoNanoseconds(m_tDelta);
}
*/

// Optional: Setter from chrono duration
void CLinearTimeDelta::SetChronoDuration(const CLinearTimeDelta::HectoNanoseconds& duration) noexcept
{
    m_tDelta = duration.count();
}


// --- Existing Implementations (Update types if changed in header) ---

bool operator==(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept
{
    return lta.m_tDelta == ltb.m_tDelta;
}

// ... other operators (!=, <=, <, >=, >) ...
// Implement >= if added to header
bool operator>=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept
{
    return lta.m_tDelta >= ltb.m_tDelta;
}


UTF8* CLinearTimeDelta::ReturnSecondsString(int nFracDigits)
{
    // Existing implementation (still uses static buffer)
    ConvertToSecondsString(m_Buffer, m_tDelta, nFracDigits);
    return m_Buffer;
}

CLinearTimeDelta::CLinearTimeDelta() noexcept
    : m_tDelta(0)
{
}

CLinearTimeDelta::CLinearTimeDelta(UnderlyingTickType arg_t100ns) noexcept
    : m_tDelta(arg_t100ns)
{
}

void CLinearTimeDelta::ReturnTimeValueStruct(struct timeval* tv) const // Added const
{
    // Prefer using standard constants if available
    // Example using chrono internally (optional refactor):
    // auto secs = std::chrono::duration_cast<std::chrono::seconds>(ToChronoDuration());
    // auto usecs = std::chrono::duration_cast<std::chrono::microseconds>(ToChronoDuration() - secs);
    // tv->tv_sec = static_cast<time_t>(secs.count()); // Check overflow potential
    // tv->tv_usec = static_cast<suseconds_t>(usecs.count()); // Check overflow potential

    // Original implementation (using assumed factors):
    UnderlyingTickType Leftover;
    // Assuming FACTOR_100NS_PER_SECOND and FACTOR_100NS_PER_MICROSECOND are accessible
    tv->tv_sec = static_cast<long>(i64FloorDivisionMod(m_tDelta, FACTOR_100NS_PER_SECOND, &Leftover));
    tv->tv_usec = static_cast<long>(i64FloorDivision(Leftover, FACTOR_100NS_PER_MICROSECOND));
}

#ifdef HAVE_NANOSLEEP
void CLinearTimeDelta::ReturnTimeSpecStruct(struct timespec* ts) const // Added const
{
    // Similar logic to timeval, using FACTOR_NANOSECONDS_PER_100NS
    UnderlyingTickType Leftover;
    ts->tv_sec = static_cast<long>(i64FloorDivisionMod(m_tDelta, FACTOR_100NS_PER_SECOND, &Leftover));
    ts->tv_nsec = static_cast<long>(Leftover * FACTOR_NANOSECONDS_PER_100NS); // Direct multiplication is fine here
}
#endif // HAVE_NANOSLEEP

void CLinearTimeDelta::SetTimeValueStruct(const struct timeval* tv) // Added const*
{
    m_tDelta = FACTOR_100NS_PER_SECOND * tv->tv_sec
        + FACTOR_100NS_PER_MICROSECOND * tv->tv_usec;
}

void CLinearTimeDelta::SetMilliseconds(long arg_dwMilliseconds)
{
    // Consider range if long is 32-bit and arg_dwMilliseconds is large
    m_tDelta = static_cast<UnderlyingTickType>(arg_dwMilliseconds) * FACTOR_100NS_PER_MILLISECOND;
}

long CLinearTimeDelta::ReturnMilliseconds() const // Added const
{
    // Potential truncation if result > LONG_MAX
    return static_cast<long>(m_tDelta / FACTOR_100NS_PER_MILLISECOND);
}

UnderlyingTickType CLinearTimeDelta::ReturnMicroseconds() const // Added const
{
    return m_tDelta / FACTOR_100NS_PER_MICROSECOND;
}

void CLinearTimeDelta::SetSecondsString(const UTF8* arg_szSeconds) // Added const*
{
    ParseFractionalSecondsString(m_tDelta, arg_szSeconds);
}

void CLinearTimeDelta::SetSeconds(UnderlyingTickType arg_tSeconds)
{
    m_tDelta = arg_tSeconds * FACTOR_100NS_PER_SECOND;
}

void CLinearTimeDelta::Set100ns(UnderlyingTickType arg_t100ns) noexcept
{
    m_tDelta = arg_t100ns;
}

UnderlyingTickType CLinearTimeDelta::Return100ns() const noexcept
{
    return m_tDelta;
}

CLinearTimeDelta::CLinearTimeDelta(CLinearTimeAbsolute t0, CLinearTimeAbsolute t1)
{
    // Assuming CLinearTimeAbsolute has a way to get its UnderlyingTickType value
    // For example: m_tDelta = t1.GetTicks() - t0.GetTicks();
    // Using placeholder access via m_tAbsolute as in original code:
    m_tDelta = t1.m_tAbsolute - t0.m_tAbsolute;
}

long CLinearTimeDelta::ReturnDays() const // Added const
{
    return static_cast<long>(m_tDelta / FACTOR_100NS_PER_DAY);
}

long CLinearTimeDelta::ReturnSeconds() const // Added const
{
    return static_cast<long>(m_tDelta / FACTOR_100NS_PER_SECOND);
}

CLinearTimeDelta& CLinearTimeDelta::operator+=(const CLinearTimeDelta& ltd) noexcept
{
    m_tDelta += ltd.m_tDelta;
    return *this;
}

CLinearTimeDelta& CLinearTimeDelta::operator-=(const CLinearTimeDelta& ltd) noexcept
{
    m_tDelta -= ltd.m_tDelta;
    return *this;
}

// Less than operator
bool operator<(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept
{
    return lta.m_tDelta < ltb.m_tDelta;
}

// Greater than operator
bool operator>(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept
{
    return lta.m_tDelta > ltb.m_tDelta;
}

// Less than or equal operator
bool operator<=(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept
{
    return lta.m_tDelta <= ltb.m_tDelta;
}

CLinearTimeDelta operator-(const CLinearTimeAbsolute& ltaA, const CLinearTimeAbsolute& ltaB)
{
    // Assumes CLinearTimeAbsolute::m_tAbsolute is accessible and is UnderlyingTickType
    CLinearTimeDelta ltd;
    ltd.Set100ns(ltaA.m_tAbsolute - ltaB.m_tAbsolute); // Use setter for encapsulation if m_tDelta becomes private
    return ltd;
}

CLinearTimeDelta operator-(const CLinearTimeDelta& lta, const CLinearTimeDelta& ltb) noexcept
{
    CLinearTimeDelta ltd;
    ltd.Set100ns(lta.Return100ns() - ltb.Return100ns()); // Use accessors
    return ltd;
}

CLinearTimeDelta operator*(const CLinearTimeDelta& ltd, int Scale)
{
    // Add overflow check? Needs <limits>
    CLinearTimeDelta ltdResult;
    ltdResult.Set100ns(ltd.Return100ns() * Scale); // Use accessors
    return ltdResult;
}

UnderlyingTickType operator/(const CLinearTimeDelta& ltdA, const CLinearTimeDelta& ltdB)
{
    // CRITICAL: Check for division by zero
    if (ltdB.Return100ns() == 0) {
        // Handle error: Throw exception, return specific value (0? MAX?), assert?
        // For now, let's assert (in debug) and return 0 (matches potential old behavior)
        mux_assert(ltdB.Return100ns() != 0 && "Division by zero CLinearTimeDelta");
        return 0; // Or std::numeric_limits<UnderlyingTickType>::max() ?
    }
    // The result is unitless ratio, UnderlyingTickType should be sufficient range
    return ltdA.Return100ns() / ltdB.Return100ns();
}
