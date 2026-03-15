/*! \file sha1.h
 * \brief Header for SHA1 hash implementation.
 *
 */

#ifndef SHA1_H
#define SHA1_H

LIBMUX_API bool mux_sha1_digest(const UTF8 *data[], const size_t lens[], int count,
                                uint8_t *out_digest, unsigned int *out_len);

#ifndef UNIX_DIGEST

typedef struct
{
    uint64_t   nTotal;
    uint32_t   H[5];
    uint8_t    block[64];
    size_t   nblock;
} MUX_SHA_CTX;

#define MUX_SHA1_DIGEST_LENGTH 20

LIBMUX_API void MUX_SHA1_Init(MUX_SHA_CTX *p);
LIBMUX_API void MUX_SHA1_Update(MUX_SHA_CTX *p, const UTF8 *buf, size_t n);
LIBMUX_API void MUX_SHA1_Final(uint8_t md[MUX_SHA1_DIGEST_LENGTH], MUX_SHA_CTX *p);

#endif

#endif // SHA1_H
