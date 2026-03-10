/*! \file svdrand.cpp
 * \brief Random Numbers — PCG family.
 *
 * Unix:    PCG-XSL-RR-128/64 (pcg64).  128-bit state, 64-bit output.
 * Windows: PCG-XSH-RR-64/32 (pcg32).   64-bit state, 32-bit output.
 *
 * Algorithm from: M.E. O'Neill, "PCG: A Family of Simple Fast
 * Space-Efficient Statistically Good Algorithms for Random Number
 * Generation", Harvey Mudd College, 2014.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "core.h"

static bool g_bSeeded = false;

#if defined(_MSC_VER)

// -----------------------------------------------------------------------
// PCG-XSH-RR-64/32 for Windows (no __int128 needed).
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
    uint32_t xorshifted = (uint32_t)(((old >> 18u) ^ old) >> 27u);
    unsigned rot = (unsigned)(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
}

void SeedRandomNumberGenerator()
{
    if (g_bSeeded)
    {
        return;
    }
    g_bSeeded = true;

    // Gather entropy from multiple sources on Windows.
    //
    unsigned int s[4];
    LARGE_INTEGER qpc;
    QueryPerformanceCounter(&qpc);
    s[0] = (unsigned int)(uintptr_t)&g_state;
    s[1] = (unsigned int)time(nullptr);
    s[2] = (unsigned int)qpc.LowPart;
    s[3] = (unsigned int)qpc.HighPart ^ GetCurrentProcessId();

    g_inc = ((uint64_t)s[2] << 32 | s[3]) | 1u;
    g_state = 0;
    pcg32_random();
    g_state += (uint64_t)s[0] << 32 | s[1];
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
    uint64_t range = (uint64_t)(lHigh - lLow) + 1u;
    uint64_t limit = -range % range;
    for (;;)
    {
        uint64_t r = ((uint64_t)pcg32_random() << 32) | pcg32_random();
        if (r >= limit)
        {
            return lLow + (int32_t)(r % range);
        }
    }
}

#else // GCC/Clang — PCG-XSL-RR-128/64

typedef unsigned __int128 uint128_t;

static const uint128_t PCG_MULT =
    (uint128_t)2549297995355413924ULL << 64
    | 4865540595714422341ULL;

static uint128_t g_state;
static uint128_t g_inc;

static uint64_t pcg64_random()
{
    uint128_t old = g_state;
    g_state = old * PCG_MULT + g_inc;

    // XSL-RR: xor high and low halves, then rotate.
    //
    uint64_t xsl = (uint64_t)(old >> 64u) ^ (uint64_t)old;
    unsigned rot = (unsigned)(old >> 122u);
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
    FILE *fp = fopen("/dev/urandom", "rb");
    if (  nullptr != fp
       && sizeof(s) == fread(s, 1, sizeof(s), fp))
    {
        fclose(fp);
    }
    else
    {
        // Fallback: use address space layout and time.
        //
        if (nullptr != fp)
        {
            fclose(fp);
        }
        s[0] = (uint64_t)(uintptr_t)&g_state;
        s[1] = (uint64_t)time(nullptr);
        s[2] = (uint64_t)(uintptr_t)&s;
        s[3] = s[1] ^ 0xDEADBEEFCAFEBABEULL;
    }

    g_inc = ((uint128_t)s[2] << 64 | s[3]) | 1u;
    g_state = 0;
    pcg64_random();
    g_state += (uint128_t)s[0] << 64 | s[1];
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
    uint64_t range = (uint64_t)(lHigh - lLow) + 1u;
    uint64_t limit = -range % range;  // = (2^64 - range) % range
    for (;;)
    {
        uint64_t r = pcg64_random();
        if (r >= limit)
        {
            return lLow + (int32_t)(r % range);
        }
    }
}

#endif
