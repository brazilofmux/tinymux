/*! \file test_digest.cpp
 * \brief Known-answer tests for the OS-backed digest entry points (#1963).
 *
 * mux_sha1_digest() feeds three surfaces whose output may never change:
 * RFC 6455 Sec-WebSocket-Accept (websocket.cpp), $SHA1$/$P6H$ password
 * verification (player.cpp), and sha1() softcode (funmath.cpp).  These
 * vectors pin all of them at the abstraction boundary, on BOTH backends —
 * OpenSSL EVP (UNIX_DIGEST) and Windows CNG — so a backend swap that
 * changes a single output byte fails here before any suite that merely
 * exercises one caller.
 *
 * Every golden value below was generated with the openssl(1) CLI as an
 * external oracle, not with either backend under test.
 *
 * Declares prototypes locally and links the built libmux, following the
 * tests/libmux pattern.
 *
 * Build: make        (Unix; needs a built mux/lib/libmux.so)
 * Run:   make test   (TAP-ish output, nonzero exit on failure)
 *
 * Windows (manual, from this directory, after building the solution):
 *   cl /nologo /EHsc /std:c++17 test_digest.cpp /Fe:test_digest.exe ^
 *      /link ..\..\mux\bin_release\libmux.lib
 *   PATH=..\..\mux\bin_release;%PATH% test_digest.exe
 */

#include <cstdio>
#include <cstring>
#include <cstdint>

typedef unsigned char UTF8;

// sha1.h exports (declared locally; see file comment)
bool mux_sha1_digest(const UTF8 *data[], const size_t lens[], int count,
                     uint8_t *out_digest, unsigned int *out_len);
bool mux_digest(const UTF8 *alg, const UTF8 *data[], const size_t lens[],
                int count, uint8_t *out_digest, unsigned int *out_len);

static int g_tests = 0;
static int g_failures = 0;

static void hexify(const uint8_t *md, unsigned int len, char *out)
{
    static const char digits[] = "0123456789abcdef";
    for (unsigned int i = 0; i < len; ++i)
    {
        out[2*i]   = digits[(md[i] >> 4) & 0x0F];
        out[2*i+1] = digits[md[i] & 0x0F];
    }
    out[2*len] = '\0';
}

static void check(const char *label, bool ok_call, const uint8_t *md,
                  unsigned int len, const char *expected_hex)
{
    ++g_tests;
    char hex[2*64 + 1] = "";
    if (ok_call)
    {
        hexify(md, len, hex);
    }
    bool pass = ok_call && 0 == strcmp(hex, expected_hex);
    printf("%s %d - %s\n", pass ? "ok" : "not ok", g_tests, label);
    if (!pass)
    {
        printf("#   expected %s\n#   got      %s%s\n",
               expected_hex, hex, ok_call ? "" : " (call failed)");
        ++g_failures;
    }
}

static void expect_unsupported(const char *label, const char *alg)
{
    ++g_tests;
    uint8_t md[64];
    unsigned int len = 0;
    const UTF8 *parts[] = { reinterpret_cast<const UTF8 *>("abc") };
    const size_t lens[] = { 3 };
    bool ok = mux_digest(reinterpret_cast<const UTF8 *>(alg), parts, lens, 1, md, &len);
    printf("%s %d - %s\n", !ok ? "ok" : "not ok", g_tests, label);
    if (ok)
    {
        ++g_failures;
    }
}

static void sha1_one(const char *label, const char *input, const char *expected_hex)
{
    uint8_t md[20];
    unsigned int len = 0;
    const UTF8 *parts[] = { reinterpret_cast<const UTF8 *>(input) };
    const size_t lens[] = { strlen(input) };
    bool ok = mux_sha1_digest(parts, lens, 1, md, &len);
    check(label, ok && 20 == len, md, len, expected_hex);
}

static void digest_one(const char *label, const char *alg, const char *input,
                       const char *expected_hex)
{
    uint8_t md[64];
    unsigned int len = 0;
    const UTF8 *parts[] = { reinterpret_cast<const UTF8 *>(input) };
    const size_t lens[] = { strlen(input) };
    bool ok = mux_digest(reinterpret_cast<const UTF8 *>(alg), parts, lens, 1, md, &len);
    check(label, ok, md, len, expected_hex);
}

int main()
{
    // ---- FIPS 180 vectors: pins sha1() softcode (funmath.cpp) ----
    sha1_one("sha1 FIPS empty string",
             "", "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    sha1_one("sha1 FIPS 'abc'",
             "abc", "a9993e364706816aba3e25717850c26c9cd0d89d");
    sha1_one("sha1 FIPS 56-char",
             "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
             "84983e441c3bd26ebaae4aa1f95129e5e54670f1");

    // ---- RFC 6455 worked example: pins Sec-WebSocket-Accept (websocket.cpp)
    // base64 of this digest is the spec's "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=".
    sha1_one("sha1 RFC 6455 key||GUID (websocket handshake)",
             "dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11",
             "b37a4f2cc0624f1690f64606cf385945b2bec4ea");

    // Same input as a two-part gather -- the call shape websocket.cpp and
    // player.cpp actually use.  Must equal the single-part digest.
    {
        uint8_t md[20];
        unsigned int len = 0;
        const UTF8 *parts[] = {
            reinterpret_cast<const UTF8 *>("dGhlIHNhbXBsZSBub25jZQ=="),
            reinterpret_cast<const UTF8 *>("258EAFA5-E914-47DA-95CA-C5AB0DC85B11"),
        };
        const size_t lens[] = { 24, 36 };
        bool ok = mux_sha1_digest(parts, lens, 2, md, &len);
        check("sha1 two-part gather == one-part (RFC 6455 input)",
              ok && 20 == len, md, len,
              "b37a4f2cc0624f1690f64606cf385945b2bec4ea");
    }

    // ---- salt||password two-part gather: pins mux_crypt $SHA1$ (player.cpp:694)
    {
        uint8_t md[20];
        unsigned int len = 0;
        const UTF8 *parts[] = {
            reinterpret_cast<const UTF8 *>("salt"),
            reinterpret_cast<const UTF8 *>("password"),
        };
        const size_t lens[] = { 4, 8 };
        bool ok = mux_sha1_digest(parts, lens, 2, md, &len);
        check("sha1 salt||password gather ($SHA1$ shape)",
              ok && 20 == len, md, len,
              "59b3e8d637cf97edbe2384cf59cb7453dfe30789");
    }

    // ---- bare password digest: pins $P6H$$1:sha1: verification (player.cpp:527)
    sha1_one("sha1 bare password ($P6H$ shape)",
             "password", "5baa61e4c9b93f3f0682250b6cf8331b7ee68fd8");

    // ---- mux_digest by name, both backends ----
    digest_one("mux_digest sha1 matches mux_sha1_digest", "sha1",
               "abc", "a9993e364706816aba3e25717850c26c9cd0d89d");
    digest_one("mux_digest SHA1 (case-insensitive)", "SHA1",
               "abc", "a9993e364706816aba3e25717850c26c9cd0d89d");
    digest_one("mux_digest sha256 'abc'", "sha256",
               "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    digest_one("mux_digest sha384 'abc'", "sha384",
               "abc", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7");
    digest_one("mux_digest sha512 'abc'", "sha512",
               "abc", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f");
    digest_one("mux_digest md5 'abc'", "md5",
               "abc", "900150983cd24fb0d6963f7d28e17f72");

#ifdef _WIN32
    // Hyphenated aliases are guaranteed by the CNG name table; on OpenSSL
    // they depend on version/provider (#1961), so only asserted here.
    digest_one("mux_digest sha-1 alias (CNG table)", "sha-1",
               "abc", "a9993e364706816aba3e25717850c26c9cd0d89d");
    digest_one("mux_digest sha-256 alias (CNG table)", "sha-256",
               "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
#endif

    // ---- rejection ----
    expect_unsupported("mux_digest rejects unknown name", "sha3-999");

    // ---- zero-part call: digest(<alg>) with no data == digest of "" ----
    {
        uint8_t md[20];
        unsigned int len = 0;
        bool ok = mux_sha1_digest(nullptr, nullptr, 0, md, &len);
        check("sha1 zero-part call == empty-string digest",
              ok && 20 == len, md, len,
              "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    }

    printf("1..%d\n", g_tests);
    if (g_failures)
    {
        printf("# FAILED: %d of %d\n", g_failures, g_tests);
        return 1;
    }
    printf("# all %d digest KATs passed\n", g_tests);
    return 0;
}
