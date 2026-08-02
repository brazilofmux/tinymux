/*! \file shacrypt.cpp
 * \brief Portable sha-crypt ($5$/$6$) over OS crypto primitives.
 *
 * Implements the standard sha-crypt construction (Ulrich Drepper's
 * SHA-256/SHA-512 crypt, as shipped by glibc/musl and produced by
 * `openssl passwd -5/-6`) so both platforms generate and verify the same
 * modular-crypt password format (#1962).  The SHA-256/512 compression
 * itself comes from the OS — OpenSSL EVP under UNIX_DIGEST, Windows CNG
 * otherwise — per the #1963 posture; what ships here is the construction
 * that orchestrates those primitives, in the same category as HMAC or the
 * RFC 6455 concatenation.
 *
 * Using our own construction on Unix too (rather than libc crypt(3)) makes
 * behavior identical on every box: macOS crypt() lacks $5$/$6$ entirely,
 * and rounds= handling stops depending on whichever libc is present.
 *
 * Output is byte-compatible with glibc: rounds= presence and clamped value
 * are preserved from the setting, salt is truncated to 16, and the
 * character permutation/base64 match — pinned against `openssl passwd`
 * oracle vectors in tests/shacrypt.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "core.h"
#include "shacrypt.h"

#include <climits>

// ---------------------------------------------------------------------------
// Incremental digest over the OS primitive: OpenSSL EVP or Windows CNG.
// The rounds loop runs password_hash_rounds digests per call, so contexts
// are created once per mux_sha_crypt() call and reused per round.
// ---------------------------------------------------------------------------

#ifdef UNIX_DIGEST

#include <openssl/evp.h>

typedef struct
{
    EVP_MD_CTX   *ctx;
    const EVP_MD *md;
    unsigned int  digest_len;
} shc_hash;

static bool shc_open(shc_hash *h, bool bSha512)
{
#if HAVE_EVP_MD_CTX_NEW
    h->ctx = EVP_MD_CTX_new();
#else
    h->ctx = EVP_MD_CTX_create();
#endif
    h->md = bSha512 ? EVP_sha512() : EVP_sha256();
    h->digest_len = bSha512 ? 64 : 32;
    return nullptr != h->ctx;
}

static bool shc_begin(shc_hash *h)
{
    return 0 != EVP_DigestInit_ex(h->ctx, h->md, nullptr);
}

static bool shc_update(shc_hash *h, const uint8_t *p, size_t n)
{
    return 0 != EVP_DigestUpdate(h->ctx, p, n);
}

static bool shc_final(shc_hash *h, uint8_t *out)
{
    unsigned int len = 0;
    return 0 != EVP_DigestFinal_ex(h->ctx, out, &len);
}

static void shc_close(shc_hash *h)
{
#if HAVE_EVP_MD_CTX_NEW
    EVP_MD_CTX_free(h->ctx);
#else
    EVP_MD_CTX_destroy(h->ctx);
#endif
    h->ctx = nullptr;
}

#elif defined(WIN32)

#include <bcrypt.h>
#include <mutex>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

typedef struct
{
    BCRYPT_HASH_HANDLE hash;
    unsigned int       digest_len;
} shc_hash;

static BCRYPT_ALG_HANDLE shc_provider(bool bSha512)
{
    // Opened once per algorithm and cached for the life of the process,
    // same rationale as sha1.cpp's provider table.
    static BCRYPT_ALG_HANDLE hSha256 = nullptr;
    static BCRYPT_ALG_HANDLE hSha512 = nullptr;
    static std::once_flag once256, once512;

    if (bSha512)
    {
        std::call_once(once512, []
        {
            BCRYPT_ALG_HANDLE h = nullptr;
            if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&h,
                    BCRYPT_SHA512_ALGORITHM, nullptr, BCRYPT_HASH_REUSABLE_FLAG)))
            {
                hSha512 = h;
            }
        });
        return hSha512;
    }
    std::call_once(once256, []
    {
        BCRYPT_ALG_HANDLE h = nullptr;
        if (NT_SUCCESS(BCryptOpenAlgorithmProvider(&h,
                BCRYPT_SHA256_ALGORITHM, nullptr, BCRYPT_HASH_REUSABLE_FLAG)))
        {
            hSha256 = h;
        }
    });
    return hSha256;
}

static bool shc_open(shc_hash *h, bool bSha512)
{
    BCRYPT_ALG_HANDLE alg = shc_provider(bSha512);
    if (nullptr == alg)
    {
        return false;
    }
    h->digest_len = bSha512 ? 64 : 32;
    // A reusable hash handle resets itself on BCryptFinishHash, so one
    // handle serves every begin/update/final cycle of this call.
    return NT_SUCCESS(BCryptCreateHash(alg, &h->hash, nullptr, 0, nullptr, 0,
                                       BCRYPT_HASH_REUSABLE_FLAG));
}

static bool shc_begin(shc_hash *h)
{
    // Reusable handles are ready after open and after each finish.
    return nullptr != h->hash;
}

static bool shc_update(shc_hash *h, const uint8_t *p, size_t n)
{
    return NT_SUCCESS(BCryptHashData(h->hash,
               const_cast<PUCHAR>(p), static_cast<ULONG>(n), 0));
}

static bool shc_final(shc_hash *h, uint8_t *out)
{
    return NT_SUCCESS(BCryptFinishHash(h->hash, out, h->digest_len, 0));
}

static void shc_close(shc_hash *h)
{
    if (h->hash)
    {
        BCryptDestroyHash(h->hash);
        h->hash = nullptr;
    }
}

#else

#error "No digest backend: need OpenSSL (UNIX_DIGEST) or Windows CNG (#1963)."

#endif

// ---------------------------------------------------------------------------
// sha-crypt proper.
// ---------------------------------------------------------------------------

#define SHC_ROUNDS_DEFAULT 5000UL
#define SHC_ROUNDS_MIN     1000UL
#define SHC_ROUNDS_MAX     999999999UL
#define SHC_SALT_MAX       16

// crypt-style base64 alphabet (distinct from MIME base64).
static const char shc_itoa64[] =
    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

static void shc_b64_from_24bit(UTF8 **pp, uint8_t b2, uint8_t b1, uint8_t b0, int n)
{
    unsigned int w = (static_cast<unsigned int>(b2) << 16)
                   | (static_cast<unsigned int>(b1) << 8)
                   |  static_cast<unsigned int>(b0);
    while (n-- > 0)
    {
        *(*pp)++ = static_cast<UTF8>(shc_itoa64[w & 0x3F]);
        w >>= 6;
    }
}

static void shc_wipe(void *p, size_t n)
{
    volatile uint8_t *vp = static_cast<volatile uint8_t *>(p);
    while (n--)
    {
        *vp++ = 0;
    }
}

const UTF8 *mux_sha_crypt(const UTF8 *szPassword, const UTF8 *szSetting)
{
    // ---- parse the setting: $5$|$6$ [rounds=N$] salt[$...] ----
    bool bSha512;
    if ('$' == szSetting[0] && '$' == szSetting[2] && '5' == szSetting[1])
    {
        bSha512 = false;
    }
    else if ('$' == szSetting[0] && '$' == szSetting[2] && '6' == szSetting[1])
    {
        bSha512 = true;
    }
    else
    {
        return nullptr;
    }

    const UTF8 *p = szSetting + 3;
    unsigned long rounds = SHC_ROUNDS_DEFAULT;
    bool bRoundsCustom = false;
    if (0 == strncmp(reinterpret_cast<const char *>(p), "rounds=", 7))
    {
        char *endp = nullptr;
        unsigned long r = strtoul(reinterpret_cast<const char *>(p) + 7, &endp, 10);
        if (endp && '$' == *endp)
        {
            // glibc clamps and then reports the clamped value in the output.
            if (r < SHC_ROUNDS_MIN)
            {
                r = SHC_ROUNDS_MIN;
            }
            if (SHC_ROUNDS_MAX < r)
            {
                r = SHC_ROUNDS_MAX;
            }
            rounds = r;
            bRoundsCustom = true;
            p = reinterpret_cast<const UTF8 *>(endp) + 1;
        }
    }

    const UTF8 *salt = p;
    size_t salt_len = 0;
    while (  salt[salt_len]
          && '$' != salt[salt_len]
          && salt_len < SHC_SALT_MAX)
    {
        salt_len++;
    }

    const size_t key_len = strlen(reinterpret_cast<const char *>(szPassword));
    const uint8_t *key = reinterpret_cast<const uint8_t *>(szPassword);
    const uint8_t *slt = reinterpret_cast<const uint8_t *>(salt);

    shc_hash H;
    if (!shc_open(&H, bSha512))
    {
        return nullptr;
    }
    const size_t L = H.digest_len;

    uint8_t alt_result[64];
    uint8_t temp_result[64];
    bool ok = true;

    // Digest B = H(password || salt || password).
    ok = ok && shc_begin(&H)
            && shc_update(&H, key, key_len)
            && shc_update(&H, slt, salt_len)
            && shc_update(&H, key, key_len)
            && shc_final(&H, alt_result);

    // Digest A = H(password || salt || B-blocks per key_len || bit-walk).
    ok = ok && shc_begin(&H)
            && shc_update(&H, key, key_len)
            && shc_update(&H, slt, salt_len);
    if (ok)
    {
        size_t cnt;
        for (cnt = key_len; ok && cnt > L; cnt -= L)
        {
            ok = shc_update(&H, alt_result, L);
        }
        ok = ok && shc_update(&H, alt_result, cnt);
        for (cnt = key_len; ok && cnt > 0; cnt >>= 1)
        {
            ok = (cnt & 1) ? shc_update(&H, alt_result, L)
                           : shc_update(&H, key, key_len);
        }
    }
    ok = ok && shc_final(&H, alt_result);

    // P sequence: H(password repeated key_len times), tiled to key_len bytes.
    ok = ok && shc_begin(&H);
    for (size_t i = 0; ok && i < key_len; i++)
    {
        ok = shc_update(&H, key, key_len);
    }
    ok = ok && shc_final(&H, temp_result);

    // p_bytes / s_bytes are small (key and salt sized); the salt is <= 16
    // and passwords are LBUF-bounded, but allocate exactly what is needed.
    uint8_t *p_bytes = nullptr;
    uint8_t *s_bytes = nullptr;
    if (ok)
    {
        p_bytes = new uint8_t[key_len ? key_len : 1];
        size_t cnt;
        uint8_t *cp = p_bytes;
        for (cnt = key_len; cnt >= L; cnt -= L)
        {
            memcpy(cp, temp_result, L);
            cp += L;
        }
        memcpy(cp, temp_result, cnt);
    }

    // S sequence: H(salt repeated 16 + alt_result[0] times), tiled to salt_len.
    ok = ok && shc_begin(&H);
    if (ok)
    {
        const size_t reps = 16u + alt_result[0];
        for (size_t i = 0; ok && i < reps; i++)
        {
            ok = shc_update(&H, slt, salt_len);
        }
    }
    ok = ok && shc_final(&H, temp_result);
    if (ok)
    {
        s_bytes = new uint8_t[salt_len ? salt_len : 1];
        size_t cnt;
        uint8_t *cp = s_bytes;
        for (cnt = salt_len; cnt >= L; cnt -= L)
        {
            memcpy(cp, temp_result, L);
            cp += L;
        }
        memcpy(cp, temp_result, cnt);
    }

    // The rounds loop: repeatedly remix password and salt material.
    for (unsigned long r = 0; ok && r < rounds; r++)
    {
        ok = shc_begin(&H);
        ok = ok && ((r & 1) ? shc_update(&H, p_bytes, key_len)
                            : shc_update(&H, alt_result, L));
        if (ok && 0 != (r % 3))
        {
            ok = shc_update(&H, s_bytes, salt_len);
        }
        if (ok && 0 != (r % 7))
        {
            ok = shc_update(&H, p_bytes, key_len);
        }
        ok = ok && ((r & 1) ? shc_update(&H, alt_result, L)
                            : shc_update(&H, p_bytes, key_len));
        ok = ok && shc_final(&H, alt_result);
    }

    shc_close(&H);
    if (p_bytes)
    {
        shc_wipe(p_bytes, key_len ? key_len : 1);
        delete [] p_bytes;
    }
    if (s_bytes)
    {
        shc_wipe(s_bytes, salt_len ? salt_len : 1);
        delete [] s_bytes;
    }
    shc_wipe(temp_result, sizeof(temp_result));
    if (!ok)
    {
        shc_wipe(alt_result, sizeof(alt_result));
        return nullptr;
    }

    // ---- emit $<id>$[rounds=N$]<salt>$<base64 permutation> ----
    //
    // "$6$rounds=999999999$" (20) + 16 salt + '$' + 86 hash + NUL = 124.
    thread_local UTF8 buf[128];
    UTF8 *q = buf;
    *q++ = '$';
    *q++ = bSha512 ? '6' : '5';
    *q++ = '$';
    if (bRoundsCustom)
    {
        q += mux_snprintf(q, 24, T("rounds=%lu$"), rounds);
    }
    memcpy(q, salt, salt_len);
    q += salt_len;
    *q++ = '$';

    if (bSha512)
    {
        static const uint8_t ord[21][3] =
        {
            { 0,21,42},{22,43, 1},{44, 2,23},{ 3,24,45},{25,46, 4},
            {47, 5,26},{ 6,27,48},{28,49, 7},{50, 8,29},{ 9,30,51},
            {31,52,10},{53,11,32},{12,33,54},{34,55,13},{56,14,35},
            {15,36,57},{37,58,16},{59,17,38},{18,39,60},{40,61,19},
            {62,20,41},
        };
        for (int i = 0; i < 21; i++)
        {
            shc_b64_from_24bit(&q, alt_result[ord[i][0]],
                                   alt_result[ord[i][1]],
                                   alt_result[ord[i][2]], 4);
        }
        shc_b64_from_24bit(&q, 0, 0, alt_result[63], 2);
    }
    else
    {
        static const uint8_t ord[10][3] =
        {
            { 0,10,20},{21, 1,11},{12,22, 2},{ 3,13,23},{24, 4,14},
            {15,25, 5},{ 6,16,26},{27, 7,17},{18,28, 8},{ 9,19,29},
        };
        for (int i = 0; i < 10; i++)
        {
            shc_b64_from_24bit(&q, alt_result[ord[i][0]],
                                   alt_result[ord[i][1]],
                                   alt_result[ord[i][2]], 4);
        }
        shc_b64_from_24bit(&q, 0, alt_result[31], alt_result[30], 3);
    }
    *q = '\0';

    shc_wipe(alt_result, sizeof(alt_result));
    return buf;
}
