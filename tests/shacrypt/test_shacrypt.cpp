/*! \file test_shacrypt.cpp
 * \brief Known-answer tests for the portable sha-crypt implementation (#1962).
 *
 * mux_sha_crypt() must be byte-compatible with glibc/musl/openssl sha-crypt:
 * a $5$/$6$ hash produced on any Unix box must verify on Windows and vice
 * versa — cross-platform password-database compatibility is the point of
 * the implementation, and these vectors are the proof.  Every golden value
 * was generated with `openssl passwd -5/-6` as an external oracle (OpenSSL
 * 3.5.6); the first two $6$ vectors are also the published sha-crypt spec
 * vectors.
 *
 * Declares the prototype locally and links the built libmux, following the
 * tests/libmux pattern.
 *
 * Build: make        (Unix; needs a built mux/lib/libmux.so)
 * Run:   make test   (TAP-ish output, nonzero exit on failure)
 *
 * Windows (manual, from this directory, after building the solution):
 *   cl /nologo /EHsc /std:c++17 test_shacrypt.cpp /Fe:test_shacrypt.exe ^
 *      /link ..\..\mux\bin_release\libmux.lib
 *   PATH=..\..\mux\bin_release;%PATH% test_shacrypt.exe
 */

#include <cstdio>
#include <cstring>

typedef unsigned char UTF8;

// shacrypt.h export (declared locally; see file comment)
const UTF8 *mux_sha_crypt(const UTF8 *szPassword, const UTF8 *szSetting);

static int g_tests = 0;
static int g_failures = 0;

static void check(const char *label, const char *password, const char *setting,
                  const char *expected)
{
    ++g_tests;
    const UTF8 *got = mux_sha_crypt(reinterpret_cast<const UTF8 *>(password),
                                    reinterpret_cast<const UTF8 *>(setting));
    bool pass;
    if (nullptr == expected)
    {
        pass = (nullptr == got);
    }
    else
    {
        pass = (nullptr != got
             && 0 == strcmp(reinterpret_cast<const char *>(got), expected));
    }
    printf("%s %d - %s\n", pass ? "ok" : "not ok", g_tests, label);
    if (!pass)
    {
        printf("#   expected %s\n#   got      %s\n",
               expected ? expected : "(nullptr)",
               got ? reinterpret_cast<const char *>(got) : "(nullptr)");
        ++g_failures;
    }
}

int main()
{
    // ---- $6$ oracle vectors (first two are the published spec vectors) ----
    check("$6$ spec vector: saltstring/password",
          "password", "$6$saltstring",
          "$6$saltstring$adDbXsJjcDlq2662QPgd.tkSOVmnG9Tt3oXl4HR60SusC3AGjirnDe"
          "nVZp3DGwLwqy6iYKCzannhaX9DR72nN1");
    check("$6$ spec vector: rounds=10000/Hello world!",
          "Hello world!", "$6$rounds=10000$saltstringsaltst",
          "$6$rounds=10000$saltstringsaltst$OW1/O6BYHV6BcXZu8QVeXbDWra3Oeqh0sb"
          "HbbMCVNSnCM/UrjmM0Dp8vOuZeHBy/YTBmSK6H9qs/y3RnOaw5v.");
    check("$6$ short salt",
          "pw", "$6$shortsl",
          "$6$shortsl$xrC./HXHUqLLcdPesLLPLFcwMb21erO1YPD.QV.FklP707/hvfRcyIRR"
          "QTF2M2GsC2QJLccZQdeDSzl9XNNRv/");
    check("$6$ rounds=1000 (spec minimum)",
          "x", "$6$rounds=1000$minrounds",
          "$6$rounds=1000$minrounds$0S4OIQDq8wIDBzTvucJkohgD79y4P17vzW7GHW33v5"
          "AfLhjx8rcWPvMay6y5x6VFfUrkFcU/RWxIBGwZcNmNL/");

    // ---- glibc-compatible quirks ----
    check("salt truncates to 16 chars (17 given)",
          "correct horse battery staple", "$6$rounds=100000$exampleSalt16char",
          "$6$rounds=100000$exampleSalt16cha$rJJvkyV/pEXwQxmLEn/N72/nUjl3PbN2D"
          "zPvrdPoiNyUUfQ21PHT/j6t643S8CdaNrAe04NZJqjHanabnKxYU.");
    check("rounds below minimum clamp, clamped value in output",
          "secret", "$6$rounds=999$clampme",
          "$6$rounds=1000$clampme$LJtSQTnZSaC74BWd.2TJdEvuyxftD3ypT5HLCVlvmzUF"
          ".sYxR0ERnXp6DVKE9EcMr.aEbYmwndFehPCygNTSm0");
    check("explicit rounds=5000 preserved in output",
          "pw", "$6$rounds=5000$explicitfive",
          "$6$rounds=5000$explicitfive$/eJNoD0.v0Lvex6BCiti2mrWURsa3WNc85C/yH3"
          "4Qj4Uj1jKrfB/cq72HDdFkNVuLDq058n6Uj6WXqUnaWxK0/");

    // ---- $5$ (verification coverage; generation is $6$-only by policy) ----
    check("$5$ oracle vector: saltstring/password",
          "password", "$5$saltstring",
          "$5$saltstring$OH4IDuTlsuTYPdED1gsuiRMyTAwNlRWyA6Xr3I4/dQ5");
    check("$5$ with rounds",
          "Hello world!", "$5$rounds=10000$saltstringsaltst",
          "$5$rounds=10000$saltstringsaltst$3xv.VbSHBb41AL9AvLeujZkZRBAwqFMz2."
          "opqey6IcA");

    // ---- verify-by-recomputation: full stored string as the setting ----
    check("round-trip: stored $6$ string re-verifies byte-identically",
          "password",
          "$6$saltstring$adDbXsJjcDlq2662QPgd.tkSOVmnG9Tt3oXl4HR60SusC3AGjirnDe"
          "nVZp3DGwLwqy6iYKCzannhaX9DR72nN1",
          "$6$saltstring$adDbXsJjcDlq2662QPgd.tkSOVmnG9Tt3oXl4HR60SusC3AGjirnDe"
          "nVZp3DGwLwqy6iYKCzannhaX9DR72nN1");
    {
        const UTF8 *got = mux_sha_crypt(reinterpret_cast<const UTF8 *>("passwerd"),
                                        reinterpret_cast<const UTF8 *>("$6$saltstring"));
        ++g_tests;
        bool pass = (nullptr != got
                  && 0 != strcmp(reinterpret_cast<const char *>(got),
                         "$6$saltstring$adDbXsJjcDlq2662QPgd.tkSOVmnG9Tt3oXl4HR"
                         "60SusC3AGjirnDenVZp3DGwLwqy6iYKCzannhaX9DR72nN1"));
        printf("%s %d - wrong password does not match stored hash\n",
               pass ? "ok" : "not ok", g_tests);
        if (!pass)
        {
            ++g_failures;
        }
    }

    // ---- malformed settings ----
    check("rejects unknown id $7$", "pw", "$7$salt", nullptr);
    check("rejects non-modular setting", "pw", "XXsalt", nullptr);
    check("rejects truncated prefix", "pw", "$6", nullptr);

    printf("1..%d\n", g_tests);
    if (g_failures)
    {
        printf("# FAILED: %d of %d\n", g_failures, g_tests);
        return 1;
    }
    printf("# all %d sha-crypt KATs passed\n", g_tests);
    return 0;
}
