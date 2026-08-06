/*! \file sha1.h
 * \brief Header for the libmux message-digest entry points.
 *
 * Both entry points are backed by platform crypto — OpenSSL EVP under
 * UNIX_DIGEST, Windows CNG (BCrypt) otherwise.  This tree ships no
 * cryptographic source (#1963).
 */

#ifndef SHA1_H
#define SHA1_H

#define MUX_SHA1_DIGEST_LENGTH 20

// Largest digest mux_digest() can produce on any backend (SHA-512).
// Callers of mux_digest() must provide at least this much output space.
//
#define MUX_MAX_DIGEST_LENGTH 64

LIBMUX_API bool mux_sha1_digest(const UTF8 * const data[], const size_t lens[], int count,
                                uint8_t *out_digest, unsigned int *out_len);

// Gather-style one-shot digest by algorithm name ("sha1", "sha256", "sha384",
// "sha512", "md5"; case-insensitive, hyphenated aliases accepted).  Returns
// false for names the backend cannot resolve.
//
LIBMUX_API bool mux_digest(const UTF8 *alg, const UTF8 *data[], const size_t lens[],
                           int count, uint8_t *out_digest, unsigned int *out_len);

#endif // SHA1_H
