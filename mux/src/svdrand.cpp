/*! \file svdrand.cpp
 * \brief Random Numbers — PCG family.
 *
 * With __uint128_t:  PCG-XSL-RR-128/64 (pcg64).  128-bit state, 64-bit output.
 * Without:           PCG-XSH-RR-64/32  (pcg32).   64-bit state, 32-bit output.
 *
 * The selection is based on whether the compiler provides native 128-bit
 * integers (__SIZEOF_INT128__), not on the OS or compiler identity.
 * GCC/Clang on 64-bit targets define it; MSVC and 32-bit targets do not.
 *
 * Algorithm from: M.E. O'Neill, "PCG: A Family of Simple Fast
 * Space-Efficient Statistically Good Algorithms for Random Number
 * Generation", Harvey Mudd College, 2014.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

static bool g_bSeeded = false;

// Platform-specific entropy gathering.
//
static void gather_entropy(void *buf, size_t len)
{
#if defined(WIN32)
    // Windows: use QueryPerformanceCounter + time + PID.
    //
    uint8_t *p = static_cast<uint8_t *>(buf);
    memset(p, 0, len);

    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    uint64_t seeds[2];
    seeds[0] = static_cast<uint64_t>(qpc.QuadPart) ^ (static_cast<uint64_t>(GetCurrentProcessId()) << 32);
    seeds[1] = static_cast<uint64_t>(time(nullptr)) ^ reinterpret_cast<uint64_t>(&g_bSeeded);

    size_t copy = (len < sizeof(seeds)) ? len : sizeof(seeds);
    memcpy(p, seeds, copy);
#else
    // Unix: read from /dev/urandom; fall back to address + time.
    //
    FILE *fp = fopen("/dev/urandom", "rb");
    if (  nullptr != fp
       && len == fread(buf, 1, len, fp))
    {
        fclose(fp);
        return;
    }
    if (nullptr != fp)
    {
        fclose(fp);
    }

    // Fallback: use address space layout and time.
    //
    uint64_t *s = static_cast<uint64_t *>(buf);
    size_t n = len / sizeof(uint64_t);
    if (n >= 1) s[0] = reinterpret_cast<uint64_t>(&g_bSeeded);
    if (n >= 2) s[1] = static_cast<uint64_t>(time(nullptr));
    if (n >= 3) s[2] = reinterpret_cast<uint64_t>(&s);
    if (n >= 4) s[3] = s[1] ^ 0xDEADBEEFCAFEBABEULL;
#endif
}

#if !defined(__SIZEOF_INT128__)

// -----------------------------------------------------------------------
// PCG-XSH-RR-64/32 — no native 128-bit integers available.
// -----------------------------------------------------------------------

static const uint64_t PCG32_MULT = 6364136223846793005ULL;

static uint64_t g_state;
static uint64_t g_inc;

static uint32_t pcg32_random()
{
    uint64_t old = g_state;
    g_state = old * PCG32_MULT + g_inc;

    // XSH-RR output function.
    //
    uint32_t xorshifted = static_cast<uint32_t>(((old >> 18u) ^ old) >> 27u);
    unsigned rot = static_cast<unsigned>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
}

void SeedRandomNumberGenerator()
{
    if (g_bSeeded)
    {
        return;
    }
    g_bSeeded = true;

    uint64_t s[2];
    gather_entropy(s, sizeof(s));

    g_inc = s[1] | 1u;
    g_state = 0;
    pcg32_random();
    g_state += s[0];
    pcg32_random();
}

int32_t RandomINT32(int32_t lLow, int32_t lHigh)
{
    if (lHigh < lLow)
    {
        return -1;
    }
    else if (lHigh == lLow)
    {
        return lLow;
    }

    // Combine two 32-bit outputs for unbiased 64-bit rejection sampling.
    //
    uint64_t range = static_cast<uint64_t>(lHigh - lLow) + 1u;
    uint64_t limit = -range % range;
    for (;;)
    {
        uint64_t r = (static_cast<uint64_t>(pcg32_random()) << 32) | pcg32_random();
        if (r >= limit)
        {
            return lLow + static_cast<int32_t>(r % range);
        }
    }
}

#else // __SIZEOF_INT128__ — PCG-XSL-RR-128/64

typedef unsigned __int128 uint128_t;

static const uint128_t PCG_MULT =
    static_cast<uint128_t>(2549297995355413924ULL) << 64
    | 4865540595714422341ULL;

static uint128_t g_state;
static uint128_t g_inc;

static uint64_t pcg64_random()
{
    uint128_t old = g_state;
    g_state = old * PCG_MULT + g_inc;

    // XSL-RR: xor high and low halves, then rotate.
    //
    uint64_t xsl = static_cast<uint64_t>(old >> 64u) ^ static_cast<uint64_t>(old);
    unsigned rot = static_cast<unsigned>(old >> 122u);
    return (xsl >> rot) | (xsl << ((-rot) & 63u));
}

void SeedRandomNumberGenerator()
{
    if (g_bSeeded)
    {
        return;
    }
    g_bSeeded = true;

    // Draw entropy from the OS to seed both state and stream.
    // The stream (increment) must be odd; we force the low bit.
    //
    uint64_t s[4];
    gather_entropy(s, sizeof(s));

    g_inc = (static_cast<uint128_t>(s[2]) << 64 | s[3]) | 1u;
    g_state = 0;
    pcg64_random();
    g_state += static_cast<uint128_t>(s[0]) << 64 | s[1];
    pcg64_random();
}

int32_t RandomINT32(int32_t lLow, int32_t lHigh)
{
    if (lHigh < lLow)
    {
        return -1;
    }
    else if (lHigh == lLow)
    {
        return lLow;
    }

    // Unbiased bounded random via rejection sampling on the
    // upper bits of a 64-bit value.  The rejection rate is
    // less than 50% in the worst case.
    //
    uint64_t range = static_cast<uint64_t>(lHigh - lLow) + 1u;
    uint64_t limit = -range % range;  // = (2^64 - range) % range
    for (;;)
    {
        uint64_t r = pcg64_random();
        if (r >= limit)
        {
            return lLow + static_cast<int32_t>(r % range);
        }
    }
}

#endif // __SIZEOF_INT128__
