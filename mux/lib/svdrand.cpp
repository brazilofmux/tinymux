/*! \file svdrand.cpp
 * \brief Random Numbers — PCG-XSL-RR-128/64 (pcg64).
 *
 * Replaces Mersenne Twister (std::mt19937).
 * State: 32 bytes (128-bit state + 128-bit stream).
 * Output: 64 bits per step, period 2^128.
 *
 * Algorithm from: M.E. O'Neill, "PCG: A Family of Simple Fast
 * Space-Efficient Statistically Good Algorithms for Random Number
 * Generation", Harvey Mudd College, 2014.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "core.h"

typedef unsigned __int128 uint128_t;

// PCG default multiplier and increment for 128-bit LCG.
//
static const uint128_t PCG_MULT =
    (uint128_t)2549297995355413924ULL << 64
    | 4865540595714422341ULL;

static const uint128_t PCG_DEFAULT_INC =
    (uint128_t)6364136223846793005ULL << 64
    | 1442695040888963407ULL;

static uint128_t g_state;
static uint128_t g_inc;
static bool g_bSeeded = false;

// PCG-XSL-RR-128/64 output function.
//
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
