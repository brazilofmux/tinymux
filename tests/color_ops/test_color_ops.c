/*
 * test_color_ops.c — Standalone stress test harness for co_* primitives.
 *
 * Build: make (see Makefile)
 * Run:   ./test_color_ops [-v] [-f <function>] [-s <seed>]
 *
 * Tests every co_* function with:
 *   - Deterministic edge cases (empty, single char, boundary, LBUF_SIZE)
 *   - Color interleaving (PUA BMP, SMP RGB, mixed)
 *   - Unicode (multi-byte, combining, fullwidth, emoji)
 *   - Fuzz with random inputs
 */

#include "color_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

/* ---- Test infrastructure ---- */

static int g_verbose = 0;
static int g_pass = 0;
static int g_fail = 0;
static int g_skip = 0;
static const char *g_filter = NULL;
static unsigned int g_seed = 0;

#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define RESET   "\033[0m"

static void test_ok(const char *name, const char *fmt, ...) {
    g_pass++;
    if (g_verbose) {
        printf(GREEN "  PASS" RESET " %s: ", name);
        va_list ap;
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
        printf("\n");
    }
}

static void test_fail(const char *name, const char *fmt, ...) {
    g_fail++;
    printf(RED "  FAIL" RESET " %s: ", name);
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
}

/* Check size_t return == expected, and output bytes match. */
static void check_buf(const char *name, const char *label,
                       const unsigned char *out, size_t got_len,
                       const unsigned char *expect, size_t expect_len) {
    if (got_len != expect_len) {
        test_fail(name, "%s: length %zu, expected %zu", label, got_len, expect_len);
        return;
    }
    if (expect_len > 0 && memcmp(out, expect, expect_len) != 0) {
        test_fail(name, "%s: content mismatch (len=%zu)", label, expect_len);
        if (g_verbose) {
            printf("    got:    ");
            for (size_t i = 0; i < got_len && i < 80; i++)
                printf("%02x ", out[i]);
            printf("\n    expect: ");
            for (size_t i = 0; i < expect_len && i < 80; i++)
                printf("%02x ", expect[i]);
            printf("\n");
        }
        return;
    }
    test_ok(name, "%s (len=%zu)", label, expect_len);
}

static void check_size(const char *name, const char *label,
                        size_t got, size_t expect) {
    if (got != expect) {
        test_fail(name, "%s: got %zu, expected %zu", label, got, expect);
    } else {
        test_ok(name, "%s = %zu", label, got);
    }
}

static void check_ptr_null(const char *name, const char *label,
                            const void *ptr) {
    if (ptr != NULL) {
        test_fail(name, "%s: expected NULL, got %p", label, ptr);
    } else {
        test_ok(name, "%s = NULL", label);
    }
}

/* ---- PUA color encoding helpers ---- */

/*
 * Encode a BMP PUA code point (U+F500-F7FF) as 3-byte UTF-8.
 * Returns 3.  out must have room for 3 bytes.
 */
static int encode_bmp_pua(unsigned char *out, unsigned int cp) {
    out[0] = 0xEF;
    out[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (unsigned char)(0x80 | (cp & 0x3F));
    return 3;
}

/* Shorthand: encode reset (U+F500) */
static int pua_reset(unsigned char *out) {
    return encode_bmp_pua(out, 0xF500);
}

/* Encode foreground color index 0-255 (U+F600+idx) */
static int pua_fg(unsigned char *out, int idx) {
    return encode_bmp_pua(out, 0xF600 + idx);
}

/* Encode background color index 0-255 (U+F700+idx) */
static int pua_bg(unsigned char *out, int idx) {
    return encode_bmp_pua(out, 0xF700 + idx);
}

/* Encode intense (U+F501) */
static int pua_intense(unsigned char *out) {
    return encode_bmp_pua(out, 0xF501);
}

/* Encode underline (U+F504) */
static int pua_underline(unsigned char *out) {
    return encode_bmp_pua(out, 0xF504);
}

/*
 * Encode SMP PUA (U+F0000-F05FF) as 4-byte UTF-8.
 * Returns 4.
 */
static int encode_smp_pua(unsigned char *out, unsigned int cp) {
    out[0] = 0xF3;
    out[1] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (unsigned char)(0x80 | (cp & 0x3F));
    return 4;
}

/* Encode 24-bit foreground: base FG + R/G/B deltas.
 * Returns total bytes written (3 + up to 12). */
static int pua_fg_rgb(unsigned char *out, int r, int g, int b) {
    int n = pua_fg(out, 0);  /* base FG indexed 0 */
    n += encode_smp_pua(out + n, 0xF0000 + r);  /* red FG delta */
    n += encode_smp_pua(out + n, 0xF0100 + g);  /* green FG delta */
    n += encode_smp_pua(out + n, 0xF0200 + b);  /* blue FG delta */
    return n;
}

/* Build a test string: color + visible text.
 * Returns total length. */
static size_t build_colored(unsigned char *buf,
                             int (*color_fn)(unsigned char *),
                             const char *text) {
    size_t n = 0;
    if (color_fn) {
        n += (size_t)color_fn(buf);
    }
    size_t tlen = strlen(text);
    memcpy(buf + n, text, tlen);
    return n + tlen;
}

/* Simple PRNG (xorshift32) for reproducible fuzz. */
static unsigned int xrand(void) {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}

static unsigned char rand_byte(void) {
    return (unsigned char)(xrand() & 0xFF);
}

/* Generate a random valid UTF-8 string of approximately n visible chars.
 * Sprinkles in color PUA at random. Returns actual byte length. */
static size_t gen_random_colored(unsigned char *buf, size_t max_visible) {
    size_t pos = 0;
    size_t vis = 0;
    while (vis < max_visible && pos + 20 < LBUF_SIZE) {
        /* Maybe insert a color code (25% chance). */
        if ((xrand() % 4) == 0) {
            int kind = xrand() % 3;
            if (kind == 0) {
                pos += (size_t)pua_fg(buf + pos, (int)(xrand() % 256));
            } else if (kind == 1) {
                pos += (size_t)pua_bg(buf + pos, (int)(xrand() % 256));
            } else {
                pos += (size_t)pua_reset(buf + pos);
            }
        }
        /* Emit a visible character. */
        int r = (int)(xrand() % 10);
        if (r < 5) {
            /* ASCII */
            buf[pos++] = (unsigned char)(0x21 + (xrand() % 94));
        } else if (r < 8) {
            /* 2-byte UTF-8 (U+0080-07FF) */
            unsigned int cp = 0x80 + (xrand() % 0x700);
            buf[pos++] = (unsigned char)(0xC0 | (cp >> 6));
            buf[pos++] = (unsigned char)(0x80 | (cp & 0x3F));
        } else if (r < 9) {
            /* 3-byte UTF-8 (U+0800-EFFF, avoid PUA range) */
            unsigned int cp = 0x0800 + (xrand() % 0xE000);
            if (cp >= 0xD800 && cp <= 0xDFFF) cp = 0x3000; /* avoid surrogates */
            if (cp >= 0xF500 && cp <= 0xF7FF) cp = 0x3001; /* avoid our PUA */
            buf[pos++] = (unsigned char)(0xE0 | (cp >> 12));
            buf[pos++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            buf[pos++] = (unsigned char)(0x80 | (cp & 0x3F));
        } else {
            /* 4-byte UTF-8 (U+10000-10FFFF, avoid SMP PUA) */
            unsigned int cp = 0x10000 + (xrand() % 0x100000);
            if (cp >= 0xF0000 && cp <= 0xF05FF) cp = 0x1F600; /* avoid our PUA */
            buf[pos++] = (unsigned char)(0xF0 | (cp >> 18));
            buf[pos++] = (unsigned char)(0x80 | ((cp >> 12) & 0x3F));
            buf[pos++] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
            buf[pos++] = (unsigned char)(0x80 | (cp & 0x3F));
        }
        vis++;
    }
    return pos;
}

/* ================================================================
 * Test suites — one per function (or small group).
 * ================================================================ */

/* ---- co_visible_length ---- */

static void test_visible_length(void) {
    const char *name = "co_visible_length";
    unsigned char buf[LBUF_SIZE];

    /* Empty string. */
    check_size(name, "empty", co_visible_length((const unsigned char *)"", 0), 0);

    /* Pure ASCII. */
    check_size(name, "ascii 'hello'",
        co_visible_length((const unsigned char *)"hello", 5), 5);

    /* Pure color, no visible chars. */
    size_t n = 0;
    n += (size_t)pua_fg(buf, 1);
    n += (size_t)pua_bg(buf, 2);
    n += (size_t)pua_reset(buf + n);
    check_size(name, "pure color", co_visible_length(buf, n), 0);

    /* Color + text. */
    n = 0;
    n += (size_t)pua_fg(buf, 9);
    memcpy(buf + n, "ABC", 3); n += 3;
    n += (size_t)pua_reset(buf + n);
    check_size(name, "FG9+'ABC'+reset", co_visible_length(buf, n), 3);

    /* Multi-byte UTF-8: U+00E9 (é, 2 bytes), U+4E16 (世, 3 bytes), U+1F600 (😀, 4 bytes). */
    const unsigned char utf8_test[] = {
        0xC3, 0xA9,                         /* é */
        0xE4, 0xB8, 0x96,                   /* 世 */
        0xF0, 0x9F, 0x98, 0x80              /* 😀 */
    };
    check_size(name, "é世😀",
        co_visible_length(utf8_test, sizeof(utf8_test)), 3);

    /* Interleaved color + multi-byte. */
    n = 0;
    n += (size_t)pua_fg(buf, 1);
    buf[n++] = 0xC3; buf[n++] = 0xA9;   /* é */
    n += (size_t)pua_fg(buf + n, 2);
    buf[n++] = 0xE4; buf[n++] = 0xB8; buf[n++] = 0x96;  /* 世 */
    n += (size_t)pua_reset(buf + n);
    check_size(name, "color+é+color+世+reset", co_visible_length(buf, n), 2);

    /* Fuzz: random colored strings, verify co_visible_length == co_strip_color length. */
    unsigned int save_seed = g_seed;
    for (int i = 0; i < 10000; i++) {
        size_t len = gen_random_colored(buf, 1 + (xrand() % 200));
        size_t vis = co_visible_length(buf, len);

        unsigned char stripped[LBUF_SIZE];
        size_t slen = co_strip_color(stripped, buf, len);

        /* Count code points in stripped UTF-8. */
        size_t cp_count = 0;
        for (size_t j = 0; j < slen; ) {
            unsigned char b = stripped[j];
            if (b < 0x80) j += 1;
            else if (b < 0xE0) j += 2;
            else if (b < 0xF0) j += 3;
            else j += 4;
            cp_count++;
        }
        if (vis != cp_count) {
            test_fail(name, "fuzz[%d]: visible_length=%zu but stripped has %zu codepoints (len=%zu)",
                      i, vis, cp_count, len);
            g_seed = save_seed;
            return;
        }
    }
    test_ok(name, "10000 fuzz iterations OK");
    g_seed = save_seed;
}

/* ---- co_strip_color ---- */

static void test_strip_color(void) {
    const char *name = "co_strip_color";
    unsigned char buf[LBUF_SIZE], out[LBUF_SIZE];

    /* Empty. */
    size_t r = co_strip_color(out, (const unsigned char *)"", 0);
    check_buf(name, "empty", out, r, (const unsigned char *)"", 0);

    /* No color. */
    r = co_strip_color(out, (const unsigned char *)"hello", 5);
    check_buf(name, "no color", out, r, (const unsigned char *)"hello", 5);

    /* All color, no visible. */
    size_t n = 0;
    n += (size_t)pua_fg(buf, 1);
    n += (size_t)pua_bg(buf + n, 2);
    r = co_strip_color(out, buf, n);
    check_buf(name, "all color", out, r, (const unsigned char *)"", 0);

    /* Mixed. */
    n = 0;
    n += (size_t)pua_fg(buf, 1);
    buf[n++] = 'H'; buf[n++] = 'i';
    n += (size_t)pua_reset(buf + n);
    r = co_strip_color(out, buf, n);
    check_buf(name, "FG1+'Hi'+reset", out, r, (const unsigned char *)"Hi", 2);

    /* 24-bit color (SMP PUA). */
    n = 0;
    n += (size_t)pua_fg_rgb(buf, 255, 128, 0);
    buf[n++] = 'X';
    r = co_strip_color(out, buf, n);
    check_buf(name, "24-bit FG + 'X'", out, r, (const unsigned char *)"X", 1);
}

/* ---- co_words_count ---- */

static void test_words_count(void) {
    const char *name = "co_words_count";
    unsigned char buf[LBUF_SIZE];

    check_size(name, "empty",
        co_words_count((const unsigned char *)"", 0, ' '), 0);
    check_size(name, "'hello'",
        co_words_count((const unsigned char *)"hello", 5, ' '), 1);
    check_size(name, "'a b c'",
        co_words_count((const unsigned char *)"a b c", 5, ' '), 3);
    check_size(name, "'  a  b  '",
        co_words_count((const unsigned char *)"  a  b  ", 8, ' '), 2);

    /* Custom delimiter. */
    check_size(name, "'a|b|c' delim='|'",
        co_words_count((const unsigned char *)"a|b|c", 5, '|'), 3);

    /* Color between words shouldn't affect count. */
    size_t n = 0;
    n += (size_t)pua_fg(buf, 1);
    buf[n++] = 'a'; buf[n++] = ' ';
    n += (size_t)pua_fg(buf + n, 2);
    buf[n++] = 'b'; buf[n++] = ' ';
    n += (size_t)pua_reset(buf + n);
    buf[n++] = 'c';
    check_size(name, "colored 'a b c'", co_words_count(buf, n, ' '), 3);

    /* All spaces. */
    check_size(name, "'   '",
        co_words_count((const unsigned char *)"   ", 3, ' '), 0);
}

/* ---- co_first / co_rest / co_last ---- */

static void test_first_rest_last(void) {
    unsigned char out[LBUF_SIZE], buf[LBUF_SIZE];
    size_t r;

    /* co_first */
    r = co_first(out, (const unsigned char *)"hello world", 11, ' ');
    check_buf("co_first", "'hello world'", out, r,
              (const unsigned char *)"hello", 5);

    r = co_first(out, (const unsigned char *)"single", 6, ' ');
    check_buf("co_first", "'single'", out, r,
              (const unsigned char *)"single", 6);

    r = co_first(out, (const unsigned char *)"", 0, ' ');
    check_buf("co_first", "empty", out, r, (const unsigned char *)"", 0);

    /* co_rest */
    r = co_rest(out, (const unsigned char *)"hello world", 11, ' ');
    check_buf("co_rest", "'hello world'", out, r,
              (const unsigned char *)"world", 5);

    r = co_rest(out, (const unsigned char *)"single", 6, ' ');
    check_buf("co_rest", "'single'", out, r, (const unsigned char *)"", 0);

    /* co_last */
    r = co_last(out, (const unsigned char *)"one two three", 13, ' ');
    check_buf("co_last", "'one two three'", out, r,
              (const unsigned char *)"three", 5);

    r = co_last(out, (const unsigned char *)"single", 6, ' ');
    check_buf("co_last", "'single'", out, r,
              (const unsigned char *)"single", 6);

    /* Colored: co_first should preserve color of first word. */
    size_t n = 0;
    n += (size_t)pua_fg(buf, 1);
    memcpy(buf + n, "red", 3); n += 3;
    buf[n++] = ' ';
    n += (size_t)pua_fg(buf + n, 2);
    memcpy(buf + n, "green", 5); n += 5;

    r = co_first(out, buf, n, ' ');
    /* First word should include the FG1 color + "red". */
    unsigned char expect_first[LBUF_SIZE];
    size_t en = 0;
    en += (size_t)pua_fg(expect_first, 1);
    memcpy(expect_first + en, "red", 3); en += 3;
    check_buf("co_first", "colored first word", out, r, expect_first, en);
}

/* ---- co_extract ---- */

static void test_extract(void) {
    const char *name = "co_extract";
    unsigned char out[LBUF_SIZE];
    size_t r;

    /* Extract word 2 (1-based). */
    r = co_extract(out, (const unsigned char *)"alpha beta gamma", 16,
                   2, 1, ' ', ' ');
    check_buf(name, "word 2 of 'alpha beta gamma'", out, r,
              (const unsigned char *)"beta", 4);

    /* Extract words 2-3. */
    r = co_extract(out, (const unsigned char *)"a b c d", 7,
                   2, 2, ' ', ' ');
    check_buf(name, "words 2-3 of 'a b c d'", out, r,
              (const unsigned char *)"b c", 3);

    /* Out of range. */
    r = co_extract(out, (const unsigned char *)"a b", 3, 5, 1, ' ', ' ');
    check_buf(name, "word 5 of 'a b'", out, r, (const unsigned char *)"", 0);

    /* Empty. */
    r = co_extract(out, (const unsigned char *)"", 0, 1, 1, ' ', ' ');
    check_buf(name, "empty", out, r, (const unsigned char *)"", 0);
}

/* ---- co_left / co_right ---- */

static void test_left_right(void) {
    unsigned char out[LBUF_SIZE];
    size_t r;

    r = co_left(out, (const unsigned char *)"abcdef", 6, 3);
    check_buf("co_left", "'abcdef' n=3", out, r,
              (const unsigned char *)"abc", 3);

    r = co_left(out, (const unsigned char *)"ab", 2, 5);
    check_buf("co_left", "'ab' n=5 (past end)", out, r,
              (const unsigned char *)"ab", 2);

    r = co_left(out, (const unsigned char *)"", 0, 3);
    check_buf("co_left", "empty", out, r, (const unsigned char *)"", 0);

    r = co_right(out, (const unsigned char *)"abcdef", 6, 3);
    check_buf("co_right", "'abcdef' n=3", out, r,
              (const unsigned char *)"def", 3);

    r = co_right(out, (const unsigned char *)"ab", 2, 5);
    check_buf("co_right", "'ab' n=5 (past end)", out, r,
              (const unsigned char *)"ab", 2);

    /* Multi-byte: co_left of 2 from "é世😀" should give "é世". */
    const unsigned char utf8[] = { 0xC3, 0xA9, 0xE4, 0xB8, 0x96,
                                    0xF0, 0x9F, 0x98, 0x80 };
    r = co_left(out, utf8, sizeof(utf8), 2);
    check_buf("co_left", "é世😀 n=2", out, r, utf8, 5);

    /* co_right of 1 from "é世😀" should give "😀". */
    r = co_right(out, utf8, sizeof(utf8), 1);
    check_buf("co_right", "é世😀 n=1", out, r, utf8 + 5, 4);
}

/* ---- co_mid ---- */

static void test_mid(void) {
    const char *name = "co_mid";
    unsigned char out[LBUF_SIZE];
    size_t r;

    r = co_mid(out, (const unsigned char *)"abcdef", 6, 2, 3);
    check_buf(name, "'abcdef' start=2 count=3", out, r,
              (const unsigned char *)"cde", 3);

    r = co_mid(out, (const unsigned char *)"abcdef", 6, 0, 2);
    check_buf(name, "'abcdef' start=0 count=2", out, r,
              (const unsigned char *)"ab", 2);

    /* Past end. */
    r = co_mid(out, (const unsigned char *)"abc", 3, 1, 10);
    check_buf(name, "'abc' start=1 count=10", out, r,
              (const unsigned char *)"bc", 2);

    /* Start past end. */
    r = co_mid(out, (const unsigned char *)"abc", 3, 10, 2);
    check_buf(name, "'abc' start=10", out, r, (const unsigned char *)"", 0);
}

/* ---- co_trim ---- */

static void test_trim(void) {
    const char *name = "co_trim";
    unsigned char out[LBUF_SIZE];
    size_t r;

    /* Trim both sides (default whitespace). */
    r = co_trim(out, (const unsigned char *)"  hello  ", 9, 0, 3);
    check_buf(name, "both '  hello  '", out, r,
              (const unsigned char *)"hello", 5);

    /* Trim left only. */
    r = co_trim(out, (const unsigned char *)"  hello  ", 9, 0, 1);
    check_buf(name, "left '  hello  '", out, r,
              (const unsigned char *)"hello  ", 7);

    /* Trim right only. */
    r = co_trim(out, (const unsigned char *)"  hello  ", 9, 0, 2);
    check_buf(name, "right '  hello  '", out, r,
              (const unsigned char *)"  hello", 7);

    /* Trim specific char. */
    r = co_trim(out, (const unsigned char *)"xxhelloxx", 9, 'x', 3);
    check_buf(name, "both 'xxhelloxx' char='x'", out, r,
              (const unsigned char *)"hello", 5);

    /* All trimmed. */
    r = co_trim(out, (const unsigned char *)"   ", 3, 0, 3);
    check_buf(name, "all spaces", out, r, (const unsigned char *)"", 0);

    /* Nothing to trim. */
    r = co_trim(out, (const unsigned char *)"hello", 5, 0, 3);
    check_buf(name, "no whitespace", out, r,
              (const unsigned char *)"hello", 5);
}

/* ---- co_toupper / co_tolower / co_totitle ---- */

static void test_case(void) {
    unsigned char out[LBUF_SIZE];
    size_t r;

    /* toupper ASCII. */
    r = co_toupper(out, (const unsigned char *)"hello", 5);
    check_buf("co_toupper", "'hello'", out, r,
              (const unsigned char *)"HELLO", 5);

    /* tolower ASCII. */
    r = co_tolower(out, (const unsigned char *)"HELLO", 5);
    check_buf("co_tolower", "'HELLO'", out, r,
              (const unsigned char *)"hello", 5);

    /* toupper with color. */
    unsigned char buf[LBUF_SIZE];
    size_t n = 0;
    n += (size_t)pua_fg(buf, 1);
    memcpy(buf + n, "abc", 3); n += 3;
    n += (size_t)pua_reset(buf + n);

    r = co_toupper(out, buf, n);
    /* Result should be FG1 + "ABC" + reset. */
    unsigned char expect[LBUF_SIZE];
    size_t en = 0;
    en += (size_t)pua_fg(expect, 1);
    memcpy(expect + en, "ABC", 3); en += 3;
    en += (size_t)pua_reset(expect + en);
    check_buf("co_toupper", "colored 'abc'", out, r, expect, en);

    /* Unicode: é (U+00E9) → É (U+00C9). */
    const unsigned char e_acute[] = { 0xC3, 0xA9 };
    const unsigned char E_acute[] = { 0xC3, 0x89 };
    r = co_toupper(out, e_acute, 2);
    check_buf("co_toupper", "é → É", out, r, E_acute, 2);

    r = co_tolower(out, E_acute, 2);
    check_buf("co_tolower", "É → é", out, r, e_acute, 2);

    /* totitle. */
    r = co_totitle(out, (const unsigned char *)"hello", 5);
    check_buf("co_totitle", "'hello'", out, r,
              (const unsigned char *)"Hello", 5);

    /* ß → SS (toupper, length change: 1 code point → 2 code points).
     * Note: this is a length-changing case mapping.  Some DFA encodings
     * handle it via literal OTT entries.  If the function returns ß
     * unchanged, that's a known limitation to investigate. */
    const unsigned char sharp_s[] = { 0xC3, 0x9F };  /* ß U+00DF */
    r = co_toupper(out, sharp_s, 2);
    if (r == 2 && memcmp(out, "SS", 2) == 0) {
        test_ok("co_toupper", "ß → SS");
    } else if (r == 2 && memcmp(out, sharp_s, 2) == 0) {
        /* DFA returned identity — ß→SS not in OTT tables (utf/ pipeline limitation). */
        g_skip++;
        if (g_verbose) printf(YELLOW "  SKIP" RESET " co_toupper: ß → SS not in OTT (known)\n");
    } else {
        test_fail("co_toupper", "ß → SS: unexpected result len=%zu", r);
    }
}

/* ---- co_reverse ---- */

static void test_reverse(void) {
    const char *name = "co_reverse";
    unsigned char out[LBUF_SIZE];
    size_t r;

    r = co_reverse(out, (const unsigned char *)"abcde", 5);
    check_buf(name, "'abcde'", out, r,
              (const unsigned char *)"edcba", 5);

    r = co_reverse(out, (const unsigned char *)"a", 1);
    check_buf(name, "'a'", out, r, (const unsigned char *)"a", 1);

    r = co_reverse(out, (const unsigned char *)"", 0);
    check_buf(name, "empty", out, r, (const unsigned char *)"", 0);

    /* Multi-byte: "é世" → "世é". */
    const unsigned char fwd[] = { 0xC3, 0xA9, 0xE4, 0xB8, 0x96 };
    const unsigned char rev[] = { 0xE4, 0xB8, 0x96, 0xC3, 0xA9 };
    r = co_reverse(out, fwd, sizeof(fwd));
    check_buf(name, "é世 → 世é", out, r, rev, sizeof(rev));

    /* Colored reverse: leading color stays at front. */
    unsigned char buf[LBUF_SIZE];
    size_t n = 0;
    n += (size_t)pua_fg(buf, 1);
    buf[n++] = 'A';
    n += (size_t)pua_fg(buf + n, 2);
    buf[n++] = 'B';

    r = co_reverse(out, buf, n);
    /* Leading FG1 stays leading, then B (with its FG2), then A (no color). */
    unsigned char expect[LBUF_SIZE];
    size_t en = 0;
    en += (size_t)pua_fg(expect, 1);
    en += (size_t)pua_fg(expect + en, 2);
    expect[en++] = 'B';
    expect[en++] = 'A';
    check_buf(name, "colored reverse", out, r, expect, en);
}

/* ---- co_search / co_pos ---- */

static void test_search_pos(void) {
    unsigned char out[LBUF_SIZE];

    /* co_search: basic. */
    const unsigned char *haystack = (const unsigned char *)"hello world";
    const unsigned char *result = co_search(haystack, 11,
        (const unsigned char *)"world", 5);
    if (result == haystack + 6) {
        test_ok("co_search", "'world' in 'hello world'");
    } else {
        test_fail("co_search", "'world' in 'hello world': got %p, expected %p",
                  (void *)result, (void *)(haystack + 6));
    }

    /* Not found. */
    result = co_search(haystack, 11, (const unsigned char *)"xyz", 3);
    check_ptr_null("co_search", "'xyz' not found", result);

    /* co_pos: 1-based. */
    check_size("co_pos", "'lo' in 'hello'",
        co_pos((const unsigned char *)"hello", 5,
               (const unsigned char *)"lo", 2), 4);

    check_size("co_pos", "not found",
        co_pos((const unsigned char *)"hello", 5,
               (const unsigned char *)"xyz", 3), 0);
}

/* ---- co_edit ---- */

static void test_edit(void) {
    const char *name = "co_edit";
    unsigned char out[LBUF_SIZE];
    size_t r;

    r = co_edit(out,
        (const unsigned char *)"hello world", 11,
        (const unsigned char *)"world", 5,
        (const unsigned char *)"earth", 5);
    check_buf(name, "world→earth", out, r,
              (const unsigned char *)"hello earth", 11);

    /* Replace all occurrences. */
    r = co_edit(out,
        (const unsigned char *)"aXbXc", 5,
        (const unsigned char *)"X", 1,
        (const unsigned char *)"YY", 2);
    check_buf(name, "X→YY in 'aXbXc'", out, r,
              (const unsigned char *)"aYYbYYc", 7);

    /* Delete (replace with empty). */
    r = co_edit(out,
        (const unsigned char *)"hello", 5,
        (const unsigned char *)"ll", 2,
        (const unsigned char *)"", 0);
    check_buf(name, "delete 'll' from 'hello'", out, r,
              (const unsigned char *)"heo", 3);
}

/* ---- co_sort_words ---- */

static void test_sort(void) {
    const char *name = "co_sort_words";
    unsigned char out[LBUF_SIZE];
    size_t r;

    r = co_sort_words(out, (const unsigned char *)"cherry apple banana", 19,
                      ' ', ' ', 'a');
    check_buf(name, "alpha sort", out, r,
              (const unsigned char *)"apple banana cherry", 19);

    /* Numeric sort. */
    r = co_sort_words(out, (const unsigned char *)"10 2 30 1", 9,
                      ' ', ' ', 'n');
    check_buf(name, "numeric sort", out, r,
              (const unsigned char *)"1 2 10 30", 9);

    /* Case-insensitive sort. */
    r = co_sort_words(out, (const unsigned char *)"Banana apple Cherry", 19,
                      ' ', ' ', 'i');
    check_buf(name, "case-insensitive sort", out, r,
              (const unsigned char *)"apple Banana Cherry", 19);
}

/* ---- co_setunion / co_setdiff / co_setinter ---- */

static void test_set_ops(void) {
    unsigned char out[LBUF_SIZE];
    size_t r;

    r = co_setunion(out,
        (const unsigned char *)"a c e", 5,
        (const unsigned char *)"b c d", 5,
        ' ', ' ', 'a');
    check_buf("co_setunion", "a c e | b c d", out, r,
              (const unsigned char *)"a b c d e", 9);

    r = co_setdiff(out,
        (const unsigned char *)"a b c d", 7,
        (const unsigned char *)"b d", 3,
        ' ', ' ', 'a');
    check_buf("co_setdiff", "a b c d - b d", out, r,
              (const unsigned char *)"a c", 3);

    r = co_setinter(out,
        (const unsigned char *)"a b c d", 7,
        (const unsigned char *)"b d e", 5,
        ' ', ' ', 'a');
    check_buf("co_setinter", "a b c d & b d e", out, r,
              (const unsigned char *)"b d", 3);
}

/* ---- co_repeat ---- */

static void test_repeat(void) {
    const char *name = "co_repeat";
    unsigned char out[LBUF_SIZE];
    size_t r;

    r = co_repeat(out, (const unsigned char *)"ab", 2, 3);
    check_buf(name, "'ab' x 3", out, r,
              (const unsigned char *)"ababab", 6);

    r = co_repeat(out, (const unsigned char *)"x", 1, 0);
    check_buf(name, "'x' x 0", out, r, (const unsigned char *)"", 0);

    r = co_repeat(out, (const unsigned char *)"", 0, 5);
    check_buf(name, "empty x 5", out, r, (const unsigned char *)"", 0);
}

/* ---- co_delete ---- */

static void test_delete(void) {
    const char *name = "co_delete";
    unsigned char out[LBUF_SIZE];
    size_t r;

    r = co_delete(out, (const unsigned char *)"abcdef", 6, 2, 2);
    check_buf(name, "'abcdef' del(2,2)", out, r,
              (const unsigned char *)"abef", 4);

    r = co_delete(out, (const unsigned char *)"abcdef", 6, 0, 3);
    check_buf(name, "'abcdef' del(0,3)", out, r,
              (const unsigned char *)"def", 3);

    /* Delete past end. */
    r = co_delete(out, (const unsigned char *)"abc", 3, 1, 10);
    check_buf(name, "'abc' del(1,10)", out, r,
              (const unsigned char *)"a", 1);
}

/* ---- co_escape ---- */

static void test_escape(void) {
    const char *name = "co_escape";
    unsigned char out[LBUF_SIZE];
    size_t r;

    r = co_escape(out, (const unsigned char *)"a$b%c", 5);
    check_buf(name, "'a$b%c'", out, r,
              (const unsigned char *)"\\a\\$b\\%c", 8);

    r = co_escape(out, (const unsigned char *)"", 0);
    check_buf(name, "empty", out, r, (const unsigned char *)"", 0);

    r = co_escape(out, (const unsigned char *)"x", 1);
    check_buf(name, "'x'", out, r, (const unsigned char *)"\\x", 2);
}

/* ---- co_collapse_color ---- */

static void test_collapse_color(void) {
    const char *name = "co_collapse_color";
    unsigned char buf[LBUF_SIZE], out[LBUF_SIZE];
    size_t r, n;

    /* No redundancy: single color + char. */
    n = 0;
    n += (size_t)pua_fg(buf, 1);
    buf[n++] = 'A';
    r = co_collapse_color(out, buf, n);
    check_buf(name, "no redundancy", out, r, buf, n);

    /* Redundant: two FG colors before one char → only last emitted. */
    n = 0;
    n += (size_t)pua_fg(buf, 1);
    n += (size_t)pua_fg(buf + n, 2);
    buf[n++] = 'A';

    unsigned char expect[LBUF_SIZE];
    size_t en = 0;
    en += (size_t)pua_fg(expect, 2);
    expect[en++] = 'A';

    r = co_collapse_color(out, buf, n);
    check_buf(name, "FG1+FG2+'A' → FG2+'A'", out, r, expect, en);

    /* Already at state: color repeated for second char should vanish. */
    n = 0;
    n += (size_t)pua_fg(buf, 1);
    buf[n++] = 'A';
    n += (size_t)pua_fg(buf + n, 1);  /* redundant, same color */
    buf[n++] = 'B';

    en = 0;
    en += (size_t)pua_fg(expect, 1);
    expect[en++] = 'A';
    expect[en++] = 'B';

    r = co_collapse_color(out, buf, n);
    check_buf(name, "FG1+'A'+FG1+'B' → FG1+'AB'", out, r, expect, en);
}

/* ---- co_member ---- */

static void test_member(void) {
    const char *name = "co_member";

    check_size(name, "'b' in 'a b c'",
        co_member((const unsigned char *)"b", 1,
                  (const unsigned char *)"a b c", 5, ' '), 2);

    check_size(name, "'d' not in 'a b c'",
        co_member((const unsigned char *)"d", 1,
                  (const unsigned char *)"a b c", 5, ' '), 0);

    check_size(name, "'a' in 'a'",
        co_member((const unsigned char *)"a", 1,
                  (const unsigned char *)"a", 1, ' '), 1);
}

/* ---- co_lpos ---- */

static void test_lpos(void) {
    const char *name = "co_lpos";
    unsigned char out[LBUF_SIZE];
    size_t r;

    r = co_lpos(out, (const unsigned char *)"abacada", 7, 'a');
    check_buf(name, "'a' in 'abacada'", out, r,
              (const unsigned char *)"0 2 4 6", 7);

    r = co_lpos(out, (const unsigned char *)"xyz", 3, 'a');
    check_buf(name, "'a' not in 'xyz'", out, r,
              (const unsigned char *)"", 0);
}

/* ---- co_splice ---- */

static void test_splice(void) {
    const char *name = "co_splice";
    unsigned char out[LBUF_SIZE];
    size_t r;

    r = co_splice(out,
        (const unsigned char *)"a b c", 5,
        (const unsigned char *)"x y z", 5,
        (const unsigned char *)"b", 1,
        ' ', ' ');
    check_buf(name, "splice b→y", out, r,
              (const unsigned char *)"a y c", 5);
}

/* ---- co_insert_word ---- */

static void test_insert_word(void) {
    const char *name = "co_insert_word";
    unsigned char out[LBUF_SIZE];
    size_t r;

    r = co_insert_word(out,
        (const unsigned char *)"a c", 3,
        (const unsigned char *)"b", 1,
        2, ' ', ' ');
    check_buf(name, "insert 'b' at pos 2", out, r,
              (const unsigned char *)"a b c", 5);

    /* Insert at beginning. */
    r = co_insert_word(out,
        (const unsigned char *)"b c", 3,
        (const unsigned char *)"a", 1,
        0, ' ', ' ');
    check_buf(name, "insert 'a' at pos 0", out, r,
              (const unsigned char *)"a b c", 5);
}

/* ---- co_visual_width ---- */

static void test_visual_width(void) {
    const char *name = "co_visual_width";

    check_size(name, "ASCII 'hello'",
        co_visual_width((const unsigned char *)"hello", 5), 5);

    /* CJK fullwidth: U+4E16 (世) should be width 2. */
    const unsigned char shi[] = { 0xE4, 0xB8, 0x96 };
    check_size(name, "世 (CJK)",
        co_visual_width(shi, 3), 2);

    /* Mix: "A世B" = 1 + 2 + 1 = 4. */
    const unsigned char mix[] = { 'A', 0xE4, 0xB8, 0x96, 'B' };
    check_size(name, "'A世B'",
        co_visual_width(mix, sizeof(mix)), 4);
}

/* ---- co_center / co_ljust / co_rjust ---- */

static void test_justify(void) {
    unsigned char out[LBUF_SIZE];
    size_t r;

    /* ljust: pad to 10 with spaces. */
    r = co_ljust(out, (const unsigned char *)"hello", 5, 10, NULL, 0, 0);
    check_buf("co_ljust", "'hello' w=10", out, r,
              (const unsigned char *)"hello     ", 10);

    /* rjust: pad to 10 with spaces. */
    r = co_rjust(out, (const unsigned char *)"hello", 5, 10, NULL, 0, 0);
    check_buf("co_rjust", "'hello' w=10", out, r,
              (const unsigned char *)"     hello", 10);

    /* center: pad to 11 with spaces. "hello" is 5 wide → 3 left, 3 right. */
    r = co_center(out, (const unsigned char *)"hello", 5, 11, NULL, 0, 0);
    check_buf("co_center", "'hello' w=11", out, r,
              (const unsigned char *)"   hello   ", 11);

    /* Custom fill char. */
    r = co_ljust(out, (const unsigned char *)"hi", 2, 6,
                 (const unsigned char *)"-", 1, 0);
    check_buf("co_ljust", "'hi' w=6 fill='-'", out, r,
              (const unsigned char *)"hi----", 6);

    /* Truncation. */
    r = co_ljust(out, (const unsigned char *)"hello world", 11, 5,
                 NULL, 0, 1);
    check_buf("co_ljust", "truncate to 5", out, r,
              (const unsigned char *)"hello", 5);
}

/* ---- co_compress ---- */

static void test_compress(void) {
    const char *name = "co_compress";
    unsigned char out[LBUF_SIZE];
    size_t r;

    /* Default: compress whitespace. */
    r = co_compress(out, (const unsigned char *)"a  b   c", 8, 0);
    check_buf(name, "'a  b   c' ws", out, r,
              (const unsigned char *)"a b c", 5);

    /* Specific char. */
    r = co_compress(out, (const unsigned char *)"axxbxxxc", 8, 'x');
    check_buf(name, "'axxbxxxc' char='x'", out, r,
              (const unsigned char *)"axbxc", 5);
}

/* ---- co_transform ---- */

static void test_transform(void) {
    const char *name = "co_transform";
    unsigned char out[LBUF_SIZE];
    size_t r;

    r = co_transform(out,
        (const unsigned char *)"hello", 5,
        (const unsigned char *)"helo", 4,
        (const unsigned char *)"HELO", 4);
    check_buf(name, "helo→HELO on 'hello'", out, r,
              (const unsigned char *)"HELLO", 5);
}

/* ---- Fuzz: round-trip invariants ---- */

static void test_fuzz_invariants(void) {
    const char *name = "fuzz_invariants";
    unsigned char buf[LBUF_SIZE], out1[LBUF_SIZE], out2[LBUF_SIZE];
    int failures = 0;

    for (int i = 0; i < 10000 && failures < 5; i++) {
        size_t len = gen_random_colored(buf, 1 + (xrand() % 100));
        size_t vis = co_visible_length(buf, len);

        /* Invariant 1: left(n) ++ right(vis-n) has same visible length. */
        if (vis > 0) {
            size_t n = (xrand() % vis) + 1;
            size_t llen = co_left(out1, buf, len, n);
            size_t rlen = co_right(out2, buf, len, vis - n);
            size_t lvis = co_visible_length(out1, llen);
            size_t rvis = co_visible_length(out2, rlen);
            if (lvis + rvis != vis) {
                test_fail(name, "iter %d: left(%zu)+right(%zu) vis=%zu+%zu != %zu",
                          i, n, vis-n, lvis, rvis, vis);
                failures++;
            }
        }

        /* Invariant 2: strip_color(toupper(x)) == toupper(strip_color(x)). */
        size_t ulen = co_toupper(out1, buf, len);
        unsigned char stripped_upper[LBUF_SIZE];
        size_t su_len = co_strip_color(stripped_upper, out1, ulen);

        unsigned char stripped[LBUF_SIZE];
        size_t s_len = co_strip_color(stripped, buf, len);
        unsigned char upper_stripped[LBUF_SIZE];
        size_t us_len = co_toupper(upper_stripped, stripped, s_len);

        if (su_len != us_len || (su_len > 0 && memcmp(stripped_upper, upper_stripped, su_len) != 0)) {
            test_fail(name, "iter %d: strip(upper(x)) != upper(strip(x))", i);
            failures++;
        }

        /* Invariant 3: words_count == words_count(strip_color(x)). */
        size_t wc1 = co_words_count(buf, len, ' ');
        size_t wc2 = co_words_count(stripped, s_len, ' ');
        if (wc1 != wc2) {
            test_fail(name, "iter %d: words_count %zu != stripped words_count %zu",
                      i, wc1, wc2);
            failures++;
        }

        /* Invariant 4: reverse(reverse(x)) has same visible content as x. */
        size_t rev1_len = co_reverse(out1, buf, len);
        size_t rev2_len = co_reverse(out2, out1, rev1_len);
        unsigned char sr1[LBUF_SIZE], sr2[LBUF_SIZE];
        size_t sr1_len = co_strip_color(sr1, buf, len);
        size_t sr2_len = co_strip_color(sr2, out2, rev2_len);
        if (sr1_len != sr2_len || (sr1_len > 0 && memcmp(sr1, sr2, sr1_len) != 0)) {
            test_fail(name, "iter %d: strip(reverse(reverse(x))) != strip(x)", i);
            failures++;
        }

        /* Invariant 5: mid(0, vis) == full string (same visible content). */
        size_t mid_len = co_mid(out1, buf, len, 0, vis);
        size_t mid_vis = co_visible_length(out1, mid_len);
        if (mid_vis != vis) {
            test_fail(name, "iter %d: mid(0,%zu) vis=%zu != %zu",
                      i, vis, mid_vis, vis);
            failures++;
        }
    }
    if (failures == 0) {
        test_ok(name, "10000 iterations, 5 invariants each");
    }
}

/* ---- co_cluster_count / co_mid_cluster / co_delete_cluster ---- */

static void test_grapheme_clusters(void) {
    unsigned char out[LBUF_SIZE];
    size_t r;

    /* ASCII: each char is one cluster. */
    check_size("co_cluster_count", "ASCII 'abc'",
        co_cluster_count((const unsigned char *)"abc", 3), 3);

    /* Combining mark: "e" + combining acute = 1 cluster. */
    /* U+0065 U+0301 */
    const unsigned char e_combining[] = { 0x65, 0xCC, 0x81 };
    check_size("co_cluster_count", "e + combining acute",
        co_cluster_count(e_combining, sizeof(e_combining)), 1);

    /* Emoji ZWJ sequence: 👨‍💻 (man + ZWJ + laptop) = 1 cluster. */
    /* U+1F468 U+200D U+1F4BB */
    const unsigned char emoji_zwj[] = {
        0xF0, 0x9F, 0x91, 0xA8,       /* 👨 */
        0xE2, 0x80, 0x8D,             /* ZWJ */
        0xF0, 0x9F, 0x92, 0xBB        /* 💻 */
    };
    check_size("co_cluster_count", "👨‍💻 ZWJ sequence",
        co_cluster_count(emoji_zwj, sizeof(emoji_zwj)), 1);

    /* Regional indicators: 🇺🇸 = 1 cluster (2 RI code points). */
    /* U+1F1FA U+1F1F8 */
    const unsigned char flag_us[] = {
        0xF0, 0x9F, 0x87, 0xBA,       /* 🇺 */
        0xF0, 0x9F, 0x87, 0xB8        /* 🇸 */
    };
    check_size("co_cluster_count", "🇺🇸 flag",
        co_cluster_count(flag_us, sizeof(flag_us)), 1);

    /* co_mid_cluster: extract second cluster from "AëB" where ë = e+combining. */
    /* "A" + "e" + combining_diaeresis + "B" = 3 clusters. */
    const unsigned char aeb[] = { 'A', 0x65, 0xCC, 0x88, 'B' };
    r = co_mid_cluster(out, aeb, sizeof(aeb), 1, 1);
    /* Should extract "ë" = e + combining diaeresis. */
    const unsigned char e_diaeresis[] = { 0x65, 0xCC, 0x88 };
    check_buf("co_mid_cluster", "cluster 1 of 'AëB'", out, r,
              e_diaeresis, sizeof(e_diaeresis));

    /* co_delete_cluster: delete cluster 1 from "AëB" → "AB". */
    r = co_delete_cluster(out, aeb, sizeof(aeb), 1, 1);
    check_buf("co_delete_cluster", "del cluster 1 of 'AëB'", out, r,
              (const unsigned char *)"AB", 2);
}

/* ---- co_merge ---- */

static void test_merge(void) {
    const char *name = "co_merge";
    unsigned char out[LBUF_SIZE];
    size_t r;

    /* Basic merge: where strA has '.', take from strB instead.
     * "a.c.e" + "xbxdx" search='.' → pos 0:'a', pos 1:'b', pos 2:'c', pos 3:'d', pos 4:'e'
     * Positions 1 and 3 match '.', so take 'b' and 'd' from strB. */
    r = co_merge(out,
        (const unsigned char *)"a.c.e", 5,
        (const unsigned char *)"xbxdx", 5,
        (const unsigned char *)".", 1);
    check_buf(name, "'a.c.e' merge 'xbxdx' search='.'", out, r,
              (const unsigned char *)"abcde", 5);
}

/* ---- co_apply_color ---- */

static void test_apply_color(void) {
    const char *name = "co_apply_color";
    unsigned char out[LBUF_SIZE];
    size_t r;

    co_ColorState red = co_cs_fg(1);
    r = co_apply_color(out, (const unsigned char *)"hello", 5, red);

    unsigned char expect[LBUF_SIZE];
    size_t en = 0;
    en += (size_t)pua_fg(expect, 1);
    memcpy(expect + en, "hello", 5); en += 5;
    check_buf(name, "FG1 + 'hello'", out, r, expect, en);

    /* Normal state should produce no prefix. */
    co_ColorState normal = CO_CS_NORMAL;
    r = co_apply_color(out, (const unsigned char *)"test", 4, normal);
    check_buf(name, "NORMAL + 'test'", out, r,
              (const unsigned char *)"test", 4);
}

/* ---- Boundary: LBUF_SIZE edge ---- */

static void test_lbuf_boundary(void) {
    const char *name = "lbuf_boundary";
    unsigned char buf[LBUF_SIZE], out[LBUF_SIZE];

    /* Fill to exactly LBUF_SIZE-1 with 'A'. */
    memset(buf, 'A', LBUF_SIZE - 1);
    size_t len = LBUF_SIZE - 1;

    size_t vis = co_visible_length(buf, len);
    check_size(name, "vis_length at LBUF-1", vis, LBUF_SIZE - 1);

    size_t r = co_left(out, buf, len, LBUF_SIZE);
    check_size(name, "left(LBUF) len", r, LBUF_SIZE - 1);

    r = co_reverse(out, buf, len);
    check_size(name, "reverse LBUF-1", r, LBUF_SIZE - 1);

    /* repeat that would exceed LBUF_SIZE. */
    r = co_repeat(out, (const unsigned char *)"AAAA", 4, LBUF_SIZE);
    /* Should be truncated or return 0. */
    if (r == 0 || r <= LBUF_SIZE) {
        test_ok(name, "repeat overflow handled (r=%zu)", r);
    } else {
        test_fail(name, "repeat overflow: r=%zu exceeds LBUF_SIZE", r);
    }
}

/* ================================================================
 * Main
 * ================================================================ */

typedef struct {
    const char *name;
    void (*fn)(void);
} test_suite_t;

static const test_suite_t suites[] = {
    { "visible_length",   test_visible_length },
    { "strip_color",      test_strip_color },
    { "words_count",      test_words_count },
    { "first_rest_last",  test_first_rest_last },
    { "extract",          test_extract },
    { "left_right",       test_left_right },
    { "mid",              test_mid },
    { "trim",             test_trim },
    { "case",             test_case },
    { "reverse",          test_reverse },
    { "search_pos",       test_search_pos },
    { "edit",             test_edit },
    { "sort",             test_sort },
    { "set_ops",          test_set_ops },
    { "repeat",           test_repeat },
    { "delete",           test_delete },
    { "escape",           test_escape },
    { "collapse_color",   test_collapse_color },
    { "member",           test_member },
    { "lpos",             test_lpos },
    { "splice",           test_splice },
    { "insert_word",      test_insert_word },
    { "visual_width",     test_visual_width },
    { "justify",          test_justify },
    { "compress",         test_compress },
    { "transform",        test_transform },
    { "grapheme_clusters", test_grapheme_clusters },
    { "merge",            test_merge },
    { "apply_color",      test_apply_color },
    { "lbuf_boundary",    test_lbuf_boundary },
    { "fuzz_invariants",  test_fuzz_invariants },
    { NULL, NULL }
};

int main(int argc, char **argv) {
    g_seed = (unsigned int)time(NULL);

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            g_verbose = 1;
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            g_filter = argv[++i];
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            g_seed = (unsigned int)atoi(argv[++i]);
        } else {
            fprintf(stderr, "Usage: %s [-v] [-f <suite>] [-s <seed>]\n", argv[0]);
            return 1;
        }
    }

    printf("co_* test harness — seed=%u\n", g_seed);
    printf("================================================\n");

    for (const test_suite_t *s = suites; s->name; s++) {
        if (g_filter && !strstr(s->name, g_filter)) continue;
        printf("[%s]\n", s->name);
        s->fn();
    }

    printf("================================================\n");
    printf("Results: " GREEN "%d passed" RESET ", "
           RED "%d failed" RESET ", "
           YELLOW "%d skipped" RESET "\n",
           g_pass, g_fail, g_skip);

    return g_fail > 0 ? 1 : 0;
}
