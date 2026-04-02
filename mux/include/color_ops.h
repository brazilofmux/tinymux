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
 *     U+F0000-F0FFF  FG CP1: (R high nibble << 8) | G
 *     U+F1000-F1FFF  FG CP2: (R low nibble << 8) | B
 *     U+F2000-F2FFF  BG CP1: (R high nibble << 8) | G
 *     U+F3000-F3FFF  BG CP2: (R low nibble << 8) | B
 *
 * A color annotation is 1-3 code points: a base (3 bytes BMP PUA),
 * optionally followed by exactly 2 SMP code points (4 bytes each).
 * 24-bit color = base + 2 SMP = always 11 bytes per layer.
 *
 * These routines operate directly on the PUA-inline UTF-8 representation,
 * in a single pass, without stripping/re-inserting color metadata.
 */

#ifndef COLOR_OPS_H
#define COLOR_OPS_H

#include <stddef.h>
#include <stdint.h>

/* Portable LIBMUX_API for DLL export/import on Windows. */
#ifndef LIBMUX_API
#if defined(_WIN32) || defined(WIN32)
#ifdef BUILDING_LIBMUX
#define LIBMUX_API __declspec(dllexport)
#else
#define LIBMUX_API __declspec(dllimport)
#endif
#else
#define LIBMUX_API __attribute__((visibility("default")))
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* LBUF_SIZE is defined in alloc.h; use it from there. */
#ifndef LBUF_SIZE
#define LBUF_SIZE 8000
#endif

/*
 * co_visible_length — Count visible code points (skipping color PUA).
 *
 * Returns the number of non-color Unicode code points in the string.
 * This counts code points in a null-terminated UTF-8 string.
 */
LIBMUX_API size_t co_visible_length(const unsigned char *p, size_t len);

/*
 * co_skip_color — Advance past any color PUA code points at current position.
 *
 * Returns pointer to the first non-color byte at or after p.
 * Does not advance past the end (p + len).
 */
LIBMUX_API const unsigned char *co_skip_color(const unsigned char *p,
                                   const unsigned char *pe);

/*
 * co_visible_advance — Advance past exactly n visible code points.
 *
 * Skips color code points transparently.  Returns pointer to the byte
 * after the nth visible code point (or pe if fewer than n remain).
 * If out_count is non-NULL, stores the actual number advanced.
 */
LIBMUX_API const unsigned char *co_visible_advance(const unsigned char *p,
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
LIBMUX_API size_t co_copy_visible(unsigned char *out, const unsigned char *p,
                       const unsigned char *pe, size_t n);

/*
 * co_find_delim — Find first occurrence of single-byte delimiter,
 *                 skipping color PUA code points.
 *
 * Returns pointer to the delimiter byte, or NULL if not found.
 * Only matches against visible bytes (never inside color sequences).
 */
LIBMUX_API const unsigned char *co_find_delim(const unsigned char *p,
                                   const unsigned char *pe,
                                   unsigned char delim);

/* ---- Stage 1: Word and substring operations ---- */

/*
 * co_strip_color — Copy string with all color PUA removed.
 *
 * Returns bytes written to out (visible content only).
 */
LIBMUX_API size_t co_strip_color(unsigned char *out, const unsigned char *p, size_t len);

/*
 * co_words_count — Count words separated by delimiter, skipping color.
 *
 * Matches MUX words() behavior: consecutive delimiters do NOT create
 * empty words (they are compressed).
 */
LIBMUX_API size_t co_words_count(const unsigned char *p, size_t len,
                      unsigned char delim);

/*
 * co_first — Extract the first word before delimiter, preserving color.
 *
 * Copies all bytes (including color) up to but not including the first
 * delimiter occurrence.  Returns bytes written to out.
 */
LIBMUX_API size_t co_first(unsigned char *out, const unsigned char *p, size_t len,
                unsigned char delim);

/*
 * co_rest — Everything after the first word, preserving color.
 *
 * Skips past the first delimiter and copies the remainder.
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_rest(unsigned char *out, const unsigned char *p, size_t len,
               unsigned char delim);

/*
 * co_last — Extract the last word after the final delimiter.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_last(unsigned char *out, const unsigned char *p, size_t len,
               unsigned char delim);

/*
 * co_extract — Extract words iFirst..iLast (1-based), preserving color.
 *
 * MUX extract() semantics: words are 1-based, delimiters are compressed.
 * Copies the extracted words (with their original color) separated by
 * the output delimiter.  Returns bytes written to out.
 */
LIBMUX_API size_t co_extract(unsigned char *out,
                  const unsigned char *p, size_t len,
                  size_t iFirst, size_t nWords,
                  unsigned char delim, unsigned char osep);

/*
 * co_left — First n visible code points, preserving color.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_left(unsigned char *out,
               const unsigned char *p, size_t len, size_t n);

/*
 * co_right — Last n visible code points, preserving color.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_right(unsigned char *out,
                const unsigned char *p, size_t len, size_t n);

/*
 * co_trim — Trim leading/trailing characters, preserving color.
 *
 * trim_flags: 1 = trim left, 2 = trim right, 3 = both.
 * Trims any visible code point whose first byte matches trim_char.
 * If trim_char is 0, trims ASCII whitespace (0x20, 0x09).
 */
LIBMUX_API size_t co_trim(unsigned char *out,
               const unsigned char *p, size_t len,
               unsigned char trim_char, int trim_flags);

/*
 * co_search — Find first occurrence of a visible substring pattern,
 *             skipping color in both haystack and needle.
 *
 * Returns pointer into haystack where the match begins (at a visible
 * code point), or NULL if not found.  Only compares visible bytes.
 */
LIBMUX_API const unsigned char *co_search(const unsigned char *haystack, size_t hlen,
                               const unsigned char *needle, size_t nlen);

/*
 * co_split_words — Split PUA-encoded string by multi-char delimiter.
 *
 * Populates word_starts[] and word_ends[] with byte offsets into data.
 * Space delimiter uses compress semantics; non-space is exact.
 * Returns number of words found.
 */
LIBMUX_API size_t co_split_words(const unsigned char *data, size_t len,
                      const unsigned char *sep, size_t sep_len,
                      size_t *word_starts, size_t *word_ends,
                      size_t max_words);

/*
 * co_trim_pattern — Trim repeating multi-byte pattern from edges.
 *
 * Pattern is matched cyclically at the raw byte level.
 * trim_flags: 1 = left, 2 = right, 3 = both.
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_trim_pattern(unsigned char *out,
                       const unsigned char *data, size_t len,
                       const unsigned char *pattern, size_t plen,
                       int trim_flags);

/*
 * co_compress_str — Compress runs of a multi-char separator into one.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_compress_str(unsigned char *out,
                       const unsigned char *data, size_t len,
                       const unsigned char *sep, size_t sep_len);

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
LIBMUX_API size_t co_toupper(unsigned char *out, const unsigned char *p, size_t len);

/*
 * co_tolower — Convert visible code points to lowercase, preserving color.
 *
 * Full Unicode case mapping via DFA tables (~1460 code points).
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_tolower(unsigned char *out, const unsigned char *p, size_t len);

/*
 * co_totitle — Title-case first visible code point, preserving color.
 *
 * Full Unicode title-case mapping via DFA tables (~1481 code points).
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_totitle(unsigned char *out, const unsigned char *p, size_t len);

/*
 * co_reverse — Reverse visible code points, preserving color attachment.
 *
 * Each visible code point's preceding color annotation stays attached
 * to it after reversal.  Leading color (before any visible char) stays
 * at the front.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_reverse(unsigned char *out, const unsigned char *p, size_t len);

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
LIBMUX_API size_t co_edit(unsigned char *out,
               const unsigned char *str, size_t slen,
               const unsigned char *from, size_t flen,
               const unsigned char *to, size_t tlen);

/*
 * co_transform — Grapheme-cluster mapping (tr/translate).
 *
 * For each visible grapheme cluster in str, if it matches the i-th
 * cluster in from_set, replace it with the i-th cluster in to_set.
 * Color PUA codes in str are preserved.  from_set and to_set should
 * have the same number of grapheme clusters (after color stripping),
 * and the count must not exceed MAX_TR_CLUSTERS.
 *
 * Dual-path: if all clusters in both sets are single ASCII bytes,
 * uses a 256-byte lookup table (fast path).  Otherwise, walks
 * clusters via next_grapheme_plain() with linear-scan matching.
 * Returns bytes written to out.
 */
#define MAX_TR_CLUSTERS 1024
LIBMUX_API size_t co_transform(unsigned char *out,
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
LIBMUX_API size_t co_compress(unsigned char *out,
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
LIBMUX_API size_t co_mid(unsigned char *out,
              const unsigned char *p, size_t len,
              size_t iStart, size_t nCount);

/*
 * co_pos — Find first visible-substring position (1-based).
 *
 * MUX pos() semantics: returns the 1-based visible code point index
 * where needle first appears in haystack.  Returns 0 if not found.
 * Color is skipped in both haystack and needle during matching.
 */
LIBMUX_API size_t co_pos(const unsigned char *haystack, size_t hlen,
              const unsigned char *needle, size_t nlen);

/*
 * co_lpos — Find all occurrences of a single-byte pattern.
 *
 * MUX lpos() semantics: writes space-separated 0-based visible
 * positions of every occurrence of the pattern byte.
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_lpos(unsigned char *out,
               const unsigned char *haystack, size_t hlen,
               unsigned char pattern);

/*
 * co_member — Find word in word-list (1-based).
 *
 * MUX member() semantics: searches the delimited word list for an
 * exact match of target.  Returns the 1-based word index, or 0 if
 * not found.  Comparison is on visible content (color stripped).
 */
LIBMUX_API size_t co_member(const unsigned char *target, size_t tlen,
                 const unsigned char *list, size_t llen,
                 unsigned char delim);

/*
 * co_console_width — Column width of a single visible code point.
 *
 * Returns 0 for combining marks, 2 for fullwidth/CJK, 1 otherwise.
 * Wraps ConsoleWidth() from stringutil.cpp with C linkage.
 */
LIBMUX_API int co_console_width(const unsigned char *pCodePoint);

/*
 * co_visual_width — Total display column width of a PUA-colored string.
 *
 * Skips color PUA codes, sums co_console_width() for visible chars.
 * Accounts for fullwidth (2 columns) and zero-width (0 columns).
 */
LIBMUX_API size_t co_visual_width(const unsigned char *p, size_t len);

/*
 * co_copy_columns — Copy up to n display columns, preserving color.
 *
 * Like co_copy_visible but counts display columns instead of code points.
 * A fullwidth character counts as 2 columns.  Stops before emitting a
 * character that would exceed the column limit.
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_copy_columns(unsigned char *out, const unsigned char *p,
                       const unsigned char *pe, size_t ncols);

/*
 * co_center — Center string with padding, preserving color.
 *
 * Pads string to exactly 'width' display columns, centered.
 * fill/fill_len is the fill pattern (cycled, phase-continuous).
 * If fill_len is 0, uses space.  bTrunc: truncate if wider.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_center(unsigned char *out,
                 const unsigned char *p, size_t len,
                 size_t width,
                 const unsigned char *fill, size_t fill_len,
                 int bTrunc);

/*
 * co_ljust — Left-justify (right-pad) to width columns, preserving color.
 *
 * bTrunc: if true, truncate input to width if wider.
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_ljust(unsigned char *out,
                const unsigned char *p, size_t len,
                size_t width,
                const unsigned char *fill, size_t fill_len,
                int bTrunc);

/*
 * co_rjust — Right-justify (left-pad) to width columns, preserving color.
 *
 * bTrunc: if true, truncate input to width if wider.
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_rjust(unsigned char *out,
                const unsigned char *p, size_t len,
                size_t width,
                const unsigned char *fill, size_t fill_len,
                int bTrunc);

/*
 * co_repeat — Repeat string n times, preserving color.
 *
 * Returns bytes written to out, or 0 if result would exceed LBUF_SIZE.
 */
LIBMUX_API size_t co_repeat(unsigned char *out,
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
LIBMUX_API size_t co_delete(unsigned char *out,
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
LIBMUX_API size_t co_splice(unsigned char *out,
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
LIBMUX_API size_t co_insert_word(unsigned char *out,
                      const unsigned char *list, size_t llen,
                      const unsigned char *word, size_t wlen,
                      size_t iPos,
                      unsigned char delim, unsigned char osep);

/*
 * co_replace_at — Replace words at given positions in a word list.
 *
 * Matches do_itemfuns IF_REPLACE semantics: positions are 1-based,
 * negative wraps from end.  Out-of-range positions are ignored.
 * Duplicate positions are collapsed.  The positions array is modified
 * (sorted in place).  Returns bytes written to out.
 */
LIBMUX_API size_t co_replace_at(unsigned char *out,
                     const unsigned char *list, size_t llen,
                     int *positions, int nPositions,
                     const unsigned char *word, size_t wlen,
                     unsigned char delim, unsigned char osep);

/*
 * co_insert_at — Insert a word at given positions in a word list.
 *
 * Matches do_itemfuns IF_INSERT semantics: positions are 1-based,
 * negative wraps from end (+nWords+1).  For empty lists, only
 * positions 1 and -1 are valid.  The positions array is modified
 * (sorted in place).  Returns bytes written to out.
 */
LIBMUX_API size_t co_insert_at(unsigned char *out,
                    const unsigned char *list, size_t llen,
                    int *positions, int nPositions,
                    const unsigned char *word, size_t wlen,
                    unsigned char delim, unsigned char osep);

/*
 * co_sort_words — Sort a word list, preserving color per-word.
 *
 * sort_type: 'a' = ASCII, 'i' = case-insensitive ASCII,
 *            'n' = numeric, 'd' = dbref.
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_sort_words(unsigned char *out,
                     const unsigned char *list, size_t llen,
                     unsigned char delim, unsigned char osep,
                     char sort_type);

/*
 * co_setunion — Set union of two word lists, sorted and deduplicated.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_setunion(unsigned char *out,
                   const unsigned char *list1, size_t len1,
                   const unsigned char *list2, size_t len2,
                   unsigned char delim, unsigned char osep,
                   char sort_type);

/*
 * co_setdiff — Set difference (list1 minus list2), sorted.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_setdiff(unsigned char *out,
                  const unsigned char *list1, size_t len1,
                  const unsigned char *list2, size_t len2,
                  unsigned char delim, unsigned char osep,
                  char sort_type);

/*
 * co_setinter — Set intersection of two word lists, sorted.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_setinter(unsigned char *out,
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
#ifdef _MSC_VER
static inline co_ColorState co_cs_normal_init_(void) {
    co_ColorState cs = {0};
    cs.fg = -1; cs.bg = -1;
    return cs;
}
#define CO_CS_NORMAL co_cs_normal_init_()
#else
#define CO_CS_NORMAL  ((co_ColorState){ -1, -1, 0,0,0, 0,0,0, 0,0,0,0 })
#endif

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
LIBMUX_API size_t co_collapse_color(unsigned char *out,
                         const unsigned char *p, size_t len);

/*
 * co_apply_color — Prepend a ColorState to a string as PUA bytes.
 *
 * Emits the minimal PUA sequence to transition from CO_CS_NORMAL
 * to the given state, then copies the string.
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_apply_color(unsigned char *out,
                      const unsigned char *p, size_t len,
                      co_ColorState cs);

/*
 * co_merge — Positional character merge with color tracking.
 *
 * MUX merge() semantics: for each position, if strA's visible code point
 * matches search, replace with strB's code point and color.
 * Tracks absolute color state for both strings and emits correct
 * PUA transitions in the output.
 *
 * Returns bytes written to out, or 0 if visible lengths differ.
 */
LIBMUX_API size_t co_merge(unsigned char *out,
                const unsigned char *strA, size_t lenA,
                const unsigned char *strB, size_t lenB,
                const unsigned char *search, size_t slen);

/*
 * co_escape — Insert backslash before escape characters, preserving color.
 *
 * MUX escape() semantics: prepends '\' before the first visible character
 * and before any character in the set: $%(),;[\]^{}.
 * Color PUA codes are passed through unchanged.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_escape(unsigned char *out,
                 const unsigned char *data, size_t len);

/* ---- Grapheme cluster operations ---- */

/*
 * co_cluster_count — Count Extended Grapheme Clusters in PUA-encoded string.
 *
 * Full UAX #29 grapheme segmentation (Unicode 16.0): handles combining
 * marks, Hangul jamo, emoji ZWJ sequences, regional indicators.
 * Color PUA code points are skipped (not counted as clusters).
 */
LIBMUX_API size_t co_cluster_count(const unsigned char *data, size_t len);

/*
 * co_cluster_advance — Advance past n grapheme clusters in PUA-encoded string.
 *
 * Returns pointer past the nth cluster (including any trailing color).
 * If out_count is non-NULL, stores the actual number of clusters advanced.
 */
LIBMUX_API const unsigned char *co_cluster_advance(const unsigned char *data,
                                        const unsigned char *pe,
                                        size_t n, size_t *out_count);

/*
 * co_mid_cluster — Substring by grapheme cluster position, preserving color.
 *
 * iStart is 0-based cluster index, nCount is number of clusters to extract.
 * Color interleaved in the extracted range is preserved.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_mid_cluster(unsigned char *out,
                      const unsigned char *data, size_t len,
                      size_t iStart, size_t nCount);

/*
 * co_delete_cluster — Delete grapheme clusters by position, preserving color.
 *
 * iStart is 0-based cluster index, nCount is number of clusters to delete.
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_delete_cluster(unsigned char *out,
                         const unsigned char *data, size_t len,
                         size_t iStart, size_t nCount);

/* ---- Stage 6: Rendering — PUA to client output ---- */

/*
 * co_dfa_ascii — Approximate a single UTF-8 code point to ASCII.
 *
 * Uses the tr_ascii DFA (generated by the ./utf pipeline) for
 * perceptual approximation (e.g., e-acute → 'e', n-tilde → 'n').
 * Returns '?' if no approximation exists.
 *
 * p must point to the first byte of a valid UTF-8 code point.
 */
LIBMUX_API unsigned char co_dfa_ascii(const unsigned char *p);

/*
 * co_render_ascii — Render PUA-colored UTF-8 to plain ASCII.
 *
 * Strips all color PUA code points and converts visible Unicode code
 * points to their best ASCII approximation via the tr_ascii DFA
 * (e.g., e-acute → 'e', n-tilde → 'n').  Pure ASCII bytes pass through.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_render_ascii(unsigned char *out,
                       const unsigned char *data, size_t len);

/*
 * co_render_ansi16 — Render PUA-colored UTF-8 to ANSI 16-color output.
 *
 * Converts PUA color codes to ANSI SGR escape sequences using the
 * basic 16-color palette (30-37/40-47 with bold for bright).
 * Indexed colors > 15 and 24-bit RGB are mapped to the nearest
 * 16-color entry via CIE97 perceptual distance.
 * Visible UTF-8 text passes through unchanged.
 *
 * Appends ESC[0m at end if color state is not normal.
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_render_ansi16(unsigned char *out,
                        const unsigned char *data, size_t len,
                        int bNoBleed);

/*
 * co_render_ansi256 — Render PUA-colored UTF-8 to ANSI 256-color output.
 *
 * Converts PUA color codes to xterm-256 SGR escape sequences
 * (ESC[38;5;Nm for fg, ESC[48;5;Nm for bg).  24-bit RGB colors
 * are mapped to the nearest 256-color entry via co_nearest_xterm256().
 * Visible UTF-8 text passes through unchanged.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_render_ansi256(unsigned char *out,
                         const unsigned char *data, size_t len,
                         int bNoBleed);

/*
 * co_render_truecolor — Render PUA-colored UTF-8 to TrueColor (24-bit) output.
 *
 * Indexed colors (0-255) use ESC[38;5;Nm / ESC[48;5;Nm (terminal knows
 * the palette).  24-bit RGB colors use ESC[38;2;R;G;Bm / ESC[48;2;R;G;Bm
 * at full fidelity — no palette approximation.
 * Visible UTF-8 text passes through unchanged.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_render_truecolor(unsigned char *out,
                           const unsigned char *data, size_t len,
                           int bNoBleed);

/*
 * co_render_html — Render PUA-colored UTF-8 to HTML.
 *
 * Converts PUA color codes to TinyMUX HTML tags:
 *   <B>/<U>/<I>/<S> for bold/underline/blink/inverse
 *   <COLOR #RRGGBB> for fg, <COLOR #RRGGBB #RRGGBB> for fg+bg
 *   <COLOR BACK=#RRGGBB> for bg only
 * HTML-escapes <, >, &, " in visible text.
 * Visible UTF-8 passes through unchanged.
 *
 * Returns bytes written to out.
 */
LIBMUX_API size_t co_render_html(unsigned char *out,
                      const unsigned char *data, size_t len);

/*
 * co_nearest_xterm256 — Find nearest xterm-256 palette entry for an RGB color.
 *
 * Uses CIE97 perceptual distance with K-d tree search through the
 * 256-entry xterm palette.  Much more accurate than Euclidean distance
 * in RGB space.
 *
 * rgb must point to 3 bytes: [R, G, B], each 0-255.
 * Returns the xterm-256 palette index (0-255).
 */
LIBMUX_API int co_nearest_xterm256(const unsigned char *rgb);

/*
 * co_nearest_xterm16 — Find nearest xterm-16 (ANSI) palette entry.
 *
 * Same algorithm as co_nearest_xterm256 but searches only the first
 * 16 palette entries.
 * Returns the palette index (0-15).
 */
LIBMUX_API int co_nearest_xterm16(const unsigned char *rgb);

/*
 * co_color_attr — Per-byte color and style attributes for GUI rendering.
 *
 * Used by GUI rendering paths (Win32 GDI, Direct2D) that need structured
 * color data instead of ANSI escape sequences.
 *
 * fg/bg:       0x00RRGGBB for truecolor or indexed (resolved to RGB).
 * fg_type/bg_type: 0 = default (use client's configured color),
 *                  1 = indexed (0-255, resolved to xterm palette RGB),
 *                  2 = 24-bit RGB.
 */
typedef struct {
    uint32_t fg;        /* 0x00RRGGBB */
    uint32_t bg;        /* 0x00RRGGBB */
    uint8_t  bold;      /* 1 = bold/intense */
    uint8_t  underline; /* 1 = underline */
    uint8_t  blink;     /* 1 = blink */
    uint8_t  inverse;   /* 1 = fg/bg swapped */
    uint8_t  fg_type;   /* 0 = default, 1 = indexed, 2 = RGB */
    uint8_t  bg_type;   /* 0 = default, 1 = indexed, 2 = RGB */
    uint8_t  pad[2];    /* Alignment padding */
} co_color_attr;

/*
 * co_render_attrs — Strip PUA color from UTF-8, emit text + parallel attrs.
 *
 * Walks PUA-encoded input, strips all color code points, writes visible
 * UTF-8 bytes to out_text, and writes one co_color_attr per output byte
 * to out_attrs.  All bytes within a single visible code point share the
 * same attribute entry.
 *
 * Indexed colors (0-255) are resolved to RGB via the xterm-256 palette
 * so that fg/bg always contain valid 0x00RRGGBB when fg_type/bg_type != 0.
 * The caller can override indexed entries at paint time by checking
 * fg_type/bg_type == 1 and using a custom palette instead.
 *
 * out_attrs:  Must have room for at least LBUF_SIZE entries.
 * out_text:   Must have room for at least LBUF_SIZE bytes.
 * data:       PUA-encoded UTF-8 input.
 * len:        Length of data in bytes.
 * bNoBleed:   If nonzero, reset color state to default at end of line.
 *
 * Returns bytes written to out_text.  out_attrs[0..return-1] is valid.
 */
LIBMUX_API size_t co_render_attrs(co_color_attr *out_attrs,
                                  unsigned char *out_text,
                                  const unsigned char *data, size_t len,
                                  int bNoBleed);

/*
 * co_parse_ansi — Parse ANSI SGR escape sequences into PUA color codes.
 *
 * Scans a byte stream, identifies ANSI SGR sequences (ESC [ ... m),
 * converts them to the corresponding PUA code points, and passes
 * non-ANSI bytes through unchanged.  This is the reverse of
 * co_render_ansi{16,256,truecolor}().
 *
 * Handles:
 *   - 16-color SGR (30-37, 40-47, 90-97, 100-107)
 *   - 256-color (38;5;N, 48;5;N)
 *   - True color (38;2;R;G;B, 48;2;R;G;B)
 *   - Attributes (0=reset, 1=bold, 4=underline, 5=blink, 7=inverse)
 *   - Non-ANSI bytes pass through unchanged
 *   - Non-SGR escape sequences (not ending in 'm') are stripped
 *
 * src:     Input byte stream (any encoding; ANSI escapes are ASCII).
 * src_len: Length of input in bytes.
 * dst:     Output buffer for PUA-encoded UTF-8.
 * dst_cap: Capacity of output buffer in bytes.
 *
 * Returns bytes written to dst.
 */
LIBMUX_API size_t co_parse_ansi(const unsigned char *src, size_t src_len,
                                unsigned char *dst, size_t dst_cap);

#ifdef __cplusplus
}
#endif

#endif /* COLOR_OPS_H */
