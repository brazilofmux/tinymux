// svdrand.cpp -- Random Numbers.
//
// $Id: svdrand.cpp,v 1.19 2002-01-22 20:48:05 sdennis Exp $
//
// Random Numbers from Makoto Matsumoto and Takuji Nishimura.
//
// RandomINT32() was derived from existing game server code.
//
// MUX 2.1
// Copyright (C) 1998 through 2001 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved. Permission is given to
// use this code for building and hosting text-based game servers.
// Permission is given to use this code for other non-commercial
// purposes. To use this code for commercial purposes other than
// building/hosting text-based game servers, contact the author at
// Stephen Dennis <sdennis@svdltd.com> for another license.
//
#include "autoconf.h"
#include "config.h"
#include "timeutil.h"
#include "svdrand.h"
#include "svdhash.h"

#ifdef WIN32
typedef BOOL WINAPI FCRYPTACQUIRECONTEXT(HCRYPTPROV *, LPCTSTR, LPCTSTR, DWORD, DWORD);
typedef BOOL WINAPI FCRYPTRELEASECONTEXT(HCRYPTPROV, DWORD);
typedef BOOL WINAPI FCRYPTGENRANDOM(HCRYPTPROV, DWORD, BYTE *);
#else
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#endif

void sgenrand(UINT32);    // seed the generator
UINT32 genrand(void);     // returns a random 32-bit integer */

#define N 624
static UINT32 mt[N];

static BOOL bSeeded = FALSE;
void SeedRandomNumberGenerator(void)
{
    if (bSeeded) return;
    bSeeded = TRUE;

#ifdef HAVE_DEV_URANDOM
    // Try to seed the PRNG from /dev/urandom 
    // If it doesn't work, just seed the normal way 
    //
    int fd = open("/dev/urandom", O_RDONLY);

    if (fd >= 0)
    {
        int len = read(fd, mt, sizeof mt);
        close(fd);
        if (len == sizeof mt)
        {
            return;
        }
    }
#endif
#ifdef WIN32
    // The Cryto API became available on Windows with Win95 OSR2. Using Crypto
    // API as follows lets us to fallback gracefully when running on pre-OSR2
    // Win95.
    //
    HINSTANCE hAdvAPI32 = LoadLibrary("advapi32");
    if (!hAdvAPI32)
    {
        Log.WriteString("Crypto API unavailable.\r\n");
    }
    else
    {
        FCRYPTACQUIRECONTEXT *fpCryptAcquireContext;
        FCRYPTRELEASECONTEXT *fpCryptReleaseContext;
        FCRYPTGENRANDOM *fpCryptGenRandom;

        // Find the entry points for CryptoAcquireContext, CrytpoGenRandom,
        // and CryptoReleaseContext.
        //
        fpCryptAcquireContext = (FCRYPTACQUIRECONTEXT *)
            GetProcAddress(hAdvAPI32, "CryptAcquireContext");
        fpCryptReleaseContext = (FCRYPTRELEASECONTEXT *)
            GetProcAddress(hAdvAPI32, "CryptReleaseContext");
        fpCryptGenRandom = (FCRYPTGENRANDOM *)
            GetProcAddress(hAdvAPI32, "CryptGenRandom");

        if (  fpCryptAcquireContext
           && fpCryptReleaseContext
           && fpCryptGenRandom)
        {
            HCRYPTPROV hProv;

            if (fpCryptAcquireContext(&hProv, NULL, NULL, PROV_DSS, 0))
            {
                if (fpCryptGenRandom(hProv, sizeof mt, (BYTE *)mt))
                {
                    fpCryptReleaseContext(hProv, 0);
                    FreeLibrary(hAdvAPI32);
                    return;
                }
                fpCryptReleaseContext(hProv, 0);
            }
        }
        FreeLibrary(hAdvAPI32);
    }
#endif

    // Determine the initial seed.
    //
    CLinearTimeAbsolute lsaNow;
    lsaNow.GetUTC();
    INT64 i64Seed = lsaNow.Return100ns();
    int pid = getpid();

    UINT32 nSeed = CRC32_ProcessBuffer(0, &i64Seed, sizeof(INT64));
    nSeed = CRC32_ProcessBuffer(nSeed, &pid, sizeof(pid));

    if (nSeed <= 1000)
    {
        nSeed += 22261048;
    }

    // ASSERT: 1000 < nSeed

    sgenrand(nSeed);
}

// RandomINT32 -- return a long on the interval [lLow, lHigh]
//
INT32 RandomINT32(INT32 lLow, INT32 lHigh)
{
    // Validate parameters
    //
    if (lHigh < lLow)
    {
        return -1;
    }
    else if (lHigh == lLow)
    {
        return lLow;
    }

    UINT32 x = lHigh-lLow;
    if (INT32_MAX_VALUE < x)
    {
        return -1;
    }
    x++;

    // We can now look for an random number on the interval [0,x-1].
    //

    // In order to be perfectly conservative about not introducing any
    // further sources of statistical bias, we're going to call getrand()
    // until we get a number less than the greatest representable
    // multiple of x. We'll then return n mod x.
    //
    // N.B. This loop happens in randomized constant time, and pretty
    // damn fast randomized constant time too, since
    //
    //      P(UINT32_MAX_VALUE - n < UINT32_MAX_VALUE % x) < 0.5, for any x.
    //
    // So even for the least desireable x, the average number of times
    // we will call getrand() is less than 2.
    //
    UINT32 n;
    UINT32 nLimit = UINT32_MAX_VALUE - (UINT32_MAX_VALUE%x);

    do
    {
        n = genrand();
    } while (n >= nLimit);

    return lLow + (n % x);
}

/* Coded by Takuji Nishimura, considering the suggestions by      */
/* Topher Cooper and Marc Rieffel in July-Aug. 1997.              */

/* This library is free software; you can redistribute it and/or   */
/* modify it under the terms of the GNU Library General Public     */
/* License as published by the Free Software Foundation; either    */
/* version 2 of the License, or (at your option) any later         */
/* version.                                                        */
/* This library is distributed in the hope that it will be useful, */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of  */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.            */
/* See the GNU Library General Public License for more details.    */
/* You should have received a copy of the GNU Library General      */
/* Public License along with this library; if not, write to the    */
/* Free Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA   */
/* 02111-1307  USA                                                 */

/* Copyright (C) 1997, 1999 Makoto Matsumoto and Takuji Nishimura. */
/* When you use this, send an email to: matumoto@math.keio.ac.jp   */
/* with an appropriate reference to your work.                     */

#define M 397
#define MATRIX_A 0x9908b0df
#define UPPER_MASK 0x80000000
#define LOWER_MASK 0x7fffffff
#define TEMPERING_MASK_B 0x9d2c5680
#define TEMPERING_MASK_C 0xefc60000
#define TEMPERING_SHIFT_U(y)  (y >> 11)
#define TEMPERING_SHIFT_S(y)  (y << 7)
#define TEMPERING_SHIFT_T(y)  (y << 15)
#define TEMPERING_SHIFT_L(y)  (y >> 18)

static int mti = N + 1;

void sgenrand(UINT32 nSeed)
{
    nSeed |= 1; // Force the seed to be odd.
    for (int i = 0; i < N; i++)
    {
        mt[i] = nSeed & 0xffff0000;
        nSeed = 69069 * nSeed + 1;
        mt[i] |= (nSeed & 0xffff0000) >> 16;
        nSeed = 69069 * nSeed + 1;
    }
    mti = N;
}

UINT32 genrand(void)
{
    UINT32 y;
    static UINT32 mag01[2] = {0x0, MATRIX_A};
    int kk;

    if (mti >= N)
    {
        if (!bSeeded)
        {
            SeedRandomNumberGenerator();
        }

        for (kk=0; kk < N-M; kk++)
        {
            y = (mt[kk] & UPPER_MASK) | (mt[kk+1] & LOWER_MASK);
            mt[kk] = mt[kk+M] ^ (y >> 1) ^ mag01[ y & 0x1 ];
        }
        for (; kk < N-1; kk++)
        {
            y = (mt[kk] & UPPER_MASK) | (mt[kk+1] & LOWER_MASK);
            mt[kk] = mt[ kk+(M-N) ] ^ (y >> 1) ^ mag01[ y & 0x1 ];
        }
        y = (mt[N-1] & UPPER_MASK) | (mt[0] & LOWER_MASK);
        mt[N-1] = mt[M-1] ^ (y >> 1) ^ mag01[ y & 0x1 ];

        mti = 0;
    }

    y = mt[mti++];
    y ^= TEMPERING_SHIFT_U(y);
    y ^= TEMPERING_SHIFT_S(y) & TEMPERING_MASK_B;
    y ^= TEMPERING_SHIFT_T(y) & TEMPERING_MASK_C;
    y ^= TEMPERING_SHIFT_L(y);
    return y;
}


