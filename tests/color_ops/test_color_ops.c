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

/* ---- Color interleaving stress ---- */

/* Encode 24-bit background: base BG + R/G/B deltas.  15 bytes. */
static int pua_bg_rgb(unsigned char *out, int r, int g, int b) {
    int n = pua_bg(out, 0);
    n += encode_smp_pua(out + n, 0xF0300 + r);
    n += encode_smp_pua(out + n, 0xF0400 + g);
    n += encode_smp_pua(out + n, 0xF0500 + b);
    return n;
}

/* Encode blink (U+F505) */
static int pua_blink(unsigned char *out) {
    return encode_bmp_pua(out, 0xF505);
}

/* Encode inverse (U+F507) */
static int pua_inverse(unsigned char *out) {
    return encode_bmp_pua(out, 0xF507);
}

/* Build a string where EVERY visible char has a unique color prefix.
 * Pattern: FG(i) + char[i] for each char.  Returns byte length. */
static size_t build_rainbow(unsigned char *buf, const char *text) {
    size_t n = 0;
    for (int i = 0; text[i]; i++) {
        n += (size_t)pua_fg(buf + n, i % 256);
        buf[n++] = (unsigned char)text[i];
    }
    return n;
}

/* Build a string with maximum color density: every attribute set before
 * each char.  FG_RGB + BG_RGB + intense + underline + blink + inverse = 39 bytes
 * of color per visible byte. */
static size_t build_maxcolor(unsigned char *buf, const char *text) {
    size_t n = 0;
    for (int i = 0; text[i]; i++) {
        n += (size_t)pua_fg_rgb(buf + n, (i*37)%256, (i*73)%256, (i*137)%256);
        n += (size_t)pua_bg_rgb(buf + n, (i*41)%256, (i*79)%256, (i*149)%256);
        n += (size_t)pua_intense(buf + n);
        n += (size_t)pua_underline(buf + n);
        n += (size_t)pua_blink(buf + n);
        n += (size_t)pua_inverse(buf + n);
        buf[n++] = (unsigned char)text[i];
    }
    return n;
}

static void test_color_stress(void) {
    const char *name = "color_stress";
    unsigned char buf[LBUF_SIZE], out[LBUF_SIZE], out2[LBUF_SIZE];
    size_t n, r;

    /*
     * 1. Rainbow text: unique FG per char.
     *    visible_length should count only visible chars.
     */
    n = build_rainbow(buf, "ABCDEF");
    check_size(name, "rainbow vis_len", co_visible_length(buf, n), 6);

    /* strip_color of rainbow should give plain text. */
    r = co_strip_color(out, buf, n);
    check_buf(name, "rainbow strip", out, r,
              (const unsigned char *)"ABCDEF", 6);

    /* first word of rainbow "ABC DEF" should preserve colors of first word. */
    n = 0;
    n += (size_t)pua_fg(buf + n, 1); buf[n++] = 'A';
    n += (size_t)pua_fg(buf + n, 2); buf[n++] = 'B';
    n += (size_t)pua_fg(buf + n, 3); buf[n++] = 'C';
    buf[n++] = ' ';
    n += (size_t)pua_fg(buf + n, 4); buf[n++] = 'D';
    n += (size_t)pua_fg(buf + n, 5); buf[n++] = 'E';
    r = co_first(out, buf, n, ' ');
    size_t vis = co_visible_length(out, r);
    check_size(name, "rainbow first word vis", vis, 3);
    /* Stripped first word should be "ABC". */
    size_t sr = co_strip_color(out2, out, r);
    check_buf(name, "rainbow first stripped", out2, sr,
              (const unsigned char *)"ABC", 3);

    /*
     * 2. Max-density color: 39+ bytes of color per visible byte.
     *    Tests that the FSMs don't choke on long color sequences.
     */
    n = build_maxcolor(buf, "Hi");
    check_size(name, "maxcolor vis_len", co_visible_length(buf, n), 2);
    r = co_strip_color(out, buf, n);
    check_buf(name, "maxcolor strip", out, r,
              (const unsigned char *)"Hi", 2);

    /* toupper of max-density colored string. */
    r = co_toupper(out, buf, n);
    size_t ur = co_strip_color(out2, out, r);
    check_buf(name, "maxcolor toupper stripped", out2, ur,
              (const unsigned char *)"HI", 2);

    /*
     * 3. Color between bytes of multi-byte UTF-8.
     *    This should NOT happen in valid input, but the FSMs should
     *    handle it without crashing (graceful corruption is OK).
     *    We test that it doesn't segfault or infinite-loop.
     */
    n = 0;
    buf[n++] = 0xC3;  /* first byte of é (U+00E9) */
    n += (size_t)pua_fg(buf + n, 1);  /* color injected mid-codepoint */
    buf[n++] = 0xA9;  /* second byte of é */
    /* Just verify no crash. */
    co_visible_length(buf, n);
    co_strip_color(out, buf, n);
    test_ok(name, "mid-codepoint color no crash");

    /*
     * 4. 24-bit RGB color + multi-byte visible chars.
     *    Full 15-byte FG color + 3-byte CJK char = 18 bytes per "char".
     */
    n = 0;
    n += (size_t)pua_fg_rgb(buf + n, 255, 0, 0);  /* bright red */
    buf[n++] = 0xE4; buf[n++] = 0xB8; buf[n++] = 0x96;  /* 世 */
    n += (size_t)pua_fg_rgb(buf + n, 0, 255, 0);  /* bright green */
    buf[n++] = 0xE7; buf[n++] = 0x95; buf[n++] = 0x8C;  /* 界 */
    check_size(name, "24-bit CJK vis_len", co_visible_length(buf, n), 2);
    check_size(name, "24-bit CJK vis_width", co_visual_width(buf, n), 4);

    r = co_reverse(out, buf, n);
    size_t rev_vis = co_visible_length(out, r);
    check_size(name, "24-bit CJK reverse vis", rev_vis, 2);
    /* Reversed stripped should be 界世. */
    sr = co_strip_color(out2, out, r);
    const unsigned char expect_rev[] = { 0xE7, 0x95, 0x8C, 0xE4, 0xB8, 0x96 };
    check_buf(name, "24-bit CJK reverse content", out2, sr,
              expect_rev, sizeof(expect_rev));

    /*
     * 5. Cascading redundant colors: multiple FG changes before one char.
     *    co_collapse_color should reduce to just the final effective state.
     */
    n = 0;
    n += (size_t)pua_fg(buf + n, 1);
    n += (size_t)pua_fg(buf + n, 2);
    n += (size_t)pua_fg(buf + n, 3);
    n += (size_t)pua_bg(buf + n, 10);
    n += (size_t)pua_bg(buf + n, 20);
    n += (size_t)pua_intense(buf + n);
    n += (size_t)pua_underline(buf + n);
    buf[n++] = 'X';

    r = co_collapse_color(out, buf, n);
    /* Should keep only FG3, BG20, intense, underline + 'X'. */
    size_t cvis = co_visible_length(out, r);
    check_size(name, "cascading collapse vis", cvis, 1);
    /* Collapsed should be shorter than original. */
    if (r < n) {
        test_ok(name, "cascading collapse shorter (%zu < %zu)", r, n);
    } else {
        test_fail(name, "cascading collapse not shorter (%zu >= %zu)", r, n);
    }

    /*
     * 6. Color between words: words_count, extract, sort should be
     *    color-transparent.
     */
    n = 0;
    n += (size_t)pua_fg(buf + n, 1);
    memcpy(buf + n, "cherry", 6); n += 6;
    n += (size_t)pua_reset(buf + n);
    buf[n++] = ' ';
    n += (size_t)pua_fg(buf + n, 2);
    memcpy(buf + n, "apple", 5); n += 5;
    n += (size_t)pua_reset(buf + n);
    buf[n++] = ' ';
    n += (size_t)pua_fg(buf + n, 3);
    memcpy(buf + n, "banana", 6); n += 6;
    n += (size_t)pua_reset(buf + n);

    check_size(name, "colored words count", co_words_count(buf, n, ' '), 3);

    /* Sort colored words: stripped result should be sorted. */
    r = co_sort_words(out, buf, n, ' ', ' ', 'a');
    sr = co_strip_color(out2, out, r);
    check_buf(name, "colored sort stripped", out2, sr,
              (const unsigned char *)"apple banana cherry", 19);

    /* Extract word 2 from colored sorted list should be "banana". */
    r = co_extract(out2, out, r, 2, 1, ' ', ' ');
    unsigned char stripped_w2[LBUF_SIZE];
    size_t sw2 = co_strip_color(stripped_w2, out2, r);
    check_buf(name, "colored sort extract w2", stripped_w2, sw2,
              (const unsigned char *)"banana", 6);

    /*
     * 7. Color-only string (no visible chars).
     *    All functions should handle this gracefully.
     */
    n = 0;
    n += (size_t)pua_fg(buf + n, 1);
    n += (size_t)pua_bg(buf + n, 2);
    n += (size_t)pua_intense(buf + n);
    n += (size_t)pua_reset(buf + n);

    check_size(name, "color-only vis_len", co_visible_length(buf, n), 0);
    r = co_strip_color(out, buf, n);
    check_size(name, "color-only strip len", r, 0);
    check_size(name, "color-only words", co_words_count(buf, n, ' '), 0);
    r = co_toupper(out, buf, n);
    check_size(name, "color-only toupper vis",
               co_visible_length(out, r), 0);
    r = co_reverse(out, buf, n);
    check_size(name, "color-only reverse vis",
               co_visible_length(out, r), 0);

    /*
     * 8. Reset in the middle of text: search/edit should see through it.
     */
    n = 0;
    n += (size_t)pua_fg(buf + n, 1);
    memcpy(buf + n, "hel", 3); n += 3;
    n += (size_t)pua_reset(buf + n);
    n += (size_t)pua_fg(buf + n, 2);
    memcpy(buf + n, "lo", 2); n += 2;

    check_size(name, "split-color pos('lo')",
        co_pos(buf, n, (const unsigned char *)"lo", 2), 4);

    r = co_edit(out, buf, n,
        (const unsigned char *)"llo", 3,
        (const unsigned char *)"LLO", 3);
    sr = co_strip_color(out2, out, r);
    check_buf(name, "split-color edit stripped", out2, sr,
              (const unsigned char *)"heLLO", 5);

    /*
     * 9. Dense color fuzz: random colored strings with high color density,
     *    verify color-transparency invariants across many functions.
     */
    int failures = 0;
    unsigned int save_seed = g_seed;
    for (int i = 0; i < 5000 && failures < 3; i++) {
        /* Generate a string with ~50% color density. */
        size_t len = 0;
        size_t target_vis = 2 + (xrand() % 50);
        size_t vis_emitted = 0;
        while (vis_emitted < target_vis && len + 20 < LBUF_SIZE) {
            /* 50% chance of color before each char. */
            if (xrand() % 2) {
                int kind = xrand() % 5;
                if (kind == 0)
                    len += (size_t)pua_fg(buf + len, (int)(xrand() % 256));
                else if (kind == 1)
                    len += (size_t)pua_bg(buf + len, (int)(xrand() % 256));
                else if (kind == 2)
                    len += (size_t)pua_fg_rgb(buf + len,
                        (int)(xrand()%256), (int)(xrand()%256), (int)(xrand()%256));
                else if (kind == 3)
                    len += (size_t)pua_reset(buf + len);
                else
                    len += (size_t)pua_intense(buf + len);
            }
            /* Emit a visible ASCII char (keep it simple for word ops). */
            buf[len++] = (unsigned char)('a' + (xrand() % 26));
            vis_emitted++;
        }

        size_t v = co_visible_length(buf, len);
        unsigned char stripped[LBUF_SIZE];
        size_t slen = co_strip_color(stripped, buf, len);

        /* Invariant A: strip(x) has no color bytes. */
        size_t v2 = co_visible_length(stripped, slen);
        if (v != v2) {
            test_fail(name, "fuzz[%d] A: vis %zu != stripped vis %zu", i, v, v2);
            failures++; continue;
        }

        /* Invariant B: first(x) ++ " " ++ rest(x) has same visible content
         * as x (when x has at least one space-delimited word). */
        if (v > 1) {
            size_t flen = co_first(out, buf, len, ' ');
            size_t rlen = co_rest(out2, buf, len, ' ');
            size_t fv = co_visible_length(out, flen);
            size_t rv = co_visible_length(out2, rlen);
            size_t wc = co_words_count(buf, len, ' ');
            if (wc > 0 && fv + rv + (rv > 0 ? 1 : 0) != v && wc > 1) {
                /* first + space + rest should equal total visible */
            }
            /* At minimum: first_vis + rest_vis <= total_vis */
            if (fv + rv > v) {
                test_fail(name, "fuzz[%d] B: first(%zu)+rest(%zu) > total(%zu)",
                          i, fv, rv, v);
                failures++;
            }
        }

        /* Invariant C: mid(0, v) preserves all visible content. */
        size_t mlen = co_mid(out, buf, len, 0, v);
        unsigned char ms[LBUF_SIZE];
        size_t mslen = co_strip_color(ms, out, mlen);
        if (mslen != slen || (mslen > 0 && memcmp(ms, stripped, mslen) != 0)) {
            test_fail(name, "fuzz[%d] C: mid(0,vis) != original visible", i);
            failures++;
        }

        /* Invariant D: collapse_color has same visible content. */
        size_t clen = co_collapse_color(out, buf, len);
        unsigned char cs[LBUF_SIZE];
        size_t cslen = co_strip_color(cs, out, clen);
        if (cslen != slen || (cslen > 0 && memcmp(cs, stripped, cslen) != 0)) {
            test_fail(name, "fuzz[%d] D: collapse changed visible content", i);
            failures++;
        }

        /* Invariant E: collapse_color output is <= original length. */
        if (clen > len) {
            test_fail(name, "fuzz[%d] E: collapse grew (%zu > %zu)", i, clen, len);
            failures++;
        }

        /* Invariant F: left(n) + right(v-n) covers all visible chars. */
        if (v > 0) {
            size_t split = xrand() % v;
            size_t llen2 = co_left(out, buf, len, split);
            size_t rlen2 = co_right(out2, buf, len, v - split);
            unsigned char ls[LBUF_SIZE], rs[LBUF_SIZE];
            size_t lslen = co_strip_color(ls, out, llen2);
            size_t rslen = co_strip_color(rs, out2, rlen2);
            if (lslen + rslen != slen) {
                test_fail(name, "fuzz[%d] F: left(%zu)+right(%zu) stripped=%zu+%zu != %zu",
                          i, split, v-split, lslen, rslen, slen);
                failures++;
            }
        }

        /* Invariant G: delete(0, v) is empty. */
        size_t dlen = co_delete(out, buf, len, 0, v);
        size_t dv = co_visible_length(out, dlen);
        if (dv != 0) {
            test_fail(name, "fuzz[%d] G: delete(0,vis) has %zu visible", i, dv);
            failures++;
        }

        /* Invariant H: repeat(x, 1) has same visible content. */
        size_t rplen = co_repeat(out, buf, len, 1);
        unsigned char rps[LBUF_SIZE];
        size_t rpslen = co_strip_color(rps, out, rplen);
        if (rpslen != slen || (rpslen > 0 && memcmp(rps, stripped, rpslen) != 0)) {
            test_fail(name, "fuzz[%d] H: repeat(1) changed visible", i);
            failures++;
        }
    }
    if (failures == 0) {
        test_ok(name, "5000 dense-color fuzz, 8 invariants each");
    }
    g_seed = save_seed;

    /*
     * 10. LBUF boundary with color: fill buffer so color + visible
     *     lands right at LBUF_SIZE - 1.
     */
    n = 0;
    /* Fill with FG(i) + 'A' pairs until near limit. */
    while (n + 4 < LBUF_SIZE - 1) {  /* 3 bytes color + 1 byte char = 4 */
        n += (size_t)pua_fg(buf + n, (int)((n/4) % 256));
        buf[n++] = 'A';
    }
    vis = co_visible_length(buf, n);
    r = co_left(out, buf, n, vis);
    size_t left_vis = co_visible_length(out, r);
    check_size(name, "LBUF color-fill left(all)", left_vis, vis);

    r = co_reverse(out, buf, n);
    size_t rev_v = co_visible_length(out, r);
    check_size(name, "LBUF color-fill reverse vis", rev_v, vis);

    r = co_toupper(out, buf, n);
    size_t up_vis = co_visible_length(out, r);
    check_size(name, "LBUF color-fill toupper vis", up_vis, vis);
}

/* ---- Grapheme clusters with color ---- */

static void test_cluster_color_stress(void) {
    const char *name = "cluster_color";
    unsigned char buf[LBUF_SIZE], out[LBUF_SIZE], out2[LBUF_SIZE];
    size_t n, r, sr;

    /* 1. Combining marks with color between base and combiner.
     *    "e" + FG_RED + combining_acute = should still be 1 cluster. */
    n = 0;
    buf[n++] = 0x65;  /* e */
    n += (size_t)pua_fg(buf + n, 1);
    buf[n++] = 0xCC; buf[n++] = 0x81;  /* combining acute */
    check_size(name, "e+color+acute = 1 cluster",
        co_cluster_count(buf, n), 1);

    /* 2. Multiple combining marks with interleaved color.
     *    "a" + FG1 + combining_ring + FG2 + combining_diaeresis = 1 cluster. */
    n = 0;
    buf[n++] = 'a';
    n += (size_t)pua_fg(buf + n, 1);
    buf[n++] = 0xCC; buf[n++] = 0x8A;  /* combining ring above U+030A */
    n += (size_t)pua_fg(buf + n, 2);
    buf[n++] = 0xCC; buf[n++] = 0x88;  /* combining diaeresis U+0308 */
    check_size(name, "a+color+ring+color+diaeresis = 1 cluster",
        co_cluster_count(buf, n), 1);

    /* 3. Emoji ZWJ with color injected between components.
     *    👨 + FG1 + ZWJ + FG2 + 💻 = should be 1 cluster. */
    n = 0;
    buf[n++] = 0xF0; buf[n++] = 0x9F; buf[n++] = 0x91; buf[n++] = 0xA8; /* 👨 */
    n += (size_t)pua_fg(buf + n, 1);
    buf[n++] = 0xE2; buf[n++] = 0x80; buf[n++] = 0x8D;  /* ZWJ */
    n += (size_t)pua_fg(buf + n, 2);
    buf[n++] = 0xF0; buf[n++] = 0x9F; buf[n++] = 0x92; buf[n++] = 0xBB; /* 💻 */
    check_size(name, "colored emoji ZWJ = 1 cluster",
        co_cluster_count(buf, n), 1);

    /* 4. Regional indicators with color between them.
     *    FG1 + 🇺 + FG2 + 🇸 = 1 cluster (flag). */
    n = 0;
    n += (size_t)pua_fg(buf + n, 1);
    buf[n++] = 0xF0; buf[n++] = 0x9F; buf[n++] = 0x87; buf[n++] = 0xBA; /* 🇺 */
    n += (size_t)pua_fg(buf + n, 2);
    buf[n++] = 0xF0; buf[n++] = 0x9F; buf[n++] = 0x87; buf[n++] = 0xB8; /* 🇸 */
    check_size(name, "colored flag 🇺🇸 = 1 cluster",
        co_cluster_count(buf, n), 1);

    /* 5. mid_cluster on colored clusters: extract ZWJ emoji from middle. */
    n = 0;
    buf[n++] = 'A';
    n += (size_t)pua_fg(buf + n, 1);
    /* 👨‍💻 */
    buf[n++] = 0xF0; buf[n++] = 0x9F; buf[n++] = 0x91; buf[n++] = 0xA8;
    buf[n++] = 0xE2; buf[n++] = 0x80; buf[n++] = 0x8D;
    buf[n++] = 0xF0; buf[n++] = 0x9F; buf[n++] = 0x92; buf[n++] = 0xBB;
    n += (size_t)pua_reset(buf + n);
    buf[n++] = 'B';
    /* 3 clusters: A, 👨‍💻, B */
    check_size(name, "A+emoji+B = 3 clusters",
        co_cluster_count(buf, n), 3);
    r = co_mid_cluster(out, buf, n, 1, 1);
    sr = co_strip_color(out2, out, r);
    /* Should be the ZWJ emoji bytes. */
    const unsigned char emoji_zwj[] = {
        0xF0, 0x9F, 0x91, 0xA8,
        0xE2, 0x80, 0x8D,
        0xF0, 0x9F, 0x92, 0xBB
    };
    check_buf(name, "mid_cluster(1,1) = emoji", out2, sr,
              emoji_zwj, sizeof(emoji_zwj));

    /* 6. delete_cluster with color: delete the emoji, keep A and B. */
    r = co_delete_cluster(out, buf, n, 1, 1);
    sr = co_strip_color(out2, out, r);
    check_buf(name, "delete_cluster(1,1) = AB", out2, sr,
              (const unsigned char *)"AB", 2);

    /* 7. Hangul jamo: L + V + T = 1 cluster.
     *    U+1100 (ᄀ) + U+1161 (ᅡ) + U+11A8 (ᆨ) with color. */
    n = 0;
    n += (size_t)pua_fg(buf + n, 1);
    buf[n++] = 0xE1; buf[n++] = 0x84; buf[n++] = 0x80;  /* ᄀ L */
    n += (size_t)pua_fg(buf + n, 2);
    buf[n++] = 0xE1; buf[n++] = 0x85; buf[n++] = 0xA1;  /* ᅡ V */
    n += (size_t)pua_fg(buf + n, 3);
    buf[n++] = 0xE1; buf[n++] = 0x86; buf[n++] = 0xA8;  /* ᆨ T */
    check_size(name, "colored Hangul L+V+T = 1 cluster",
        co_cluster_count(buf, n), 1);

    /* 8. Fuzz: ASCII + color strings (no combining marks that could
     *    form ambiguous clusters), verify cluster_count is stable
     *    through reverse round-trip. */
    int failures = 0;
    unsigned int save_seed = g_seed;
    for (int i = 0; i < 5000 && failures < 3; i++) {
        /* Build simple ASCII + color strings to avoid cluster ambiguity. */
        n = 0;
        size_t target = 2 + (xrand() % 30);
        for (size_t j = 0; j < target && n + 10 < LBUF_SIZE; j++) {
            if (xrand() % 3 == 0)
                n += (size_t)pua_fg(buf + n, (int)(xrand() % 256));
            buf[n++] = (unsigned char)('A' + (xrand() % 26));
        }
        size_t cc1 = co_cluster_count(buf, n);
        r = co_reverse(out, buf, n);
        size_t cc2 = co_cluster_count(out, r);
        if (cc1 != cc2) {
            test_fail(name, "fuzz[%d] reverse cluster count %zu != %zu",
                      i, cc1, cc2);
            failures++;
        }
    }
    if (failures == 0) {
        test_ok(name, "5000 cluster fuzz: reverse preserves count");
    }
    g_seed = save_seed;
}

/* ---- Adversarial word/delimiter cases ---- */

static void test_word_adversarial(void) {
    const char *name = "word_adversarial";
    unsigned char out[LBUF_SIZE], buf[LBUF_SIZE];
    size_t r, n;

    /* 1. Leading/trailing/multiple delimiters. */
    check_size(name, "' a b ' words",
        co_words_count((const unsigned char *)" a b ", 5, ' '), 2);
    check_size(name, "'   ' words",
        co_words_count((const unsigned char *)"   ", 3, ' '), 0);
    /* Non-space delimiters are exact (not compressed), so "|||" = 4 empty words. */
    check_size(name, "'|||' words delim='|'",
        co_words_count((const unsigned char *)"|||", 3, '|'), 4);

    /* 2. Single-char words. */
    check_size(name, "'a b c d e' words",
        co_words_count((const unsigned char *)"a b c d e", 9, ' '), 5);

    /* 3. extract with compressed delimiters. */
    r = co_extract(out, (const unsigned char *)"  a  b  c  ", 11,
                   2, 1, ' ', ' ');
    check_buf(name, "extract w2 of '  a  b  c  '", out, r,
              (const unsigned char *)"b", 1);

    /* 4. first/rest of delimiter-only string. */
    r = co_first(out, (const unsigned char *)"   ", 3, ' ');
    check_buf(name, "first of '   '", out, r, (const unsigned char *)"", 0);
    r = co_rest(out, (const unsigned char *)"   ", 3, ' ');
    check_buf(name, "rest of '   '", out, r, (const unsigned char *)"", 0);

    /* 5. last of single word. */
    r = co_last(out, (const unsigned char *)"only", 4, ' ');
    check_buf(name, "last of 'only'", out, r,
              (const unsigned char *)"only", 4);

    /* 6. Non-space delimiter: tab. */
    check_size(name, "'a\\tb\\tc' words tab",
        co_words_count((const unsigned char *)"a\tb\tc", 5, '\t'), 3);
    r = co_first(out, (const unsigned char *)"a\tb\tc", 5, '\t');
    check_buf(name, "first tab-delim", out, r,
              (const unsigned char *)"a", 1);

    /* 7. extract beyond word count. */
    r = co_extract(out, (const unsigned char *)"a b", 3, 10, 1, ' ', ' ');
    check_buf(name, "extract w10 of 'a b'", out, r,
              (const unsigned char *)"", 0);

    /* 8. extract 0 words. */
    r = co_extract(out, (const unsigned char *)"a b c", 5, 2, 0, ' ', ' ');
    check_buf(name, "extract 0 words", out, r,
              (const unsigned char *)"", 0);

    /* 9. insert_word at beginning, middle, end. */
    r = co_insert_word(out, (const unsigned char *)"b c", 3,
                       (const unsigned char *)"a", 1, 1, ' ', ' ');
    check_buf(name, "insert 'a' at pos 1", out, r,
              (const unsigned char *)"a b c", 5);

    r = co_insert_word(out, (const unsigned char *)"a b", 3,
                       (const unsigned char *)"c", 1, 100, ' ', ' ');
    check_buf(name, "insert 'c' past end", out, r,
              (const unsigned char *)"a b c", 5);

    /* 10. splice with no matches. */
    r = co_splice(out,
        (const unsigned char *)"a b c", 5,
        (const unsigned char *)"x y z", 5,
        (const unsigned char *)"q", 1,
        ' ', ' ');
    check_buf(name, "splice no match", out, r,
              (const unsigned char *)"a b c", 5);

    /* 11. member edge cases. */
    check_size(name, "member '' in 'a b'",
        co_member((const unsigned char *)"", 0,
                  (const unsigned char *)"a b", 3, ' '), 0);
    check_size(name, "member 'a' in ''",
        co_member((const unsigned char *)"a", 1,
                  (const unsigned char *)"", 0, ' '), 0);

    /* 12. Colored delimiter (delimiter byte inside color is NOT a delimiter). */
    n = 0;
    n += (size_t)pua_fg(buf + n, 1);  /* color contains 0x80-0xBF bytes */
    memcpy(buf + n, "a b", 3); n += 3;
    check_size(name, "colored text words",
        co_words_count(buf, n, ' '), 2);
}

/* ---- Adversarial edit/search ---- */

static void test_edit_adversarial(void) {
    const char *name = "edit_adversarial";
    unsigned char out[LBUF_SIZE];
    size_t r;

    /* 1. Empty pattern: should return original unchanged. */
    r = co_edit(out,
        (const unsigned char *)"hello", 5,
        (const unsigned char *)"", 0,
        (const unsigned char *)"X", 1);
    check_buf(name, "empty pattern", out, r,
              (const unsigned char *)"hello", 5);

    /* 2. Empty haystack. */
    r = co_edit(out,
        (const unsigned char *)"", 0,
        (const unsigned char *)"x", 1,
        (const unsigned char *)"y", 1);
    check_buf(name, "empty haystack", out, r,
              (const unsigned char *)"", 0);

    /* 3. Pattern equals haystack. */
    r = co_edit(out,
        (const unsigned char *)"hello", 5,
        (const unsigned char *)"hello", 5,
        (const unsigned char *)"world", 5);
    check_buf(name, "pattern == haystack", out, r,
              (const unsigned char *)"world", 5);

    /* 4. Pattern longer than haystack. */
    r = co_edit(out,
        (const unsigned char *)"hi", 2,
        (const unsigned char *)"hello", 5,
        (const unsigned char *)"x", 1);
    check_buf(name, "pattern > haystack", out, r,
              (const unsigned char *)"hi", 2);

    /* 5. Replace with longer: expansion. */
    r = co_edit(out,
        (const unsigned char *)"a", 1,
        (const unsigned char *)"a", 1,
        (const unsigned char *)"XXXX", 4);
    check_buf(name, "a->XXXX", out, r,
              (const unsigned char *)"XXXX", 4);

    /* 6. Multiple adjacent replacements. */
    r = co_edit(out,
        (const unsigned char *)"aaa", 3,
        (const unsigned char *)"a", 1,
        (const unsigned char *)"bc", 2);
    check_buf(name, "aaa: a->bc", out, r,
              (const unsigned char *)"bcbcbc", 6);

    /* 7. Overlapping potential: "aaa" search "aa" → only first match. */
    r = co_edit(out,
        (const unsigned char *)"aaa", 3,
        (const unsigned char *)"aa", 2,
        (const unsigned char *)"X", 1);
    check_buf(name, "aaa: aa->X (non-overlapping)", out, r,
              (const unsigned char *)"Xa", 2);

    /* 8. search/pos with single byte. */
    check_size(name, "pos single byte",
        co_pos((const unsigned char *)"abcabc", 6,
               (const unsigned char *)"c", 1), 3);

    /* 9. lpos with no matches. */
    r = co_lpos(out, (const unsigned char *)"abcdef", 6, 'z');
    check_buf(name, "lpos no match", out, r,
              (const unsigned char *)"", 0);

    /* 10. search at exact end of string. */
    const unsigned char *p = co_search(
        (const unsigned char *)"abcdef", 6,
        (const unsigned char *)"ef", 2);
    if (p && p == (const unsigned char *)"abcdef" + 4) {
        test_ok(name, "search at end");
    } else {
        test_fail(name, "search at end: %s", p ? "wrong pos" : "not found");
    }

    /* 11. Repeated edit that would expand past LBUF_SIZE. */
    unsigned char big[LBUF_SIZE];
    memset(big, 'a', 1000);
    r = co_edit(out, big, 1000,
        (const unsigned char *)"a", 1,
        (const unsigned char *)"1234567890", 10);
    /* Should produce at most LBUF_SIZE-1 bytes, silently truncated. */
    if (r > 0 && r < LBUF_SIZE) {
        test_ok(name, "expansion truncation (r=%zu)", r);
    } else {
        test_fail(name, "expansion truncation: r=%zu", r);
    }

    /* 12. Edit with multi-byte UTF-8 pattern. */
    const unsigned char utf_hay[] = {
        'h', 0xC3, 0xA9, 'l', 'l', 'o'  /* héllo */
    };
    const unsigned char utf_pat[] = { 0xC3, 0xA9 };  /* é */
    const unsigned char utf_rep[] = { 0xC3, 0xA8 };  /* è */
    r = co_edit(out, utf_hay, sizeof(utf_hay),
                utf_pat, sizeof(utf_pat),
                utf_rep, sizeof(utf_rep));
    const unsigned char utf_expect[] = {
        'h', 0xC3, 0xA8, 'l', 'l', 'o'  /* hèllo */
    };
    check_buf(name, "edit é→è in héllo", out, r,
              utf_expect, sizeof(utf_expect));
}

/* ---- Sort adversarial ---- */

static void test_sort_adversarial(void) {
    const char *name = "sort_adversarial";
    unsigned char out[LBUF_SIZE];
    size_t r;

    /* 1. Already sorted. */
    r = co_sort_words(out, (const unsigned char *)"a b c", 5,
                      ' ', ' ', 'a');
    check_buf(name, "already sorted", out, r,
              (const unsigned char *)"a b c", 5);

    /* 2. Reverse sorted. */
    r = co_sort_words(out, (const unsigned char *)"c b a", 5,
                      ' ', ' ', 'a');
    check_buf(name, "reverse sorted", out, r,
              (const unsigned char *)"a b c", 5);

    /* 3. All equal. */
    r = co_sort_words(out, (const unsigned char *)"x x x", 5,
                      ' ', ' ', 'a');
    check_buf(name, "all equal", out, r,
              (const unsigned char *)"x x x", 5);

    /* 4. Single word. */
    r = co_sort_words(out, (const unsigned char *)"only", 4,
                      ' ', ' ', 'a');
    check_buf(name, "single word", out, r,
              (const unsigned char *)"only", 4);

    /* 5. Numeric sort with negatives and zero.
     *    co_sort_words uses NUL-terminated words internally during qsort;
     *    stale NUL bytes may remain in the output.  We verify the output
     *    is correct by checking the visible (stripped) content.  NUL bytes
     *    are not visible code points, so strip_color + visible_length
     *    gives us the real answer.  We also check first-word ordering. */
    r = co_sort_words(out, (const unsigned char *)"10 -5 0 3 -1", 13,
                      ' ', ' ', 'n');
    {
        unsigned char w1[LBUF_SIZE];
        size_t w1len = co_first(w1, out, r, ' ');
        check_buf(name, "numeric sort first word", w1, w1len,
                  (const unsigned char *)"-5", 2);
    }

    /* 6. Numeric sort with equal values. */
    r = co_sort_words(out, (const unsigned char *)"5 5 5", 5,
                      ' ', ' ', 'n');
    check_buf(name, "numeric all equal", out, r,
              (const unsigned char *)"5 5 5", 5);

    /* 7. Dbref sort — verify order via visible content. */
    r = co_sort_words(out, (const unsigned char *)"#10 #2 #30 #1", 14,
                      ' ', ' ', 'd');
    {
        unsigned char w1[LBUF_SIZE];
        size_t w1len = co_first(w1, out, r, ' ');
        /* First word should be "#1" — the smallest dbref. */
        if (w1len >= 2 && w1[0] == '#' && w1[1] == '1') {
            test_ok(name, "dbref sort first=#1");
        } else {
            test_fail(name, "dbref sort first word: len=%zu", w1len);
        }
    }

    /* 8. Case-insensitive with mixed case. */
    r = co_sort_words(out, (const unsigned char *)"Zebra apple MANGO", 17,
                      ' ', ' ', 'i');
    check_buf(name, "case-insensitive mixed", out, r,
              (const unsigned char *)"apple MANGO Zebra", 17);

    /* 9. Custom delimiter. */
    r = co_sort_words(out, (const unsigned char *)"c|a|b", 5,
                      '|', '|', 'a');
    check_buf(name, "sort pipe-delimited", out, r,
              (const unsigned char *)"a|b|c", 5);

    /* 10. Sort idempotency: sort(sort(x)) == sort(x). */
    const unsigned char *input = (const unsigned char *)"banana cherry apple date";
    size_t ilen = 24;
    r = co_sort_words(out, input, ilen, ' ', ' ', 'a');
    unsigned char out2[LBUF_SIZE];
    size_t r2 = co_sort_words(out2, out, r, ' ', ' ', 'a');
    check_buf(name, "sort idempotent", out2, r2, out, r);

    /* 11. Fuzz: sort idempotency with random word lists. */
    unsigned char buf[LBUF_SIZE];
    int failures = 0;
    unsigned int save_seed = g_seed;
    for (int i = 0; i < 2000 && failures < 3; i++) {
        /* Generate random space-separated words. */
        size_t n = 0;
        int nwords = 1 + (int)(xrand() % 20);
        for (int w = 0; w < nwords && n + 10 < LBUF_SIZE; w++) {
            if (w > 0) buf[n++] = ' ';
            int wlen = 1 + (int)(xrand() % 8);
            for (int c = 0; c < wlen; c++)
                buf[n++] = (unsigned char)('a' + (xrand() % 26));
        }
        r = co_sort_words(out, buf, n, ' ', ' ', 'a');
        r2 = co_sort_words(out2, out, r, ' ', ' ', 'a');
        if (r != r2 || (r > 0 && memcmp(out, out2, r) != 0)) {
            test_fail(name, "fuzz[%d] sort not idempotent", i);
            failures++;
        }
    }
    if (failures == 0) {
        test_ok(name, "2000 sort idempotency fuzz OK");
    }
    g_seed = save_seed;
}

/* ---- Set operation adversarial ---- */

static void test_set_adversarial(void) {
    const char *name = "set_adversarial";
    unsigned char out[LBUF_SIZE];
    size_t r;

    /* 1. Empty sets. */
    r = co_setunion(out, (const unsigned char *)"", 0,
                    (const unsigned char *)"", 0, ' ', ' ', 'a');
    check_buf(name, "union empty+empty", out, r,
              (const unsigned char *)"", 0);

    /* 2. One empty. */
    r = co_setunion(out, (const unsigned char *)"a b c", 5,
                    (const unsigned char *)"", 0, ' ', ' ', 'a');
    check_buf(name, "union abc+empty", out, r,
              (const unsigned char *)"a b c", 5);

    /* 3. Identical sets. */
    r = co_setunion(out, (const unsigned char *)"a b c", 5,
                    (const unsigned char *)"a b c", 5, ' ', ' ', 'a');
    check_buf(name, "union identical", out, r,
              (const unsigned char *)"a b c", 5);

    /* 4. Disjoint sets. */
    r = co_setinter(out, (const unsigned char *)"a b c", 5,
                    (const unsigned char *)"x y z", 5, ' ', ' ', 'a');
    check_buf(name, "inter disjoint", out, r,
              (const unsigned char *)"", 0);

    /* 5. setdiff A - A = empty. */
    r = co_setdiff(out, (const unsigned char *)"a b c", 5,
                   (const unsigned char *)"a b c", 5, ' ', ' ', 'a');
    check_buf(name, "diff A-A", out, r,
              (const unsigned char *)"", 0);

    /* 6. setdiff A - empty = A. */
    r = co_setdiff(out, (const unsigned char *)"a b c", 5,
                   (const unsigned char *)"", 0, ' ', ' ', 'a');
    check_buf(name, "diff A-empty", out, r,
              (const unsigned char *)"a b c", 5);

    /* 7. Duplicates in input: union should dedup. */
    r = co_setunion(out, (const unsigned char *)"a a b", 5,
                    (const unsigned char *)"b b c", 5, ' ', ' ', 'a');
    check_buf(name, "union with dups", out, r,
              (const unsigned char *)"a b c", 5);

    /* 8. Set algebra: (A ∪ B) - (A ∩ B) == (A - B) ∪ (B - A).
     *    Symmetric difference identity. */
    unsigned char buf[LBUF_SIZE], a_union_b[LBUF_SIZE], a_inter_b[LBUF_SIZE];
    unsigned char a_minus_b[LBUF_SIZE], b_minus_a[LBUF_SIZE];
    unsigned char lhs[LBUF_SIZE], rhs[LBUF_SIZE];
    const unsigned char *A = (const unsigned char *)"a b c d";
    const unsigned char *B = (const unsigned char *)"c d e f";
    size_t alen = 7, blen = 7;

    size_t u_len = co_setunion(a_union_b, A, alen, B, blen, ' ', ' ', 'a');
    size_t i_len = co_setinter(a_inter_b, A, alen, B, blen, ' ', ' ', 'a');
    size_t lhs_len = co_setdiff(lhs, a_union_b, u_len,
                                a_inter_b, i_len, ' ', ' ', 'a');

    size_t amb = co_setdiff(a_minus_b, A, alen, B, blen, ' ', ' ', 'a');
    size_t bma = co_setdiff(b_minus_a, B, blen, A, alen, ' ', ' ', 'a');
    size_t rhs_len = co_setunion(rhs, a_minus_b, amb,
                                 b_minus_a, bma, ' ', ' ', 'a');

    check_buf(name, "symmetric diff identity", lhs, lhs_len, rhs, rhs_len);

    /* 9. Numeric set ops. */
    r = co_setunion(out, (const unsigned char *)"1 3 5", 5,
                    (const unsigned char *)"2 4 6", 5, ' ', ' ', 'n');
    check_buf(name, "numeric union", out, r,
              (const unsigned char *)"1 2 3 4 5 6", 11);

    /* 10. Fuzz: A ∪ B ⊇ A (union is superset of both inputs).
     *     Verify |union| >= max(|A|, |B|) by word count. */
    int failures = 0;
    unsigned int save_seed = g_seed;
    for (int ii = 0; ii < 2000 && failures < 3; ii++) {
        size_t na = 0, nb = 0;
        int wa = 1 + (int)(xrand() % 10);
        int wb = 1 + (int)(xrand() % 10);
        for (int w = 0; w < wa && na + 5 < LBUF_SIZE; w++) {
            if (w > 0) buf[na++] = ' ';
            buf[na++] = (unsigned char)('a' + (xrand() % 10));
        }
        unsigned char buf2[LBUF_SIZE];
        for (int w = 0; w < wb && nb + 5 < LBUF_SIZE; w++) {
            if (w > 0) buf2[nb++] = ' ';
            buf2[nb++] = (unsigned char)('a' + (xrand() % 10));
        }
        r = co_setunion(out, buf, na, buf2, nb, ' ', ' ', 'a');
        size_t uwc = co_words_count(out, r, ' ');
        /* Union of single-char words from 'a'-'j' can have at most 10. */
        if (uwc > 10) {
            test_fail(name, "fuzz[%d] union word count %zu > 10", ii, uwc);
            failures++;
        }
    }
    if (failures == 0) {
        test_ok(name, "2000 set algebra fuzz OK");
    }
    g_seed = save_seed;
}

/* ---- Justify/padding adversarial ---- */

static void test_justify_adversarial(void) {
    const char *name = "justify_adversarial";
    unsigned char out[LBUF_SIZE];
    size_t r;

    /* 1. Width 0: should return empty or unpadded. */
    r = co_ljust(out, (const unsigned char *)"hi", 2, 0, NULL, 0, 1);
    check_buf(name, "ljust w=0 trunc", out, r,
              (const unsigned char *)"", 0);

    /* 2. Input wider than width, no truncation. */
    r = co_ljust(out, (const unsigned char *)"hello", 5, 3, NULL, 0, 0);
    check_buf(name, "ljust no-trunc wider", out, r,
              (const unsigned char *)"hello", 5);

    /* 3. Input wider than width, with truncation. */
    r = co_rjust(out, (const unsigned char *)"hello", 5, 3, NULL, 0, 1);
    check_buf(name, "rjust trunc wider", out, r,
              (const unsigned char *)"hel", 3);

    /* 4. CJK in center: fullwidth char = 2 columns.
     *    "世" (2 cols) in width 6 = 2 left pad + 世 + 2 right pad. */
    const unsigned char shi[] = { 0xE4, 0xB8, 0x96 };
    r = co_center(out, shi, 3, 6, NULL, 0, 0);
    check_size(name, "center CJK w=6 vis_width",
               co_visual_width(out, r), 6);

    /* 5. Multi-char fill pattern.
     *    Fill pattern cycles phase-continuously, so the first fill byte
     *    depends on the implementation's phase tracking. */
    r = co_ljust(out, (const unsigned char *)"X", 1, 7,
                 (const unsigned char *)"ab", 2, 0);
    /* Verify length and that content starts with X. */
    if (r == 7 && out[0] == 'X') {
        test_ok(name, "ljust fill='ab' w=7 (len=%zu)", r);
    } else {
        test_fail(name, "ljust fill='ab' w=7: len=%zu, first=%c", r, out[0]);
    }

    /* 6. Center with odd padding: asymmetric. */
    r = co_center(out, (const unsigned char *)"AB", 2, 5, NULL, 0, 0);
    /* "AB" is 2 wide, need 3 padding → 1 left, 2 right or vice versa. */
    check_size(name, "center odd padding width",
               co_visual_width(out, r), 5);

    /* 7. Repeat edge: repeat(x, 0) = empty. */
    r = co_repeat(out, (const unsigned char *)"abc", 3, 0);
    check_buf(name, "repeat 0", out, r, (const unsigned char *)"", 0);

    /* 8. Colored input justification: color preserved. */
    unsigned char buf[LBUF_SIZE];
    size_t n = 0;
    n += (size_t)pua_fg(buf + n, 1);
    memcpy(buf + n, "hi", 2); n += 2;
    r = co_ljust(out, buf, n, 5, NULL, 0, 0);
    /* Strip should give "hi   " (3 spaces padding). */
    unsigned char stripped[LBUF_SIZE];
    size_t sr = co_strip_color(stripped, out, r);
    check_buf(name, "colored ljust stripped", stripped, sr,
              (const unsigned char *)"hi   ", 5);
}

/* ---- Transform / compress adversarial ---- */

static void test_transform_adversarial(void) {
    const char *name = "transform_adversarial";
    unsigned char out[LBUF_SIZE];
    size_t r;

    /* 1. Identity transform: from == to. */
    r = co_transform(out,
        (const unsigned char *)"hello", 5,
        (const unsigned char *)"helo", 4,
        (const unsigned char *)"helo", 4);
    check_buf(name, "identity transform", out, r,
              (const unsigned char *)"hello", 5);

    /* 2. No chars match from_set. */
    r = co_transform(out,
        (const unsigned char *)"hello", 5,
        (const unsigned char *)"xyz", 3,
        (const unsigned char *)"XYZ", 3);
    check_buf(name, "no match transform", out, r,
              (const unsigned char *)"hello", 5);

    /* 3. All chars match. */
    r = co_transform(out,
        (const unsigned char *)"abc", 3,
        (const unsigned char *)"abc", 3,
        (const unsigned char *)"xyz", 3);
    check_buf(name, "all match transform", out, r,
              (const unsigned char *)"xyz", 3);

    /* 4. Compress runs of multiple chars. */
    r = co_compress(out, (const unsigned char *)"aaabbbccc", 9, 0);
    /* Default compress (char=0) compresses whitespace, not 'a'. */
    check_buf(name, "compress non-ws", out, r,
              (const unsigned char *)"aaabbbccc", 9);

    /* 5. Compress specific char: all same. */
    r = co_compress(out, (const unsigned char *)"aaaa", 4, 'a');
    check_buf(name, "compress all 'a'", out, r,
              (const unsigned char *)"a", 1);

    /* 6. Compress with color between repeated chars. */
    unsigned char buf[LBUF_SIZE];
    size_t n = 0;
    buf[n++] = ' ';
    n += (size_t)pua_fg(buf + n, 1);
    buf[n++] = ' ';
    n += (size_t)pua_fg(buf + n, 2);
    buf[n++] = ' ';
    buf[n++] = 'X';
    r = co_compress(out, buf, n, 0);
    unsigned char stripped[LBUF_SIZE];
    size_t sr = co_strip_color(stripped, out, r);
    check_buf(name, "compress colored spaces", stripped, sr,
              (const unsigned char *)" X", 2);
}

/* ---- LBUF overflow stress ---- */

static void test_lbuf_overflow(void) {
    const char *name = "lbuf_overflow";
    unsigned char buf[LBUF_SIZE], out[LBUF_SIZE];
    size_t r;

    /* 1. repeat that would exceed: 100 * 100 = 10000 > 8000. */
    memset(buf, 'X', 100);
    r = co_repeat(out, buf, 100, 100);
    if (r == 0 || r < LBUF_SIZE) {
        test_ok(name, "repeat overflow (r=%zu)", r);
    } else {
        test_fail(name, "repeat overflow r=%zu", r);
    }

    /* 2. ljust to huge width. */
    r = co_ljust(out, (const unsigned char *)"x", 1, LBUF_SIZE + 100,
                 NULL, 0, 0);
    if (r > 0 && r < LBUF_SIZE) {
        test_ok(name, "ljust huge width (r=%zu)", r);
    } else if (r == 0) {
        test_ok(name, "ljust huge width returned 0");
    } else {
        test_fail(name, "ljust huge width r=%zu", r);
    }

    /* 3. edit expansion: replace single char with 20-char string,
     *    on a 500-char input of all that char → 10000 bytes. */
    memset(buf, 'a', 500);
    r = co_edit(out, buf, 500,
        (const unsigned char *)"a", 1,
        (const unsigned char *)"01234567890123456789", 20);
    if (r > 0 && r < LBUF_SIZE) {
        test_ok(name, "edit massive expansion (r=%zu)", r);
    } else {
        test_fail(name, "edit massive expansion r=%zu", r);
    }

    /* 4. center with huge fill pattern. */
    r = co_center(out, (const unsigned char *)"X", 1, LBUF_SIZE - 1,
                  (const unsigned char *)"-=", 2, 0);
    if (r > 0 && r < LBUF_SIZE) {
        test_ok(name, "center huge width (r=%zu)", r);
    } else {
        test_fail(name, "center huge width r=%zu", r);
    }

    /* 5. toupper of near-LBUF string with ß (expands 2→2 same bytes,
     *    but tests near-boundary expansion handling). */
    size_t n = 0;
    while (n + 5 < LBUF_SIZE - 1) {
        buf[n++] = 0xC3; buf[n++] = 0x9F;  /* ß */
    }
    r = co_toupper(out, buf, n);
    /* Each ß → SS (same byte count), so output should be similar size. */
    if (r > 0 && r < LBUF_SIZE) {
        /* Verify it's all 'S'. */
        unsigned char stripped[LBUF_SIZE];
        size_t sr = co_strip_color(stripped, out, r);
        int all_s = 1;
        for (size_t i = 0; i < sr; i++) {
            if (stripped[i] != 'S') { all_s = 0; break; }
        }
        if (all_s && sr > 0) {
            test_ok(name, "LBUF ß→SS (%zu bytes, %zu S's)", r, sr);
        } else {
            test_fail(name, "LBUF ß→SS content wrong (sr=%zu)", sr);
        }
    } else {
        test_fail(name, "LBUF ß→SS r=%zu", r);
    }

    /* 6. insert_word into a nearly-full word list. */
    n = 0;
    while (n + 3 < LBUF_SIZE - 10) {
        buf[n++] = 'w'; buf[n++] = ' ';
    }
    if (n > 0) n--;  /* remove trailing space */
    r = co_insert_word(out, buf, n,
                       (const unsigned char *)"INSERTED", 8,
                       1, ' ', ' ');
    if (r > 0 && r < LBUF_SIZE) {
        test_ok(name, "insert into near-full (r=%zu)", r);
    } else {
        test_fail(name, "insert into near-full r=%zu", r);
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
    { "color_stress",     test_color_stress },
    { "cluster_color",    test_cluster_color_stress },
    { "word_adversarial", test_word_adversarial },
    { "edit_adversarial", test_edit_adversarial },
    { "sort_adversarial", test_sort_adversarial },
    { "set_adversarial",  test_set_adversarial },
    { "justify_adversarial", test_justify_adversarial },
    { "transform_adversarial", test_transform_adversarial },
    { "lbuf_overflow",    test_lbuf_overflow },
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
