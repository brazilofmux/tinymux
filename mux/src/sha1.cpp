/*! \file sha1.cpp
 * \brief Implementation of SHA1 hash.
 *
 * $Id$
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "sha1.h"

void SHA1_Init(SHA1_CONTEXT *p)
{
    p->H[0] = 0x67452301;
    p->H[1] = 0xEFCDAB89;
    p->H[2] = 0x98BADCFE;
    p->H[3] = 0x10325476;
    p->H[4] = 0xC3D2E1F0;
    p->nTotal = 0;
    p->nblock = 0;
}

#ifdef WIN32
#define ROTL(d,n) _lrotl(d,n)
#else // WIN32
#define ROTL(d,n) (((d) << (n)) | ((d) >> (32-(n))))
#endif // WIN32

#define Ch(x,y,z)      (((x) & (y)) ^ (~(x) & (z)))
#define Parity(x,y,z)  ((x) ^ (y) ^ (z))
#define Maj(x,y,z)     (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

static void SHA1_HashBlock(SHA1_CONTEXT *p)
{
    int t;
    UINT32 W[80];

    // Prepare Message Schedule, {W sub t}.
    //
    int j;
    for (t = 0, j = 0; t <= 15; t++, j += 4)
    {
        W[t] = (p->block[j  ] << 24)
             | (p->block[j+1] << 16)
             | (p->block[j+2] <<  8)
             | (p->block[j+3]      );
    }
    for (t = 16; t <= 79; t++)
    {
        W[t] = ROTL(W[t-3] ^ W[t-8] ^ W[t-14] ^ W[t-16], 1);
    }

    UINT32 a = p->H[0];
    UINT32 b = p->H[1];
    UINT32 c = p->H[2];
    UINT32 d = p->H[3];
    UINT32 e = p->H[4];

    UINT32 T;
    for (t =  0; t <= 19; t++)
    {
        T = ROTL(a,5) + Ch(b,c,d) + e + 0x5A827999 + W[t];
        e = d;
        d = c;
        c = ROTL(b,30);
        b = a;
        a = T;
    }
    for (t = 20; t <= 39; t++)
    {
        T = ROTL(a,5) + Parity(b,c,d) + e + 0x6ED9EBA1 + W[t];
        e = d;
        d = c;
        c = ROTL(b,30);
        b = a;
        a = T;
    }
    for (t = 40; t <= 59; t++)
    {
        T = ROTL(a,5) + Maj(b,c,d) + e + 0x8F1BBCDC + W[t];
        e = d;
        d = c;
        c = ROTL(b,30);
        b = a;
        a = T;
    }
    for (t = 60; t <= 79; t++)
    {
        T = ROTL(a,5) + Parity(b,c,d) + e + 0xCA62C1D6 + W[t];
        e = d;
        d = c;
        c = ROTL(b,30);
        b = a;
        a = T;
    }

    p->H[0] += a;
    p->H[1] += b;
    p->H[2] += c;
    p->H[3] += d;
    p->H[4] += e;
}

void SHA1_Compute(SHA1_CONTEXT *p, size_t n, const UTF8 *buf)
{
    while (n)
    {
        size_t m = sizeof(p->block) - p->nblock;
        if (n < m)
        {
            m = n;
        }
        memcpy(p->block + p->nblock, buf, m);
        buf += m;
        n -= m;
        p->nblock += m;
        p->nTotal += m;

        if (p->nblock == sizeof(p->block))
        {
            SHA1_HashBlock(p);
            p->nblock = 0;
        }
    }
}

void SHA1_Final(SHA1_CONTEXT *p)
{
    p->block[p->nblock++] = 0x80;
    if (sizeof(p->block) - sizeof(UINT64) < p->nblock)
    {
        memset(p->block + p->nblock, 0, sizeof(p->block) - p->nblock);
        SHA1_HashBlock(p);
        memset(p->block, 0, sizeof(p->block) - sizeof(UINT64));
    }
    else
    {
        memset(p->block + p->nblock, 0, sizeof(p->block) - p->nblock - sizeof(UINT64));
    }
    p->nTotal *= 8;

    p->block[sizeof(p->block) - 8] = static_cast<UINT8>((p->nTotal >> 56) & 0xFF);
    p->block[sizeof(p->block) - 7] = static_cast<UINT8>((p->nTotal >> 48) & 0xFF);
    p->block[sizeof(p->block) - 6] = static_cast<UINT8>((p->nTotal >> 40) & 0xFF);
    p->block[sizeof(p->block) - 5] = static_cast<UINT8>((p->nTotal >> 32) & 0xFF);
    p->block[sizeof(p->block) - 4] = static_cast<UINT8>((p->nTotal >> 24) & 0xFF);
    p->block[sizeof(p->block) - 3] = static_cast<UINT8>((p->nTotal >> 16) & 0xFF);
    p->block[sizeof(p->block) - 2] = static_cast<UINT8>((p->nTotal >>  8) & 0xFF);
    p->block[sizeof(p->block) - 1] = static_cast<UINT8>((p->nTotal      ) & 0xFF);
    SHA1_HashBlock(p);
}

#if 0

typedef struct
{
    const UTF8 *p;
    UINT32   H[5];
} sha1_test_vector;

#define NUM_VECTORS 5
sha1_test_vector vectors[NUM_VECTORS] =
{
    {
        T("abc"),
        { 0xA9993E36, 0x4706816A, 0xBA3E2571, 0x7850C26C, 0x9CD0D89D }
    },
    {
        // 64-byte block composed of 55 bytes, 1-byte pad (0x80), and 8-byte
        // length.
        //
        T("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnop"),
        { 0x47B17281, 0x0795699F, 0xE739197D, 0x1A1F5960, 0x700242F1 }
    },
    {
        // First 64-byte block composed of 56 bytes, 1-byte pad (0x80), and 7
        // zeros.  Second 64-byte block composed of 56 zeros (0x00) and 8-byte
        // length.
        //
        T("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        { 0x84983E44, 0x1C3BD26E, 0xBAAE4AA1, 0xF95129E5, 0xE54670F1 }
    },
    {
        // First 64-byte block composed of 63 bytes and 1-byte pad (0x80).
        // Second 64-byte block composed of 56 zeros (0x00), and 8-byte
        // length.
        //
        T("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq1234567"),
        { 0x55A0C42A, 0x00BD4B49, 0x6A16D1AC, 0x32E20B5A, 0x7FA3E087 }
    },
    {
        // First 64-byte block composed of 64 bytes.  Second 64-byte block
        // composed of 1-byte pad (0x80), 55 zeros (0x00), and 8-byte length.
        //
        T("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq12345678"),
        { 0x9EF5C682, 0xD93914E7, 0x7A5D345A, 0xBB957436, 0x445A6FB6 }
    }
};

int main(int argc, char *argv[])
{
    int i;
    for (i = 0; i < NUM_VECTORS; i++)
    {
        SHA1_CONTEXT shac;
        SHA1_Init(&shac);
        SHA1_Compute(&shac, strlen((const char *)vectors[i].p), vectors[i].p);
        SHA1_Final(&shac);

        int j;
        for (j = 0; j < 5; j++)
        {
            if (shac.H[j] != vectors[i].H[j])
            {
                printf("%s", T("Failed." ENDLINE));
                return 0;
            }
        }
    }
    printf("%s", T("Passed." ENDLINE));
    return 1;
}

#endif
