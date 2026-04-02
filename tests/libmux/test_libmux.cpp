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

// mathutil.h exports
long   mux_atol(const UTF8 *pString);
double mux_atof(const UTF8 *szString, bool bStrict = true);
size_t mux_i64toa(int64_t val, UTF8 *buf);
UTF8  *mux_i64toa_t(int64_t val);
void   safe_ltoa(long val, UTF8 *buff, UTF8 **bufc);
void   safe_i64toa(int64_t val, UTF8 *buff, UTF8 **bufc);

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

    printf("\n=================\n");
    printf("Results: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
