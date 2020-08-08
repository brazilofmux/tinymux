/*! \file sha1.cpp
 * \brief Implementation of SHA1 hash.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"

#ifndef UNIX_DIGEST

#include "sha1.h"

void SHA1_Init(SHA_CTX *p)
{
    p->H[0] = 0x67452301;
    p->H[1] = 0xEFCDAB89;
    p->H[2] = 0x98BADCFE;
    p->H[3] = 0x10325476;
    p->H[4] = 0xC3D2E1F0;
    p->nTotal = 0;
    p->nblock = 0;
}

#ifdef WINDOWS_INSTRINSICS
#define ROTL(d,n) _lrotl(d,n)
#else // WINDOWS_INSTRINSICS
#define ROTL(d,n) (((d) << (n)) | ((d) >> (32-(n))))
#endif // WINDOWS_INSTRINSICS

#define Ch(x,y,z)      (((x) & (y)) ^ (~(x) & (z)))
#define Parity(x,y,z)  ((x) ^ (y) ^ (z))
#define Maj(x,y,z)     (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))

static void SHA1_HashBlock(SHA_CTX *p)
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

void SHA1_Update(SHA_CTX *p, __in_ecount(n) const UTF8 *buf, __in size_t n)
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

void SHA1_Final(UINT8 md[SHA_DIGEST_LENGTH], SHA_CTX *p)
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

    // Serialize 5 UINT32 to 20 UINT8 in big-endian order.
    //
    for (int i = 0, j = 0; i <= 4; i++, j += sizeof(UINT32))
    {
        UINT32 h = p->H[i];
        md[j + 0] = (UINT8)(h >> 24);
        md[j + 1] = (UINT8)(h >> 16);
        md[j + 2] = (UINT8)(h >>  8);
        md[j + 3] = (UINT8)(h      );
    }
}

#if 0

typedef struct
{
    const UTF8 *p;
    UINT8 md[SHA_DIGEST_LENGTH];
} sha1_test_vector;

#define NUM_VECTORS 5
sha1_test_vector vectors[NUM_VECTORS] =
{
    {
        T("abc"),
        { 0xA9, 0x99, 0x3E, 0x36, 0x47, 0x06, 0x81, 0x6A, 0xBA, 0x3E, 0x25, 0x71, 0x78, 0x50, 0xC2, 0x6C, 0x9C, 0xD0, 0xD8, 0x9D }
    },
    {
        // 64-byte block composed of 55 bytes, 1-byte pad (0x80), and 8-byte
        // length.
        //
        T("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnop"),
        { 0x47, 0xB1, 0x72, 0x81, 0x07, 0x95, 0x69, 0x9F, 0xE7, 0x39, 0x19, 0x7D, 0x1A, 0x1F, 0x59, 0x60, 0x70, 0x02, 0x42, 0xF1 }
    },
    {
        // First 64-byte block composed of 56 bytes, 1-byte pad (0x80), and 7
        // zeros.  Second 64-byte block composed of 56 zeros (0x00) and 8-byte
        // length.
        //
        T("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        { 0x84, 0x98, 0x3E, 0x44, 0x1C, 0x3B, 0xD2, 0x6E, 0xBA, 0xAE, 0x4A, 0xA1, 0xF9, 0x51, 0x29, 0xE5, 0xE5, 0x46, 0x70, 0xF1 }
    },
    {
        // First 64-byte block composed of 63 bytes and 1-byte pad (0x80).
        // Second 64-byte block composed of 56 zeros (0x00), and 8-byte
        // length.
        //
        T("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq1234567"),
        { 0x55, 0xA0, 0xC4, 0x2A, 0x00, 0xBD, 0x4B, 0x49, 0x6A, 0x16, 0xD1, 0xAC, 0x32, 0xE2, 0x0B, 0x5A, 0x7F, 0xA3, 0xE0, 0x87 }
    },
    {
        // First 64-byte block composed of 64 bytes.  Second 64-byte block
        // composed of 1-byte pad (0x80), 55 zeros (0x00), and 8-byte length.
        //
        T("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq12345678"),
        { 0x9E, 0xF5, 0xC6, 0x82, 0xD9, 0x39, 0x14, 0xE7, 0x7A, 0x5D, 0x34, 0x5A, 0xBB, 0x95, 0x74, 0x36, 0x44, 0x5A, 0x6F, 0xB6 }
    }
};

int main(int argc, char *argv[])
{
    int i;
    for (i = 0; i < NUM_VECTORS; i++)
    {
        SHA_CTX shac;
        SHA1_Init(&shac);
        SHA1_Update(&shac, vectors[i].p, strlen((const char *)vectors[i].p));

        UINT8 md[SHA_DIGEST_LENGTH];
        SHA1_Final(md, &shac);

        int j;
        for (j = 0; j < SHA_DIGEST_LENGTH; j++)
        {
            if (md[j] != vectors[i].md[j])
            {
                printf("Failed. Expected 0x%02X. Found 0x%02X" ENDLINE, vectors[i].md[j], md[j]);
                return 0;
            }
        }
    }
    printf("%s", T("Passed." ENDLINE));
    return 1;
}

#endif

#endif
