/*! \file shacrypt.h
 * \brief Header for the portable sha-crypt ($5$/$6$) implementation.
 */

#ifndef SHACRYPT_H
#define SHACRYPT_H

// Standard sha-crypt (Ulrich Drepper's SHA-256/SHA-512 crypt, the $5$/$6$
// modular-crypt formats implemented by glibc/musl), computed over OS crypto
// primitives (OpenSSL EVP / Windows CNG).  Returns the full encoded string
// ($<id>$[rounds=N$]<salt>$<hash>) in a thread-local buffer, or nullptr if
// the setting is not a well-formed $5$/$6$ setting.
//
// Verification is by recomputation: pass the stored string as `setting` and
// strcmp the result, crypt(3)-style.  Output preserves the rounds= field's
// presence and clamped value exactly as glibc does, so round-tripping a
// glibc/musl/openssl-produced hash is byte-identical.
//
LIBMUX_API const UTF8 *mux_sha_crypt(const UTF8 *szPassword, const UTF8 *szSetting);

#endif // SHACRYPT_H
