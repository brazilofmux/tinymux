/*! \file sha1.h
 * \brief Header for SHA1 hash implementation.
 *
 */

#ifndef SHA1_H
#define SHA1_H

typedef struct
{
    UINT64   nTotal;
    UINT32   H[5];
    UINT8    block[64];
    size_t   nblock;
} SHA_CTX;

#define SHA_DIGEST_LENGTH 20

void SHA1_Init(SHA_CTX *p);
void SHA1_Update(SHA_CTX *p, __in_ecount(n) const UTF8 *buf, __in size_t n);
void SHA1_Final(__out UINT8 md[SHA_DIGEST_LENGTH], SHA_CTX *p);

#endif // SHA1_H
