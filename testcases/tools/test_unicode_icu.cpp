/*
 * test_unicode_icu.cpp — Unicode correctness tests against ICU reference.
 *
 * Tests NFC normalization and DUCET collation by comparing libmux
 * results against ICU.  Run after building libmux.so:
 *
 *   make test_unicode_icu
 *   ./test_unicode_icu
 *
 * Requires: libmux.so, ICU development libraries (libicu-dev).
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>

// libmux headers
extern "C" {
#include "copyright.h"
}
#include "autoconf.h"
#include "config.h"
#include "core.h"
#include "utf8tables.h"
#include "stringutil.h"

// ICU headers
#include <unicode/ucol.h>
#include <unicode/unorm2.h>
#include <unicode/ustring.h>
#include <unicode/utypes.h>

static int g_pass = 0, g_fail = 0;

static int sign(int x) { return (x > 0) - (x < 0); }

// ---------------------------------------------------------------------------
// NFC tests
// ---------------------------------------------------------------------------

static void test_nfc(const char *label, const char *input,
                     const UNormalizer2 *norm)
{
    size_t inLen = strlen(input);

    // libmux
    UTF8 dst[8192];
    size_t nDst;
    utf8_normalize_nfc(reinterpret_cast<const UTF8 *>(input), inLen,
                       dst, sizeof(dst), &nDst);

    // ICU reference
    UErrorCode err = U_ZERO_ERROR;
    UChar usrc[8192], udst[8192];
    int32_t usrcLen, udstLen;
    u_strFromUTF8(usrc, 8192, &usrcLen, input, static_cast<int32_t>(inLen), &err);
    udstLen = unorm2_normalize(norm, usrc, usrcLen, udst, 8192, &err);
    char icu_utf8[8192];
    int32_t icu_utf8_len;
    u_strToUTF8(icu_utf8, 8192, &icu_utf8_len, udst, udstLen, &err);

    if (static_cast<size_t>(icu_utf8_len) != nDst ||
        memcmp(dst, icu_utf8, nDst) != 0)
    {
        printf("  FAIL nfc %s: libmux=%zu bytes, ICU=%d bytes\n",
               label, nDst, icu_utf8_len);
        printf("    libmux: ");
        for (size_t i = 0; i < nDst && i < 20; i++)
            printf("%02x ", dst[i]);
        printf("\n    ICU:    ");
        for (int i = 0; i < icu_utf8_len && i < 20; i++)
            printf("%02x ", static_cast<unsigned char>(icu_utf8[i]));
        printf("\n");
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_is_nfc(const char *label, const char *input,
                         const UNormalizer2 *norm)
{
    size_t inLen = strlen(input);
    bool libutf_r = utf8_is_nfc(reinterpret_cast<const UTF8 *>(input), inLen);

    UErrorCode err = U_ZERO_ERROR;
    UChar usrc[8192];
    int32_t usrcLen;
    u_strFromUTF8(usrc, 8192, &usrcLen, input, static_cast<int32_t>(inLen), &err);
    UBool icu_r = unorm2_isNormalized(norm, usrc, usrcLen, &err);

    if (libutf_r != static_cast<bool>(icu_r)) {
        printf("  FAIL is_nfc %s: libmux=%d ICU=%d\n",
               label, libutf_r ? 1 : 0, static_cast<int>(icu_r));
        g_fail++;
    } else {
        g_pass++;
    }
}

// ---------------------------------------------------------------------------
// Collation tests
// ---------------------------------------------------------------------------

static void test_cmp(const char *label,
                     const char *a, const char *b,
                     UCollator *coll)
{
    int lr = mux_collate_cmp(reinterpret_cast<const UTF8 *>(a), strlen(a),
                              reinterpret_cast<const UTF8 *>(b), strlen(b));
    UErrorCode err = U_ZERO_ERROR;
    UChar ua[512], ub[512];
    int32_t ual, ubl;
    u_strFromUTF8(ua, 512, &ual, a, static_cast<int32_t>(strlen(a)), &err);
    u_strFromUTF8(ub, 512, &ubl, b, static_cast<int32_t>(strlen(b)), &err);
    UCollationResult ir = ucol_strcoll(coll, ua, ual, ub, ubl);

    int ls = sign(lr);
    int is = (ir == UCOL_LESS) ? -1 : (ir == UCOL_GREATER) ? 1 : 0;

    if (ls != is) {
        printf("  FAIL cmp %s: \"%s\" vs \"%s\": libmux=%d ICU=%d\n",
               label, a, b, ls, is);
        g_fail++;
    } else {
        g_pass++;
    }
}

static void test_sortkey_consistency(const char *label,
                                      const char *a, const char *b)
{
    UTF8 ka[1024], kb[1024];
    size_t kla = mux_collate_sortkey(
        reinterpret_cast<const UTF8 *>(a), strlen(a), ka, sizeof(ka));
    size_t klb = mux_collate_sortkey(
        reinterpret_cast<const UTF8 *>(b), strlen(b), kb, sizeof(kb));

    int key_cmp = memcmp(ka, kb, (kla < klb) ? kla : klb);
    if (0 == key_cmp) key_cmp = (kla > klb) - (kla < klb);
    int key_sign = sign(key_cmp);

    int cmp_sign = sign(mux_collate_cmp(
        reinterpret_cast<const UTF8 *>(a), strlen(a),
        reinterpret_cast<const UTF8 *>(b), strlen(b)));

    if (key_sign != cmp_sign) {
        printf("  FAIL sortkey %s: cmp=%d sortkey=%d\n",
               label, cmp_sign, key_sign);
        g_fail++;
    } else {
        g_pass++;
    }
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void)
{
    UErrorCode err = U_ZERO_ERROR;
    const UNormalizer2 *norm = unorm2_getNFCInstance(&err);
    if (U_FAILURE(err)) {
        fprintf(stderr, "ICU NFC error: %s\n", u_errorName(err));
        return 1;
    }

    UCollator *coll = ucol_open("", &err);
    if (U_FAILURE(err)) {
        fprintf(stderr, "ICU collator error: %s\n", u_errorName(err));
        return 1;
    }

    // ----- NFC normalization -----
    printf("[nfc_normalize]\n");

    test_nfc("empty", "", norm);
    test_nfc("ascii", "Hello, World!", norm);
    test_nfc("precomposed_latin", "\xc3\xa9\xc3\xa8\xc3\xaa\xc3\xab", norm);
    test_nfc("cjk", "\xe4\xb8\xad\xe6\x96\x87\xe6\xb5\x8b\xe8\xaf\x95", norm);
    test_nfc("hangul_precomposed", "\xed\x95\x9c\xea\xb5\xad\xec\x96\xb4", norm);
    test_nfc("combining_acute", "caf\x65\xcc\x81", norm);
    test_nfc("combining_resume", "r\x65\xcc\x81sum\x65\xcc\x81", norm);
    test_nfc("angstrom_mixed", "\x41\xcc\x8a\x6e\x67\x73\x74\x72\xc3\xb6\x6d", norm);
    test_nfc("a_cedilla_acute", "a\xcc\xa7\xcc\x81", norm);
    test_nfc("a_acute_cedilla", "a\xcc\x81\xcc\xa7", norm);
    test_nfc("below_above", "a\xcc\xa3\xcc\x81", norm);
    test_nfc("above_below_reorder", "a\xcc\x81\xcc\xa3", norm);
    test_nfc("same_ccc", "a\xcc\x88\xcc\x81", norm);
    test_nfc("hangul_lv", "\xe1\x84\x80\xe1\x85\xa1", norm);
    test_nfc("hangul_lvt", "\xe1\x84\x80\xe1\x85\xa1\xe1\x86\xa8", norm);
    test_nfc("hangul_lv_plus_t", "\xea\xb0\x80\xe1\x86\xa8", norm);
    test_nfc("ohm_sign", "\xe2\x84\xa6", norm);
    test_nfc("angstrom_sign", "\xe2\x84\xab", norm);

    printf("\n[nfc_is_nfc]\n");

    test_is_nfc("ascii", "Hello!", norm);
    test_is_nfc("precomposed", "\xc3\xa9\xc3\xa8", norm);
    test_is_nfc("combining_not_nfc", "e\xcc\x81", norm);
    test_is_nfc("hangul_precomposed", "\xed\x95\x9c", norm);
    test_is_nfc("hangul_jamo", "\xe1\x84\x80\xe1\x85\xa1", norm);
    test_is_nfc("ohm_sign", "\xe2\x84\xa6", norm);
    test_is_nfc("cjk", "\xe4\xb8\xad\xe6\x96\x87", norm);

    // ----- Collation -----
    printf("\n[collate_cmp vs ICU]\n");

    test_cmp("a_vs_b", "a", "b", coll);
    test_cmp("z_vs_a", "z", "a", coll);
    test_cmp("same", "abc", "abc", coll);
    test_cmp("prefix", "abc", "abcd", coll);
    test_cmp("empty_vs_a", "", "a", coll);
    test_cmp("empty_vs_empty", "", "", coll);
    test_cmp("case_Aa", "A", "a", coll);
    test_cmp("case_aA", "a", "A", coll);
    test_cmp("case_Hello", "Hello", "hello", coll);
    test_cmp("accent_e_eacute", "e", "\xc3\xa9", coll);
    test_cmp("accent_cafe", "cafe", "caf\xc3\xa9", coll);
    test_cmp("decomp_vs_precomp", "caf\x65\xcc\x81", "caf\xc3\xa9", coll);
    test_cmp("strasse", "stra\xc3\x9f""e", "strasse", coll);
    test_cmp("cjk", "\xe4\xb8\xad\xe6\x96\x87", "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e", coll);
    test_cmp("hangul", "\xed\x95\x9c\xea\xb5\xad", "\xec\xa4\x91\xea\xb5\xad", coll);
    test_cmp("latin_vs_cyrillic", "a", "\xd0\xb0", coll);
    test_cmp("latin_vs_greek", "a", "\xce\xb1", coll);
    test_cmp("combining_order", "a\xcc\xa3\xcc\x81", "a\xcc\x81\xcc\xa3", coll);
    test_cmp("num_1_2", "1", "2", coll);
    test_cmp("num_9_10", "9", "10", coll);

    // ----- Sortkey consistency -----
    printf("\n[sortkey consistency]\n");

    test_sortkey_consistency("sk_a_b", "a", "b");
    test_sortkey_consistency("sk_case", "A", "a");
    test_sortkey_consistency("sk_accent", "e", "\xc3\xa9");
    test_sortkey_consistency("sk_prefix", "abc", "abcd");
    test_sortkey_consistency("sk_same", "abc", "abc");
    test_sortkey_consistency("sk_hangul", "\xed\x95\x9c\xea\xb5\xad", "\xec\xa4\x91\xea\xb5\xad");

    ucol_close(coll);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
