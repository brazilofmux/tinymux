/*
 * color_ops.h — Ragel -G2 string mutation primitives for PUA-colored UTF-8.
 *
 * TinyMUX encodes color as Unicode Private Use Area code points inline
 * in UTF-8 strings:
 *
 *   BMP PUA (3-byte UTF-8):
 *     U+F500         reset
 *     U+F501         intense
 *     U+F504         underline
 *     U+F505         blink
 *     U+F507         inverse
 *     U+F600-F6FF    256 foreground XTERM indexed colors
 *     U+F700-F7FF    256 background XTERM indexed colors
 *
 *   Supplementary PUA (4-byte UTF-8, Plane 15):
 *     U+F0000-F00FF  red FG delta
 *     U+F0100-F01FF  green FG delta
 *     U+F0200-F02FF  blue FG delta
 *     U+F0300-F03FF  red BG delta
 *     U+F0400-F04FF  green BG delta
 *     U+F0500-F05FF  blue BG delta
 *
 * A color annotation is 1-4 code points: a base (3 bytes BMP PUA),
 * optionally followed by up to 3 RGB delta code points (4 bytes SMP PUA).
 * 24-bit color = base + up to 3 deltas = up to 15 bytes.
 *
 * These routines operate directly on the PUA-inline UTF-8 representation,
 * in a single pass, without stripping/re-inserting color metadata.
 */

#ifndef COLOR_OPS_H
#define COLOR_OPS_H

#include <stddef.h>
#include <stdint.h>

/* LBUF_SIZE matches TinyMUX. */
#define LBUF_SIZE 8000

/*
 * co_visible_length — Count visible code points (skipping color PUA).
 *
 * Returns the number of non-color Unicode code points in the string.
 * This is equivalent to mux_string::length_point() after import.
 */
size_t co_visible_length(const unsigned char *p, size_t len);

/*
 * co_skip_color — Advance past any color PUA code points at current position.
 *
 * Returns pointer to the first non-color byte at or after p.
 * Does not advance past the end (p + len).
 */
const unsigned char *co_skip_color(const unsigned char *p,
                                   const unsigned char *pe);

/*
 * co_visible_advance — Advance past exactly n visible code points.
 *
 * Skips color code points transparently.  Returns pointer to the byte
 * after the nth visible code point (or pe if fewer than n remain).
 * If out_count is non-NULL, stores the actual number advanced.
 */
const unsigned char *co_visible_advance(const unsigned char *p,
                                        const unsigned char *pe,
                                        size_t n,
                                        size_t *out_count);

/*
 * co_copy_visible — Copy up to n visible code points, preserving color.
 *
 * Copies bytes from [p, pe) to out, including any interleaved color
 * code points, but counting only visible code points toward the limit.
 * Returns the number of bytes written to out.
 * out must have room for at least LBUF_SIZE bytes.
 */
size_t co_copy_visible(unsigned char *out, const unsigned char *p,
                       const unsigned char *pe, size_t n);

/*
 * co_find_delim — Find first occurrence of single-byte delimiter,
 *                 skipping color PUA code points.
 *
 * Returns pointer to the delimiter byte, or NULL if not found.
 * Only matches against visible bytes (never inside color sequences).
 */
const unsigned char *co_find_delim(const unsigned char *p,
                                   const unsigned char *pe,
                                   unsigned char delim);

/* ---- Stage 1: Word and substring operations ---- */

/*
 * co_strip_color — Copy string with all color PUA removed.
 *
 * Returns bytes written to out (visible content only).
 */
size_t co_strip_color(unsigned char *out, const unsigned char *p, size_t len);

/*
 * co_words_count — Count words separated by delimiter, skipping color.
 *
 * Matches MUX words() behavior: consecutive delimiters do NOT create
 * empty words (they are compressed).
 */
size_t co_words_count(const unsigned char *p, size_t len,
                      unsigned char delim);

/*
 * co_first — Extract the first word before delimiter, preserving color.
 *
 * Copies all bytes (including color) up to but not including the first
 * delimiter occurrence.  Returns bytes written to out.
 */
size_t co_first(unsigned char *out, const unsigned char *p, size_t len,
                unsigned char delim);

/*
 * co_rest — Everything after the first word, preserving color.
 *
 * Skips past the first delimiter and copies the remainder.
 * Returns bytes written to out.
 */
size_t co_rest(unsigned char *out, const unsigned char *p, size_t len,
               unsigned char delim);

/*
 * co_last — Extract the last word after the final delimiter.
 *
 * Returns bytes written to out.
 */
size_t co_last(unsigned char *out, const unsigned char *p, size_t len,
               unsigned char delim);

/*
 * co_extract — Extract words iFirst..iLast (1-based), preserving color.
 *
 * MUX extract() semantics: words are 1-based, delimiters are compressed.
 * Copies the extracted words (with their original color) separated by
 * the output delimiter.  Returns bytes written to out.
 */
size_t co_extract(unsigned char *out,
                  const unsigned char *p, size_t len,
                  size_t iFirst, size_t nWords,
                  unsigned char delim, unsigned char osep);

/*
 * co_left — First n visible code points, preserving color.
 *
 * Returns bytes written to out.
 */
size_t co_left(unsigned char *out,
               const unsigned char *p, size_t len, size_t n);

/*
 * co_right — Last n visible code points, preserving color.
 *
 * Returns bytes written to out.
 */
size_t co_right(unsigned char *out,
                const unsigned char *p, size_t len, size_t n);

/*
 * co_trim — Trim leading/trailing characters, preserving color.
 *
 * trim_flags: 1 = trim left, 2 = trim right, 3 = both.
 * Trims any visible code point whose first byte matches trim_char.
 * If trim_char is 0, trims ASCII whitespace (0x20, 0x09).
 */
size_t co_trim(unsigned char *out,
               const unsigned char *p, size_t len,
               unsigned char trim_char, int trim_flags);

/*
 * co_search — Find first occurrence of a visible substring pattern,
 *             skipping color in both haystack and needle.
 *
 * Returns pointer into haystack where the match begins (at a visible
 * code point), or NULL if not found.  Only compares visible bytes.
 */
const unsigned char *co_search(const unsigned char *haystack, size_t hlen,
                               const unsigned char *needle, size_t nlen);

/* ---- Stage 2: Transforms, edit, reverse ---- */

/*
 * co_toupper — Convert visible code points to uppercase, preserving color.
 *
 * Full Unicode case mapping via DFA tables from the utf/ pipeline
 * (~1477 code points).  Output may be longer or shorter than input
 * (e.g., ß → SS).  Color PUA code points are passed through unchanged.
 *
 * Returns bytes written to out.
 */
size_t co_toupper(unsigned char *out, const unsigned char *p, size_t len);

/*
 * co_tolower — Convert visible code points to lowercase, preserving color.
 *
 * Full Unicode case mapping via DFA tables (~1460 code points).
 * Returns bytes written to out.
 */
size_t co_tolower(unsigned char *out, const unsigned char *p, size_t len);

/*
 * co_totitle — Title-case first visible code point, preserving color.
 *
 * Full Unicode title-case mapping via DFA tables (~1481 code points).
 * Returns bytes written to out.
 */
size_t co_totitle(unsigned char *out, const unsigned char *p, size_t len);

/*
 * co_reverse — Reverse visible code points, preserving color attachment.
 *
 * Each visible code point's preceding color annotation stays attached
 * to it after reversal.  Leading color (before any visible char) stays
 * at the front.
 *
 * Returns bytes written to out.
 */
size_t co_reverse(unsigned char *out, const unsigned char *p, size_t len);

/*
 * co_edit — Find-and-replace, color-aware.
 *
 * Replaces all non-overlapping occurrences of pattern with replacement.
 * Pattern matching skips color (same as co_search).
 * Color in the haystack around the matched region is preserved;
 * the replacement is inserted without color.
 *
 * Returns bytes written to out.
 */
size_t co_edit(unsigned char *out,
               const unsigned char *str, size_t slen,
               const unsigned char *from, size_t flen,
               const unsigned char *to, size_t tlen);

/*
 * co_transform — Character mapping (tr/translate).
 *
 * For each visible code point in str, if it appears in from_set at
 * position i, replace it with the code point at position i in to_set.
 * Color is preserved.  from_set and to_set must have the same number
 * of visible code points.
 *
 * ASCII fast path: if both sets are pure ASCII, uses 256-byte lookup.
 * Returns bytes written to out.
 */
size_t co_transform(unsigned char *out,
                    const unsigned char *str, size_t slen,
                    const unsigned char *from_set, size_t flen,
                    const unsigned char *to_set, size_t tlen);

/*
 * co_compress — Collapse runs of a character.
 *
 * Reduces consecutive runs of trim_char to a single occurrence.
 * If trim_char is 0, compresses runs of ASCII whitespace.
 * Color is preserved (color between compressed chars is dropped).
 *
 * Returns bytes written to out.
 */
size_t co_compress(unsigned char *out,
                   const unsigned char *p, size_t len,
                   unsigned char compress_char);

/* ---- Stage 3: Position, substring, padding ---- */

/*
 * co_mid — Substring by visible position, preserving color.
 *
 * MUX mid() semantics: iStart is 0-based visible index, nCount is the
 * number of visible code points to extract.  Color interleaved in the
 * extracted range is preserved.  Color preceding the range is included
 * if it immediately precedes the first extracted visible code point.
 *
 * Returns bytes written to out.
 */
size_t co_mid(unsigned char *out,
              const unsigned char *p, size_t len,
              size_t iStart, size_t nCount);

/*
 * co_pos — Find first visible-substring position (1-based).
 *
 * MUX pos() semantics: returns the 1-based visible code point index
 * where needle first appears in haystack.  Returns 0 if not found.
 * Color is skipped in both haystack and needle during matching.
 */
size_t co_pos(const unsigned char *haystack, size_t hlen,
              const unsigned char *needle, size_t nlen);

/*
 * co_lpos — Find all occurrences of a single-byte pattern.
 *
 * MUX lpos() semantics: writes space-separated 0-based visible
 * positions of every occurrence of the pattern byte.
 * Returns bytes written to out.
 */
size_t co_lpos(unsigned char *out,
               const unsigned char *haystack, size_t hlen,
               unsigned char pattern);

/*
 * co_member — Find word in word-list (1-based).
 *
 * MUX member() semantics: searches the delimited word list for an
 * exact match of target.  Returns the 1-based word index, or 0 if
 * not found.  Comparison is on visible content (color stripped).
 */
size_t co_member(const unsigned char *target, size_t tlen,
                 const unsigned char *list, size_t llen,
                 unsigned char delim);

/*
 * co_center — Center string with padding, preserving color.
 *
 * Pads string to exactly width visible code points, centered.
 * fill/fill_len is the fill pattern (cycled if multi-character).
 * If fill_len is 0, uses space.  Truncates if string is wider.
 *
 * Returns bytes written to out.
 */
size_t co_center(unsigned char *out,
                 const unsigned char *p, size_t len,
                 size_t width,
                 const unsigned char *fill, size_t fill_len);

/*
 * co_ljust — Left-justify (right-pad) to width, preserving color.
 *
 * Returns bytes written to out.
 */
size_t co_ljust(unsigned char *out,
                const unsigned char *p, size_t len,
                size_t width,
                const unsigned char *fill, size_t fill_len);

/*
 * co_rjust — Right-justify (left-pad) to width, preserving color.
 *
 * Returns bytes written to out.
 */
size_t co_rjust(unsigned char *out,
                const unsigned char *p, size_t len,
                size_t width,
                const unsigned char *fill, size_t fill_len);

/*
 * co_repeat — Repeat string n times, preserving color.
 *
 * Returns bytes written to out, or 0 if result would exceed LBUF_SIZE.
 */
size_t co_repeat(unsigned char *out,
                 const unsigned char *p, size_t len,
                 size_t count);

/* ---- Stage 4: Delete, splice, insert, sort, set operations ---- */

/*
 * co_delete — Delete visible code points by position, preserving color.
 *
 * MUX delete() semantics: iStart is 0-based visible index, nCount is
 * the number of visible code points to delete.  Color interleaved in
 * the deleted range is dropped.  Color before and after is preserved.
 *
 * Returns bytes written to out.
 */
size_t co_delete(unsigned char *out,
                 const unsigned char *p, size_t len,
                 size_t iStart, size_t nCount);

/*
 * co_splice — Word-level splice: replace matching words from list2.
 *
 * MUX splice() semantics: for each word in list1, if it matches
 * search_word, output the corresponding word from list2 instead.
 * list1 and list2 must have the same number of words.
 * Returns bytes written to out, or 0 on error (mismatched lengths).
 */
size_t co_splice(unsigned char *out,
                 const unsigned char *list1, size_t len1,
                 const unsigned char *list2, size_t len2,
                 const unsigned char *search, size_t slen,
                 unsigned char delim, unsigned char osep);

/*
 * co_insert_word — Insert a word at a position in a word list.
 *
 * MUX insert() semantics: iPos is 1-based word position.
 * The new word is inserted before position iPos.
 * If iPos <= 0, inserts at the beginning.
 * If iPos > number of words, appends at the end.
 * Returns bytes written to out.
 */
size_t co_insert_word(unsigned char *out,
                      const unsigned char *list, size_t llen,
                      const unsigned char *word, size_t wlen,
                      size_t iPos,
                      unsigned char delim, unsigned char osep);

/*
 * co_sort_words — Sort a word list, preserving color per-word.
 *
 * sort_type: 'a' = ASCII, 'i' = case-insensitive ASCII,
 *            'n' = numeric, 'd' = dbref.
 * Returns bytes written to out.
 */
size_t co_sort_words(unsigned char *out,
                     const unsigned char *list, size_t llen,
                     unsigned char delim, unsigned char osep,
                     char sort_type);

/*
 * co_setunion — Set union of two word lists, sorted and deduplicated.
 *
 * Returns bytes written to out.
 */
size_t co_setunion(unsigned char *out,
                   const unsigned char *list1, size_t len1,
                   const unsigned char *list2, size_t len2,
                   unsigned char delim, unsigned char osep,
                   char sort_type);

/*
 * co_setdiff — Set difference (list1 minus list2), sorted.
 *
 * Returns bytes written to out.
 */
size_t co_setdiff(unsigned char *out,
                  const unsigned char *list1, size_t len1,
                  const unsigned char *list2, size_t len2,
                  unsigned char delim, unsigned char osep,
                  char sort_type);

/*
 * co_setinter — Set intersection of two word lists, sorted.
 *
 * Returns bytes written to out.
 */
size_t co_setinter(unsigned char *out,
                   const unsigned char *list1, size_t len1,
                   const unsigned char *list2, size_t len2,
                   unsigned char delim, unsigned char osep,
                   char sort_type);

/* ---- Stage 5: Color collapse ---- */

/*
 * co_ColorState — Tracks the current color/attribute state.
 *
 * fg/bg: -1 = default (terminal default), 0-255 = XTERM palette index,
 *        -2 = 24-bit RGB (use fg_r/g/b or bg_r/g/b).
 * Attributes: 0 = off, 1 = on.
 */
typedef struct {
    int16_t fg;        /* -1=default, 0-255=indexed, -2=RGB */
    int16_t bg;        /* -1=default, 0-255=indexed, -2=RGB */
    uint8_t fg_r, fg_g, fg_b;
    uint8_t bg_r, bg_g, bg_b;
    uint8_t intense, underline, blink, inverse;
} co_ColorState;

/* The default (reset) state. */
#define CO_CS_NORMAL  ((co_ColorState){ -1, -1, 0,0,0, 0,0,0, 0,0,0,0 })

/* Convenience constructors for common states. */
static inline co_ColorState co_cs_fg(int idx) {
    co_ColorState cs = CO_CS_NORMAL;
    cs.fg = (int16_t)idx;
    return cs;
}
static inline co_ColorState co_cs_bg(int idx) {
    co_ColorState cs = CO_CS_NORMAL;
    cs.bg = (int16_t)idx;
    return cs;
}

/* Compare two color states for equality. */
static inline int co_cs_equal(const co_ColorState *a, const co_ColorState *b) {
    return a->fg == b->fg && a->bg == b->bg
        && a->fg_r == b->fg_r && a->fg_g == b->fg_g && a->fg_b == b->fg_b
        && a->bg_r == b->bg_r && a->bg_g == b->bg_g && a->bg_b == b->bg_b
        && a->intense == b->intense && a->underline == b->underline
        && a->blink == b->blink && a->inverse == b->inverse;
}

/*
 * co_collapse_color — Eliminate redundant PUA color sequences.
 *
 * Scans input PUA-colored UTF-8.  For each visible code point, computes
 * the net color state from all preceding PUA codes, then emits only the
 * minimal PUA transition from the previously emitted state.
 *
 * Example: FG_RED + FG_GREEN + "A" → FG_GREEN + "A" (FG_RED dropped).
 *
 * Returns bytes written to out.
 */
size_t co_collapse_color(unsigned char *out,
                         const unsigned char *p, size_t len);

/*
 * co_apply_color — Prepend a ColorState to a string as PUA bytes.
 *
 * Emits the minimal PUA sequence to transition from CO_CS_NORMAL
 * to the given state, then copies the string.
 * Returns bytes written to out.
 */
size_t co_apply_color(unsigned char *out,
                      const unsigned char *p, size_t len,
                      co_ColorState cs);

/*
 * co_escape — Insert backslash before escape characters, preserving color.
 *
 * MUX escape() semantics: prepends '\' before the first visible character
 * and before any character in the set: $%(),;[\]^{}.
 * Color PUA codes are passed through unchanged.
 *
 * Returns bytes written to out.
 */
size_t co_escape(unsigned char *out,
                 const unsigned char *data, size_t len);

#endif /* COLOR_OPS_H */
