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
extern "C" size_t co_copy_columns(unsigned char *out, const unsigned char *p,
                                  const unsigned char *pe, size_t ncols);

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


// ---------------------------------------------------------------------------
// Module transport (libmux.cpp).  Declared locally, as above, rather than
// pulling in libmux.h, which needs the driver's config/type chain.
// ---------------------------------------------------------------------------

#define QUEUE_BLOCK_SIZE 32768
typedef struct QueueBlock {
    struct QueueBlock *pNext;
    struct QueueBlock *pPrev;
    char   *pBuffer;
    size_t  nBuffer;
    char    aBuffer[QUEUE_BLOCK_SIZE];
} QUEUE_BLOCK;
typedef struct { QUEUE_BLOCK *pHead; QUEUE_BLOCK *pTail; size_t nBytes; } QUEUE_INFO;
typedef int MUX_RESULT;
#define MUX_E_NOTREADY (-8)

extern "C" void Pipe_InitializeQueueInfo(QUEUE_INFO *pqi);
extern "C" void Pipe_AppendBytes(QUEUE_INFO *pqi, size_t n, const void *p);
extern "C" void Pipe_EmptyQueue(QUEUE_INFO *pqi);
extern "C" MUX_RESULT Pipe_SendCallPacketAndWait(uint32_t nChannel, QUEUE_INFO *pqi);
extern "C" MUX_RESULT Pipe_SendMsgPacket(uint32_t nChannel, QUEUE_INFO *pqi);
extern "C" MUX_RESULT Pipe_SendDiscPacket(uint32_t nChannel, QUEUE_INFO *pqi);

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

// PUA color codes as the engine stores them (ansi() output).
#define C_GREEN "\xEF\x98\x82"        // green foreground on
#define C_RESET "\xEF\x94\x80"        // color reset

static void test_copy_columns_color_inside_cluster()
{
    // A color reset between an emoji base and its skin-tone modifier is
    // exactly what strcat(ansi(c,base),tone) produces.  Stripped, this
    // is ONE cluster of width 2; co_copy_columns must keep or drop it
    // atomically -- segmenting the raw buffer used to split it, charge
    // 4 columns, and truncate between base and modifier (#787).
    static const char s[] = C_GREEN E_THUMB C_RESET E_TONE;
    const size_t len = sizeof(s) - 1;
    unsigned char out[256];

    // Fits at 2 columns: every byte copied, color codes in place.
    size_t n = co_copy_columns(out, (const unsigned char *)s,
                               (const unsigned char *)s + len, 2);
    ASSERT_EQ(n, len);
    ASSERT_TRUE(memcmp(out, s, len) == 0);

    // At 1 column the 2-wide cluster does not fit: nothing visible is
    // emitted, only the leading color code passes through.
    n = co_copy_columns(out, (const unsigned char *)s,
                        (const unsigned char *)s + len, 1);
    ASSERT_EQ(n, (size_t)3);
    ASSERT_TRUE(memcmp(out, C_GREEN, 3) == 0);

    // GB11 family with a color flip inside the ZWJ sequence: still one
    // cluster, charged 2 columns once.  At 3 columns the family and the
    // following 'a' fit; 'b' is truncated.
    static const char fam[] = E_MAN C_RESET E_ZWJ E_WOMAN E_ZWJ E_GIRL "ab";
    const size_t flen = sizeof(fam) - 1;
    n = co_copy_columns(out, (const unsigned char *)fam,
                        (const unsigned char *)fam + flen, 3);
    ASSERT_EQ(n, flen - 1);
    ASSERT_TRUE(memcmp(out, fam, flen - 1) == 0);

    // Agreement with co_visual_width: the colored skin-tone string
    // measures 2 columns, same as its stripped form.
    ASSERT_EQ(co_visual_width((const unsigned char *)s, len), (size_t)2);
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

// ---------------------------------------------------------------------------
// ASCII fast path in utf8_grapheme.cpp (#1910 lead A)
//
// The fast path is a 128-entry table filled from the GCB/ExtPict DFAs at
// load time, so it cannot disagree with the tables by construction.  What
// these tests pin is the *premise* and the *boundary*: the class layout of
// ASCII that the fast path's existence relies on (verified against the
// exported DFA tables, not against Unicode knowledge), and segmentation
// behavior across the fast/slow seam.
// ---------------------------------------------------------------------------

// The GCB / ExtPict DFA tables are LIBMUX_API exports.
extern const unsigned char  tr_gcb_itt[];
extern const unsigned short tr_gcb_sot[];
extern const unsigned char  tr_gcb_sbt[];
#define TR_GCB_START_STATE (0)
#define TR_GCB_ACCEPTING_STATES_START (207)

extern const unsigned char  cl_extpict_itt[];
extern const unsigned short cl_extpict_sot[];
extern const unsigned char  cl_extpict_sbt[];
#define CL_EXTPICT_START_STATE (0)
#define CL_EXTPICT_ACCEPTING_STATES_START (44)

// Same reader as utf8_grapheme.cpp's RunIntegerDFA_GCB: stop at the first
// accepting state (the pruned table can accept before the last byte).
static int run_property_dfa(const unsigned char *itt, const unsigned short *sot,
                            const unsigned char *sbt, int start, int accepting,
                            int defval, const UTF8 *p, const UTF8 *pEnd)
{
    int iState = start;
    while (p < pEnd && iState < accepting)
    {
        unsigned char ch = *p++;
        int iColumn = itt[ch];
        int iOffset = sot[iState];
        for (;;)
        {
            int y = static_cast<signed char>(sbt[iOffset]);
            if (0 < y)
            {
                if (iColumn < y) { iState = sbt[iOffset + 1]; break; }
                iColumn -= y; iOffset += 2;
            }
            else
            {
                y = -y;
                if (iColumn < y) { iState = sbt[iOffset + iColumn + 1]; break; }
                iColumn -= y; iOffset += y + 1;
            }
        }
    }
    return (iState >= accepting) ? (iState - accepting) : defval;
}

static int gcb_of(const UTF8 *p, size_t n)
{
    return run_property_dfa(tr_gcb_itt, tr_gcb_sot, tr_gcb_sbt,
                            TR_GCB_START_STATE, TR_GCB_ACCEPTING_STATES_START,
                            0, p, p + n);
}

static bool extpict_of(const UTF8 *p, size_t n)
{
    return 1 == run_property_dfa(cl_extpict_itt, cl_extpict_sot, cl_extpict_sbt,
                                 CL_EXTPICT_START_STATE,
                                 CL_EXTPICT_ACCEPTING_STATES_START, 0, p, p + n);
}

// GCB values as gen_gcb.pl maps them (mirrors utf8_grapheme.cpp).
#define T_GCB_OTHER   0
#define T_GCB_CR      1
#define T_GCB_LF      2
#define T_GCB_CONTROL 3
#define T_GCB_EXTEND  4

static void test_gcb_ascii_class_layout()
{
    // ASCII is NOT uniform for GCB: it spans exactly four classes.  This is
    // the fact that makes a bare "< 0x80 means Other" skip a correctness bug
    // and a 128-entry table the required shape.  Verified against the
    // exported DFA tables themselves.
    int count[4] = {0, 0, 0, 0};
    for (int c = 0; c < 128; c++)
    {
        UTF8 b = static_cast<UTF8>(c);
        int g = gcb_of(&b, 1);
        int expected;
        if (0x0D == c)                  expected = T_GCB_CR;
        else if (0x0A == c)             expected = T_GCB_LF;
        else if (c < 0x20 || 0x7F == c) expected = T_GCB_CONTROL;
        else                            expected = T_GCB_OTHER;
        ASSERT_EQ(g, expected);
        count[g]++;

        // ...and none of ASCII is Extended_Pictographic.  The GB11 pair
        // test below is what makes a wrong 'true' here observable.
        ASSERT_TRUE(!extpict_of(&b, 1));
    }
    ASSERT_EQ(count[T_GCB_OTHER],   95);
    ASSERT_EQ(count[T_GCB_CR],       1);
    ASSERT_EQ(count[T_GCB_LF],       1);
    ASSERT_EQ(count[T_GCB_CONTROL], 31);
}

static void test_gcb_nonascii_negative_control()
{
    // The claim above is specific to ASCII, not vacuously true of low code
    // points in general: just past ASCII the classes diverge (145 of
    // U+0080..U+03FF are not Other on current tables).  Without this the
    // layout test could pass against a DFA that answered Other for
    // everything.
    int nonother = 0;
    for (unsigned int cp = 0x80; cp <= 0x3FF; cp++)
    {
        UTF8 b[2];
        b[0] = static_cast<UTF8>(0xC0 | (cp >> 6));
        b[1] = static_cast<UTF8>(0x80 | (cp & 0x3F));
        if (T_GCB_OTHER != gcb_of(b, 2))
        {
            nonother++;
        }
    }
    ASSERT_TRUE(nonother > 100);

    // One concrete pin: U+0301 combining acute is Extend.
    ASSERT_EQ(gcb_of(U(E_ACUTE), 2), T_GCB_EXTEND);
}

static void test_grapheme_ascii_pairs_exhaustive()
{
    // Differential over every ASCII pair: with only Other/CR/LF/Control in
    // play, UAX #29 joins exactly one pair — GB3's CR x LF.  Everything else
    // breaks (GB4/GB5 around controls, GB999 otherwise).  Expectation is
    // derived from the DFA-read classes, so this exercises the fast path
    // against the tables for all 16384 combinations.
    for (int c1 = 0; c1 < 128; c1++)
    {
        for (int c2 = 0; c2 < 128; c2++)
        {
            UTF8 s[2] = { static_cast<UTF8>(c1), static_cast<UTF8>(c2) };
            int g1 = gcb_of(s, 1);
            int g2 = gcb_of(s + 1, 1);
            size_t expected =
                (T_GCB_CR == g1 && T_GCB_LF == g2) ? 1 : 2;
            size_t got = utf8_cluster_count(s, 2);
            if (got != expected)
            {
                // Report the failing pair, then let the assert count it.
                printf("    pair U+%04X U+%04X: got %zu, expected %zu\n",
                       c1, c2, got, expected);
            }
            ASSERT_EQ(got, expected);
        }
    }
    // Every single ASCII byte on its own is one cluster.
    for (int c = 0; c < 128; c++)
    {
        UTF8 b = static_cast<UTF8>(c);
        ASSERT_EQ(utf8_cluster_count(&b, 1), (size_t)1);
    }
}

static void test_grapheme_ascii_seam()
{
    // Boundaries between a fast-path (ASCII) code point and a slow-path
    // (non-ASCII) one, in both directions.

    // GB9: ASCII base + combining mark joins; controls/CR/LF break instead
    // (GB4 wins over GB9).
    ASSERT_EQ(clusters("ab" E_ACUTE),  (size_t)2);   // "a", "b+acute"
    ASSERT_EQ(clusters("\r" E_ACUTE),  (size_t)2);
    ASSERT_EQ(clusters("\n" E_ACUTE),  (size_t)2);
    ASSERT_EQ(clusters("\t" E_ACUTE),  (size_t)2);
    // Degenerate leading mark is its own cluster (GB999 start).
    ASSERT_EQ(clusters(E_ACUTE "a"),   (size_t)2);

    // GB9 joins 'a'+ZWJ, but GB11 must NOT then glue a following ExtPict:
    // 'a' is not Extended_Pictographic.  If the fast path ever answered
    // ExtPict=true for ASCII this collapses to 1 and fails.
    ASSERT_EQ(clusters("a" E_ZWJ E_MAN), (size_t)2); // "a+ZWJ", man
    // Control for the pin above: with a real ExtPict base GB11 does glue.
    ASSERT_EQ(clusters(E_MAN E_ZWJ E_MAN), (size_t)1);

    // CR x LF still joins when flanked by slow-path neighbors.
    ASSERT_EQ(clusters(E_MAN "\r\n" E_MAN), (size_t)3);
}

#define E_VS16  "\xEF\xB8\x8F"          // U+FE0F variation selector-16 (Extend)
#define E_UMLAUT "\xCC\x88"             // U+0308 combining diaeresis (Extend)

static void test_grapheme_gb12_ri_run_ends_at_nonri()
{
    // GB12/13 pair only adjacent RIs.  Once anything else joins the cluster
    // (VS16 or a combining mark via GB9), the RI run is over and the next RI
    // starts a new flag instead of gluing on.  ICU-differential caught the
    // old behavior joining all of these into one cluster.
    ASSERT_EQ(clusters(E_RI_U E_UMLAUT E_RI_S), (size_t)2); // [RI+mark] [RI]
    ASSERT_EQ(clusters(E_RI_U E_VS16 E_RI_S),  (size_t)2); // [RI+VS16] [RI]
    // Baselines around it: a plain pair still joins, a third RI still breaks.
    ASSERT_EQ(clusters(E_RI_U E_RI_S),         (size_t)1);
    ASSERT_EQ(clusters(E_RI_U E_RI_S E_RI_U),  (size_t)2);
}

static void test_grapheme_gb11_double_zwj_breaks()
{
    // GB11 is "ExtPict Extend* ZWJ x ExtPict" — exactly one ZWJ adjacent to
    // the joining ExtPict.  A doubled ZWJ is outside the rule, so the final
    // emoji starts its own cluster (matches ICU).
    ASSERT_EQ(clusters(E_MAN E_ZWJ E_ZWJ E_MAN), (size_t)2);
    // Chained single-ZWJ joins still form one cluster...
    ASSERT_EQ(clusters(E_MAN E_ZWJ E_MAN E_ZWJ E_MAN), (size_t)1);
    // ...including with Extend between the ExtPict and its ZWJ.
    ASSERT_EQ(clusters(E_THUMB E_TONE E_ZWJ E_MAN), (size_t)1);
}


// ---------------------------------------------------------------------------
// Module transport used before mux_InitModuleLibraryPump (#1340)
// ---------------------------------------------------------------------------
//
// The three Pipe_Send* entry points are exported, and everything they reach
// -- both queues and the pump callback -- is null until the hosting process
// installs a transport.  Pipe_AppendBytes checks its length and its source
// pointer but not its queue, and the receive path calls the pump through a
// bare function pointer, so calling any of them first used to be an
// immediate segfault with no diagnostic.
//
// That is how muxscript built with --enable-stubslave died: it reached the
// module transport without ever calling init_stubslave(), and took signal 11
// at the first module test.  #1332 made it boot the slave, which removed
// that particular trigger but left the entry points reachable by anything
// else that gets here early.
//
// This test runs before any transport is installed -- nothing else in this
// binary installs one -- so it exercises exactly that state.  A regression
// does not fail these assertions, it *crashes the whole test binary*, which
// is itself a clear enough signal.
//
static void test_pipe_send_without_pump_is_not_ready()
{
    static QUEUE_INFO qi;
    Pipe_InitializeQueueInfo(&qi);
    const uint8_t payload[4] = { 1, 2, 3, 4 };

    Pipe_AppendBytes(&qi, sizeof(payload), payload);
    ASSERT_EQ(Pipe_SendCallPacketAndWait(1, &qi), MUX_E_NOTREADY);

    Pipe_EmptyQueue(&qi);
    Pipe_AppendBytes(&qi, sizeof(payload), payload);
    ASSERT_EQ(Pipe_SendMsgPacket(1, &qi), MUX_E_NOTREADY);

    Pipe_EmptyQueue(&qi);
    Pipe_AppendBytes(&qi, sizeof(payload), payload);
    ASSERT_EQ(Pipe_SendDiscPacket(1, &qi), MUX_E_NOTREADY);

    Pipe_EmptyQueue(&qi);
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
    RUN_TEST(test_copy_columns_color_inside_cluster);
    RUN_TEST(test_grapheme_baselines);
    RUN_TEST(test_gcb_ascii_class_layout);
    RUN_TEST(test_gcb_nonascii_negative_control);
    RUN_TEST(test_grapheme_ascii_pairs_exhaustive);
    RUN_TEST(test_grapheme_ascii_seam);
    RUN_TEST(test_grapheme_gb12_ri_run_ends_at_nonri);
    RUN_TEST(test_grapheme_gb11_double_zwj_breaks);

    printf("\n--- module transport before init (#1340) ---\n");
    RUN_TEST(test_pipe_send_without_pump_is_not_ready);

    printf("\n=================\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
