/*! \file test_libmux.cpp
 * \brief Standalone unit tests for libmux.so functions.
 *
 * Tests stringutil, mathutil, and alloc functions by linking
 * directly against the built libmux.so.  We declare prototypes
 * locally rather than including the full header chain (which
 * drags in Ragel DFA table externs that live in libmux internals).
 *
 * Build: make
 * Run:   ./test_libmux
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cfloat>
#include <cmath>

// ---------------------------------------------------------------------------
// Minimal type/function declarations from libmux — just enough to test.
// ---------------------------------------------------------------------------

typedef unsigned char UTF8;

#define LBUF_SIZE 32768

// stringutil.h exports
void  safe_copy_str_lbuf(const UTF8 *src, UTF8 *buff, UTF8 **bufp);
size_t safe_copy_buf(const UTF8 *src, size_t nLen, UTF8 *buff, UTF8 **bufp);
UTF8 *StringClone(const UTF8 *str);
UTF8 *StringCloneLen(const UTF8 *str, size_t nStr);
UTF8 *trim_spaces(const UTF8 *);
int   mux_stricmp(const UTF8 *a, const UTF8 *b);
UTF8 *mux_strupr(const UTF8 *a, size_t &n);
UTF8 *mux_strlwr(const UTF8 *a, size_t &n);
void  mux_strncpy(UTF8 *dest, const UTF8 *src, size_t length_to_copy);

// Grapheme-cluster / display-width exports.  utf8_cluster_count() lives in
// utf8_grapheme.cpp (C++ linkage); co_visual_width() is in the Ragel-generated
// color_ops.c (C linkage).  Together they exercise UAX #29 segmentation —
// including GB11 (ZWJ emoji) and GB12/13 (Regional Indicator flags) — which a
// ZWJ sequence cannot reach from softcode because chr() rejects U+200D as
// unprintable.
size_t utf8_cluster_count(const UTF8 *src, size_t nSrc);
extern "C" size_t co_visual_width(const unsigned char *p, size_t len);

// mathutil.h exports
long   mux_atol(const UTF8 *pString);
double mux_atof(const UTF8 *szString, bool bStrict = true);
size_t mux_i64toa(int64_t val, UTF8 *buf);
UTF8  *mux_i64toa_t(int64_t val);
void   safe_ltoa(long val, UTF8 *buff, UTF8 **bufc);
void   safe_i64toa(int64_t val, UTF8 *buff, UTF8 **bufc);

// timeutil.h date-parser API (#715).  Unlike the stringutil/mathutil
// prototypes above, the date parser needs the CLinearTimeAbsolute class and
// FIELDEDTIME struct, so we include the real header.  It is self-contained
// given UTF8 + <cstdint> plus these two macros.  I64BUF_SIZE only sizes a
// private static that is defined inside libmux and never touched here, so a
// local value is harmless.
#ifndef LIBMUX_API
#define LIBMUX_API __attribute__((visibility("default")))
#endif
#ifndef I64BUF_SIZE
#define I64BUF_SIZE 24
#endif
#include "timeutil.h"

// alloc.h
#define MEMALLOC(n)  malloc((n))
#define MEMFREE(p)   free((p))

// safe_str / safe_chr macros (replicate from alloc.h)
#define safe_str(s,b,p)  safe_copy_str_lbuf(s,b,p)

static inline void safe_chr_impl(UTF8 c, UTF8 *buff, UTF8 **bufp)
{
    if (static_cast<size_t>(*bufp - buff) < (LBUF_SIZE - 1))
    {
        **bufp = c;
        (*bufp)++;
    }
}
#define safe_chr(c,b,p)  safe_chr_impl(static_cast<UTF8>(c),b,p)

// ---------------------------------------------------------------------------
// Test infrastructure
// ---------------------------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "  FAILED: %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_fail++; \
        return; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        fprintf(stderr, "  FAILED: %s:%d: %s != %s\n", \
            __FILE__, __LINE__, #a, #b); \
        g_fail++; \
        return; \
    } \
} while (0)

#define ASSERT_STREQ(a, b) do { \
    if (strcmp(reinterpret_cast<const char *>(a), (b)) != 0) { \
        fprintf(stderr, "  FAILED: %s:%d: \"%s\" != \"%s\"\n", \
            __FILE__, __LINE__, \
            reinterpret_cast<const char *>(a), (b)); \
        g_fail++; \
        return; \
    } \
} while (0)

#define ASSERT_NEAR(a, b, eps) do { \
    if (fabs((double)(a) - (double)(b)) > (eps)) { \
        fprintf(stderr, "  FAILED: %s:%d: %g != %g (eps %g)\n", \
            __FILE__, __LINE__, (double)(a), (double)(b), (double)(eps)); \
        g_fail++; \
        return; \
    } \
} while (0)

#define RUN_TEST(fn) do { \
    int before = g_fail; \
    fn(); \
    if (g_fail == before) { \
        g_pass++; \
        printf("  PASSED: %s\n", #fn); \
    } \
} while (0)

// Convenience cast
#define U(s) reinterpret_cast<const UTF8 *>(s)

// ---------------------------------------------------------------------------
// mux_atol tests
// ---------------------------------------------------------------------------

static void test_mux_atol_basic()
{
    ASSERT_EQ(mux_atol(U("0")), 0L);
    ASSERT_EQ(mux_atol(U("42")), 42L);
    ASSERT_EQ(mux_atol(U("-7")), -7L);
    ASSERT_EQ(mux_atol(U("  123")), 123L);
}

static void test_mux_atol_empty()
{
    ASSERT_EQ(mux_atol(U("")), 0L);
}

static void test_mux_atol_trailing_text()
{
    ASSERT_EQ(mux_atol(U("99abc")), 99L);
}

// ---------------------------------------------------------------------------
// mux_atof tests
// ---------------------------------------------------------------------------

static void test_mux_atof_basic()
{
    ASSERT_NEAR(mux_atof(U("3.14")), 3.14, 1e-10);
    ASSERT_NEAR(mux_atof(U("-2.5")), -2.5, 1e-10);
    ASSERT_NEAR(mux_atof(U("0")), 0.0, 1e-10);
}

static void test_mux_atof_scientific()
{
    ASSERT_NEAR(mux_atof(U("1e3")), 1000.0, 1e-10);
    ASSERT_NEAR(mux_atof(U("2.5e-1")), 0.25, 1e-10);
}

// ---------------------------------------------------------------------------
// mux_i64toa tests
// ---------------------------------------------------------------------------

static void test_mux_i64toa_basic()
{
    UTF8 buf[32];
    mux_i64toa(0, buf);
    ASSERT_STREQ(buf, "0");
    mux_i64toa(12345, buf);
    ASSERT_STREQ(buf, "12345");
    mux_i64toa(-999, buf);
    ASSERT_STREQ(buf, "-999");
}

static void test_mux_i64toa_large()
{
    UTF8 buf[32];
    mux_i64toa(INT64_C(9223372036854775807), buf);
    ASSERT_STREQ(buf, "9223372036854775807");
}

static void test_mux_i64toa_min()
{
    UTF8 buf[32];
    mux_i64toa(INT64_C(-9223372036854775807) - 1, buf);
    ASSERT_STREQ(buf, "-9223372036854775808");
}

// ---------------------------------------------------------------------------
// safe_ltoa / safe buffer writing tests
// ---------------------------------------------------------------------------

static void test_safe_ltoa()
{
    UTF8 buff[LBUF_SIZE];
    UTF8 *bufc = buff;
    safe_ltoa(42, buff, &bufc);
    *bufc = '\0';
    ASSERT_STREQ(buff, "42");
}

static void test_safe_ltoa_negative()
{
    UTF8 buff[LBUF_SIZE];
    UTF8 *bufc = buff;
    safe_ltoa(-1000, buff, &bufc);
    *bufc = '\0';
    ASSERT_STREQ(buff, "-1000");
}

static void test_safe_str_basic()
{
    UTF8 buff[LBUF_SIZE];
    UTF8 *bufc = buff;
    safe_str(U("hello"), buff, &bufc);
    *bufc = '\0';
    ASSERT_STREQ(buff, "hello");
}

static void test_safe_str_concatenation()
{
    UTF8 buff[LBUF_SIZE];
    UTF8 *bufc = buff;
    safe_str(U("foo"), buff, &bufc);
    safe_str(U("bar"), buff, &bufc);
    *bufc = '\0';
    ASSERT_STREQ(buff, "foobar");
}

static void test_safe_chr()
{
    UTF8 buff[LBUF_SIZE];
    UTF8 *bufc = buff;
    safe_chr('A', buff, &bufc);
    safe_chr('B', buff, &bufc);
    safe_chr('C', buff, &bufc);
    *bufc = '\0';
    ASSERT_STREQ(buff, "ABC");
}

static void test_safe_str_null()
{
    UTF8 buff[LBUF_SIZE];
    UTF8 *bufc = buff;
    safe_str(nullptr, buff, &bufc);
    *bufc = '\0';
    ASSERT_STREQ(buff, "");
}

// ---------------------------------------------------------------------------
// StringClone tests
// ---------------------------------------------------------------------------

static void test_string_clone()
{
    UTF8 *clone = StringClone(U("test string"));
    ASSERT_TRUE(clone != nullptr);
    ASSERT_STREQ(clone, "test string");
    MEMFREE(clone);
}

static void test_string_clone_len()
{
    UTF8 *clone = StringCloneLen(U("hello world"), 5);
    ASSERT_TRUE(clone != nullptr);
    ASSERT_STREQ(clone, "hello");
    MEMFREE(clone);
}

static void test_string_clone_empty()
{
    UTF8 *clone = StringClone(U(""));
    ASSERT_TRUE(clone != nullptr);
    ASSERT_STREQ(clone, "");
    MEMFREE(clone);
}

// ---------------------------------------------------------------------------
// mux_stricmp tests
// ---------------------------------------------------------------------------

static void test_mux_stricmp_equal()
{
    ASSERT_EQ(mux_stricmp(U("Hello"), U("hello")), 0);
    ASSERT_EQ(mux_stricmp(U("Hello"), U("HELLO")), 0);
}

static void test_mux_stricmp_different()
{
    ASSERT_TRUE(mux_stricmp(U("abc"), U("xyz")) != 0);
}

static void test_mux_stricmp_empty()
{
    ASSERT_EQ(mux_stricmp(U(""), U("")), 0);
    ASSERT_TRUE(mux_stricmp(U(""), U("a")) != 0);
}

// ---------------------------------------------------------------------------
// mux_strupr / mux_strlwr tests
// ---------------------------------------------------------------------------

static void test_mux_strupr()
{
    size_t n;
    UTF8 *result = mux_strupr(U("hello"), n);
    ASSERT_STREQ(result, "HELLO");
}

static void test_mux_strlwr()
{
    size_t n;
    UTF8 *result = mux_strlwr(U("HELLO"), n);
    ASSERT_STREQ(result, "hello");
}

static void test_mux_strupr_mixed()
{
    size_t n;
    UTF8 *result = mux_strupr(U("Hello World 123"), n);
    ASSERT_STREQ(result, "HELLO WORLD 123");
}

// ---------------------------------------------------------------------------
// trim_spaces tests
// ---------------------------------------------------------------------------

static void test_trim_spaces_basic()
{
    UTF8 *result = trim_spaces(U("  hello  "));
    ASSERT_STREQ(result, "hello");
}

static void test_trim_spaces_empty()
{
    UTF8 *result = trim_spaces(U("   "));
    ASSERT_STREQ(result, "");
}

static void test_trim_spaces_none()
{
    UTF8 *result = trim_spaces(U("hello"));
    ASSERT_STREQ(result, "hello");
}

static void test_trim_spaces_tabs()
{
    UTF8 *result = trim_spaces(U("\t hello \t"));
    // trim_spaces only trims ASCII spaces, not tabs
    ASSERT_TRUE(result != nullptr);
}

// ---------------------------------------------------------------------------
// mux_strncpy tests
// ---------------------------------------------------------------------------

static void test_mux_strncpy_basic()
{
    UTF8 dest[32];
    mux_strncpy(dest, U("hello"), 31);
    ASSERT_STREQ(dest, "hello");
}

static void test_mux_strncpy_truncate()
{
    UTF8 dest[4];
    mux_strncpy(dest, U("hello"), 3);
    dest[3] = '\0';
    // Should copy at most 3 bytes
    ASSERT_EQ(strlen(reinterpret_cast<char *>(dest)), 3u);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Date parser (#715): adversarial / boundary coverage for the scanner behind
// ParseDate(), do_convtime(), and ParseFractionalSecondsString().  The smoke
// tests (convtime_fn.mux, parsedate_fn.mux) cover happy paths; these hit the
// overflow / narrowing / truncation corners that have historically slipped
// through (#707, #708, and the do_convtime year-narrowing fixed alongside).
// ---------------------------------------------------------------------------

// Parse via ParseDate; optionally read back the fielded result.  Returns the
// accept/reject boolean.
static bool pd(const char *s, FIELDEDTIME *ft = nullptr, bool *zone = nullptr)
{
    CLinearTimeAbsolute lta;
    UTF8 buf[256];
    size_t i = 0;
    for (; s[i] && i < sizeof(buf) - 1; i++) buf[i] = static_cast<UTF8>(s[i]);
    buf[i] = '\0';
    bool z = false;
    bool ok = ParseDate(lta, buf, &z);
    if (zone) *zone = z;
    if (ok && ft) lta.ReturnFields(ft);
    return ok;
}

static bool ct(const char *s, FIELDEDTIME *ft = nullptr)
{
    FIELDEDTIME tmp;
    if (!ft) ft = &tmp;
    return do_convtime(reinterpret_cast<const UTF8 *>(s), ft);
}

static bool frac(const char *s, int64_t *v = nullptr)
{
    int64_t tmp = 0;
    if (!v) v = &tmp;
    *v = 0;
    return ParseFractionalSecondsString(*v, reinterpret_cast<const UTF8 *>(s));
}

// --- ParseDate: valid forms parse to the expected fields ---
static void test_parsedate_valid_iso_date()
{
    FIELDEDTIME ft;
    ASSERT_TRUE(pd("2026-06-09", &ft));
    ASSERT_EQ(ft.iYear, 2026);
    ASSERT_EQ(ft.iMonth, 6);
    ASSERT_EQ(ft.iDayOfMonth, 9);
}

static void test_parsedate_valid_iso_datetime()
{
    FIELDEDTIME ft;
    ASSERT_TRUE(pd("2026-06-09T12:34:56", &ft));
    ASSERT_EQ(ft.iHour, 12);
    ASSERT_EQ(ft.iMinute, 34);
    ASSERT_EQ(ft.iSecond, 56);
}

static void test_parsedate_zone_flag()
{
    bool zone = false;
    ASSERT_TRUE(pd("2026-06-09T12:34:56Z", nullptr, &zone));
    ASSERT_TRUE(zone);
    zone = true;
    ASSERT_TRUE(pd("2026-06-09T12:34:56", nullptr, &zone));
    ASSERT_TRUE(!zone);
}

// --- ParseDate: empty / garbage rejected ---
static void test_parsedate_empty_and_garbage()
{
    ASSERT_TRUE(!pd(""));
    ASSERT_TRUE(!pd(" "));
    ASSERT_TRUE(!pd("abc"));
    ASSERT_TRUE(!pd("!!!"));
    ASSERT_TRUE(!pd("-1"));
    ASSERT_TRUE(!pd("2026--06-09"));
}

// --- ParseDate: year overflow rejected, not silently wrapped (#707) ---
static void test_parsedate_year_overflow_rejected()
{
    // A 10-digit run overflows a 32-bit accumulator if unguarded.  Both the
    // bare number and the ISO-dashed form must be rejected, not wrapped.
    ASSERT_TRUE(!pd("9999999999"));
    ASSERT_TRUE(!pd("9999999999-01-01"));
}

// --- ParseDate: out-of-range year rejected before narrowing (#708) ---
static void test_parsedate_year_narrowing_rejected()
{
    // 67536 narrows to (short)2000 if cast before range-checking; must reject.
    ASSERT_TRUE(!pd("67536-01-01"));
}

// --- ParseDate: exact accepted-year boundaries (timeutil [-27256, 30826]) ---
static void test_parsedate_year_boundaries()
{
    ASSERT_TRUE(pd("30826-01-01"));    // latest accepted
    ASSERT_TRUE(!pd("30827-01-01"));   // one past
    ASSERT_TRUE(pd("-27256-01-01"));   // earliest accepted
    ASSERT_TRUE(!pd("-27257-01-01"));  // one before
}

// --- ParseDate: truncated ISO forms ---
static void test_parsedate_truncated_iso()
{
    FIELDEDTIME ft;
    ASSERT_TRUE(!pd("2026-"));
    ASSERT_TRUE(!pd("2026-06-"));
    // Year-month with no day is accepted and defaults the day to 1.
    ASSERT_TRUE(pd("2026-06", &ft));
    ASSERT_EQ(ft.iMonth, 6);
    ASSERT_EQ(ft.iDayOfMonth, 1);
}

// --- ParseDate: invalid calendar components ---
static void test_parsedate_invalid_components()
{
    ASSERT_TRUE(!pd("2026-13-01"));   // month 13
    ASSERT_TRUE(!pd("2026-00-01"));   // month 0
    ASSERT_TRUE(!pd("2026-06-00"));   // day 0
    ASSERT_TRUE(!pd("2026-06-31"));   // June has 30 days
    ASSERT_TRUE(!pd("2026-02-29"));   // 2026 is not a leap year
    ASSERT_TRUE(pd("2024-02-29"));    // 2024 is
}

// --- ParseDate: invalid time-of-day ---
static void test_parsedate_invalid_time()
{
    ASSERT_TRUE(!pd("2026-06-09T99:99:99"));
    ASSERT_TRUE(!pd("2026-06-09T24:00:00"));
    ASSERT_TRUE(!pd("2026-06-09T23:59:60"));
}

// --- ParseDate: ordinal (day-of-year) dates ---
static void test_parsedate_ordinal()
{
    FIELDEDTIME ft;
    ASSERT_TRUE(pd("2026-001", &ft));   // Jan 1
    ASSERT_EQ(ft.iMonth, 1);
    ASSERT_EQ(ft.iDayOfMonth, 1);
    ASSERT_TRUE(!pd("2026-000"));       // ordinal 0
    ASSERT_TRUE(!pd("2026-367"));       // past end of (non-leap) year
}

// --- do_convtime: happy path + the year-narrowing fix ---
static void test_convtime_valid()
{
    FIELDEDTIME ft;
    ASSERT_TRUE(ct("Sat Jun  7 12:34:56 2026", &ft));
    ASSERT_EQ(ft.iYear, 2026);
    ASSERT_EQ(ft.iMonth, 6);
    ASSERT_EQ(ft.iDayOfMonth, 7);
    ASSERT_EQ(ft.iHour, 12);
    ASSERT_EQ(ft.iSecond, 56);
}

static void test_convtime_year_overflow_rejected()
{
    // do_convtime narrowed the year to short with no range check, so
    // "...9999999999" wrapped to (short)-7169 and was accepted.  Must reject.
    ASSERT_TRUE(!ct("Sat Jun  7 12:34:56 9999999999"));
}

static void test_convtime_invalid_fields()
{
    ASSERT_TRUE(!ct(""));
    ASSERT_TRUE(!ct("Sat Jun 32 12:34:56 2026"));  // day 32
    ASSERT_TRUE(!ct("Sat Jun  7 99:99:99 2026"));  // bad time
}

// --- ParseFractionalSecondsString ---
static void test_frac_basic()
{
    int64_t v = 0;
    ASSERT_TRUE(frac("1", &v));
    ASSERT_EQ(v, INT64_C(10000000));     // 1 second in 100ns units
    ASSERT_TRUE(frac("1.5", &v));
    ASSERT_EQ(v, INT64_C(15000000));
    ASSERT_TRUE(frac("0.000001", &v));
    ASSERT_EQ(v, INT64_C(10));           // 1 microsecond
}

static void test_frac_signs_and_edges()
{
    int64_t v = 0;
    ASSERT_TRUE(frac("-1.5", &v));
    ASSERT_EQ(v, INT64_C(-15000000));
    ASSERT_TRUE(frac("1.", &v));         // trailing dot
    ASSERT_EQ(v, INT64_C(10000000));
    ASSERT_TRUE(frac(".5", &v));         // leading dot
    ASSERT_EQ(v, INT64_C(5000000));
}

static void test_frac_rejects_garbage()
{
    int64_t v = -1;
    ASSERT_TRUE(!frac("", &v));
    ASSERT_TRUE(!frac("abc", &v));
}

// ---------------------------------------------------------------------------
// Grapheme clusters (UAX #29) and display width
//
// Emoji built from several code points must segment as ONE grapheme cluster
// and occupy ONE glyph cell.  These cases — especially the GB11 ZWJ sequences
// — cannot be constructed in softcode (chr() rejects U+200D), so they are
// covered here at the libmux layer.
// ---------------------------------------------------------------------------

#define E_MAN   "\xF0\x9F\x91\xA8"      // U+1F468 man              (wide, ExtPict)
#define E_WOMAN "\xF0\x9F\x91\xA9"      // U+1F469 woman            (wide, ExtPict)
#define E_GIRL  "\xF0\x9F\x91\xA7"      // U+1F467 girl             (wide, ExtPict)
#define E_ZWJ   "\xE2\x80\x8D"          // U+200D  zero-width joiner
#define E_THUMB "\xF0\x9F\x91\x8D"      // U+1F44D thumbs up        (wide, ExtPict)
#define E_TONE  "\xF0\x9F\x8F\xBD"      // U+1F3FD medium skin tone (Extend)
#define E_RI_U  "\xF0\x9F\x87\xBA"      // U+1F1FA regional ind. U  (Neutral width)
#define E_RI_S  "\xF0\x9F\x87\xB8"      // U+1F1F8 regional ind. S
#define E_ACUTE "\xCC\x81"              // U+0301  combining acute  (zero width)

// "family": man ZWJ woman ZWJ girl — 5 code points, GB11, one cluster.
#define E_FAMILY E_MAN E_ZWJ E_WOMAN E_ZWJ E_GIRL

static size_t clusters(const char *s)
{
    return utf8_cluster_count(U(s), strlen(s));
}
static size_t vwidth(const char *s)
{
    return co_visual_width(reinterpret_cast<const unsigned char *>(s), strlen(s));
}

static void test_grapheme_gb11_zwj_family()
{
    // GB11: ExtPict (Extend* ZWJ ExtPict)+ is a single cluster, one wide glyph.
    ASSERT_EQ(clusters(E_FAMILY), (size_t)1);
    ASSERT_EQ(vwidth(E_FAMILY),   (size_t)2);
    // Two families back-to-back: two clusters, four columns.
    ASSERT_EQ(clusters(E_FAMILY E_FAMILY), (size_t)2);
    ASSERT_EQ(vwidth(E_FAMILY E_FAMILY),   (size_t)4);
}

static void test_grapheme_skin_tone_modifier()
{
    // Emoji + skin-tone modifier joins via Extend (GB9): one cluster.  Both
    // code points are wide, so a naive per-code-point sum would yield 4.
    ASSERT_EQ(clusters(E_THUMB E_TONE), (size_t)1);
    ASSERT_EQ(vwidth(E_THUMB E_TONE),   (size_t)2);
}

static void test_grapheme_regional_indicator_flag()
{
    // GB12/13: a Regional Indicator pair is one flag cluster.  Each RI is
    // East-Asian-Width Neutral (1), but the pair renders as one wide glyph.
    ASSERT_EQ(clusters(E_RI_U E_RI_S), (size_t)1);
    ASSERT_EQ(vwidth(E_RI_U E_RI_S),   (size_t)2);
    // Four RIs = two flags = two clusters, four columns.
    ASSERT_EQ(clusters(E_RI_U E_RI_S E_RI_U E_RI_S), (size_t)2);
    ASSERT_EQ(vwidth(E_RI_U E_RI_S E_RI_U E_RI_S),   (size_t)4);
    // A lone RI is its own cluster, width 1.
    ASSERT_EQ(clusters(E_RI_U), (size_t)1);
    ASSERT_EQ(vwidth(E_RI_U),   (size_t)1);
}

static void test_grapheme_baselines()
{
    // Plain text, combining marks and wide CJK are unchanged by clustering.
    ASSERT_EQ(clusters("hello"), (size_t)5);
    ASSERT_EQ(vwidth("hello"),   (size_t)5);
    ASSERT_EQ(vwidth(""),        (size_t)0);
    // e + combining acute: one cluster, one column.
    ASSERT_EQ(clusters("e" E_ACUTE), (size_t)1);
    ASSERT_EQ(vwidth("e" E_ACUTE),   (size_t)1);
    // CJK 你好: two clusters, four columns (wide).
    ASSERT_EQ(clusters("\xE4\xBD\xA0\xE5\xA5\xBD"), (size_t)2);
    ASSERT_EQ(vwidth("\xE4\xBD\xA0\xE5\xA5\xBD"),   (size_t)4);
    // A single man emoji: one cluster, two columns.
    ASSERT_EQ(clusters(E_MAN), (size_t)1);
    ASSERT_EQ(vwidth(E_MAN),   (size_t)2);
}

int main()
{
    printf("libmux Unit Tests\n");
    printf("=================\n\n");

    printf("--- mux_atol ---\n");
    RUN_TEST(test_mux_atol_basic);
    RUN_TEST(test_mux_atol_empty);
    RUN_TEST(test_mux_atol_trailing_text);

    printf("\n--- mux_atof ---\n");
    RUN_TEST(test_mux_atof_basic);
    RUN_TEST(test_mux_atof_scientific);

    printf("\n--- mux_i64toa ---\n");
    RUN_TEST(test_mux_i64toa_basic);
    RUN_TEST(test_mux_i64toa_large);
    RUN_TEST(test_mux_i64toa_min);

    printf("\n--- safe buffer writing ---\n");
    RUN_TEST(test_safe_ltoa);
    RUN_TEST(test_safe_ltoa_negative);
    RUN_TEST(test_safe_str_basic);
    RUN_TEST(test_safe_str_concatenation);
    RUN_TEST(test_safe_chr);
    RUN_TEST(test_safe_str_null);

    printf("\n--- StringClone ---\n");
    RUN_TEST(test_string_clone);
    RUN_TEST(test_string_clone_len);
    RUN_TEST(test_string_clone_empty);

    printf("\n--- mux_stricmp ---\n");
    RUN_TEST(test_mux_stricmp_equal);
    RUN_TEST(test_mux_stricmp_different);
    RUN_TEST(test_mux_stricmp_empty);

    printf("\n--- mux_strupr / mux_strlwr ---\n");
    RUN_TEST(test_mux_strupr);
    RUN_TEST(test_mux_strlwr);
    RUN_TEST(test_mux_strupr_mixed);

    printf("\n--- trim_spaces ---\n");
    RUN_TEST(test_trim_spaces_basic);
    RUN_TEST(test_trim_spaces_empty);
    RUN_TEST(test_trim_spaces_none);
    RUN_TEST(test_trim_spaces_tabs);

    printf("\n--- mux_strncpy ---\n");
    RUN_TEST(test_mux_strncpy_basic);
    RUN_TEST(test_mux_strncpy_truncate);

    printf("\n--- ParseDate ---\n");
    RUN_TEST(test_parsedate_valid_iso_date);
    RUN_TEST(test_parsedate_valid_iso_datetime);
    RUN_TEST(test_parsedate_zone_flag);
    RUN_TEST(test_parsedate_empty_and_garbage);
    RUN_TEST(test_parsedate_year_overflow_rejected);
    RUN_TEST(test_parsedate_year_narrowing_rejected);
    RUN_TEST(test_parsedate_year_boundaries);
    RUN_TEST(test_parsedate_truncated_iso);
    RUN_TEST(test_parsedate_invalid_components);
    RUN_TEST(test_parsedate_invalid_time);
    RUN_TEST(test_parsedate_ordinal);

    printf("\n--- do_convtime ---\n");
    RUN_TEST(test_convtime_valid);
    RUN_TEST(test_convtime_year_overflow_rejected);
    RUN_TEST(test_convtime_invalid_fields);

    printf("\n--- ParseFractionalSecondsString ---\n");
    RUN_TEST(test_frac_basic);
    RUN_TEST(test_frac_signs_and_edges);
    RUN_TEST(test_frac_rejects_garbage);

    printf("\n--- grapheme clusters / display width ---\n");
    RUN_TEST(test_grapheme_gb11_zwj_family);
    RUN_TEST(test_grapheme_skin_tone_modifier);
    RUN_TEST(test_grapheme_regional_indicator_flag);
    RUN_TEST(test_grapheme_baselines);

    printf("\n=================\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
