/*! \file sha1.h
 * \brief Header for SHA1 hash implementation.
 *
 */

#ifndef SHA1_H
#define SHA1_H

typedef struct
{
    uint64_t   nTotal;
    uint32_t   H[5];
    uint8_t    block[64];
    size_t   nblock;
} MUX_SHA_CTX;

#define MUX_SHA1_DIGEST_LENGTH 20

void MUX_SHA1_Init(MUX_SHA_CTX *p);
void MUX_SHA1_Update(MUX_SHA_CTX *p, const UTF8 *buf, size_t n);
void MUX_SHA1_Final(uint8_t md[MUX_SHA1_DIGEST_LENGTH], MUX_SHA_CTX *p);

#endif // SHA1_H
