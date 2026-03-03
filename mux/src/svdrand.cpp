/*! \file svdrand.cpp
 * \brief Random Numbers.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <random>

static std::mt19937 g_rng;
static bool g_bSeeded = false;

void SeedRandomNumberGenerator()
{
    if (g_bSeeded)
    {
        return;
    }
    g_bSeeded = true;

    std::random_device rd;
    std::seed_seq ss{rd(), rd(), rd(), rd(), rd(), rd(), rd(), rd()};
    g_rng.seed(ss);
}

INT32 RandomINT32(INT32 lLow, INT32 lHigh)
{
    if (lHigh < lLow)
    {
        return -1;
    }
    else if (lHigh == lLow)
    {
        return lLow;
    }

    std::uniform_int_distribution<INT32> dist(lLow, lHigh);
    return dist(g_rng);
}
