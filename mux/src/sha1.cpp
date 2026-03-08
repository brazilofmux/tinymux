/*! \file sha1.cpp
 * \brief Implementation of SHA1 hash.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "core.h"

#ifndef UNIX_DIGEST

void MUX_SHA1_Init(MUX_SHA_CTX *p)
{
    p->H[0] = 0x67452301;
    p->H[1] = 0xEFCDAB89;
    p->H[2] = 0x98BADCFE;
    p->H[3] = 0x10325476;
    p->H[4] = 0xC3D2E1F0;
    p->nTotal = 0;
    p->nblock = 0;
}

#ifdef WINDOWS_INTRINSICS
#define ROTL(d,n) _lrotl(d,n)
#else // WINDOWS_INTRINSICS
#define ROTL(d,n) (((d) << (n)) | ((d) >> (32-(n))))
#endif // WINDOWS_INTRINSICS

#define Ch(x,y,z)      (((x) & (y)) ^ (~(x) & (z)))
#define Parity(x,y,z)  ((x) ^ (y) ^ (z))
#define Maj(x,y,z)     (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

static void MUX_SHA1_HashBlock(MUX_SHA_CTX *p)
{
    int t;
    uint32_t W[80];

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

    uint32_t a = p->H[0];
    uint32_t b = p->H[1];
    uint32_t c = p->H[2];
    uint32_t d = p->H[3];
    uint32_t e = p->H[4];

    uint32_t T;
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

void MUX_SHA1_Update(MUX_SHA_CTX *p, const UTF8 *buf, size_t n)
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
            MUX_SHA1_HashBlock(p);
            p->nblock = 0;
        }
    }
}

void MUX_SHA1_Final(uint8_t md[MUX_SHA1_DIGEST_LENGTH], MUX_SHA_CTX *p)
{
    p->block[p->nblock++] = 0x80;
    if (sizeof(p->block) - sizeof(uint64_t) < p->nblock)
    {
        memset(p->block + p->nblock, 0, sizeof(p->block) - p->nblock);
        MUX_SHA1_HashBlock(p);
        memset(p->block, 0, sizeof(p->block) - sizeof(uint64_t));
    }
    else
    {
        memset(p->block + p->nblock, 0, sizeof(p->block) - p->nblock - sizeof(uint64_t));
    }
    p->nTotal *= 8;

    p->block[sizeof(p->block) - 8] = static_cast<uint8_t>((p->nTotal >> 56) & 0xFF);
    p->block[sizeof(p->block) - 7] = static_cast<uint8_t>((p->nTotal >> 48) & 0xFF);
    p->block[sizeof(p->block) - 6] = static_cast<uint8_t>((p->nTotal >> 40) & 0xFF);
    p->block[sizeof(p->block) - 5] = static_cast<uint8_t>((p->nTotal >> 32) & 0xFF);
    p->block[sizeof(p->block) - 4] = static_cast<uint8_t>((p->nTotal >> 24) & 0xFF);
    p->block[sizeof(p->block) - 3] = static_cast<uint8_t>((p->nTotal >> 16) & 0xFF);
    p->block[sizeof(p->block) - 2] = static_cast<uint8_t>((p->nTotal >>  8) & 0xFF);
    p->block[sizeof(p->block) - 1] = static_cast<uint8_t>((p->nTotal      ) & 0xFF);
    MUX_SHA1_HashBlock(p);

    // Serialize 5 uint32_t to 20 uint8_t in big-endian order.
    //
    for (int i = 0, j = 0; i <= 4; i++, j += sizeof(uint32_t))
    {
        uint32_t h = p->H[i];
        md[j + 0] = static_cast<uint8_t>(h >> 24);
        md[j + 1] = static_cast<uint8_t>(h >> 16);
        md[j + 2] = static_cast<uint8_t>(h >>  8);
        md[j + 3] = static_cast<uint8_t>(h      );
    }
}

#endif
