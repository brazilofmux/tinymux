// Unit tests for utf8_normalize_nfc — Unicode NFC normalization.
//
// There was previously NO test for normalization anywhere in tests/.  The
// combining-character smoke cases (graphemes, comp, chr) reach this code, but
// only incidentally: nothing asserts that a decomposed sequence composes, that
// combining marks are reordered by canonical combining class, or that the
// result is idempotent.  #1998 moved this routine's 512 KB code-point table
// off the stack and had to be reviewed against that incidental coverage, which
// is what prompted this harness.
//
// The assertions come from two sources, kept separate on purpose:
//
//   - Unicode-mandated results (composition of a base plus a combining mark,
//     canonical ordering by CCC, Hangul composition).  These are properties of
//     the standard, so they are correct expectations independent of what this
//     implementation currently does.
//
//   - Characterization of this implementation's choices where the standard
//     does not dictate one — chiefly what happens to malformed UTF-8 and to
//     output that does not fit nDstMax.  These lock current behaviour so a
//     change is visible; they are not claims that the behaviour is required.
//
// Links against libmux, which supplies utf8_normalize_nfc.
// Needs a built tree:  make install   (from the repo root)
//
// Build: make
// Run:   make test

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

#include "autoconf.h"
#include "config.h"

typedef unsigned char TestUTF8;

extern void utf8_normalize_nfc(const TestUTF8 *src, size_t nSrc, TestUTF8 *dst,
                               size_t nDstMax, size_t *pnDst);

// --- tiny test framework ---------------------------------------------------
static int g_pass = 0;
static int g_fail = 0;

static std::string hex(const std::string &s)
{
    std::string r;
    char b[4];
    for (size_t i = 0; i < s.size(); i++)
    {
        snprintf(b, sizeof(b), "%02x", static_cast<unsigned char>(s[i]));
        r += b;
    }
    return r;
}

// Normalize into a generously-sized buffer and return the result.
static std::string nfc(const std::string &in)
{
    std::string out(in.size() * 4 + 64, '\0');
    size_t n = 0;
    utf8_normalize_nfc(reinterpret_cast<const TestUTF8 *>(in.data()), in.size(),
                       reinterpret_cast<TestUTF8 *>(&out[0]), out.size(), &n);
    out.resize(n);
    return out;
}

static void expect_eq(const char *what, const std::string &got,
                      const std::string &want)
{
    if (got == want)
    {
        g_pass++;
    }
    else
    {
        g_fail++;
        printf("FAIL %s\n     got  %s\n     want %s\n",
               what, hex(got).c_str(), hex(want).c_str());
    }
}

static void expect_true(const char *what, bool b)
{
    if (b)
    {
        g_pass++;
    }
    else
    {
        g_fail++;
        printf("FAIL %s\n", what);
    }
}

// --- corpus ----------------------------------------------------------------
// Named so a failure report says which sequence broke.

static const std::string A            = "A";
static const std::string COMB_RING    = "\xCC\x8A";      // U+030A, ccc 230
static const std::string COMB_ACUTE   = "\xCC\x81";      // U+0301, ccc 230
static const std::string COMB_DIAER   = "\xCC\x88";      // U+0308, ccc 230
static const std::string COMB_DOTBLW  = "\xCC\xA3";      // U+0323, ccc 220
static const std::string A_RING       = "\xC3\x85";      // U+00C5
static const std::string E_ACUTE      = "\xC3\xA9";      // U+00E9
static const std::string O_DIAER      = "\xC3\xB6";      // U+00F6

static std::string rep(const std::string &s, int k)
{
    std::string r;
    for (int i = 0; i < k; i++)
    {
        r += s;
    }
    return r;
}

// --- Unicode-mandated behaviour --------------------------------------------

static void test_composition(void)
{
    // Base + combining mark composes to the precomposed character.
    expect_eq("A + COMBINING RING ABOVE -> U+00C5", nfc(A + COMB_RING), A_RING);
    expect_eq("e + COMBINING ACUTE -> U+00E9",      nfc(std::string("e") + COMB_ACUTE), E_ACUTE);

    // Already composed: unchanged.
    expect_eq("U+00C5 unchanged", nfc(A_RING), A_RING);
    expect_eq("U+00E9 unchanged", nfc(E_ACUTE), E_ACUTE);

    // Only the first mark composes; the second has no precomposed form with
    // the result, so it stays a combining mark.
    expect_eq("o + diaeresis + acute", nfc(std::string("o") + COMB_DIAER + COMB_ACUTE),
              O_DIAER + COMB_ACUTE);

    // Hangul: conjoining jamo L+V compose algorithmically.
    // U+1100 (G) + U+1161 (A) -> U+AC00 (GA).
    expect_eq("Hangul jamo L+V -> U+AC00",
              nfc("\xE1\x84\x80\xE1\x85\xA1"), std::string("\xEA\xB0\x80"));

    // ASCII and the empty string pass through untouched.
    expect_eq("ascii pass-through", nfc("hello world"), "hello world");
    expect_eq("empty",              nfc(""),            "");
}

static void test_canonical_ordering(void)
{
    // Marks are sorted by combining class: dot-below (ccc 220) must precede
    // acute (ccc 230) regardless of input order, and the two orderings must
    // therefore normalize to the same string.  This is the property that
    // makes NFC a normal form, so it is worth asserting directly rather than
    // trusting that composition happened to work.
    const std::string in1 = std::string("q") + COMB_ACUTE + COMB_DOTBLW;   // 230 then 220
    const std::string in2 = std::string("q") + COMB_DOTBLW + COMB_ACUTE;   // already ordered
    expect_eq("ccc reorder: acute+dotbelow -> dotbelow+acute",
              nfc(in1), std::string("q") + COMB_DOTBLW + COMB_ACUTE);
    expect_eq("ccc ordering is stable for already-ordered input",
              nfc(in2), std::string("q") + COMB_DOTBLW + COMB_ACUTE);
    expect_true("both mark orders converge on one normal form",
                nfc(in1) == nfc(in2));

    // Equal combining classes must NOT be reordered relative to each other:
    // diaeresis and acute are both ccc 230, so the two inputs stay distinct.
    expect_true("equal-ccc marks keep their relative order",
                nfc(std::string("o") + COMB_DIAER + COMB_ACUTE)
                != nfc(std::string("o") + COMB_ACUTE + COMB_DIAER));
}

static void test_idempotence(void)
{
    // NFC(NFC(x)) == NFC(x) for every input.  This is the defining property
    // of a normal form and catches a broad class of regressions -- a partial
    // compose, a mis-sorted mark, a dropped code point -- without needing a
    // hand-computed expectation for each case.
    const std::string cases[] = {
        "", "hello world", A + COMB_RING, std::string("e") + COMB_ACUTE,
        std::string("o") + COMB_DIAER + COMB_ACUTE, std::string("q") + COMB_ACUTE + COMB_DOTBLW,
        A_RING, E_ACUTE, "\xE1\x84\x80\xE1\x85\xA1",
        COMB_ACUTE + COMB_ACUTE + "q",          // leading marks, no starter
        std::string("caf") + E_ACUTE + " vs cafe" + COMB_ACUTE,
        rep(std::string("o") + COMB_DIAER + COMB_ACUTE, 50),
        rep(std::string("e") + COMB_ACUTE, 2000),
        rep("a", 8000),
        rep(std::string("a") + E_ACUTE + "b", 1000),
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        char what[96];
        snprintf(what, sizeof(what), "idempotent [%zu] (%zu bytes)",
                 i, cases[i].size());
        const std::string once = nfc(cases[i]);
        expect_eq(what, nfc(once), once);
    }
}

static void test_bulk(void)
{
    // The slow path over a long run: 2000 decomposed sequences must each
    // compose, so the result is exactly 2000 two-byte characters.
    expect_eq("2000x (e + acute) all compose",
              nfc(rep(std::string("e") + COMB_ACUTE, 2000)), rep(E_ACUTE, 2000));

    // A long pure-ASCII string takes the already-NFC fast path unchanged.
    expect_eq("8000x ascii unchanged", nfc(rep("a", 8000)), rep("a", 8000));

    // Interleaved starters and decomposed sequences.
    expect_eq("1000x (a + e-acute + b)",
              nfc(rep(std::string("ae") + COMB_ACUTE + "b", 1000)),
              rep(std::string("a") + E_ACUTE + "b", 1000));
}

// --- characterization of this implementation -------------------------------
// Not standard-mandated; these lock current choices so a change is visible.

static void test_characterization(void)
{
    // Malformed UTF-8: the decoder reports UNI_EOF for the bad bytes and the
    // slow path skips them, so the surrounding text survives and the invalid
    // bytes are dropped rather than passed through or replaced.
    expect_eq("invalid UTF-8 bytes are dropped",
              nfc(std::string("ab\xFF\xFE" "cd", 6)), "abcd");

    // A combining mark with no preceding starter has nothing to compose with
    // and is preserved in place.
    expect_eq("leading marks without a starter are preserved",
              nfc(COMB_ACUTE + COMB_ACUTE + "q"),
              COMB_ACUTE + COMB_ACUTE + "q");

    // Control bytes are not special to normalization.
    expect_eq("control byte passes through", nfc(std::string("a\x01" "b", 3)),
              std::string("a\x01" "b", 3));

    // Output bound.  The two paths differ and both are intentional:
    //
    //   already-NFC input takes the fast path, which is a truncating memcpy
    //   and can cut mid-character;
    //
    //   input needing composition takes the slow path, which emits only whole
    //   characters that fit and skips the rest, so it never cuts mid-character
    //   but can drop from the middle.
    {
        TestUTF8 buf[8];
        size_t   n = 0;
        const std::string ascii = "abcdefghij";
        utf8_normalize_nfc(reinterpret_cast<const TestUTF8 *>(ascii.data()),
                           ascii.size(), buf, sizeof(buf), &n);
        expect_true("fast path fills the buffer exactly", 8 == n);

        const std::string decomposed = rep(std::string("e") + COMB_ACUTE, 10);  // -> 10x 2-byte
        n = 0;
        utf8_normalize_nfc(reinterpret_cast<const TestUTF8 *>(decomposed.data()),
                           decomposed.size(), buf, 5, &n);
        expect_true("slow path emits only whole characters within the bound",
                    n <= 5 && 0 == (n % 2));
    }

    // Never writes past the reported length, and never reports more than the
    // bound it was given.
    {
        TestUTF8 buf[64];
        memset(buf, 0xAA, sizeof(buf));
        size_t n = 0;
        const std::string in = std::string("e") + COMB_ACUTE;
        utf8_normalize_nfc(reinterpret_cast<const TestUTF8 *>(in.data()),
                           in.size(), buf, 16, &n);
        expect_true("respects nDstMax", n <= 16);
        expect_true("does not write past the reported length",
                    0xAA == buf[n]);
    }
}

int main(void)
{
    printf("=== utf8_normalize_nfc tests ===\n");
    test_composition();
    test_canonical_ordering();
    test_idempotence();
    test_bulk();
    test_characterization();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return (0 == g_fail) ? EXIT_SUCCESS : EXIT_FAILURE;
}
