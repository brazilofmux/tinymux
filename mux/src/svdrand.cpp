// svdrand.cpp -- Random Numbers.
//
// $Id: svdrand.cpp,v 1.5 2004-04-13 06:34:22 sdennis Exp $
//
// MUX 2.4
// Copyright (C) 1998 through 2004 Solid Vertical Domains, Ltd. All
// rights not explicitly given are reserved.  
//
// Random Numbers from Makoto Matsumoto and Takuji Nishimura.
//
#include "copyright.h"
#include "autoconf.h"
#include "config.h"

#include "timeutil.h"

#include "svdhash.h"
#include "svdrand.h"

#ifdef WIN32
#include <wincrypt.h>
typedef BOOL WINAPI FCRYPTACQUIRECONTEXT(HCRYPTPROV *, LPCTSTR, LPCTSTR, DWORD, DWORD);
typedef BOOL WINAPI FCRYPTRELEASECONTEXT(HCRYPTPROV, DWORD);
typedef BOOL WINAPI FCRYPTGENRANDOM(HCRYPTPROV, DWORD, BYTE *);
#else // WIN32
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#endif // WIN32

// Seed the generator.
//
static void sgenrand(UINT32);
static void sgenrand_from_array(UINT32 *, int);

// Returns a random 32-bit integer.
//
static UINT32 genrand(void);

#define NUM_RANDOM_UINT32 1024

bool bCryptoAPI = false;
static bool bSeeded = false;
void SeedRandomNumberGenerator(void)
{
    if (bSeeded) return;
    bSeeded = true;

    UINT32 aRandomSystemBytes[NUM_RANDOM_UINT32];
    unsigned int nRandomSystemBytes = 0;

#ifdef HAVE_DEV_URANDOM
    // Try to seed the PRNG from /dev/urandom 
    // If it doesn't work, just seed the normal way 
    //
    int fd = open("/dev/urandom", O_RDONLY);

    if (fd >= 0)
    {
        int len = read(fd, aRandomSystemBytes, sizeof aRandomSystemBytes);
        close(fd);
        if (len > 0)
        {
            nRandomSystemBytes = len/sizeof(UINT32);
        }
    }
#endif // HAVE_DEV_URANDOM
#ifdef WIN32
    // The Cryto API became available on Windows with Win95 OSR2. Using Crypto
    // API as follows lets us to fallback gracefully when running on pre-OSR2
    // Win95.
    //
    bCryptoAPI = false;
    HINSTANCE hAdvAPI32 = LoadLibrary("advapi32");
    if (hAdvAPI32)
    {
        FCRYPTACQUIRECONTEXT *fpCryptAcquireContext;
        FCRYPTRELEASECONTEXT *fpCryptReleaseContext;
        FCRYPTGENRANDOM *fpCryptGenRandom;

        // Find the entry points for CryptoAcquireContext, CrytpoGenRandom,
        // and CryptoReleaseContext.
        //
        fpCryptAcquireContext = (FCRYPTACQUIRECONTEXT *)
            GetProcAddress(hAdvAPI32, "CryptAcquireContextA");
        fpCryptReleaseContext = (FCRYPTRELEASECONTEXT *)
            GetProcAddress(hAdvAPI32, "CryptReleaseContext");
        fpCryptGenRandom = (FCRYPTGENRANDOM *)
            GetProcAddress(hAdvAPI32, "CryptGenRandom");

        if (  fpCryptAcquireContext
           && fpCryptReleaseContext
           && fpCryptGenRandom)
        {
            HCRYPTPROV hProv;

            if (  fpCryptAcquireContext(&hProv, NULL, NULL, PROV_DSS, 0)
               || fpCryptAcquireContext(&hProv, NULL, NULL, PROV_DSS, CRYPT_NEWKEYSET))
            {
                if (fpCryptGenRandom(hProv, sizeof aRandomSystemBytes,
                    (BYTE *)aRandomSystemBytes))
                {
                    nRandomSystemBytes = NUM_RANDOM_UINT32;
                    bCryptoAPI = true;
                }
                fpCryptReleaseContext(hProv, 0);
            }
        }
        FreeLibrary(hAdvAPI32);
    }
#endif // WIN32

    if (nRandomSystemBytes >= sizeof(UINT32))
    {
        unsigned int i;
        for (i = 0; i < nRandomSystemBytes; i++)
        {
            aRandomSystemBytes[i] &= 0xFFFFFFFFUL;
        }
        sgenrand_from_array(aRandomSystemBytes, nRandomSystemBytes);
        return;
    }

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

/* A C-program for MT19937, with initialization improved 2002/2/10.*/
/* Coded by Takuji Nishimura and Makoto Matsumoto.                 */
/* This is a faster version by taking Shawn Cokus's optimization,  */
/* Matthe Bellew's simplification, Isaku Wada's real version.      */

/* Before using, initialize the state by using init_genrand(seed)  */
/* or init_by_array(init_key, key_length).                         */

/* This library is free software.                                  */
/* This library is distributed in the hope that it will be useful, */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of  */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.            */

/* Copyright (C) 1997, 2002 Makoto Matsumoto and Takuji Nishimura. */
/* Any feedback is very welcome.                                   */
/* http://www.math.keio.ac.jp/matumoto/emt.html                    */
/* email: matumoto@math.keio.ac.jp                                 */

#define N 624
#define M 397
#define MATRIX_A 0x9908b0dfUL
#define UMASK 0x80000000UL // most significant w-r bits
#define LMASK 0x7fffffffUL // least significant r bits
#define MIXBITS(u,v) ( ((u) & UMASK) | ((v) & LMASK) )
#define TWIST(u,v) ((MIXBITS(u,v) >> 1) ^ ((v)&1UL ? MATRIX_A : 0UL))

static UINT32 mt[N];
static int left = 1;
static UINT32 *next;

// initializes mt[N] with a seed.
//
static void sgenrand(UINT32 nSeed)
{
    int j;
    mt[0] = nSeed & 0xffffffffUL;
    for (j = 1; j < N; j++)
    {
        // See Knuth TAOCP Vol2. 3rd Ed. P.106 for multiplier.
        // In the previous versions, MSBs of the seed affect
        // only MSBs of the array mt[].
        // 2002/01/09 modified by Makoto Matsumoto.
        //
        mt[j] = 1812433253UL * (mt[j-1] ^ (mt[j-1] >> 30)) + j;
        mt[j] &= 0xffffffffUL;  // for >32 bit machines.
    }
    left = 1;
}

// initialize by an array with array-length
// init_key is the array for initializing keys
// key_length is its length
//
static void sgenrand_from_array(UINT32 *init_key, int key_length)
{
    sgenrand(19650218UL);
    int i = 1;
    int j = 0;
    int k = (N > key_length ? N : key_length);
    for (; k; k--)
    {
        mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 30)) * 1664525UL))
              + init_key[j] + j;
        mt[i] &= 0xffffffffUL; // for > 32-bit machines.
        i++;
        j++;
        if (i >= N)
        {
            mt[0] = mt[N-1];
            i = 1;
        }
        if (j >= key_length)
        {
            j = 0;
        }
    }
    for (k = N-1; k; k--)
    {
        mt[i] = (mt[i] ^ ((mt[i-1] ^ (mt[i-1] >> 30)) * 1566083941UL)) - i;
        mt[i] &= 0xffffffffUL; // for > 32-bit machines.
        i++;
        if (i >= N)
        {
            mt[0] = mt[N-1];
            i = 1;
        }
    }

    mt[0] = 0x80000000UL; /* MSB is 1; assuring non-zero initial array */
    left = 1;
}

static void next_state(void)
{
    UINT32 *p = mt;
    int j;

    if (!bSeeded)
    {
        SeedRandomNumberGenerator();
    }

    for (j = N-M+1; --j; p++)
    {
        *p = p[M] ^ TWIST(p[0], p[1]);
    }

    for (j = M; --j; p++)
    {
        *p = p[M-N] ^ TWIST(p[0], p[1]);
    }

    *p = p[M-N] ^ TWIST(p[0], mt[0]);

    left = N;
    next = mt;
}

// generates a random number on the interval [0,0xffffffff]
//
static UINT32 genrand(void)
{
    UINT32 y;

    if (--left == 0)
    {
        next_state();
    }
    y = *next++;

    // Tempering.
    //
    y ^= (y >> 11);
    y ^= (y << 7) & 0x9d2c5680UL;
    y ^= (y << 15) & 0xefc60000UL;
    y ^= (y >> 18);

    return y;
}
