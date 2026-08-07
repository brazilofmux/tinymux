/*! \file sha1.cpp
 * \brief OS-backed message digests.
 *
 * Two backends, both platform crypto (#1963): OpenSSL EVP under UNIX_DIGEST,
 * Windows CNG (BCrypt) otherwise.  The homegrown FIPS-180 SHA-1 that used to
 * live here is retired; this tree ships no cryptographic source.  Output is
 * byte-identical across backends — mux_sha1_digest() feeds surfaces whose
 * bytes may never change (RFC 6455 Sec-WebSocket-Accept, $SHA1$/$P6H$
 * password verification, sha1() softcode) — pinned by tests/digest.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "core.h"
#include "sha1.h"

#ifdef UNIX_DIGEST

#include <openssl/evp.h>

static bool evp_digest(const EVP_MD *md, const UTF8 * const data[], const size_t lens[],
                       int count, uint8_t *out_digest, unsigned int *out_len)
{
    EVP_MD_CTX *ctx =
    #if HAVE_EVP_MD_CTX_NEW
        EVP_MD_CTX_new();
    #else
        EVP_MD_CTX_create();
    #endif

    if (!ctx)
    {
        return false;
    }

    bool ok = (0 != EVP_DigestInit_ex(ctx, md, nullptr));
    for (int i = 0; ok && i < count; ++i)
    {
        ok = (0 != EVP_DigestUpdate(ctx, data[i], lens[i]));
    }
    if (ok)
    {
        ok = (0 != EVP_DigestFinal_ex(ctx, out_digest, out_len));
    }

    #if HAVE_EVP_MD_CTX_NEW
        EVP_MD_CTX_free(ctx);
    #else
        EVP_MD_CTX_destroy(ctx);
    #endif

    return ok;
}

bool mux_sha1_digest(const UTF8 * const data[], const size_t lens[], int count,
                     uint8_t *out_digest, unsigned int *out_len)
{
    return evp_digest(EVP_sha1(), data, lens, count, out_digest, out_len);
}

bool mux_digest(const UTF8 *alg, const UTF8 *data[], const size_t lens[],
                int count, uint8_t *out_digest, unsigned int *out_len)
{
    // Provider-native fetch on OpenSSL 3.0+ so aliases resolve from a cold
    // process; see fun_digest (#1961).  Fetched EVP_MD is a ref to free.
#if OPENSSL_VERSION_NUMBER >= 0x30000000L && !defined(LIBRESSL_VERSION_NUMBER)
    EVP_MD *md = EVP_MD_fetch(nullptr, reinterpret_cast<const char *>(alg), nullptr);
    if (nullptr == md)
    {
        return false;
    }
    bool ok = evp_digest(md, data, lens, count, out_digest, out_len);
    EVP_MD_free(md);
    return ok;
#else
    const EVP_MD *md = EVP_get_digestbyname(reinterpret_cast<const char *>(alg));
    if (nullptr == md)
    {
        return false;
    }
    return evp_digest(md, data, lens, count, out_digest, out_len);
#endif
}

#elif defined(WIN32)

// Windows CNG backend.  <windows.h> arrives via config.h.
#include <bcrypt.h>
#include <mutex>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

typedef struct
{
    const wchar_t     *bcrypt_id;   // BCRYPT_*_ALGORITHM
    const char        *name;        // canonical lowercase
    const char        *alias;       // hyphenated form, or nullptr
    ULONG              digest_len;
    BCRYPT_ALG_HANDLE  handle;      // opened once, cached for process life
    std::once_flag     opened;
} cng_alg;

static cng_alg s_cng_algs[] =
{
    { BCRYPT_SHA1_ALGORITHM,   "sha1",   "sha-1",   20, nullptr, {} },
    { BCRYPT_SHA256_ALGORITHM, "sha256", "sha-256", 32, nullptr, {} },
    { BCRYPT_SHA384_ALGORITHM, "sha384", "sha-384", 48, nullptr, {} },
    { BCRYPT_SHA512_ALGORITHM, "sha512", "sha-512", 64, nullptr, {} },
    { BCRYPT_MD5_ALGORITHM,    "md5",    nullptr,   16, nullptr, {} },
};

static bool cng_digest(cng_alg &alg, const UTF8 * const data[], const size_t lens[],
                       int count, uint8_t *out_digest, unsigned int *out_len)
{
    // The provider handle is opened once and kept for the life of the
    // process: BCryptOpenAlgorithmProvider is the expensive call, and CNG
    // documents provider handles as safe for concurrent use.
    std::call_once(alg.opened, [&alg]
    {
        BCRYPT_ALG_HANDLE h = nullptr;
        if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&h, alg.bcrypt_id, nullptr, 0)))
        {
            alg.handle = h;
        }
    });
    if (nullptr == alg.handle)
    {
        return false;
    }

    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!NT_SUCCESS(BCryptCreateHash(alg.handle, &hash, nullptr, 0, nullptr, 0, 0)))
    {
        return false;
    }

    bool ok = true;
    for (int i = 0; ok && i < count; ++i)
    {
        ok = NT_SUCCESS(BCryptHashData(hash,
                 reinterpret_cast<PUCHAR>(const_cast<UTF8 *>(data[i])),
                 static_cast<ULONG>(lens[i]), 0));
    }
    if (ok)
    {
        ok = NT_SUCCESS(BCryptFinishHash(hash, out_digest, alg.digest_len, 0));
    }
    BCryptDestroyHash(hash);

    if (ok)
    {
        *out_len = alg.digest_len;
    }
    return ok;
}

bool mux_sha1_digest(const UTF8 * const data[], const size_t lens[], int count,
                     uint8_t *out_digest, unsigned int *out_len)
{
    return cng_digest(s_cng_algs[0], data, lens, count, out_digest, out_len);
}

bool mux_digest(const UTF8 *alg, const UTF8 *data[], const size_t lens[],
                int count, uint8_t *out_digest, unsigned int *out_len)
{
    for (cng_alg &entry : s_cng_algs)
    {
        if (  mux_stricmp(alg, reinterpret_cast<const UTF8 *>(entry.name)) == 0
           || (  entry.alias
              && mux_stricmp(alg, reinterpret_cast<const UTF8 *>(entry.alias)) == 0))
        {
            return cng_digest(entry, data, lens, count, out_digest, out_len);
        }
    }
    return false;
}

#else

// configure.ac hard-errors without OpenSSL, so a Unix build always has
// UNIX_DIGEST; Windows always has CNG.  If a new platform lands here, it
// needs a digest backend decision — the homegrown SHA-1 that used to be the
// fallback was retired by #1963 and must not come back.
#error "No digest backend: need OpenSSL (UNIX_DIGEST) or Windows CNG (#1963)."

#endif
