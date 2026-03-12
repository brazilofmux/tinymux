/*
 * test_harness.c — Stage 0 tests for Ragel color_ops primitives.
 *
 * Tests PUA-colored UTF-8 string operations against known-good results.
 * Colored test strings use the actual TinyMUX PUA encoding:
 *
 *   BMP PUA:  EF 94..9F 80..BF   (U+F500-F7FF)
 *   SMP PUA:  F3 B0 80..97 80..BF (U+F0000-F05FF)
 */

#include "color_ops.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) \
    do { printf("  %-50s ", name); } while (0)

#define PASS() \
    do { printf("PASS\n"); g_pass++; } while (0)

#define FAIL(fmt, ...) \
    do { printf("FAIL " fmt "\n", ##__VA_ARGS__); g_fail++; } while (0)

#define CHECK_EQ(name, got, expected) \
    do { \
        TEST(name); \
        if ((got) == (expected)) { PASS(); } \
        else { FAIL("(got %zu, expected %zu)", (size_t)(got), (size_t)(expected)); } \
    } while (0)

/* ---- PUA color byte sequences ---- */

/* U+F500 COLOR_RESET = EF 94 80 */
static const unsigned char COLOR_RESET[] = { 0xEF, 0x94, 0x80 };

/* U+F601 COLOR_FG_RED = EF 98 81 */
static const unsigned char COLOR_FG_RED[] = { 0xEF, 0x98, 0x81 };

/* U+F602 COLOR_FG_GREEN = EF 98 82 */
static const unsigned char COLOR_FG_GREEN[] = { 0xEF, 0x98, 0x82 };

/* U+F700 COLOR_BG_BLACK = EF 9C 80 */
static const unsigned char COLOR_BG_BLACK[] = { 0xEF, 0x9C, 0x80 };

/* U+F0000 RED_FG_DELTA_0 = F3 B0 80 80 (start of SMP color range) */
static const unsigned char RED_FG_DELTA_42[] = { 0xF3, 0xB0, 0x80, 0xAA };

/* U+F0100 GREEN_FG_DELTA_0 = F3 B0 84 80 */
static const unsigned char GREEN_FG_DELTA_17[] = { 0xF3, 0xB0, 0x84, 0x91 };

/* U+F0263 BLUE_FG_DELTA_99 = F3 B0 89 A3 */
static const unsigned char BLUE_FG_DELTA_99[] = { 0xF3, 0xB0, 0x89, 0xA3 };

/*
 * Build a test string by appending chunks.
 * Caller provides a static buffer.
 */
static size_t append(unsigned char *buf, size_t pos,
                     const unsigned char *data, size_t len)
{
    memcpy(buf + pos, data, len);
    return pos + len;
}

static size_t append_str(unsigned char *buf, size_t pos, const char *s)
{
    size_t len = strlen(s);
    memcpy(buf + pos, s, len);
    return pos + len;
}

/* ---- Test: plain ASCII, no color ---- */

static void test_plain_ascii(void)
{
    printf("\n--- Plain ASCII (no color) ---\n");

    const unsigned char *s = (const unsigned char *)"hello world";
    size_t len = 11;

    CHECK_EQ("visible_length(\"hello world\")", co_visible_length(s, len), 11);

    /* skip_color on non-color should return same pointer */
    const unsigned char *skipped = co_skip_color(s, s + len);
    TEST("skip_color on non-color returns same ptr");
    if (skipped == s) { PASS(); } else { FAIL("advanced unexpectedly"); }

    /* visible_advance 5 */
    size_t count;
    const unsigned char *adv = co_visible_advance(s, s + len, 5, &count);
    CHECK_EQ("visible_advance(5) count", count, 5);
    TEST("visible_advance(5) position");
    if (adv == s + 5) { PASS(); } else { FAIL("wrong position"); }

    /* find_delim space */
    const unsigned char *found = co_find_delim(s, s + len, ' ');
    TEST("find_delim(' ') in \"hello world\"");
    if (found == s + 5) { PASS(); } else { FAIL("wrong position"); }

    /* find_delim not present */
    found = co_find_delim(s, s + len, 'Z');
    TEST("find_delim('Z') not found");
    if (found == NULL) { PASS(); } else { FAIL("should be NULL"); }

    /* copy_visible 5 */
    unsigned char out[LBUF_SIZE];
    size_t nb = co_copy_visible(out, s, s + len, 5);
    TEST("copy_visible(5) -> \"hello\"");
    if (nb == 5 && memcmp(out, "hello", 5) == 0) { PASS(); }
    else { FAIL("got %zu bytes", nb); }
}

/* ---- Test: simple BMP color ---- */

static void test_bmp_color(void)
{
    printf("\n--- BMP PUA color ---\n");

    /* Build: RED "hi" RESET = [FG_RED] h i [RESET] */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "hi");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    /* Total bytes: 3 + 2 + 3 = 8.  Visible: 2 */
    CHECK_EQ("visible_length(RED\"hi\"RESET)", co_visible_length(buf, pos), 2);

    /* skip_color at start should jump over RED */
    const unsigned char *skipped = co_skip_color(buf, buf + pos);
    TEST("skip_color skips FG_RED prefix");
    if (skipped == buf + 3) { PASS(); } else { FAIL("got offset %td", skipped - buf); }

    /* visible_advance 1 should land after RED + 'h' */
    size_t count;
    const unsigned char *adv = co_visible_advance(buf, buf + pos, 1, &count);
    CHECK_EQ("visible_advance(1) count", count, 1);
    TEST("visible_advance(1) position (after RED+'h')");
    if (adv == buf + 4) { PASS(); } else { FAIL("offset %td", adv - buf); }

    /* find_delim 'i' should find it at offset 4 (past RED + 'h') */
    const unsigned char *found = co_find_delim(buf, buf + pos, 'i');
    TEST("find_delim('i') skips color prefix");
    if (found && *found == 'i') { PASS(); } else { FAIL("not found or wrong"); }

    /* copy_visible(1) should copy RED + 'h' (color precedes visible char) */
    unsigned char out[LBUF_SIZE];
    size_t nb = co_copy_visible(out, buf, buf + pos, 1);
    TEST("copy_visible(1) preserves leading color");
    /* Should be: EF 98 81 'h' = 4 bytes */
    if (nb == 4 && memcmp(out, buf, 4) == 0) { PASS(); }
    else { FAIL("got %zu bytes", nb); }

    /* copy_visible(2) copies RED + 'h' + 'i' = 5 bytes.
     * Trailing RESET is not copied — it follows the last visible char,
     * not precedes one.  Callers append reset separately if needed. */
    nb = co_copy_visible(out, buf, buf + pos, 2);
    TEST("copy_visible(2) copies color + both visible chars");
    if (nb == 5 && memcmp(out, buf, 5) == 0) { PASS(); }
    else { FAIL("got %zu bytes, expected 5", nb); }
}

/* ---- Test: 24-bit color with SMP deltas ---- */

static void test_24bit_color(void)
{
    printf("\n--- 24-bit color (SMP PUA deltas) ---\n");

    /* Build: [FG_RED][R_DELTA_42][G_DELTA_17][B_DELTA_99] "AB" [RESET] */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);        /* XTERM base */
    pos = append(buf, pos, RED_FG_DELTA_42, 4);     /* R delta */
    pos = append(buf, pos, GREEN_FG_DELTA_17, 4);   /* G delta */
    pos = append(buf, pos, BLUE_FG_DELTA_99, 4);    /* B delta */
    pos = append_str(buf, pos, "AB");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    /* Total: 3 + 4 + 4 + 4 + 2 + 3 = 20 bytes.  Visible: 2 */
    CHECK_EQ("visible_length(24bit \"AB\" RESET)", co_visible_length(buf, pos), 2);

    /* skip_color should jump all 4 color code points (15 bytes) */
    const unsigned char *skipped = co_skip_color(buf, buf + pos);
    TEST("skip_color jumps 15-byte 24-bit color annotation");
    if (skipped == buf + 15) { PASS(); } else { FAIL("offset %td", skipped - buf); }

    /* find_delim 'B' */
    const unsigned char *found = co_find_delim(buf, buf + pos, 'B');
    TEST("find_delim('B') past 24-bit color");
    if (found && *found == 'B') { PASS(); } else { FAIL("not found"); }

    /* copy_visible(1) should copy all color + 'A' = 16 bytes */
    unsigned char out[LBUF_SIZE];
    size_t nb = co_copy_visible(out, buf, buf + pos, 1);
    TEST("copy_visible(1) preserves 24-bit color prefix");
    if (nb == 16 && memcmp(out, buf, 16) == 0) { PASS(); }
    else { FAIL("got %zu bytes, expected 16", nb); }
}

/* ---- Test: multi-byte visible UTF-8 with interleaved color ---- */

static void test_utf8_with_color(void)
{
    printf("\n--- Multi-byte UTF-8 with interleaved color ---\n");

    /* U+00E9 (e-acute) = C3 A9 (2-byte UTF-8) */
    /* U+4E16 (CJK "world") = E4 B8 96 (3-byte UTF-8) */
    /* U+1F600 (grinning face) = F0 9F 98 80 (4-byte UTF-8) */

    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_GREEN, 3);
    pos = append(buf, pos, (const unsigned char *)"\xC3\xA9", 2);  /* e-acute */
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append(buf, pos, (const unsigned char *)"\xE4\xB8\x96", 3);  /* CJK */
    pos = append(buf, pos, COLOR_RESET, 3);
    pos = append(buf, pos, (const unsigned char *)"\xF0\x9F\x98\x80", 4);  /* emoji */
    buf[pos] = '\0';

    /* Visible: e-acute + CJK + emoji = 3 */
    CHECK_EQ("visible_length(colored UTF-8 mix)", co_visible_length(buf, pos), 3);

    /* visible_advance(2) should land after GREEN + e-acute + RED + CJK */
    size_t count;
    const unsigned char *adv = co_visible_advance(buf, buf + pos, 2, &count);
    CHECK_EQ("visible_advance(2) count", count, 2);
    TEST("visible_advance(2) lands at RESET");
    /* Expected offset: 3(GREEN) + 2(e-acute) + 3(RED) + 3(CJK) = 11 */
    if (adv == buf + 11) { PASS(); } else { FAIL("offset %td, expected 11", adv - buf); }
}

/* ---- Test: color-only string (no visible content) ---- */

static void test_color_only(void)
{
    printf("\n--- Color-only string ---\n");

    unsigned char buf[64];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append(buf, pos, COLOR_BG_BLACK, 3);
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    CHECK_EQ("visible_length(color-only)", co_visible_length(buf, pos), 0);

    const unsigned char *found = co_find_delim(buf, buf + pos, ' ');
    TEST("find_delim in color-only -> NULL");
    if (found == NULL) { PASS(); } else { FAIL("found something"); }

    unsigned char out[LBUF_SIZE];
    size_t nb = co_copy_visible(out, buf, buf + pos, 10);
    TEST("copy_visible(10) on color-only");
    /* Should copy all color bytes but 0 visible chars */
    if (nb == pos) { PASS(); } else { FAIL("got %zu, expected %zu", nb, pos); }
}

/* ---- Test: empty string ---- */

static void test_empty(void)
{
    printf("\n--- Empty string ---\n");

    CHECK_EQ("visible_length(\"\")", co_visible_length(NULL, 0), 0);

    unsigned char out[LBUF_SIZE];
    size_t nb = co_copy_visible(out, (const unsigned char *)"", (const unsigned char *)"", 10);
    CHECK_EQ("copy_visible on empty", nb, 0);
}

/* ---- Test: delimiter between colored words ---- */

static void test_colored_words(void)
{
    printf("\n--- Colored words with delimiter ---\n");

    /* Build: RED "hello" RESET " " GREEN "world" RESET */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "hello");
    pos = append(buf, pos, COLOR_RESET, 3);
    pos = append_str(buf, pos, " ");
    pos = append(buf, pos, COLOR_FG_GREEN, 3);
    pos = append_str(buf, pos, "world");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    /* Visible: hello + space + world = 11 */
    CHECK_EQ("visible_length(colored words)", co_visible_length(buf, pos), 11);

    /* find_delim ' ' should find the space */
    const unsigned char *found = co_find_delim(buf, buf + pos, ' ');
    TEST("find_delim(' ') between colored words");
    if (found && *found == ' ') { PASS(); } else { FAIL("not found"); }

    /* The space is at offset 3(RED) + 5("hello") + 3(RESET) = 11 */
    TEST("delimiter at correct byte offset");
    if (found == buf + 11) { PASS(); } else { FAIL("offset %td", found - buf); }

    /* copy_visible(5) should give RED "hello" = 8 bytes */
    unsigned char out[LBUF_SIZE];
    size_t nb = co_copy_visible(out, buf, buf + pos, 5);
    TEST("copy_visible(5) -> RED \"hello\"");
    if (nb == 8 && memcmp(out, buf, 8) == 0) { PASS(); }
    else { FAIL("got %zu bytes", nb); }
}

/* ================================================================
 * Stage 1 Tests: Word and substring operations
 * ================================================================ */

/* ---- Test: words_count ---- */

static void test_words_count(void)
{
    printf("\n--- words_count ---\n");

    CHECK_EQ("words(\"hello world\", ' ')",
             co_words_count((const unsigned char *)"hello world", 11, ' '), 2);
    CHECK_EQ("words(\"a b c d e\", ' ')",
             co_words_count((const unsigned char *)"a b c d e", 9, ' '), 5);
    CHECK_EQ("words(\"\", ' ')",
             co_words_count((const unsigned char *)"", 0, ' '), 0);
    CHECK_EQ("words(\"  a  b  \", ' ') — compressed delims",
             co_words_count((const unsigned char *)"  a  b  ", 8, ' '), 2);
    CHECK_EQ("words(\"one\", ' ')",
             co_words_count((const unsigned char *)"one", 3, ' '), 1);

    /* Colored input. */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "hello");
    pos = append(buf, pos, COLOR_RESET, 3);
    pos = append_str(buf, pos, " ");
    pos = append(buf, pos, COLOR_FG_GREEN, 3);
    pos = append_str(buf, pos, "world");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';
    CHECK_EQ("words(colored \"hello world\", ' ')",
             co_words_count(buf, pos, ' '), 2);
}

/* ---- Test: first/rest/last ---- */

static void test_first_rest_last(void)
{
    printf("\n--- first / rest / last ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    /* Plain ASCII. */
    nb = co_first(out, (const unsigned char *)"hello world", 11, ' ');
    TEST("first(\"hello world\", ' ')");
    if (nb == 5 && memcmp(out, "hello", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu bytes)", (int)nb, out, nb); }

    nb = co_rest(out, (const unsigned char *)"hello world", 11, ' ');
    TEST("rest(\"hello world\", ' ')");
    if (nb == 5 && memcmp(out, "world", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu bytes)", (int)nb, out, nb); }

    nb = co_last(out, (const unsigned char *)"a b c d", 7, ' ');
    TEST("last(\"a b c d\", ' ')");
    if (nb == 1 && out[0] == 'd') { PASS(); }
    else { FAIL("got \"%.*s\" (%zu bytes)", (int)nb, out, nb); }

    /* No delimiter. */
    nb = co_first(out, (const unsigned char *)"hello", 5, ' ');
    TEST("first(\"hello\", ' ') — no delim");
    if (nb == 5 && memcmp(out, "hello", 5) == 0) { PASS(); }
    else { FAIL("got %zu bytes", nb); }

    nb = co_rest(out, (const unsigned char *)"hello", 5, ' ');
    TEST("rest(\"hello\", ' ') — no delim -> empty");
    if (nb == 0) { PASS(); } else { FAIL("got %zu bytes", nb); }

    /* Colored first/rest. */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "alpha");
    pos = append(buf, pos, COLOR_RESET, 3);
    pos = append_str(buf, pos, " beta gamma");
    buf[pos] = '\0';

    nb = co_first(out, buf, pos, ' ');
    TEST("first(RED\"alpha\"RESET\" beta gamma\")");
    /* Should be RED + "alpha" + RESET = 3 + 5 + 3 = 11 bytes */
    if (nb == 11) { PASS(); } else { FAIL("got %zu bytes, expected 11", nb); }

    nb = co_rest(out, buf, pos, ' ');
    TEST("rest(RED\"alpha\"RESET\" beta gamma\")");
    if (nb == 10 && memcmp(out, "beta gamma", 10) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu bytes)", (int)nb, out, nb); }
}

/* ---- Test: extract ---- */

static void test_extract(void)
{
    printf("\n--- extract ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    /* extract("a b c d e", 2, 3, ' ', ' ') -> "b c d" */
    nb = co_extract(out, (const unsigned char *)"a b c d e", 9,
                    2, 3, ' ', ' ');
    TEST("extract(\"a b c d e\", 2, 3)");
    if (nb == 5 && memcmp(out, "b c d", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* extract("a b c", 1, 1, ' ', ' ') -> "a" */
    nb = co_extract(out, (const unsigned char *)"a b c", 5,
                    1, 1, ' ', ' ');
    TEST("extract(\"a b c\", 1, 1)");
    if (nb == 1 && out[0] == 'a') { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* extract("a b c", 3, 1, ' ', ' ') -> "c" */
    nb = co_extract(out, (const unsigned char *)"a b c", 5,
                    3, 1, ' ', ' ');
    TEST("extract(\"a b c\", 3, 1)");
    if (nb == 1 && out[0] == 'c') { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* extract past end. */
    nb = co_extract(out, (const unsigned char *)"a b", 3,
                    5, 1, ' ', ' ');
    TEST("extract(\"a b\", 5, 1) — past end");
    if (nb == 0) { PASS(); } else { FAIL("got %zu bytes", nb); }

    /* Different osep. */
    nb = co_extract(out, (const unsigned char *)"a|b|c|d", 7,
                    2, 2, '|', ',');
    TEST("extract(\"a|b|c|d\", 2, 2, '|', ',')");
    if (nb == 3 && memcmp(out, "b,c", 3) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }
}

/* ---- Test: left/right ---- */

static void test_left_right(void)
{
    printf("\n--- left / right ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    nb = co_left(out, (const unsigned char *)"hello world", 11, 5);
    TEST("left(\"hello world\", 5)");
    if (nb == 5 && memcmp(out, "hello", 5) == 0) { PASS(); }
    else { FAIL("got %zu bytes", nb); }

    nb = co_right(out, (const unsigned char *)"hello world", 11, 5);
    TEST("right(\"hello world\", 5)");
    if (nb == 5 && memcmp(out, "world", 5) == 0) { PASS(); }
    else { FAIL("got %zu bytes", nb); }

    /* Colored left. */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "ABCDE");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_left(out, buf, pos, 3);
    TEST("left(RED\"ABCDE\"RESET, 3) preserves color");
    /* RED + "ABC" = 3 + 3 = 6 bytes */
    if (nb == 6 && memcmp(out, buf, 6) == 0) { PASS(); }
    else { FAIL("got %zu bytes, expected 6", nb); }

    nb = co_right(out, buf, pos, 3);
    TEST("right(RED\"ABCDE\"RESET, 3) -> \"CDE\"+RESET");
    /* Should start at 'C' (offset 5=RED+A+B), copy CDE+RESET = 3+3=6 */
    if (nb == 6 && memcmp(out, buf + 5, 6) == 0) { PASS(); }
    else { FAIL("got %zu bytes", nb); }
}

/* ---- Test: trim ---- */

static void test_trim(void)
{
    printf("\n--- trim ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    nb = co_trim(out, (const unsigned char *)"  hello  ", 9, 0, 3);
    TEST("trim(\"  hello  \", both)");
    if (nb == 5 && memcmp(out, "hello", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    nb = co_trim(out, (const unsigned char *)"  hello  ", 9, 0, 1);
    TEST("trim(\"  hello  \", left only)");
    if (nb == 7 && memcmp(out, "hello  ", 7) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    nb = co_trim(out, (const unsigned char *)"  hello  ", 9, 0, 2);
    TEST("trim(\"  hello  \", right only)");
    if (nb == 7 && memcmp(out, "  hello", 7) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    nb = co_trim(out, (const unsigned char *)"xxhelloxx", 9, 'x', 3);
    TEST("trim(\"xxhelloxx\", 'x', both)");
    if (nb == 5 && memcmp(out, "hello", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    nb = co_trim(out, (const unsigned char *)"   ", 3, 0, 3);
    TEST("trim(\"   \", both) -> empty");
    if (nb == 0) { PASS(); } else { FAIL("got %zu bytes", nb); }

    /* Colored trim: RED "  hi  " RESET — trim spaces, keep color. */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "  hi  ");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_trim(out, buf, pos, 0, 3);
    TEST("trim(RED\"  hi  \"RESET, both) preserves color");
    /* Should be: RED + "hi" = 3 + 2 = 5 bytes (RESET trimmed as trailing) */
    if (nb == 5 && memcmp(out, buf, 3) == 0 && memcmp(out+3, "hi", 2) == 0) { PASS(); }
    else { FAIL("got %zu bytes", nb); }
}

/* ---- Test: search ---- */

static void test_search(void)
{
    printf("\n--- search ---\n");

    const unsigned char *s = (const unsigned char *)"hello world";
    const unsigned char *found;

    found = co_search(s, 11, (const unsigned char *)"world", 5);
    TEST("search(\"hello world\", \"world\")");
    if (found == s + 6) { PASS(); } else { FAIL("got offset %td", found ? found - s : -1); }

    found = co_search(s, 11, (const unsigned char *)"xyz", 3);
    TEST("search(\"hello world\", \"xyz\") -> NULL");
    if (found == NULL) { PASS(); } else { FAIL("found at %td", found - s); }

    found = co_search(s, 11, (const unsigned char *)"", 0);
    TEST("search(\"hello world\", \"\") -> start");
    if (found == s) { PASS(); } else { FAIL("expected start"); }

    /* Search in colored text. */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "abc");
    pos = append(buf, pos, COLOR_FG_GREEN, 3);
    pos = append_str(buf, pos, "def");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    found = co_search(buf, pos, (const unsigned char *)"cd", 2);
    TEST("search(RED\"abc\"GREEN\"def\", \"cd\") across color boundary");
    /* 'c' is at offset 5 (RED + a + b), should find it there. */
    if (found && *found == 'c') { PASS(); }
    else { FAIL("got %p", (void*)found); }

    found = co_search(buf, pos, (const unsigned char *)"cde", 3);
    TEST("search(RED\"abc\"GREEN\"def\", \"cde\") across color boundary");
    if (found && *found == 'c') { PASS(); }
    else { FAIL("got %p", (void*)found); }
}

/* ================================================================
 * Stage 2 Tests: Transforms, edit, reverse, compress
 * ================================================================ */

static void test_case_transforms(void)
{
    printf("\n--- Case transforms ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    nb = co_toupper(out, (const unsigned char *)"hello World 123", 15);
    TEST("toupper(\"hello World 123\")");
    if (nb == 15 && memcmp(out, "HELLO WORLD 123", 15) == 0) { PASS(); }
    else { FAIL("got \"%.*s\"", (int)nb, out); }

    nb = co_tolower(out, (const unsigned char *)"HELLO World 123", 15);
    TEST("tolower(\"HELLO World 123\")");
    if (nb == 15 && memcmp(out, "hello world 123", 15) == 0) { PASS(); }
    else { FAIL("got \"%.*s\"", (int)nb, out); }

    nb = co_totitle(out, (const unsigned char *)"hello world", 11);
    TEST("totitle(\"hello world\")");
    if (nb == 11 && memcmp(out, "Hello world", 11) == 0) { PASS(); }
    else { FAIL("got \"%.*s\"", (int)nb, out); }

    /* Colored toupper. */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "hello");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_toupper(out, buf, pos);
    TEST("toupper(RED\"hello\"RESET) preserves color");
    /* Should be RED + "HELLO" + RESET = 11 bytes */
    if (nb == 11) {
        int ok = (memcmp(out, COLOR_FG_RED, 3) == 0
               && memcmp(out + 3, "HELLO", 5) == 0
               && memcmp(out + 8, COLOR_RESET, 3) == 0);
        if (ok) { PASS(); } else { FAIL("wrong content"); }
    } else { FAIL("got %zu bytes, expected 11", nb); }

    /* totitle with leading color. */
    nb = co_totitle(out, buf, pos);
    TEST("totitle(RED\"hello\"RESET)");
    if (nb == 11 && memcmp(out + 3, "Hello", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* ---- Unicode case mapping tests ---- */

    /* é (U+00E9, C3 A9) → É (U+00C9, C3 89) */
    {
        static const unsigned char e_acute_lower[] = { 0xC3, 0xA9 };  /* é */
        static const unsigned char e_acute_upper[] = { 0xC3, 0x89 };  /* É */

        nb = co_toupper(out, e_acute_lower, 2);
        TEST("toupper(U+00E9 e-acute) -> U+00C9 E-acute");
        if (nb == 2 && memcmp(out, e_acute_upper, 2) == 0) { PASS(); }
        else { FAIL("got %zu bytes: %02x %02x", nb, out[0], out[1]); }

        nb = co_tolower(out, e_acute_upper, 2);
        TEST("tolower(U+00C9 E-acute) -> U+00E9 e-acute");
        if (nb == 2 && memcmp(out, e_acute_lower, 2) == 0) { PASS(); }
        else { FAIL("got %zu bytes: %02x %02x", nb, out[0], out[1]); }
    }

    /* Mixed ASCII + Unicode: "café" → "CAFÉ" */
    {
        static const unsigned char cafe_lower[] = { 'c','a','f', 0xC3,0xA9 };  /* café */
        static const unsigned char cafe_upper[] = { 'C','A','F', 0xC3,0x89 };  /* CAFÉ */

        nb = co_toupper(out, cafe_lower, 5);
        TEST("toupper(\"caf\\xC3\\xA9\") -> \"CAF\\xC3\\x89\"");
        if (nb == 5 && memcmp(out, cafe_upper, 5) == 0) { PASS(); }
        else { FAIL("got %zu bytes", nb); }

        nb = co_tolower(out, cafe_upper, 5);
        TEST("tolower(\"CAF\\xC3\\x89\") -> \"caf\\xC3\\xA9\"");
        if (nb == 5 && memcmp(out, cafe_lower, 5) == 0) { PASS(); }
        else { FAIL("got %zu bytes", nb); }
    }

    /* Greek: Σ (U+03A3, CE A3) → σ (U+03C3, CF 83) */
    {
        static const unsigned char sigma_upper[] = { 0xCE, 0xA3 };  /* Σ */
        static const unsigned char sigma_lower[] = { 0xCF, 0x83 };  /* σ */

        nb = co_tolower(out, sigma_upper, 2);
        TEST("tolower(U+03A3 Sigma) -> U+03C3 sigma");
        if (nb == 2 && memcmp(out, sigma_lower, 2) == 0) { PASS(); }
        else { FAIL("got %zu bytes: %02x %02x", nb, out[0], out[1]); }

        nb = co_toupper(out, sigma_lower, 2);
        TEST("toupper(U+03C3 sigma) -> U+03A3 Sigma");
        if (nb == 2 && memcmp(out, sigma_upper, 2) == 0) { PASS(); }
        else { FAIL("got %zu bytes: %02x %02x", nb, out[0], out[1]); }
    }

    /* totitle with Unicode: "élan" → "Élan" */
    {
        static const unsigned char elan_lower[] = { 0xC3,0xA9, 'l','a','n' };  /* élan */
        static const unsigned char elan_title[] = { 0xC3,0x89, 'l','a','n' };  /* Élan */

        nb = co_totitle(out, elan_lower, 5);
        TEST("totitle(\"\\xC3\\xA9lan\") -> \"\\xC3\\x89lan\"");
        if (nb == 5 && memcmp(out, elan_title, 5) == 0) { PASS(); }
        else { FAIL("got %zu bytes: %02x %02x", nb, out[0], out[1]); }
    }

    /* Unicode toupper with color: RED + é + RESET → RED + É + RESET */
    {
        unsigned char cbuf[64];
        size_t cpos = 0;
        cpos = append(cbuf, cpos, COLOR_FG_RED, 3);
        cbuf[cpos++] = 0xC3; cbuf[cpos++] = 0xA9;  /* é */
        cpos = append(cbuf, cpos, COLOR_RESET, 3);
        cbuf[cpos] = '\0';

        nb = co_toupper(out, cbuf, cpos);
        TEST("toupper(RED + U+00E9 + RESET) -> RED + U+00C9 + RESET");
        /* RED(3) + É(2) + RESET(3) = 8 bytes */
        if (nb == 8 && memcmp(out, COLOR_FG_RED, 3) == 0
            && out[3] == 0xC3 && out[4] == 0x89
            && memcmp(out + 5, COLOR_RESET, 3) == 0) { PASS(); }
        else { FAIL("got %zu bytes", nb); }
    }

    /* ẞ (U+1E9E, E1 BA 9E) → ß (U+00DF, C3 9F) via tolower.
     * This is a 3-byte → 2-byte mapping (code point count stays 1). */
    {
        static const unsigned char sharp_s_upper[] = { 0xE1, 0xBA, 0x9E };  /* ẞ */
        static const unsigned char sharp_s_lower[] = { 0xC3, 0x9F };        /* ß */

        nb = co_tolower(out, sharp_s_upper, 3);
        TEST("tolower(U+1E9E cap-sharp-S) -> U+00DF sharp-s");
        if (nb == 2 && memcmp(out, sharp_s_lower, 2) == 0) { PASS(); }
        else { FAIL("got %zu bytes: %02x %02x", nb, out[0], nb > 1 ? out[1] : 0); }
    }

    /* µ (U+00B5, C2 B5) → Μ (U+039C, CE 9C) via toupper.
     * Micro sign maps to Greek capital Mu — cross-script. */
    {
        static const unsigned char micro[] = { 0xC2, 0xB5 };  /* µ */
        static const unsigned char mu_upper[] = { 0xCE, 0x9C };  /* Μ */

        nb = co_toupper(out, micro, 2);
        TEST("toupper(U+00B5 micro) -> U+039C Greek Mu");
        if (nb == 2 && memcmp(out, mu_upper, 2) == 0) { PASS(); }
        else { FAIL("got %zu bytes: %02x %02x", nb, out[0], out[1]); }
    }
}

static void test_reverse(void)
{
    printf("\n--- reverse ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    nb = co_reverse(out, (const unsigned char *)"abcde", 5);
    TEST("reverse(\"abcde\")");
    if (nb == 5 && memcmp(out, "edcba", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\"", (int)nb, out); }

    nb = co_reverse(out, (const unsigned char *)"a", 1);
    TEST("reverse(\"a\")");
    if (nb == 1 && out[0] == 'a') { PASS(); }
    else { FAIL("got \"%.*s\"", (int)nb, out); }

    nb = co_reverse(out, (const unsigned char *)"", 0);
    TEST("reverse(\"\")");
    if (nb == 0) { PASS(); } else { FAIL("got %zu bytes", nb); }

    /* Colored reverse: RED "AB" GREEN "CD" RESET → GREEN "DC" RED "BA" */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "AB");
    pos = append(buf, pos, COLOR_FG_GREEN, 3);
    pos = append_str(buf, pos, "CD");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_reverse(out, buf, pos);
    TEST("reverse(RED\"AB\"GREEN\"CD\"RESET)");
    /* RESET is trailing color → becomes an element.
     * Elements in order: RED+'A', 'B', GREEN+'C', 'D', RESET
     * Reversed: RESET, 'D', GREEN+'C', 'B', RED+'A'
     * Wait — leading color stays. Let me think...
     * Actually RED is color before 'A', it's part of the first element.
     * Leading color = empty (no color before first visible char? No, RED IS before 'A')
     * Let me re-examine: leading_end = co_skip_color(data, pe) = buf+3.
     * So leading color = buf[0..3) = RED.
     * Then elements start at buf+3:
     *   elem 0: 'A' (buf+3..buf+4)
     *   elem 1: 'B' (buf+4..buf+5)
     *   elem 2: GREEN+'C' (buf+5..buf+11 = 3+3) wait...
     *   Actually buf+5 is GREEN (EF 98 82), buf+8 is 'C', buf+9 is 'D'
     *   elem 2: GREEN(3)+'C' = buf+5..buf+9
     *   elem 3: 'D' = buf+9..buf+10
     *   elem 4: RESET(3) = buf+10..buf+13 (trailing color)
     * Reversed: RED(leading) + RESET + 'D' + GREEN+'C' + 'B' + 'A'
     * = 3 + 3 + 1 + 4 + 1 + 1 = 13
     * Hmm, that means leading RED stays, then the visible chars are DCBA
     * with GREEN attached to 'C'. That's semantically correct. */
    CHECK_EQ("reverse colored length", nb, 13);

    /* Verify visible content is "DCBA". */
    unsigned char plain[LBUF_SIZE];
    size_t pn = co_strip_color(plain, out, nb);
    TEST("reverse colored visible content = \"DCBA\"");
    if (pn == 4 && memcmp(plain, "DCBA", 4) == 0) { PASS(); }
    else { FAIL("got \"%.*s\"", (int)pn, plain); }
}

static void test_edit(void)
{
    printf("\n--- edit ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    nb = co_edit(out,
                 (const unsigned char *)"hello world", 11,
                 (const unsigned char *)"world", 5,
                 (const unsigned char *)"earth", 5);
    TEST("edit(\"hello world\", \"world\", \"earth\")");
    if (nb == 11 && memcmp(out, "hello earth", 11) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Multiple replacements. */
    nb = co_edit(out,
                 (const unsigned char *)"aXbXcX", 6,
                 (const unsigned char *)"X", 1,
                 (const unsigned char *)"Y", 1);
    TEST("edit(\"aXbXcX\", \"X\", \"Y\")");
    if (nb == 6 && memcmp(out, "aYbYcY", 6) == 0) { PASS(); }
    else { FAIL("got \"%.*s\"", (int)nb, out); }

    /* Replacement shorter. */
    nb = co_edit(out,
                 (const unsigned char *)"hello world", 11,
                 (const unsigned char *)"world", 5,
                 (const unsigned char *)"!", 1);
    TEST("edit(\"hello world\", \"world\", \"!\")");
    if (nb == 7 && memcmp(out, "hello !", 7) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Replacement empty (deletion). */
    nb = co_edit(out,
                 (const unsigned char *)"hello world", 11,
                 (const unsigned char *)" world", 6,
                 (const unsigned char *)"", 0);
    TEST("edit(\"hello world\", \" world\", \"\")");
    if (nb == 5 && memcmp(out, "hello", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Pattern not found. */
    nb = co_edit(out,
                 (const unsigned char *)"hello", 5,
                 (const unsigned char *)"xyz", 3,
                 (const unsigned char *)"!", 1);
    TEST("edit — pattern not found");
    if (nb == 5 && memcmp(out, "hello", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\"", (int)nb, out); }

    /* Edit across color boundary. */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "ab");
    pos = append(buf, pos, COLOR_FG_GREEN, 3);
    pos = append_str(buf, pos, "cd");
    buf[pos] = '\0';

    nb = co_edit(out, buf, pos,
                 (const unsigned char *)"bc", 2,
                 (const unsigned char *)"XX", 2);
    TEST("edit across color boundary");
    unsigned char plain[LBUF_SIZE];
    size_t pn = co_strip_color(plain, out, nb);
    if (pn == 4 && memcmp(plain, "aXXd", 4) == 0) { PASS(); }
    else { FAIL("got \"%.*s\"", (int)pn, plain); }
}

static void test_transform(void)
{
    printf("\n--- transform ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    nb = co_transform(out,
                      (const unsigned char *)"hello", 5,
                      (const unsigned char *)"helo", 4,
                      (const unsigned char *)"HELO", 4);
    TEST("transform(\"hello\", \"helo\", \"HELO\")");
    if (nb == 5 && memcmp(out, "HELLO", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\"", (int)nb, out); }

    nb = co_transform(out,
                      (const unsigned char *)"abcabc", 6,
                      (const unsigned char *)"abc", 3,
                      (const unsigned char *)"xyz", 3);
    TEST("transform(\"abcabc\", \"abc\", \"xyz\")");
    if (nb == 6 && memcmp(out, "xyzxyz", 6) == 0) { PASS(); }
    else { FAIL("got \"%.*s\"", (int)nb, out); }

    /* With color. */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "abc");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_transform(out, buf, pos,
                      (const unsigned char *)"abc", 3,
                      (const unsigned char *)"XYZ", 3);
    TEST("transform with color preserved");
    unsigned char plain[LBUF_SIZE];
    size_t pn = co_strip_color(plain, out, nb);
    if (pn == 3 && memcmp(plain, "XYZ", 3) == 0) { PASS(); }
    else { FAIL("got \"%.*s\"", (int)pn, plain); }
}

static void test_compress(void)
{
    printf("\n--- compress ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    nb = co_compress(out, (const unsigned char *)"a   b   c", 9, 0);
    TEST("compress(\"a   b   c\", whitespace)");
    if (nb == 5 && memcmp(out, "a b c", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    nb = co_compress(out, (const unsigned char *)"xxhelloxxworldxx", 16, 'x');
    TEST("compress(\"xxhelloxxworldxx\", 'x')");
    if (nb == 13 && memcmp(out, "xhelloxworldx", 13) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    nb = co_compress(out, (const unsigned char *)"abc", 3, 0);
    TEST("compress(\"abc\", whitespace) — no change");
    if (nb == 3 && memcmp(out, "abc", 3) == 0) { PASS(); }
    else { FAIL("got \"%.*s\"", (int)nb, out); }

    nb = co_compress(out, (const unsigned char *)"   ", 3, 0);
    TEST("compress(\"   \", whitespace) -> \" \"");
    if (nb == 1 && out[0] == ' ') { PASS(); }
    else { FAIL("got %zu bytes", nb); }
}

static void test_strip_color(void)
{
    printf("\n--- strip_color ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "hello");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_strip_color(out, buf, pos);
    TEST("strip_color(RED\"hello\"RESET)");
    if (nb == 5 && memcmp(out, "hello", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    nb = co_strip_color(out, (const unsigned char *)"plain", 5);
    TEST("strip_color(\"plain\") — no change");
    if (nb == 5 && memcmp(out, "plain", 5) == 0) { PASS(); }
    else { FAIL("got %zu bytes", nb); }
}

/* ================================================================
 * Stage 3 Tests: Position, substring, padding
 * ================================================================ */

static void test_mid(void)
{
    printf("\n--- mid ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    /* mid("foobar", 1, 3) -> "oob" (0-based: start=1, count=3) */
    nb = co_mid(out, (const unsigned char *)"foobar", 6, 1, 3);
    TEST("mid(\"foobar\", 1, 3) -> \"oob\"");
    if (nb == 3 && memcmp(out, "oob", 3) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* mid from start. */
    nb = co_mid(out, (const unsigned char *)"hello", 5, 0, 3);
    TEST("mid(\"hello\", 0, 3) -> \"hel\"");
    if (nb == 3 && memcmp(out, "hel", 3) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* mid past end. */
    nb = co_mid(out, (const unsigned char *)"hi", 2, 0, 10);
    TEST("mid(\"hi\", 0, 10) -> \"hi\"");
    if (nb == 2 && memcmp(out, "hi", 2) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* mid with zero count. */
    nb = co_mid(out, (const unsigned char *)"hello", 5, 2, 0);
    TEST("mid(\"hello\", 2, 0) -> \"\"");
    if (nb == 0) { PASS(); } else { FAIL("got %zu bytes", nb); }

    /* mid with color: RED"ABCDE"RESET, start=1, count=3 -> "BCD" with color */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "ABCDE");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_mid(out, buf, pos, 1, 3);
    TEST("mid(RED\"ABCDE\"RESET, 1, 3) -> \"BCD\"");
    unsigned char plain[LBUF_SIZE];
    size_t pn = co_strip_color(plain, out, nb);
    if (pn == 3 && memcmp(plain, "BCD", 3) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu vis)", (int)pn, plain, pn); }

    /* mid at start preserves leading color. */
    nb = co_mid(out, buf, pos, 0, 2);
    TEST("mid(RED\"ABCDE\"RESET, 0, 2) preserves RED");
    if (nb >= 5 && memcmp(out, COLOR_FG_RED, 3) == 0) { PASS(); }
    else { FAIL("got %zu bytes, no RED prefix", nb); }
}

static void test_pos(void)
{
    printf("\n--- pos ---\n");

    size_t idx;

    idx = co_pos((const unsigned char *)"hello world", 11,
                 (const unsigned char *)"world", 5);
    TEST("pos(\"hello world\", \"world\") -> 7");
    if (idx == 7) { PASS(); } else { FAIL("got %zu", idx); }

    idx = co_pos((const unsigned char *)"hello world", 11,
                 (const unsigned char *)"hello", 5);
    TEST("pos(\"hello world\", \"hello\") -> 1");
    if (idx == 1) { PASS(); } else { FAIL("got %zu", idx); }

    idx = co_pos((const unsigned char *)"hello world", 11,
                 (const unsigned char *)"xyz", 3);
    TEST("pos(\"hello world\", \"xyz\") -> 0");
    if (idx == 0) { PASS(); } else { FAIL("got %zu", idx); }

    /* pos in colored text. */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "abc");
    pos = append(buf, pos, COLOR_FG_GREEN, 3);
    pos = append_str(buf, pos, "def");
    buf[pos] = '\0';

    idx = co_pos(buf, pos, (const unsigned char *)"def", 3);
    TEST("pos(RED\"abc\"GREEN\"def\", \"def\") -> 4");
    if (idx == 4) { PASS(); } else { FAIL("got %zu", idx); }

    idx = co_pos(buf, pos, (const unsigned char *)"cd", 2);
    TEST("pos(RED\"abc\"GREEN\"def\", \"cd\") -> 3");
    if (idx == 3) { PASS(); } else { FAIL("got %zu", idx); }
}

static void test_lpos(void)
{
    printf("\n--- lpos ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    nb = co_lpos(out, (const unsigned char *)"a-bc-def-g", 10, '-');
    TEST("lpos(\"a-bc-def-g\", '-') -> \"1 4 8\"");
    if (nb == 5 && memcmp(out, "1 4 8", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    nb = co_lpos(out, (const unsigned char *)"hello", 5, '-');
    TEST("lpos(\"hello\", '-') -> \"\" (not found)");
    if (nb == 0) { PASS(); } else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    nb = co_lpos(out, (const unsigned char *)"xxx", 3, 'x');
    TEST("lpos(\"xxx\", 'x') -> \"0 1 2\"");
    if (nb == 5 && memcmp(out, "0 1 2", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }
}

static void test_member(void)
{
    printf("\n--- member ---\n");

    size_t idx;

    idx = co_member((const unsigned char *)"cat", 3,
                    (const unsigned char *)"dog cat fish", 12, ' ');
    TEST("member(\"cat\", \"dog cat fish\") -> 2");
    if (idx == 2) { PASS(); } else { FAIL("got %zu", idx); }

    idx = co_member((const unsigned char *)"bird", 4,
                    (const unsigned char *)"dog cat fish", 12, ' ');
    TEST("member(\"bird\", \"dog cat fish\") -> 0");
    if (idx == 0) { PASS(); } else { FAIL("got %zu", idx); }

    idx = co_member((const unsigned char *)"dog", 3,
                    (const unsigned char *)"dog cat fish", 12, ' ');
    TEST("member(\"dog\", \"dog cat fish\") -> 1");
    if (idx == 1) { PASS(); } else { FAIL("got %zu", idx); }

    idx = co_member((const unsigned char *)"fish", 4,
                    (const unsigned char *)"dog cat fish", 12, ' ');
    TEST("member(\"fish\", \"dog cat fish\") -> 3");
    if (idx == 3) { PASS(); } else { FAIL("got %zu", idx); }

    /* Partial match should NOT match. */
    idx = co_member((const unsigned char *)"do", 2,
                    (const unsigned char *)"dog cat fish", 12, ' ');
    TEST("member(\"do\", \"dog cat fish\") -> 0");
    if (idx == 0) { PASS(); } else { FAIL("got %zu", idx); }
}

static void test_center(void)
{
    printf("\n--- center ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    nb = co_center(out, (const unsigned char *)"hi", 2,
                   10, NULL, 0);
    TEST("center(\"hi\", 10) -> \"    hi    \"");
    if (nb == 10 && memcmp(out, "    hi    ", 10) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Odd padding: extra goes to right. */
    nb = co_center(out, (const unsigned char *)"hi", 2,
                   9, NULL, 0);
    TEST("center(\"hi\", 9) -> odd padding");
    /* 7 pad total: 3 left, 4 right */
    if (nb == 9 && memcmp(out, "   hi    ", 9) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Custom fill. */
    nb = co_center(out, (const unsigned char *)"hi", 2,
                   8, (const unsigned char *)"=-", 2);
    TEST("center(\"hi\", 8, \"=-\") -> cyclic fill");
    /* 3 left, 3 right: "=-=" + "hi" + "=-=" = 8 vis */
    if (nb == 8 && memcmp(out, "=-=hi=-=", 8) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Truncation: string wider than width. */
    nb = co_center(out, (const unsigned char *)"hello world", 11,
                   5, NULL, 0);
    TEST("center(\"hello world\", 5) -> truncate");
    if (nb == 5 && memcmp(out, "hello", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Exact fit. */
    nb = co_center(out, (const unsigned char *)"abc", 3,
                   3, NULL, 0);
    TEST("center(\"abc\", 3) -> exact fit");
    if (nb == 3 && memcmp(out, "abc", 3) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }
}

static void test_ljust_rjust(void)
{
    printf("\n--- ljust / rjust ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    nb = co_ljust(out, (const unsigned char *)"hi", 2,
                  8, NULL, 0);
    TEST("ljust(\"hi\", 8) -> \"hi      \"");
    if (nb == 8 && memcmp(out, "hi      ", 8) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    nb = co_rjust(out, (const unsigned char *)"hi", 2,
                  8, NULL, 0);
    TEST("rjust(\"hi\", 8) -> \"      hi\"");
    if (nb == 8 && memcmp(out, "      hi", 8) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Custom fill. */
    nb = co_ljust(out, (const unsigned char *)"x", 1,
                  5, (const unsigned char *)".", 1);
    TEST("ljust(\"x\", 5, \".\") -> \"x....\"");
    if (nb == 5 && memcmp(out, "x....", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    nb = co_rjust(out, (const unsigned char *)"x", 1,
                  5, (const unsigned char *)".", 1);
    TEST("rjust(\"x\", 5, \".\") -> \"....x\"");
    if (nb == 5 && memcmp(out, "....x", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Truncation. */
    nb = co_ljust(out, (const unsigned char *)"hello", 5,
                  3, NULL, 0);
    TEST("ljust(\"hello\", 3) -> truncate to \"hel\"");
    if (nb == 3 && memcmp(out, "hel", 3) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* With color: ljust preserves color in content. */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "AB");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_ljust(out, buf, pos, 6, NULL, 0);
    TEST("ljust(RED\"AB\"RESET, 6) preserves color");
    /* RED(3) + "AB" + RESET(3) + 4 spaces = 12 bytes, 6 visible */
    size_t vis = co_visible_length(out, nb);
    if (vis == 6 && memcmp(out, COLOR_FG_RED, 3) == 0) { PASS(); }
    else { FAIL("got %zu vis, %zu bytes", vis, nb); }
}

static void test_repeat(void)
{
    printf("\n--- repeat ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    nb = co_repeat(out, (const unsigned char *)"ab", 2, 3);
    TEST("repeat(\"ab\", 3) -> \"ababab\"");
    if (nb == 6 && memcmp(out, "ababab", 6) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    nb = co_repeat(out, (const unsigned char *)"x", 1, 5);
    TEST("repeat(\"x\", 5) -> \"xxxxx\"");
    if (nb == 5 && memcmp(out, "xxxxx", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    nb = co_repeat(out, (const unsigned char *)"hi", 2, 0);
    TEST("repeat(\"hi\", 0) -> \"\"");
    if (nb == 0) { PASS(); } else { FAIL("got %zu bytes", nb); }

    nb = co_repeat(out, (const unsigned char *)"", 0, 5);
    TEST("repeat(\"\", 5) -> \"\"");
    if (nb == 0) { PASS(); } else { FAIL("got %zu bytes", nb); }

    /* With color. */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "A");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_repeat(out, buf, pos, 3);
    TEST("repeat(RED\"A\"RESET, 3) -> 3 copies");
    /* Each copy is 7 bytes (RED + A + RESET), 3 copies = 21 bytes */
    if (nb == 21) {
        size_t vis = co_visible_length(out, nb);
        if (vis == 3) { PASS(); } else { FAIL("got %zu vis", vis); }
    } else { FAIL("got %zu bytes, expected 21", nb); }

    /* Overflow protection. */
    nb = co_repeat(out, (const unsigned char *)"hello", 5, 2000);
    TEST("repeat overflow -> 0");
    if (nb == 0) { PASS(); } else { FAIL("got %zu bytes", nb); }
}

/* ================================================================
 * Stage 4 Tests: Delete, splice, insert, sort, set operations
 * ================================================================ */

static void test_delete(void)
{
    printf("\n--- delete ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    /* delete("hello world", 5, 1) -> "helloworld" (delete space at pos 5) */
    nb = co_delete(out, (const unsigned char *)"hello world", 11, 5, 1);
    TEST("delete(\"hello world\", 5, 1) -> \"helloworld\"");
    if (nb == 10 && memcmp(out, "helloworld", 10) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* delete from start. */
    nb = co_delete(out, (const unsigned char *)"abcde", 5, 0, 2);
    TEST("delete(\"abcde\", 0, 2) -> \"cde\"");
    if (nb == 3 && memcmp(out, "cde", 3) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* delete from end. */
    nb = co_delete(out, (const unsigned char *)"abcde", 5, 3, 5);
    TEST("delete(\"abcde\", 3, 5) -> \"abc\"");
    if (nb == 3 && memcmp(out, "abc", 3) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* delete zero count = no-op. */
    nb = co_delete(out, (const unsigned char *)"hello", 5, 2, 0);
    TEST("delete(\"hello\", 2, 0) -> \"hello\"");
    if (nb == 5 && memcmp(out, "hello", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* delete with color: RED"ABCDE"RESET, delete pos 1 count 2 -> RED"A"+"DE"RESET */
    unsigned char buf[256];
    size_t pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "ABCDE");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_delete(out, buf, pos, 1, 2);
    TEST("delete(RED\"ABCDE\"RESET, 1, 2) -> \"ADE\"");
    unsigned char plain[LBUF_SIZE];
    size_t pn = co_strip_color(plain, out, nb);
    if (pn == 3 && memcmp(plain, "ADE", 3) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu vis)", (int)pn, plain, pn); }
}

static void test_splice(void)
{
    printf("\n--- splice ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    /* splice("a b c d", "1 2 3 4", "c") -> "a b 3 d" */
    nb = co_splice(out,
                   (const unsigned char *)"a b c d", 7,
                   (const unsigned char *)"1 2 3 4", 7,
                   (const unsigned char *)"c", 1,
                   ' ', ' ');
    TEST("splice(\"a b c d\", \"1 2 3 4\", \"c\")");
    if (nb == 7 && memcmp(out, "a b 3 d", 7) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Multiple matches. */
    nb = co_splice(out,
                   (const unsigned char *)"x y x z", 7,
                   (const unsigned char *)"1 2 3 4", 7,
                   (const unsigned char *)"x", 1,
                   ' ', ' ');
    TEST("splice multiple matches");
    if (nb == 7 && memcmp(out, "1 y 3 z", 7) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* No matches. */
    nb = co_splice(out,
                   (const unsigned char *)"a b c", 5,
                   (const unsigned char *)"1 2 3", 5,
                   (const unsigned char *)"z", 1,
                   ' ', ' ');
    TEST("splice no matches -> unchanged");
    if (nb == 5 && memcmp(out, "a b c", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Mismatched lengths -> empty. */
    nb = co_splice(out,
                   (const unsigned char *)"a b c", 5,
                   (const unsigned char *)"1 2", 3,
                   (const unsigned char *)"a", 1,
                   ' ', ' ');
    TEST("splice mismatched lengths -> empty");
    if (nb == 0) { PASS(); } else { FAIL("got %zu bytes", nb); }
}

static void test_insert_word(void)
{
    printf("\n--- insert_word ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    /* insert at position 2 (1-based). */
    nb = co_insert_word(out,
                        (const unsigned char *)"a c d", 5,
                        (const unsigned char *)"b", 1,
                        2, ' ', ' ');
    TEST("insert(\"a c d\", \"b\", 2) -> \"a b c d\"");
    if (nb == 7 && memcmp(out, "a b c d", 7) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* insert at beginning. */
    nb = co_insert_word(out,
                        (const unsigned char *)"b c", 3,
                        (const unsigned char *)"a", 1,
                        1, ' ', ' ');
    TEST("insert(\"b c\", \"a\", 1) -> \"a b c\"");
    if (nb == 5 && memcmp(out, "a b c", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* insert past end (append). */
    nb = co_insert_word(out,
                        (const unsigned char *)"a b", 3,
                        (const unsigned char *)"c", 1,
                        99, ' ', ' ');
    TEST("insert(\"a b\", \"c\", 99) -> \"a b c\"");
    if (nb == 5 && memcmp(out, "a b c", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* insert into empty list. */
    nb = co_insert_word(out,
                        (const unsigned char *)"", 0,
                        (const unsigned char *)"x", 1,
                        1, ' ', ' ');
    TEST("insert(\"\", \"x\", 1) -> \"x\"");
    if (nb == 1 && out[0] == 'x') { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }
}

static void test_sort_words(void)
{
    printf("\n--- sort_words ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    /* ASCII sort. */
    nb = co_sort_words(out, (const unsigned char *)"cat apple banana", 16,
                       ' ', ' ', 'a');
    TEST("sort(\"cat apple banana\", 'a')");
    if (nb == 16 && memcmp(out, "apple banana cat", 16) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Numeric sort. */
    nb = co_sort_words(out, (const unsigned char *)"10 2 30 1", 9,
                       ' ', ' ', 'n');
    TEST("sort(\"10 2 30 1\", 'n')");
    if (nb == 9 && memcmp(out, "1 2 10 30", 9) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Case-insensitive sort. */
    nb = co_sort_words(out, (const unsigned char *)"Banana apple Cherry", 19,
                       ' ', ' ', 'i');
    TEST("sort(\"Banana apple Cherry\", 'i')");
    if (nb == 19 && memcmp(out, "apple Banana Cherry", 19) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Dbref sort. */
    nb = co_sort_words(out, (const unsigned char *)"#10 #2 #30 #1", 13,
                       ' ', ' ', 'd');
    TEST("sort(\"#10 #2 #30 #1\", 'd')");
    if (nb == 13 && memcmp(out, "#1 #2 #10 #30", 13) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Single word. */
    nb = co_sort_words(out, (const unsigned char *)"alone", 5,
                       ' ', ' ', 'a');
    TEST("sort(\"alone\") -> \"alone\"");
    if (nb == 5 && memcmp(out, "alone", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Empty. */
    nb = co_sort_words(out, (const unsigned char *)"", 0,
                       ' ', ' ', 'a');
    TEST("sort(\"\") -> \"\"");
    if (nb == 0) { PASS(); } else { FAIL("got %zu bytes", nb); }

    /* Custom delimiter. */
    nb = co_sort_words(out, (const unsigned char *)"c|a|b", 5,
                       '|', '|', 'a');
    TEST("sort(\"c|a|b\", '|') -> \"a|b|c\"");
    if (nb == 5 && memcmp(out, "a|b|c", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }
}

static void test_setops(void)
{
    printf("\n--- set operations ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    /* setunion. */
    nb = co_setunion(out,
                     (const unsigned char *)"a c e", 5,
                     (const unsigned char *)"b c d", 5,
                     ' ', ' ', 'a');
    TEST("setunion(\"a c e\", \"b c d\")");
    if (nb == 9 && memcmp(out, "a b c d e", 9) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* setunion with duplicates. */
    nb = co_setunion(out,
                     (const unsigned char *)"a a b", 5,
                     (const unsigned char *)"b b c", 5,
                     ' ', ' ', 'a');
    TEST("setunion with duplicates");
    if (nb == 5 && memcmp(out, "a b c", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* setdiff. */
    nb = co_setdiff(out,
                    (const unsigned char *)"a b c d", 7,
                    (const unsigned char *)"b d", 3,
                    ' ', ' ', 'a');
    TEST("setdiff(\"a b c d\", \"b d\") -> \"a c\"");
    if (nb == 3 && memcmp(out, "a c", 3) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* setdiff — no overlap. */
    nb = co_setdiff(out,
                    (const unsigned char *)"x y z", 5,
                    (const unsigned char *)"a b c", 5,
                    ' ', ' ', 'a');
    TEST("setdiff no overlap -> same");
    if (nb == 5 && memcmp(out, "x y z", 5) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* setinter. */
    nb = co_setinter(out,
                     (const unsigned char *)"a b c d", 7,
                     (const unsigned char *)"b d f", 5,
                     ' ', ' ', 'a');
    TEST("setinter(\"a b c d\", \"b d f\") -> \"b d\"");
    if (nb == 3 && memcmp(out, "b d", 3) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* setinter — no overlap. */
    nb = co_setinter(out,
                     (const unsigned char *)"a b c", 5,
                     (const unsigned char *)"x y z", 5,
                     ' ', ' ', 'a');
    TEST("setinter no overlap -> empty");
    if (nb == 0) { PASS(); } else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Numeric set operations. */
    nb = co_setunion(out,
                     (const unsigned char *)"1 3 5", 5,
                     (const unsigned char *)"2 3 4", 5,
                     ' ', ' ', 'n');
    TEST("setunion numeric");
    if (nb == 9 && memcmp(out, "1 2 3 4 5", 9) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }

    /* Empty lists. */
    nb = co_setunion(out,
                     (const unsigned char *)"", 0,
                     (const unsigned char *)"a b", 3,
                     ' ', ' ', 'a');
    TEST("setunion empty + \"a b\"");
    if (nb == 3 && memcmp(out, "a b", 3) == 0) { PASS(); }
    else { FAIL("got \"%.*s\" (%zu)", (int)nb, out, nb); }
}

/* ================================================================
 * Stage 5 Tests: Color collapse
 * ================================================================ */

static void test_collapse_color(void)
{
    printf("\n--- collapse_color ---\n");
    unsigned char out[LBUF_SIZE];
    unsigned char buf[256];
    size_t nb, pos;

    /* No color: passthrough. */
    nb = co_collapse_color(out, (const unsigned char *)"hello", 5);
    TEST("collapse plain text -> unchanged");
    if (nb == 5 && memcmp(out, "hello", 5) == 0) { PASS(); }
    else { FAIL("got %zu bytes", nb); }

    /* Already minimal: RED "A" RESET -> unchanged. */
    pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "A");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_collapse_color(out, buf, pos);
    TEST("collapse RED\"A\"RESET -> unchanged");
    if (nb == pos && memcmp(out, buf, pos) == 0) { PASS(); }
    else { FAIL("got %zu bytes, expected %zu", nb, pos); }

    /* Redundant: RED RED "A" RESET -> RED "A" RESET. */
    pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "A");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_collapse_color(out, buf, pos);
    TEST("collapse RED RED \"A\" RESET -> RED \"A\" RESET");
    /* RED(3) + A(1) + RESET(3) = 7 */
    if (nb == 7) {
        int ok = memcmp(out, COLOR_FG_RED, 3) == 0
              && out[3] == 'A'
              && memcmp(out + 4, COLOR_RESET, 3) == 0;
        if (ok) { PASS(); } else { FAIL("wrong content"); }
    } else { FAIL("got %zu bytes, expected 7", nb); }

    /* Superseded: RED GREEN "A" RESET -> GREEN "A" RESET. */
    pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append(buf, pos, COLOR_FG_GREEN, 3);
    pos = append_str(buf, pos, "A");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_collapse_color(out, buf, pos);
    TEST("collapse RED GREEN \"A\" RESET -> GREEN \"A\"");
    /* GREEN(3) + A(1) + RESET(3) = 7 */
    if (nb == 7 && memcmp(out, COLOR_FG_GREEN, 3) == 0 && out[3] == 'A') {
        PASS();
    } else { FAIL("got %zu bytes", nb); }

    /* Mixed: RED "A" GREEN "B" RESET.
     * Already minimal — two different colors on two chars. */
    pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "A");
    pos = append(buf, pos, COLOR_FG_GREEN, 3);
    pos = append_str(buf, pos, "B");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_collapse_color(out, buf, pos);
    TEST("collapse RED\"A\"GREEN\"B\"RESET -> unchanged");
    if (nb == pos && memcmp(out, buf, pos) == 0) { PASS(); }
    else { FAIL("got %zu bytes, expected %zu", nb, pos); }

    /* Redundant RESET: "A" RESET RESET -> "A" RESET. */
    pos = 0;
    pos = append_str(buf, pos, "A");
    pos = append(buf, pos, COLOR_RESET, 3);
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_collapse_color(out, buf, pos);
    TEST("collapse \"A\" RESET RESET -> \"A\" RESET");
    /* "A"(1) + RESET(3) = 4. But emitted starts as NORMAL,
     * two RESETs set pending to NORMAL. emitted is NORMAL after A.
     * So trailing RESET is not emitted because pending==emitted==NORMAL. */
    if (nb == 1 && out[0] == 'A') { PASS(); }
    else { FAIL("got %zu bytes", nb); }

    /* RESET at start is also redundant. */
    pos = 0;
    pos = append(buf, pos, COLOR_RESET, 3);
    pos = append_str(buf, pos, "hello");
    buf[pos] = '\0';

    nb = co_collapse_color(out, buf, pos);
    TEST("collapse RESET\"hello\" -> \"hello\"");
    if (nb == 5 && memcmp(out, "hello", 5) == 0) { PASS(); }
    else { FAIL("got %zu bytes", nb); }

    /* Same FG before two chars: RED "AB" -> RED "AB" (no change between). */
    pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "AB");
    buf[pos] = '\0';

    nb = co_collapse_color(out, buf, pos);
    TEST("collapse RED\"AB\" -> same (no extra between)");
    if (nb == pos && memcmp(out, buf, pos) == 0) { PASS(); }
    else { FAIL("got %zu bytes, expected %zu", nb, pos); }

    /* Visible length preserved through collapse. */
    pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append(buf, pos, COLOR_FG_GREEN, 3);
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append_str(buf, pos, "XYZ");
    pos = append(buf, pos, COLOR_RESET, 3);
    buf[pos] = '\0';

    nb = co_collapse_color(out, buf, pos);
    size_t vis = co_visible_length(out, nb);
    TEST("collapse preserves visible length");
    if (vis == 3) { PASS(); } else { FAIL("got %zu vis", vis); }
}

static void test_collapse_24bit(void)
{
    printf("\n--- collapse 24-bit color ---\n");
    unsigned char out[LBUF_SIZE];
    unsigned char buf[256];
    size_t nb, pos;

    /* 24-bit FG: XTERM_RED + RED_DELTA_42 + GREEN_DELTA_17 + BLUE_DELTA_99 + "A"
     * This is already minimal (one base + 3 deltas), should pass through. */
    pos = 0;
    pos = append(buf, pos, COLOR_FG_RED, 3);
    pos = append(buf, pos, RED_FG_DELTA_42, 4);
    pos = append(buf, pos, GREEN_FG_DELTA_17, 4);
    pos = append(buf, pos, BLUE_FG_DELTA_99, 4);
    pos = append_str(buf, pos, "A");
    buf[pos] = '\0';

    nb = co_collapse_color(out, buf, pos);
    TEST("collapse 24-bit FG -> preserves visible");
    size_t vis24 = co_visible_length(out, nb);
    if (vis24 == 1) { PASS(); } else { FAIL("got %zu vis", vis24); }

    /* Round-trip: collapse should be idempotent. */
    unsigned char out2[LBUF_SIZE];
    size_t nb2 = co_collapse_color(out2, out, nb);
    TEST("collapse 24-bit is idempotent");
    if (nb2 == nb && memcmp(out, out2, nb) == 0) { PASS(); }
    else { FAIL("first %zu bytes, second %zu bytes", nb, nb2); }
}

static void test_apply_color(void)
{
    printf("\n--- apply_color ---\n");
    unsigned char out[LBUF_SIZE];
    size_t nb;

    /* Apply FG_RED to "hello". */
    co_ColorState red_fg = CO_CS_NORMAL;
    red_fg.fg = 1;  /* XTERM palette index 1 = red */
    red_fg.fg_r = 128; red_fg.fg_g = 0; red_fg.fg_b = 0;

    nb = co_apply_color(out, (const unsigned char *)"hello", 5, red_fg);
    TEST("apply_color FG_RED to \"hello\"");
    /* Should be FG_RED(3) + "hello"(5) = 8 bytes */
    if (nb == 8 && memcmp(out, COLOR_FG_RED, 3) == 0
               && memcmp(out + 3, "hello", 5) == 0) { PASS(); }
    else { FAIL("got %zu bytes", nb); }

    /* Apply INTENSE to "hi". */
    co_ColorState bold = CO_CS_NORMAL;
    bold.intense = 1;

    nb = co_apply_color(out, (const unsigned char *)"hi", 2, bold);
    TEST("apply_color INTENSE to \"hi\"");
    /* INTENSE = U+F501 = EF 94 81 (3 bytes) + "hi"(2) = 5 */
    unsigned char expected_intense[] = { 0xEF, 0x94, 0x81 };
    if (nb == 5 && memcmp(out, expected_intense, 3) == 0
               && memcmp(out + 3, "hi", 2) == 0) { PASS(); }
    else { FAIL("got %zu bytes", nb); }

    /* Apply NORMAL (no-op). */
    co_ColorState normal = CO_CS_NORMAL;
    nb = co_apply_color(out, (const unsigned char *)"test", 4, normal);
    TEST("apply_color NORMAL -> no prefix");
    if (nb == 4 && memcmp(out, "test", 4) == 0) { PASS(); }
    else { FAIL("got %zu bytes", nb); }

    /* Apply FG + BG + attr. */
    co_ColorState combo = CO_CS_NORMAL;
    combo.fg = 1;  /* red */
    combo.fg_r = 128; combo.fg_g = 0; combo.fg_b = 0;
    combo.bg = 0;  /* black */
    combo.bg_r = 0; combo.bg_g = 0; combo.bg_b = 0;
    combo.intense = 1;

    nb = co_apply_color(out, (const unsigned char *)"X", 1, combo);
    TEST("apply_color FG+BG+INTENSE to \"X\"");
    /* INTENSE(3) + FG_RED(3) + BG_BLACK(3) + "X"(1) = 10 */
    size_t vis = co_visible_length(out, nb);
    if (vis == 1 && nb == 10) { PASS(); }
    else { FAIL("got %zu bytes, %zu vis", nb, vis); }

    /* Round-trip: apply then collapse should be stable. */
    unsigned char out2[LBUF_SIZE];
    size_t nb2 = co_collapse_color(out2, out, nb);
    TEST("apply then collapse is stable");
    if (nb2 == nb && memcmp(out, out2, nb) == 0) { PASS(); }
    else { FAIL("got %zu bytes after collapse, expected %zu", nb2, nb); }
}

/* ---- Main ---- */

int main(void)
{
    printf("=== Ragel color_ops Tests ===\n");

    /* Stage 0 */
    test_empty();
    test_plain_ascii();
    test_bmp_color();
    test_24bit_color();
    test_utf8_with_color();
    test_color_only();
    test_colored_words();

    /* Stage 1 */
    test_words_count();
    test_first_rest_last();
    test_extract();
    test_left_right();
    test_trim();
    test_search();

    /* Stage 2 */
    test_strip_color();
    test_case_transforms();
    test_reverse();
    test_edit();
    test_transform();
    test_compress();

    /* Stage 3 */
    test_mid();
    test_pos();
    test_lpos();
    test_member();
    test_center();
    test_ljust_rjust();
    test_repeat();

    /* Stage 4 */
    test_delete();
    test_splice();
    test_insert_word();
    test_sort_words();
    test_setops();

    /* Stage 5 */
    test_collapse_color();
    test_collapse_24bit();
    test_apply_color();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
