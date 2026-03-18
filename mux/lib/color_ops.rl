/*
 * color_ops.rl — Ragel -G2 string mutation primitives for PUA-colored UTF-8.
 *
 * TinyMUX color encoding: Unicode PUA code points inline in UTF-8.
 * See color_ops.h for the full encoding description.
 *
 * UTF-8 byte patterns for the color ranges:
 *
 *   BMP PUA U+F500-F7FF (attributes + FG + BG indexed):
 *     EF 94..9F 80..BF    (3 bytes)
 *
 *   SMP PUA U+F0000-F3FFF (24-bit RGB, 4 blocks of 4096):
 *     F3 B0 (80-BF) (80-BF)   Block 0: FG CP1 (R high nibble + G)
 *     F3 B1 (80-BF) (80-BF)   Block 1: FG CP2 (R low nibble + B)
 *     F3 B2 (80-BF) (80-BF)   Block 2: BG CP1 (R high nibble + G)
 *     F3 B3 (80-BF) (80-BF)   Block 3: BG CP2 (R low nibble + B)
 *
 *   So SMP color = F3 (B0..B3) (80..BF) (80..BF)
 */

#include "color_ops.h"
#include "unicode_tables_c.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Grapheme Cluster Break (GCB) DFA tables from utf8tables ---- */

#define TR_GCB_START_STATE (0)
#define TR_GCB_ACCEPTING_STATES_START (208)
#define CL_EXTPICT_START_STATE (0)
#define CL_EXTPICT_ACCEPTING_STATES_START (44)

extern const unsigned char tr_gcb_itt[256];
extern const unsigned short tr_gcb_sot[208];
extern const unsigned char tr_gcb_sbt[2922];
extern const unsigned char cl_extpict_itt[256];
extern const unsigned short cl_extpict_sot[44];
extern const unsigned char cl_extpict_sbt[510];

/* GCB property values (must match utf8_grapheme.cpp). */
enum {
    GCB_Other              = 0,
    GCB_CR                 = 1,
    GCB_LF                 = 2,
    GCB_Control            = 3,
    GCB_Extend             = 4,
    GCB_ZWJ                = 5,
    GCB_Regional_Indicator = 6,
    GCB_Prepend            = 7,
    GCB_SpacingMark        = 8,
    GCB_L                  = 9,
    GCB_V                  = 10,
    GCB_T                  = 11,
    GCB_LV                 = 12,
    GCB_LVT                = 13
};

/* Run DFA to get integer result from a byte sequence. */
static int run_dfa(const unsigned char *itt, const unsigned short *sot,
                   const unsigned char *sbt, int start, int accept_start,
                   int default_val, const unsigned char *p,
                   const unsigned char *pEnd)
{
    int iState = start;
    while (p < pEnd) {
        int iCol = itt[*p++];
        int iOff = sot[iState];
        for (;;) {
            int y = (signed char)sbt[iOff];
            if (y > 0) {
                if (iCol < y) { iState = sbt[iOff + 1]; break; }
                iCol -= y;
                iOff += 2;
            } else {
                y = -y;
                if (iCol < y) { iState = sbt[iOff + iCol + 1]; break; }
                iCol -= y;
                iOff += y + 1;
            }
        }
    }
    return (iState >= accept_start) ? iState - accept_start : default_val;
}

static int gcb_get(const unsigned char *p, const unsigned char *pEnd)
{
    return run_dfa(tr_gcb_itt, tr_gcb_sot, tr_gcb_sbt,
                   TR_GCB_START_STATE, TR_GCB_ACCEPTING_STATES_START,
                   GCB_Other, p, pEnd);
}

static int gcb_is_extpict(const unsigned char *p, const unsigned char *pEnd)
{
    int r = run_dfa(cl_extpict_itt, cl_extpict_sot, cl_extpict_sbt,
                    CL_EXTPICT_START_STATE, CL_EXTPICT_ACCEPTING_STATES_START,
                    0, p, pEnd);
    return (r == 1);
}

/* Advance one UTF-8 code point (plain, no PUA awareness).
 * Returns pointer past the code point, or p+1 on invalid. */
static const unsigned char *utf8_cp_advance(const unsigned char *p,
                                            const unsigned char *pEnd)
{
    if (p >= pEnd) return p;
    unsigned char ch = *p;
    size_t n;
    if (ch < 0x80) n = 1;
    else if (ch < 0xE0) n = 2;
    else if (ch < 0xF0) n = 3;
    else n = 4;
    return (p + n <= pEnd) ? p + n : pEnd;
}

/*
 * next_grapheme_plain: advance past one Extended Grapheme Cluster
 * in a PLAIN (no PUA) UTF-8 buffer.  Returns bytes consumed.
 * Implements UAX #29 GB rules (same as utf8_next_grapheme).
 */
static size_t next_grapheme_plain(const unsigned char *src, size_t nSrc)
{
    const unsigned char *p = src;
    const unsigned char *pEnd = src + nSrc;

    if (p >= pEnd) return 0;

    /* First code point. */
    const unsigned char *pFirstEnd = utf8_cp_advance(p, pEnd);
    int prevGCB = gcb_get(p, pFirstEnd);
    int bPrevExtPict = gcb_is_extpict(p, pFirstEnd);
    const unsigned char *pCur = pFirstEnd;

    /* GB4: (Control|CR|LF) ÷ */
    if (GCB_Control == prevGCB || GCB_LF == prevGCB)
        return (size_t)(pCur - src);

    int bSeenEPEZ = bPrevExtPict;  /* GB11 */
    int nRI = (GCB_Regional_Indicator == prevGCB) ? 1 : 0;  /* GB12/13 */

    while (pCur < pEnd) {
        const unsigned char *pNextEnd = utf8_cp_advance(pCur, pEnd);
        int curGCB = gcb_get(pCur, pNextEnd);
        int bCurExtPict = gcb_is_extpict(pCur, pNextEnd);

        /* GB3: CR × LF */
        if (GCB_CR == prevGCB && GCB_LF == curGCB)
            return (size_t)(pNextEnd - src);

        /* GB5: ÷ (Control|CR|LF) */
        if (GCB_Control == curGCB || GCB_CR == curGCB || GCB_LF == curGCB)
            break;

        int extend = 0;

        /* GB6: L × (L|V|LV|LVT) */
        if (GCB_L == prevGCB &&
            (GCB_L == curGCB || GCB_V == curGCB ||
             GCB_LV == curGCB || GCB_LVT == curGCB))
            extend = 1;

        /* GB7: (LV|V) × (V|T) */
        if (!extend && (GCB_LV == prevGCB || GCB_V == prevGCB) &&
            (GCB_V == curGCB || GCB_T == curGCB))
            extend = 1;

        /* GB8: (LVT|T) × T */
        if (!extend && (GCB_LVT == prevGCB || GCB_T == prevGCB) &&
            GCB_T == curGCB)
            extend = 1;

        /* GB9: × (Extend|ZWJ) */
        if (!extend && (GCB_Extend == curGCB || GCB_ZWJ == curGCB))
            extend = 1;

        /* GB9a: × SpacingMark */
        if (!extend && GCB_SpacingMark == curGCB)
            extend = 1;

        /* GB9b: Prepend × */
        if (!extend && GCB_Prepend == prevGCB)
            extend = 1;

        /* GB11: ExtPict Extend* ZWJ × ExtPict */
        if (!extend && bSeenEPEZ && GCB_ZWJ == prevGCB && bCurExtPict)
            extend = 1;

        /* GB12/13: RI × RI (pairs only) */
        if (!extend && GCB_Regional_Indicator == curGCB && (nRI % 2) == 1)
            extend = 1;

        if (!extend) break;  /* GB999: ÷ */

        /* Continue cluster. */
        if (GCB_Regional_Indicator == curGCB) nRI++;
        if (bCurExtPict) bSeenEPEZ = 1;
        else if (!bSeenEPEZ || (GCB_Extend != curGCB && GCB_ZWJ != curGCB))
            bSeenEPEZ = 0;

        prevGCB = curGCB;
        pCur = pNextEnd;
    }
    return (size_t)(pCur - src);
}

/* Safe-write helpers: all output buffers are LBUF_SIZE.
 * wp_end = out + LBUF_SIZE - 1 (leave room for NUL).
 * These macros silently truncate when the buffer is full. */

#define WP_SAFE(wp, wp_end, byte) \
    do { if ((wp) < (wp_end)) *(wp)++ = (byte); } while (0)

static inline size_t wp_safe_copy(unsigned char *wp, const unsigned char *wp_end,
                                  const unsigned char *src, size_t n)
{
    size_t avail = (size_t)(wp_end - wp);
    if (n > avail) n = avail;
    memcpy(wp, src, n);
    return n;
}

/* ---------- Ragel machine definitions ---------- */

%%{
    machine color_scan;
    alphtype unsigned char;

    # BMP PUA color: U+F500-F7FF
    # UTF-8: EF (94-9F) (80-BF)
    color_bmp = 0xEF (0x94..0x9F) (0x80..0xBF);

    # SMP PUA color: U+F0000-F05FF (RGB deltas)
    # UTF-8: F3 (B0-B3) (80-BF) (80-BF)
    color_smp = 0xF3 (0xB0..0xB3) (0x80..0xBF) (0x80..0xBF);

    # Any color code point.
    color = color_bmp | color_smp;

    # Visible UTF-8 code points (everything that is NOT a color code point).
    #
    # ASCII:
    vis_1 = 0x01..0x7F;

    # 2-byte UTF-8:
    vis_2 = (0xC2..0xDF) (0x80..0xBF);

    # 3-byte UTF-8 — excluding the EF prefix that overlaps color_bmp.
    # E0: E0 (A0-BF) (80-BF)
    # E1-EC: (E1-EC) (80-BF) (80-BF)
    # ED: ED (80-9F) (80-BF)
    # EE: EE (80-BF) (80-BF)
    # EF with non-color second byte: EF (80-93|A0-BF) (80-BF)
    vis_3_e0 = 0xE0 (0xA0..0xBF) (0x80..0xBF);
    vis_3_e1_ec = (0xE1..0xEC) (0x80..0xBF) (0x80..0xBF);
    vis_3_ed = 0xED (0x80..0x9F) (0x80..0xBF);
    vis_3_ee = 0xEE (0x80..0xBF) (0x80..0xBF);
    vis_3_ef = 0xEF (0x80..0x93 | 0xA0..0xBF) (0x80..0xBF);
    vis_3 = vis_3_e0 | vis_3_e1_ec | vis_3_ed | vis_3_ee | vis_3_ef;

    # 4-byte UTF-8 — excluding the F3 (B0-B3) prefix that overlaps color_smp.
    # F0: F0 (90-BF) (80-BF) (80-BF)
    # F1-F2: (F1-F2) (80-BF) (80-BF) (80-BF)
    # F3 with non-color second byte: F3 (80-AF|B4-BF) (80-BF) (80-BF)
    # F4: F4 (80-8F) (80-BF) (80-BF)
    vis_4_f0 = 0xF0 (0x90..0xBF) (0x80..0xBF) (0x80..0xBF);
    vis_4_f1_f2 = (0xF1..0xF2) (0x80..0xBF) (0x80..0xBF) (0x80..0xBF);
    vis_4_f3_other = 0xF3 (0x80..0xAF | 0xB4..0xBF) (0x80..0xBF) (0x80..0xBF);
    vis_4_f4 = 0xF4 (0x80..0x8F) (0x80..0xBF) (0x80..0xBF);
    vis_4 = vis_4_f0 | vis_4_f1_f2 | vis_4_f3_other | vis_4_f4;

    visible = vis_1 | vis_2 | vis_3 | vis_4;
}%%

/* ---- co_visible_length ---- */

%%{
    machine visible_length;
    include color_scan;

    action count { nVisible++; }

    main := ( color | visible @count )*;

    write data noerror nofinal;
}%%

size_t co_visible_length(const unsigned char *data, size_t len)
{
    int cs;
    const unsigned char *p = data;
    const unsigned char *pe = data + len;
    size_t nVisible = 0;

    %% write init;
    %% write exec;

    return nVisible;
}

/* ---- co_skip_color ---- */

const unsigned char *co_skip_color(const unsigned char *p,
                                   const unsigned char *pe)
{
    while (p < pe) {
        /* BMP PUA color: EF (94-9F) xx */
        if (p[0] == 0xEF && (p + 2) < pe
            && p[1] >= 0x94 && p[1] <= 0x9F) {
            p += 3;
            continue;
        }
        /* SMP PUA color: F3 (B0-B3) xx xx */
        if (p[0] == 0xF3 && (p + 3) < pe
            && p[1] >= 0xB0 && p[1] <= 0xB3) {
            p += 4;
            continue;
        }
        break;
    }
    return p;
}

/* ---- co_visible_advance ---- */

%%{
    machine visible_advance;
    include color_scan;

    action vis_hit {
        nSeen++;
        if (nSeen >= n) {
            /* p points to the last byte of this visible code point.
             * Ragel will advance p by 1 after the action, so after
             * exec, p will be just past it.  We use fbreak to stop. */
            fbreak;
        }
    }

    main := ( color | visible @vis_hit )*;

    write data noerror nofinal;
}%%

const unsigned char *co_visible_advance(const unsigned char *data,
                                        const unsigned char *pe,
                                        size_t n,
                                        size_t *out_count)
{
    int cs;
    const unsigned char *p = data;
    size_t nSeen = 0;

    if (n == 0) {
        if (out_count) *out_count = 0;
        return data;
    }

    %% write init;
    %% write exec;

    if (out_count) *out_count = nSeen;
    return p;
}

/* ---- co_copy_visible ---- */

%%{
    machine copy_visible;
    include color_scan;

    action copy_byte {
        WP_SAFE(wp, wp_end, fc);
    }

    action vis_copied {
        nCopied++;
        if (nCopied >= limit || wp >= wp_end) {
            fbreak;
        }
    }

    main := (
        (color $copy_byte) |
        (visible $copy_byte %vis_copied)
    )*;

    write data noerror nofinal;
}%%

size_t co_copy_visible(unsigned char *out, const unsigned char *data,
                       const unsigned char *pe, size_t limit)
{
    if (limit == 0) return 0;

    int cs;
    const unsigned char *p = data;
    const unsigned char *eof = pe;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;
    size_t nCopied = 0;

    %% write init;
    %% write exec;

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_find_delim ---- */

%%{
    machine find_delim;
    include color_scan;

    action check_delim {
        if (fc == target) {
            found = p;  /* p points at the byte during action */
            fbreak;
        }
    }

    main := ( color | visible @check_delim )*;

    write data noerror nofinal;
}%%

const unsigned char *co_find_delim(const unsigned char *data,
                                   const unsigned char *pe,
                                   unsigned char target)
{
    int cs;
    const unsigned char *p = data;
    const unsigned char *found = NULL;

    %% write init;
    %% write exec;

    return found;
}

/* ================================================================
 * Stage 1: Word and substring operations.
 * These compose on the Stage 0 Ragel primitives.
 * ================================================================ */

/*
 * Helper: copy bytes from [start, end) to out, return bytes written.
 * Clamped to LBUF_SIZE - 1.
 */
static size_t raw_copy(unsigned char *out, const unsigned char *start,
                       const unsigned char *end)
{
    size_t n = (size_t)(end - start);
    if (n > LBUF_SIZE - 1) n = LBUF_SIZE - 1;
    memcpy(out, start, n);
    out[n] = '\0';
    return n;
}

/* ---- co_words_count ---- */

size_t co_words_count(const unsigned char *data, size_t len,
                      unsigned char delim)
{
    const unsigned char *p = data;
    const unsigned char *pe = data + len;
    size_t count = 0;

    if (delim == ' ') {
        /* Space: compress consecutive delimiters. */
        while (p < pe) {
            p = co_skip_color(p, pe);
            if (p >= pe) break;
            if (*p == ' ') { p++; continue; }

            count++;
            const unsigned char *d = co_find_delim(p, pe, ' ');
            if (!d) break;
            p = d + 1;
        }
    } else {
        /* Non-space: each delimiter is significant.
         * word count = 1 + number of delimiters (if any visible content).
         * Empty/color-only string → 0 words. */
        if (co_visible_length(data, len) == 0) return 0;

        count = 1;
        while (p < pe) {
            const unsigned char *d = co_find_delim(p, pe, delim);
            if (!d) break;
            count++;
            p = d + 1;
        }
    }
    return count;
}

/* ---- co_first ---- */

size_t co_first(unsigned char *out, const unsigned char *data, size_t len,
                unsigned char delim)
{
    const unsigned char *pe = data + len;

    if (delim == ' ') {
        /* Space: skip leading delimiters, then take first word. */
        const unsigned char *p = data;
        const unsigned char *content_start = data;
        while (p < pe) {
            const unsigned char *q = co_skip_color(p, pe);
            if (q >= pe) break;
            if (*q == ' ') { p = q + 1; content_start = p; continue; }
            break;
        }
        const unsigned char *d = co_find_delim(content_start, pe, ' ');
        const unsigned char *end = d ? d : pe;
        return raw_copy(out, content_start, end);
    } else {
        /* Non-space: first word is everything before the first delimiter. */
        const unsigned char *d = co_find_delim(data, pe, delim);
        const unsigned char *end = d ? d : pe;
        return raw_copy(out, data, end);
    }
}

/* ---- co_rest ---- */

size_t co_rest(unsigned char *out, const unsigned char *data, size_t len,
               unsigned char delim)
{
    const unsigned char *pe = data + len;

    if (delim == ' ') {
        /* Space: skip leading spaces, find end of first word, skip delimiter. */
        const unsigned char *p = data;
        while (p < pe) {
            const unsigned char *q = co_skip_color(p, pe);
            if (q >= pe) break;
            if (*q == ' ') { p = q + 1; continue; }
            p = q;
            break;
        }
        const unsigned char *d = co_find_delim(p, pe, ' ');
        if (!d) { out[0] = '\0'; return 0; }
        const unsigned char *rest_start = d + 1;
        return raw_copy(out, rest_start, pe);
    } else {
        /* Non-space: find first delimiter, return everything after it. */
        const unsigned char *d = co_find_delim(data, pe, delim);
        if (!d) { out[0] = '\0'; return 0; }
        const unsigned char *rest_start = d + 1;
        return raw_copy(out, rest_start, pe);
    }
}

/* ---- co_last ---- */

size_t co_last(unsigned char *out, const unsigned char *data, size_t len,
               unsigned char delim)
{
    const unsigned char *pe = data + len;

    /* Find the last delimiter by scanning forward. */
    const unsigned char *last_delim = NULL;
    const unsigned char *p = data;

    while (p < pe) {
        const unsigned char *d = co_find_delim(p, pe, delim);
        if (!d) break;
        last_delim = d;
        p = d + 1;
    }

    if (!last_delim) {
        /* No delimiter — whole string is the last word. */
        return raw_copy(out, data, pe);
    }

    /* Copy from after the last delimiter to end.
     * Skip any trailing delimiters at the start of the last word. */
    const unsigned char *start = last_delim + 1;
    return raw_copy(out, start, pe);
}

/* ---- co_extract ---- */

size_t co_extract(unsigned char *out,
                  const unsigned char *data, size_t len,
                  size_t iFirst, size_t nWords,
                  unsigned char delim, unsigned char osep)
{
    const unsigned char *pe = data + len;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    if (iFirst == 0) iFirst = 1;
    if (nWords == 0 || len == 0) {
        out[0] = '\0';
        return 0;
    }

    size_t iWord = 0;
    size_t copied = 0;

    if (delim == ' ') {
        /* Space: compress consecutive delimiters. */
        const unsigned char *p = data;
        while (p < pe && wp < wp_end) {
            const unsigned char *q = co_skip_color(p, pe);
            if (q >= pe) {
                if (copied > 0 && q > p) {
                    size_t cb = (size_t)(q - p);
                    wp += wp_safe_copy(wp, wp_end, p, cb);
                }
                break;
            }
            if (*q == ' ') { p = q + 1; continue; }

            iWord++;
            const unsigned char *word_start = p;
            const unsigned char *d = co_find_delim(q, pe, ' ');
            const unsigned char *word_end = d ? d : pe;

            if (iWord >= iFirst && iWord < iFirst + nWords) {
                if (copied > 0) {
                    WP_SAFE(wp, wp_end, osep);
                }
                size_t cb = (size_t)(word_end - word_start);
                wp += wp_safe_copy(wp, wp_end, word_start, cb);
                copied++;
                if (copied >= nWords) break;
            }

            if (!d) break;
            p = d + 1;
        }
    } else {
        /* Non-space: each delimiter is a word boundary.
         * Word 1 = start to first delimiter.
         * Word 2 = after first delimiter to second.
         * Empty words between consecutive delimiters are valid. */
        const unsigned char *word_start = data;

        for (;;) {
            const unsigned char *d = co_find_delim(word_start, pe, delim);
            const unsigned char *word_end = d ? d : pe;

            iWord++;
            if (iWord >= iFirst && iWord < iFirst + nWords) {
                if (copied > 0) {
                    WP_SAFE(wp, wp_end, osep);
                }
                size_t cb = (size_t)(word_end - word_start);
                wp += wp_safe_copy(wp, wp_end, word_start, cb);
                copied++;
                if (copied >= nWords) break;
            }

            if (!d) break;
            word_start = d + 1;
        }
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_left ---- */

size_t co_left(unsigned char *out,
               const unsigned char *data, size_t len, size_t n)
{
    return co_copy_visible(out, data, data + len, n);
}

/* ---- co_right ---- */

size_t co_right(unsigned char *out,
                const unsigned char *data, size_t len, size_t n)
{
    const unsigned char *pe = data + len;

    /* Count total visible code points. */
    size_t total = co_visible_length(data, len);
    if (n >= total) {
        return raw_copy(out, data, pe);
    }

    /* Advance past (total - n) visible code points. */
    size_t skip = total - n;
    const unsigned char *start = co_visible_advance(data, pe, skip, NULL);

    return raw_copy(out, start, pe);
}

/* ---- co_trim ---- */

size_t co_trim(unsigned char *out,
               const unsigned char *data, size_t len,
               unsigned char trim_char, int trim_flags)
{
    const unsigned char *pe = data + len;
    const unsigned char *start = data;
    const unsigned char *end = pe;

    /* Trim left: skip trim chars but remember the most recent color
     * sequence so we can re-emit it before the first non-trim char. */
    const unsigned char *color_prefix = NULL;
    size_t color_prefix_len = 0;
    if (trim_flags & 1) {
        const unsigned char *p = data;
        while (p < pe) {
            const unsigned char *before_skip = p;
            const unsigned char *q = co_skip_color(p, pe);
            if (q > before_skip) {
                /* Remember this color run (could be multiple PUA points). */
                color_prefix = before_skip;
                color_prefix_len = (size_t)(q - before_skip);
            }
            if (q >= pe) { start = pe; break; }
            if (trim_char == 0) {
                if (*q == ' ' || *q == '\t') { p = q + 1; continue; }
            } else {
                if (*q == trim_char) { p = q + 1; continue; }
            }
            /* First non-trim visible char starts here. */
            start = q;
            break;
        }
    }

    /* Trim right: find the last non-trim visible byte. */
    if (trim_flags & 2) {
        const unsigned char *last_nontrim_end = start;
        const unsigned char *p = start;
        while (p < pe) {
            const unsigned char *q = co_skip_color(p, pe);
            if (q >= pe) break;

            int is_trim;
            if (trim_char == 0) {
                is_trim = (*q == ' ' || *q == '\t');
            } else {
                is_trim = (*q == trim_char);
            }

            const unsigned char *after = co_visible_advance(q, pe, 1, NULL);

            if (!is_trim) {
                last_nontrim_end = after;
            }

            p = after;
        }
        end = last_nontrim_end;
    }

    if (end <= start && !color_prefix) {
        out[0] = '\0';
        return 0;
    }

    /* Build output: optional color prefix + trimmed content. */
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;
    if (color_prefix && end > start) {
        wp += wp_safe_copy(wp, wp_end, color_prefix, color_prefix_len);
    }
    if (end > start) {
        size_t content_len = (size_t)(end - start);
        wp += wp_safe_copy(wp, wp_end, start, content_len);
    }
    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_search ---- */

const unsigned char *co_search(const unsigned char *haystack, size_t hlen,
                               const unsigned char *needle, size_t nlen)
{
    const unsigned char *hpe = haystack + hlen;
    const unsigned char *npe = needle + nlen;

    /* Strip color from needle to get plain comparison bytes. */
    unsigned char nplain[LBUF_SIZE];
    size_t nplain_len = 0;
    {
        const unsigned char *np = needle;
        while (np < npe) {
            np = co_skip_color(np, npe);
            if (np >= npe) break;
            /* Copy one visible code point's bytes. */
            const unsigned char *after = co_visible_advance(np, npe, 1, NULL);
            while (np < after) {
                nplain[nplain_len++] = *np++;
            }
        }
    }
    if (nplain_len == 0) return haystack;

    /* Scan haystack, comparing visible bytes against nplain. */
    const unsigned char *hp = haystack;
    while (hp < hpe) {
        hp = co_skip_color(hp, hpe);
        if (hp >= hpe) break;

        /* Try matching nplain starting at this visible position. */
        const unsigned char *match_start = hp;
        const unsigned char *tp = hp;
        size_t ni = 0;
        int matched = 1;

        while (ni < nplain_len && tp < hpe) {
            tp = co_skip_color(tp, hpe);
            if (tp >= hpe) { matched = 0; break; }

            if (*tp != nplain[ni]) { matched = 0; break; }
            tp++;
            ni++;
        }

        if (matched && ni == nplain_len) {
            return match_start;
        }

        /* Advance past this visible code point. */
        hp = co_visible_advance(hp, hpe, 1, NULL);
    }

    return NULL;
}

/* ---- co_search_with_end ---- */
/*
 * Like co_search but also returns a pointer past the matched delimiter
 * in the haystack.  Needed by co_split_words to know where the next
 * word starts.
 */
static const unsigned char *co_search_with_end(
    const unsigned char *haystack, const unsigned char *hpe,
    const unsigned char *nplain, size_t nplain_len,
    const unsigned char **match_end)
{
    const unsigned char *hp = haystack;
    while (hp < hpe) {
        hp = co_skip_color(hp, hpe);
        if (hp >= hpe) break;

        const unsigned char *match_start = hp;
        const unsigned char *tp = hp;
        size_t ni = 0;
        int matched = 1;

        while (ni < nplain_len && tp < hpe) {
            tp = co_skip_color(tp, hpe);
            if (tp >= hpe) { matched = 0; break; }
            if (*tp != nplain[ni]) { matched = 0; break; }
            tp++;
            ni++;
        }

        if (matched && ni == nplain_len) {
            if (match_end) *match_end = tp;
            return match_start;
        }

        hp = co_visible_advance(hp, hpe, 1, NULL);
    }

    return NULL;
}

/* ---- co_split_words ---- */
/*
 * Split PUA-encoded string into word boundary pairs using a multi-char
 * delimiter.  The delimiter is color-stripped before matching.
 *
 * For space delimiter: consecutive spaces are compressed (standard MUX
 * word semantics).  For non-space: each delimiter occurrence is significant,
 * and empty words are possible.
 *
 * word_starts[i] and word_ends[i] are byte offsets into data.
 * Returns number of words found.
 */
size_t co_split_words(const unsigned char *data, size_t len,
                      const unsigned char *sep, size_t sep_len,
                      size_t *word_starts, size_t *word_ends,
                      size_t max_words)
{
    if (max_words == 0 || len == 0) return 0;

    const unsigned char *pe = data + len;

    /* Strip color from delimiter. */
    unsigned char splain[LBUF_SIZE];
    size_t splain_len = 0;
    {
        const unsigned char *sp = sep;
        const unsigned char *spe = sep + sep_len;
        while (sp < spe) {
            sp = co_skip_color(sp, spe);
            if (sp >= spe) break;
            const unsigned char *after = co_visible_advance(sp, spe, 1, NULL);
            while (sp < after && splain_len < LBUF_SIZE - 1)
                splain[splain_len++] = *sp++;
            sp = after;
        }
    }

    int is_space = (splain_len == 1 && splain[0] == ' ');
    size_t nWords = 0;
    const unsigned char *p = data;

    if (is_space) {
        /* Space-compress mode: skip leading spaces. */
        while (p < pe) {
            const unsigned char *q = co_skip_color(p, pe);
            if (q >= pe) { p = pe; break; }
            if (*q == ' ') { p = q + 1; continue; }
            p = q;
            break;
        }

        while (p < pe && nWords < max_words) {
            /* Record word start. */
            word_starts[nWords] = (size_t)(p - data);

            /* Find next space. */
            const unsigned char *dp = p;
            while (dp < pe) {
                dp = co_skip_color(dp, pe);
                if (dp >= pe) break;
                if (*dp == ' ') break;
                dp = co_visible_advance(dp, pe, 1, NULL);
            }

            /* Word end is just before the space (or end of string). */
            word_ends[nWords] = (size_t)(dp - data);
            nWords++;

            /* Skip past consecutive spaces. */
            while (dp < pe) {
                const unsigned char *q = co_skip_color(dp, pe);
                if (q >= pe) { dp = pe; break; }
                if (*q == ' ') { dp = q + 1; continue; }
                dp = q;
                break;
            }
            p = dp;
        }
    } else {
        /* Non-space delimiter: each occurrence is significant. */
        while (nWords + 1 < max_words) {
            const unsigned char *match_end = NULL;
            const unsigned char *found = co_search_with_end(
                p, pe, splain, splain_len, &match_end);

            if (!found) break;

            word_starts[nWords] = (size_t)(p - data);
            word_ends[nWords] = (size_t)(found - data);
            nWords++;
            p = match_end;
        }

        /* Last word: everything after the last delimiter. */
        word_starts[nWords] = (size_t)(p - data);
        word_ends[nWords] = (size_t)(pe - data);
        nWords++;
    }

    return nWords;
}

/* ---- co_trim_pattern ---- */
/*
 * Multi-char pattern trim: trims repeating occurrences of a multi-byte
 * pattern from left/right of data.  Pattern is matched cyclically at
 * the raw byte level (same as mux_string::trim).
 *
 * trim_flags: 1 = trim left, 2 = trim right, 3 = both.
 * Returns bytes written to out.
 */
size_t co_trim_pattern(unsigned char *out,
                       const unsigned char *data, size_t len,
                       const unsigned char *pattern, size_t plen,
                       int trim_flags)
{
    if (len == 0 || plen == 0) {
        if (len > LBUF_SIZE - 1) len = LBUF_SIZE - 1;
        memcpy(out, data, len);
        out[len] = '\0';
        return len;
    }

    size_t start = 0;
    size_t end = len;

    /* Trim left: match pattern cyclically from the beginning. */
    if (trim_flags & 1) {
        size_t i = 0;
        while (i < len && data[i] == pattern[i % plen])
            i++;
        start = i;
    }

    /* Trim right: match pattern cyclically from the end. */
    if (trim_flags & 2) {
        size_t i = len;
        size_t dist = plen - 1;
        while (i > start && data[i - 1] == pattern[dist]) {
            i--;
            dist = (dist > 0) ? dist - 1 : plen - 1;
        }
        end = i;
    }

    if (end <= start) {
        out[0] = '\0';
        return 0;
    }

    size_t nb = end - start;
    if (nb > LBUF_SIZE - 1) nb = LBUF_SIZE - 1;
    memcpy(out, data + start, nb);
    out[nb] = '\0';
    return nb;
}

/* ---- co_compress_str ---- */
/*
 * Compress runs of a multi-char separator into a single occurrence.
 * Like co_compress but for multi-byte separators.
 *
 * Returns bytes written to out.
 */
size_t co_compress_str(unsigned char *out,
                       const unsigned char *data, size_t len,
                       const unsigned char *sep, size_t sep_len)
{
    const unsigned char *pe = data + len;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    /* Strip color from separator for matching. */
    unsigned char splain[LBUF_SIZE];
    size_t splain_len = 0;
    {
        const unsigned char *sp = sep;
        const unsigned char *spe = sep + sep_len;
        while (sp < spe) {
            sp = co_skip_color(sp, spe);
            if (sp >= spe) break;
            const unsigned char *after = co_visible_advance(sp, spe, 1, NULL);
            while (sp < after && splain_len < LBUF_SIZE - 1)
                splain[splain_len++] = *sp++;
            sp = after;
        }
    }

    if (splain_len == 0) {
        /* Empty separator: just copy. */
        size_t nb = len;
        if (nb > LBUF_SIZE - 1) nb = LBUF_SIZE - 1;
        memcpy(out, data, nb);
        out[nb] = '\0';
        return nb;
    }

    const unsigned char *p = data;
    while (p < pe && wp < wp_end) {
        const unsigned char *match_end = NULL;
        const unsigned char *found = co_search_with_end(
            p, pe, splain, splain_len, &match_end);

        if (!found) {
            /* Copy remainder. */
            wp += wp_safe_copy(wp, wp_end, p, (size_t)(pe - p));
            break;
        }

        /* Copy up to the match. */
        if (found > p)
            wp += wp_safe_copy(wp, wp_end, p, (size_t)(found - p));

        /* Emit one copy of the separator (original bytes with color). */
        if (sep_len <= (size_t)(wp_end - wp))
            wp += wp_safe_copy(wp, wp_end, sep, sep_len);

        /* Skip past consecutive occurrences of the separator. */
        p = match_end;
        while (p < pe) {
            const unsigned char *next_end = NULL;
            const unsigned char *next = co_search_with_end(
                p, pe, splain, splain_len, &next_end);
            if (next != p) break;
            p = next_end;
        }
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ================================================================
 * Stage 2: Transforms, edit, reverse, compress.
 * ================================================================ */

/* ---- co_strip_color ---- */

%%{
    machine strip_color;
    include color_scan;

    action mark { mark = p; }
    action emit_vis {
        const unsigned char *s = mark;
        while (s <= p) WP_SAFE(wp, wp_end, *s++);
    }

    main := ( color | visible >mark @emit_vis )*;

    write data noerror nofinal;
}%%

size_t co_strip_color(unsigned char *out, const unsigned char *data,
                      size_t len)
{
    int cs;
    const unsigned char *p = data;
    const unsigned char *pe = data + len;
    const unsigned char *mark = data;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    %% write init;
    %% write exec;

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_toupper (full Unicode via DFA tables) ---- */

%%{
    machine toupper_machine;
    include color_scan;

    action mark { mark = p; }
    action emit_color {
        const unsigned char *s = mark;
        while (s <= p) WP_SAFE(wp, wp_end, *s++);
    }
    action emit_upper {
        size_t src = (size_t)(p - mark + 1);
        int bXor;
        const co_string_desc *desc = co_dfa_toupper(mark, &bXor);
        if (!desc) {
            const unsigned char *s = mark;
            while (s <= p) WP_SAFE(wp, wp_end, *s++);
        } else {
            size_t out_len = desc->n_bytes;
            if (bXor && out_len != src) {
                const unsigned char *s = mark;
                while (s <= p) WP_SAFE(wp, wp_end, *s++);
            } else if (bXor) {
                for (size_t j = 0; j < out_len; j++)
                    WP_SAFE(wp, wp_end, mark[j] ^ desc->p[j]);
            } else {
                for (size_t j = 0; j < out_len; j++)
                    WP_SAFE(wp, wp_end, desc->p[j]);
            }
        }
    }

    main := ( color >mark @emit_color | visible >mark @emit_upper )*;

    write data noerror nofinal;
}%%

size_t co_toupper(unsigned char *out, const unsigned char *data, size_t len)
{
    int cs;
    const unsigned char *p = data;
    const unsigned char *pe = data + len;
    const unsigned char *mark = data;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    %% write init;
    %% write exec;

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_tolower (full Unicode via DFA tables) ---- */

%%{
    machine tolower_machine;
    include color_scan;

    action mark { mark = p; }
    action emit_color {
        const unsigned char *s = mark;
        while (s <= p) WP_SAFE(wp, wp_end, *s++);
    }
    action emit_lower {
        size_t src = (size_t)(p - mark + 1);
        int bXor;
        const co_string_desc *desc = co_dfa_tolower(mark, &bXor);
        if (!desc) {
            const unsigned char *s = mark;
            while (s <= p) WP_SAFE(wp, wp_end, *s++);
        } else {
            size_t out_len = desc->n_bytes;
            if (bXor && out_len != src) {
                const unsigned char *s = mark;
                while (s <= p) WP_SAFE(wp, wp_end, *s++);
            } else if (bXor) {
                for (size_t j = 0; j < out_len; j++)
                    WP_SAFE(wp, wp_end, mark[j] ^ desc->p[j]);
            } else {
                for (size_t j = 0; j < out_len; j++)
                    WP_SAFE(wp, wp_end, desc->p[j]);
            }
        }
    }

    main := ( color >mark @emit_color | visible >mark @emit_lower )*;

    write data noerror nofinal;
}%%

size_t co_tolower(unsigned char *out, const unsigned char *data, size_t len)
{
    int cs;
    const unsigned char *p = data;
    const unsigned char *pe = data + len;
    const unsigned char *mark = data;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    %% write init;
    %% write exec;

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_totitle (full Unicode via DFA tables) ---- */

size_t co_totitle(unsigned char *out, const unsigned char *data, size_t len)
{
    const unsigned char *pe = data + len;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    /* Skip any leading color. */
    const unsigned char *p = co_skip_color(data, pe);

    /* Copy any leading color. */
    unsigned char *wp = out;
    if (p > data) {
        size_t cb = (size_t)(p - data);
        wp += wp_safe_copy(wp, wp_end, data, cb);
    }

    if (p < pe) {
        /* Determine byte length of first visible code point. */
        size_t src = utf8_FirstByte[*p];
        if (src >= CO_UTF8_CONTINUE) src = CO_UTF8_SIZE1;

        /* Validate continuation bytes. */
        size_t j;
        for (j = 1; j < src && p + j < pe; j++) {
            if (utf8_FirstByte[p[j]] != CO_UTF8_CONTINUE) {
                src = CO_UTF8_SIZE1;
                break;
            }
        }

        /* Title-case via DFA. */
        int bXor;
        const co_string_desc *desc = co_dfa_totitle(p, &bXor);
        if (!desc) {
            for (j = 0; j < src; j++) WP_SAFE(wp, wp_end, p[j]);
        } else {
            size_t out_len = desc->n_bytes;
            if (bXor && out_len != src) {
                for (j = 0; j < src; j++) WP_SAFE(wp, wp_end, p[j]);
            } else if (bXor) {
                for (j = 0; j < out_len; j++)
                    WP_SAFE(wp, wp_end, p[j] ^ desc->p[j]);
            } else {
                for (j = 0; j < out_len; j++)
                    WP_SAFE(wp, wp_end, desc->p[j]);
            }
        }
        p += src;
    }

    /* Copy remainder unchanged. */
    if (p < pe) {
        size_t remaining = (size_t)(pe - p);
        wp += wp_safe_copy(wp, wp_end, p, remaining);
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_reverse ---- */

size_t co_reverse(unsigned char *out, const unsigned char *data, size_t len)
{
    const unsigned char *pe = data + len;

    /* First, collect an array of (start, end) pointers for each
     * "element" — where an element is [optional color] + visible_codepoint.
     * We reverse the order of elements. */

    /* Maximum elements: one per byte (ASCII). */
    typedef struct { const unsigned char *start; const unsigned char *end; } elem_t;
    elem_t elems[LBUF_SIZE];
    size_t nElems = 0;

    /* Leading color (before any visible char) is emitted first, unreversed. */
    const unsigned char *p = data;
    const unsigned char *leading_end = co_skip_color(p, pe);

    p = leading_end;
    while (p < pe) {
        const unsigned char *elem_start = p;

        /* Skip color preceding this visible char. */
        p = co_skip_color(p, pe);
        if (p >= pe) {
            /* Trailing color after last visible char. */
            if (p > elem_start) {
                elems[nElems].start = elem_start;
                elems[nElems].end = p;
                nElems++;
            }
            break;
        }

        /* Advance past the visible code point. */
        const unsigned char *after = co_visible_advance(p, pe, 1, NULL);

        elems[nElems].start = elem_start;
        elems[nElems].end = after;
        nElems++;

        p = after;
    }

    /* Build output: leading color + reversed elements. */
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    /* Emit leading color. */
    if (leading_end > data) {
        size_t cb = (size_t)(leading_end - data);
        wp += wp_safe_copy(wp, wp_end, data, cb);
    }

    /* Emit elements in reverse order. */
    for (size_t i = nElems; i > 0 && wp < wp_end; i--) {
        size_t cb = (size_t)(elems[i-1].end - elems[i-1].start);
        wp += wp_safe_copy(wp, wp_end, elems[i-1].start, cb);
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_edit ---- */

size_t co_edit(unsigned char *out,
               const unsigned char *str, size_t slen,
               const unsigned char *from, size_t flen,
               const unsigned char *to, size_t tlen)
{
    const unsigned char *pe = str + slen;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    /* Empty pattern: return original unchanged. */
    if (flen == 0) {
        size_t cb = slen < LBUF_SIZE - 1 ? slen : LBUF_SIZE - 1;
        memcpy(out, str, cb);
        out[cb] = '\0';
        return cb;
    }

    /* Strip color from pattern for matching. */
    unsigned char fplain[LBUF_SIZE];
    size_t fplain_len = co_strip_color(fplain, from, flen);
    if (fplain_len == 0) {
        size_t cb = slen < LBUF_SIZE - 1 ? slen : LBUF_SIZE - 1;
        memcpy(out, str, cb);
        out[cb] = '\0';
        return cb;
    }

    /* Count visible code points in pattern (for advancing past match). */
    size_t fvis = co_visible_length(from, flen);

    const unsigned char *p = str;

    while (p < pe && wp < wp_end) {
        /* Search for pattern from current position. */
        const unsigned char *match = co_search(p, (size_t)(pe - p), from, flen);

        if (!match) {
            /* No more matches — copy remainder. */
            size_t cb = (size_t)(pe - p);
            wp += wp_safe_copy(wp, wp_end, p, cb);
            break;
        }

        /* Copy everything before the match. */
        if (match > p) {
            size_t cb = (size_t)(match - p);
            wp += wp_safe_copy(wp, wp_end, p, cb);
        }

        /* Emit replacement. */
        if (tlen > 0) {
            wp += wp_safe_copy(wp, wp_end, to, tlen);
        }

        /* Advance past the matched visible code points in the source. */
        p = co_visible_advance(match, pe, fvis, NULL);

        /* Also skip any trailing color that was part of the matched region. */
        p = co_skip_color(p, pe);
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_transform ---- */

size_t co_transform(unsigned char *out,
                    const unsigned char *str, size_t slen,
                    const unsigned char *from_set, size_t flen,
                    const unsigned char *to_set, size_t tlen)
{
    /* Strip color from both sets. */
    unsigned char fplain[LBUF_SIZE], tplain[LBUF_SIZE];
    size_t fp_len = co_strip_color(fplain, from_set, flen);
    size_t tp_len = co_strip_color(tplain, to_set, tlen);

    /* Build ASCII lookup table. */
    unsigned char table[256];
    for (int i = 0; i < 256; i++) table[i] = (unsigned char)i;

    /* Map from_set[i] → to_set[i] for single-byte (ASCII) chars. */
    size_t n = fp_len < tp_len ? fp_len : tp_len;
    for (size_t i = 0; i < n; i++) {
        if (fplain[i] < 0x80 && tplain[i] < 0x80) {
            table[fplain[i]] = tplain[i];
        }
    }

    /* Apply transform: color copied, ASCII visible transformed. */
    const unsigned char *p = str;
    const unsigned char *pe = str + slen;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    while (p < pe && wp < wp_end) {
        /* Copy color bytes unchanged. */
        const unsigned char *q = co_skip_color(p, pe);
        while (p < q) WP_SAFE(wp, wp_end, *p++);
        if (p >= pe) break;

        /* Visible code point: transform if single-byte ASCII. */
        if (*p < 0x80) {
            WP_SAFE(wp, wp_end, table[*p++]);
        } else {
            /* Multi-byte visible: copy unchanged. */
            const unsigned char *after = co_visible_advance(p, pe, 1, NULL);
            while (p < after) WP_SAFE(wp, wp_end, *p++);
        }
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_compress ---- */

size_t co_compress(unsigned char *out,
                   const unsigned char *data, size_t len,
                   unsigned char compress_char)
{
    const unsigned char *pe = data + len;
    const unsigned char *p = data;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;
    int in_run = 0;

    while (p < pe && wp < wp_end) {
        /* Copy color bytes. */
        const unsigned char *q = co_skip_color(p, pe);
        if (!in_run) {
            while (p < q) WP_SAFE(wp, wp_end, *p++);
        } else {
            p = q;  /* Skip color inside compressed run. */
        }
        if (p >= pe) break;

        int is_compress;
        if (compress_char == 0) {
            is_compress = (*p == ' ' || *p == '\t');
        } else {
            is_compress = (*p == compress_char);
        }

        if (is_compress) {
            if (!in_run) {
                /* First char of run: copy it. */
                WP_SAFE(wp, wp_end, *p);
                in_run = 1;
            }
            /* Skip this char (either first copied or subsequent). */
            p++;
        } else {
            in_run = 0;
            /* Copy visible code point. */
            const unsigned char *after = co_visible_advance(p, pe, 1, NULL);
            while (p < after) WP_SAFE(wp, wp_end, *p++);
        }
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ================================================================
 * Stage 3: Position, substring, padding.
 * ================================================================ */

/* ---- co_mid ---- */

size_t co_mid(unsigned char *out,
              const unsigned char *data, size_t len,
              size_t iStart, size_t nCount)
{
    if (nCount == 0) {
        out[0] = '\0';
        return 0;
    }

    const unsigned char *pe = data + len;

    /* Advance to the iStart'th visible code point.
     * We want to include any color that precedes it. */
    const unsigned char *p = data;
    if (iStart > 0) {
        p = co_visible_advance(data, pe, iStart, NULL);
    }

    /* Now copy nCount visible code points with color. */
    return co_copy_visible(out, p, pe, nCount);
}

/* ---- co_pos ---- */

size_t co_pos(const unsigned char *haystack, size_t hlen,
              const unsigned char *needle, size_t nlen)
{
    const unsigned char *found = co_search(haystack, hlen, needle, nlen);
    if (!found) return 0;

    /* Count visible code points before the match to get 1-based index. */
    size_t vis_before = 0;
    const unsigned char *p = haystack;
    while (p < found) {
        p = co_skip_color(p, haystack + hlen);
        if (p >= found) break;
        p = co_visible_advance(p, haystack + hlen, 1, NULL);
        vis_before++;
    }
    return vis_before + 1;  /* 1-based */
}

/* ---- co_lpos ---- */

size_t co_lpos(unsigned char *out,
               const unsigned char *data, size_t hlen,
               unsigned char pattern)
{
    const unsigned char *pe = data + hlen;
    const unsigned char *p = data;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;
    size_t vis_idx = 0;
    int first = 1;

    while (p < pe) {
        p = co_skip_color(p, pe);
        if (p >= pe) break;

        /* Check if this visible byte matches. */
        if (*p < 0x80 && *p == pattern) {
            if (!first) WP_SAFE(wp, wp_end, ' ');
            /* Write the 0-based index as decimal. */
            char num[20];
            int n = snprintf(num, sizeof(num), "%zu", vis_idx);
            wp += wp_safe_copy(wp, wp_end, (const unsigned char *)num, (size_t)n);
            first = 0;
        }

        /* Advance past this visible code point. */
        p = co_visible_advance(p, pe, 1, NULL);
        vis_idx++;
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_member ---- */

size_t co_member(const unsigned char *target, size_t tlen,
                 const unsigned char *list, size_t llen,
                 unsigned char delim)
{
    /* Strip color from target for comparison. */
    unsigned char tplain[LBUF_SIZE];
    size_t tp_len = co_strip_color(tplain, target, tlen);

    const unsigned char *pe = list + llen;
    const unsigned char *p = list;
    size_t word_num = 0;
    unsigned char wplain[LBUF_SIZE];

    while (p < pe) {
        /* Skip leading delimiters. */
        p = co_skip_color(p, pe);
        if (p >= pe) break;
        if (*p == delim) { p++; continue; }

        /* Found start of a word. */
        word_num++;
        const unsigned char *word_start = p;

        /* Find end of word. */
        const unsigned char *d = co_find_delim(p, pe, delim);
        const unsigned char *word_end = d ? d : pe;

        /* Strip color from this word and compare. */
        size_t wlen = co_strip_color(wplain, word_start,
                                      (size_t)(word_end - word_start));
        if (wlen == tp_len && memcmp(wplain, tplain, tp_len) == 0) {
            return word_num;  /* 1-based */
        }

        if (!d) break;
        p = d + 1;
    }

    return 0;  /* not found */
}

/* ---- column-width helpers ---- */

/*
 * co_visual_width — Total display column width, skipping PUA color.
 */
size_t co_visual_width(const unsigned char *p, size_t len)
{
    const unsigned char *pe = p + len;
    size_t cols = 0;
    while (p < pe) {
        /* Skip PUA color codes. */
        if (p[0] == 0xEF && (p + 2) < pe
            && p[1] >= 0x94 && p[1] <= 0x9F) {
            p += 3;
            continue;
        }
        if (p[0] == 0xF3 && (p + 3) < pe
            && p[1] >= 0xB0 && p[1] <= 0xB3) {
            p += 4;
            continue;
        }
        /* Visible code point — get column width. */
        cols += (size_t)co_console_width(p);
        /* Advance past UTF-8 sequence. */
        if (*p < 0x80)      p += 1;
        else if (*p < 0xE0) p += 2;
        else if (*p < 0xF0) p += 3;
        else                p += 4;
    }
    return cols;
}

/*
 * co_copy_columns — Copy up to ncols display columns, preserving color.
 *
 * Stops before emitting a character that would exceed the column limit.
 * Returns bytes written to out.
 */
size_t co_copy_columns(unsigned char *out, const unsigned char *p,
                       const unsigned char *pe, size_t ncols)
{
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;
    size_t cols_emitted = 0;

    while (p < pe && wp < wp_end) {
        /* Copy PUA color codes transparently. */
        if (p[0] == 0xEF && (p + 2) < pe
            && p[1] >= 0x94 && p[1] <= 0x9F) {
            if (wp + 3 <= wp_end) {
                wp[0] = p[0]; wp[1] = p[1]; wp[2] = p[2];
                wp += 3;
            }
            p += 3;
            continue;
        }
        if (p[0] == 0xF3 && (p + 3) < pe
            && p[1] >= 0xB0 && p[1] <= 0xB3) {
            if (wp + 4 <= wp_end) {
                wp[0] = p[0]; wp[1] = p[1]; wp[2] = p[2]; wp[3] = p[3];
                wp += 4;
            }
            p += 4;
            continue;
        }

        /* Visible code point. */
        int w = co_console_width(p);
        if (cols_emitted + (size_t)w > ncols) break;

        size_t cplen;
        if (*p < 0x80)      cplen = 1;
        else if (*p < 0xE0) cplen = 2;
        else if (*p < 0xF0) cplen = 3;
        else                cplen = 4;

        if (wp + cplen > wp_end) break;
        for (size_t i = 0; i < cplen && p + i < pe; i++)
            wp[i] = p[i];
        wp += cplen;
        p += cplen;
        cols_emitted += (size_t)w;
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* Forward declarations for color helpers defined later in file. */
static int parse_bmp_color(const unsigned char *p, co_ColorState *cs);
static int parse_smp_color(const unsigned char *p, co_ColorState *cs);
static size_t emit_transition(unsigned char *wp,
                              const unsigned char *wp_end,
                              const co_ColorState *old_cs,
                              const co_ColorState *new_cs);

/*
 * strip_crnltab — Remove \r, \n, \t from fill pattern in-place.
 * Returns new length.
 */
static size_t strip_crnltab(unsigned char *buf, size_t len)
{
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (buf[i] != '\r' && buf[i] != '\n' && buf[i] != '\t')
            buf[j++] = buf[i];
    }
    buf[j] = '\0';
    return j;
}

/*
 * Parsed fill character: visible bytes + color state at that position.
 */
typedef struct {
    unsigned char bytes[4]; /* UTF-8 code point */
    size_t len;             /* byte length (1-4) */
    int width;              /* display column width */
    co_ColorState color;    /* color state at this position */
} fill_char_t;

/*
 * parse_fill_chars — Pre-process fill pattern into per-character colors.
 *
 * Walks the PUA-encoded fill string, absorbing PUA codes into a running
 * color state and recording the (bytes, color) for each visible char.
 * This mimics mux_string's import: PUA codes are consumed, not passed
 * through, so internal resets (%cn) don't affect the outer context.
 *
 * Returns number of visible characters parsed.
 */
static size_t parse_fill_chars(fill_char_t *chars, size_t max_chars,
                               const unsigned char *fill, size_t fill_len,
                               size_t *out_total_width)
{
    const unsigned char *p = fill;
    const unsigned char *pe = fill + fill_len;
    co_ColorState cs = CO_CS_NORMAL;
    size_t n = 0;
    size_t total_w = 0;

    while (p < pe && n < max_chars) {
        /* Consume PUA codes. */
        if (p[0] == 0xEF && (p + 2) < pe
            && p[1] >= 0x94 && p[1] <= 0x9F) {
            parse_bmp_color(p, &cs);
            p += 3;
            continue;
        }
        if (p[0] == 0xF3 && (p + 3) < pe
            && p[1] >= 0xB0 && p[1] <= 0xB3) {
            parse_smp_color(p, &cs);
            p += 4;
            continue;
        }

        /* Visible character. */
        size_t cplen;
        if (*p < 0x80)      cplen = 1;
        else if (*p < 0xE0) cplen = 2;
        else if (*p < 0xF0) cplen = 3;
        else                cplen = 4;

        if (p + cplen > pe) break;

        chars[n].len = cplen;
        for (size_t i = 0; i < cplen; i++)
            chars[n].bytes[i] = p[i];
        chars[n].color = cs;
        chars[n].width = co_console_width(p);
        total_w += (size_t)chars[n].width;
        n++;
        p += cplen;
    }

    if (out_total_width) *out_total_width = total_w;
    return n;
}

/*
 * emit_fill_from_chars — Emit ncols display columns from pre-parsed
 * fill characters, starting at column offset 'phase'.
 *
 * Emits PUA transitions as needed, tracking color state in *emitted.
 * Returns bytes written.
 */
static size_t emit_fill_from_chars(unsigned char *wp, const unsigned char *wp_end,
                                   size_t ncols, size_t phase,
                                   const fill_char_t *chars, size_t nchars,
                                   size_t fill_width,
                                   co_ColorState *emitted)
{
    if (ncols == 0 || nchars == 0) return 0;

    unsigned char *start = wp;
    size_t col_offset = phase % fill_width;
    size_t cols_done = 0;

    while (cols_done < ncols && wp < wp_end) {
        /* Find starting character index for col_offset. */
        size_t idx = 0;
        size_t fcol = 0;
        while (idx < nchars && fcol + (size_t)chars[idx].width <= col_offset) {
            fcol += (size_t)chars[idx].width;
            idx++;
        }

        /* Emit characters from idx onward. */
        while (idx < nchars && cols_done < ncols && wp < wp_end) {
            if (cols_done + (size_t)chars[idx].width > ncols) break;

            /* Emit color transition. */
            wp += emit_transition(wp, wp_end, emitted, &chars[idx].color);
            *emitted = chars[idx].color;

            /* Emit character bytes. */
            if (wp + chars[idx].len > wp_end) break;
            for (size_t i = 0; i < chars[idx].len; i++)
                wp[i] = chars[idx].bytes[i];
            wp += chars[idx].len;
            cols_done += (size_t)chars[idx].width;
            idx++;
        }

        /* Wrap to start for next cycle. */
        col_offset = 0;
    }

    return (size_t)(wp - start);
}

/*
 * parse_data_chars — Walk data string to extract per-character colors.
 *
 * Same as parse_fill_chars but for the content string.  Returns the
 * final color state in *out_final.
 */
static size_t emit_data_with_tracking(unsigned char *wp, const unsigned char *wp_end,
                                      const unsigned char *data, size_t len,
                                      co_ColorState *emitted)
{
    const unsigned char *p = data;
    const unsigned char *pe = data + len;
    co_ColorState cs = *emitted;
    unsigned char *start = wp;

    while (p < pe && wp < wp_end) {
        /* Consume PUA codes. */
        if (p[0] == 0xEF && (p + 2) < pe
            && p[1] >= 0x94 && p[1] <= 0x9F) {
            parse_bmp_color(p, &cs);
            p += 3;
            continue;
        }
        if (p[0] == 0xF3 && (p + 3) < pe
            && p[1] >= 0xB0 && p[1] <= 0xB3) {
            parse_smp_color(p, &cs);
            p += 4;
            continue;
        }

        /* Visible character: emit transition + bytes. */
        size_t cplen;
        if (*p < 0x80)      cplen = 1;
        else if (*p < 0xE0) cplen = 2;
        else if (*p < 0xF0) cplen = 3;
        else                cplen = 4;

        if (p + cplen > pe) break;

        wp += emit_transition(wp, wp_end, emitted, &cs);
        *emitted = cs;

        if (wp + cplen > wp_end) break;
        for (size_t i = 0; i < cplen; i++)
            wp[i] = p[i];
        wp += cplen;
        p += cplen;
    }

    return (size_t)(wp - start);
}

/* ---- co_center ---- */

size_t co_center(unsigned char *out,
                 const unsigned char *data, size_t len,
                 size_t width,
                 const unsigned char *fill, size_t fill_len,
                 int bTrunc)
{
    const unsigned char *wp_end = out + LBUF_SIZE - 1;
    size_t str_width = co_visual_width(data, len);

    if (str_width >= width) {
        if (bTrunc) {
            return co_copy_columns(out, data, data + len, width);
        }
        size_t nb = wp_safe_copy(out, wp_end, data, len);
        out[nb] = '\0';
        return nb;
    }

    /* Prepare fill pattern. */
    unsigned char fill_buf[LBUF_SIZE];
    size_t flen = 0;
    if (fill && fill_len > 0) {
        if (fill_len > LBUF_SIZE - 1) fill_len = LBUF_SIZE - 1;
        memcpy(fill_buf, fill, fill_len);
        flen = strip_crnltab(fill_buf, fill_len);
    }

    /* Parse fill into per-character colors. */
    fill_char_t fchars[LBUF_SIZE];
    size_t fill_width = 0;
    size_t nfchars = parse_fill_chars(fchars, LBUF_SIZE, fill_buf, flen,
                                      &fill_width);
    if (fill_width == 0) {
        /* Default to space fill. */
        fchars[0].bytes[0] = ' ';
        fchars[0].len = 1;
        fchars[0].width = 1;
        fchars[0].color = CO_CS_NORMAL;
        nfchars = 1;
        fill_width = 1;
    }

    size_t total_pad = width - str_width;
    size_t left_pad = total_pad / 2;
    size_t right_pad = total_pad - left_pad;

    unsigned char *wp = out;
    co_ColorState emitted = CO_CS_NORMAL;

    /* Leading padding: phase 0. */
    wp += emit_fill_from_chars(wp, wp_end, left_pad, 0,
                               fchars, nfchars, fill_width, &emitted);

    /* Content with color tracking. */
    wp += emit_data_with_tracking(wp, wp_end, data, len, &emitted);

    /* Trailing padding: phase = left_pad + str_width. */
    wp += emit_fill_from_chars(wp, wp_end, right_pad,
                               left_pad + str_width,
                               fchars, nfchars, fill_width, &emitted);

    /* Final reset to CS_NORMAL. */
    {
        co_ColorState normal = CO_CS_NORMAL;
        if (!co_cs_equal(&emitted, &normal)) {
            wp += emit_transition(wp, wp_end, &emitted, &normal);
        }
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_ljust ---- */

size_t co_ljust(unsigned char *out,
                const unsigned char *data, size_t len,
                size_t width,
                const unsigned char *fill, size_t fill_len,
                int bTrunc)
{
    const unsigned char *wp_end = out + LBUF_SIZE - 1;
    size_t str_width = co_visual_width(data, len);

    if (str_width >= width) {
        if (bTrunc) {
            return co_copy_columns(out, data, data + len, width);
        }
        size_t nb = wp_safe_copy(out, wp_end, data, len);
        out[nb] = '\0';
        return nb;
    }

    /* Prepare fill. */
    unsigned char fill_buf[LBUF_SIZE];
    size_t flen = 0;
    if (fill && fill_len > 0) {
        if (fill_len > LBUF_SIZE - 1) fill_len = LBUF_SIZE - 1;
        memcpy(fill_buf, fill, fill_len);
        flen = strip_crnltab(fill_buf, fill_len);
    }

    /* Parse fill into per-character colors. */
    fill_char_t fchars[LBUF_SIZE];
    size_t fill_width = 0;
    size_t nfchars = parse_fill_chars(fchars, LBUF_SIZE, fill_buf, flen,
                                      &fill_width);
    if (fill_width == 0) {
        fchars[0].bytes[0] = ' ';
        fchars[0].len = 1;
        fchars[0].width = 1;
        fchars[0].color = CO_CS_NORMAL;
        nfchars = 1;
        fill_width = 1;
    }

    unsigned char *wp = out;
    co_ColorState emitted = CO_CS_NORMAL;

    /* Content with color tracking. */
    wp += emit_data_with_tracking(wp, wp_end, data, len, &emitted);

    /* Right padding: phase = str_width. */
    wp += emit_fill_from_chars(wp, wp_end, width - str_width,
                               str_width, fchars, nfchars, fill_width,
                               &emitted);

    /* Final reset to CS_NORMAL. */
    {
        co_ColorState normal = CO_CS_NORMAL;
        if (!co_cs_equal(&emitted, &normal)) {
            wp += emit_transition(wp, wp_end, &emitted, &normal);
        }
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_rjust ---- */

size_t co_rjust(unsigned char *out,
                const unsigned char *data, size_t len,
                size_t width,
                const unsigned char *fill, size_t fill_len,
                int bTrunc)
{
    const unsigned char *wp_end = out + LBUF_SIZE - 1;
    size_t str_width = co_visual_width(data, len);

    if (str_width >= width) {
        if (bTrunc) {
            return co_copy_columns(out, data, data + len, width);
        }
        size_t nb = wp_safe_copy(out, wp_end, data, len);
        out[nb] = '\0';
        return nb;
    }

    /* Prepare fill. */
    unsigned char fill_buf[LBUF_SIZE];
    size_t flen = 0;
    if (fill && fill_len > 0) {
        if (fill_len > LBUF_SIZE - 1) fill_len = LBUF_SIZE - 1;
        memcpy(fill_buf, fill, fill_len);
        flen = strip_crnltab(fill_buf, fill_len);
    }

    /* Parse fill into per-character colors. */
    fill_char_t fchars[LBUF_SIZE];
    size_t fill_width = 0;
    size_t nfchars = parse_fill_chars(fchars, LBUF_SIZE, fill_buf, flen,
                                      &fill_width);
    if (fill_width == 0) {
        fchars[0].bytes[0] = ' ';
        fchars[0].len = 1;
        fchars[0].width = 1;
        fchars[0].color = CO_CS_NORMAL;
        nfchars = 1;
        fill_width = 1;
    }

    unsigned char *wp = out;
    co_ColorState emitted = CO_CS_NORMAL;

    /* Left padding: phase 0. */
    wp += emit_fill_from_chars(wp, wp_end, width - str_width,
                               0, fchars, nfchars, fill_width, &emitted);

    /* Content with color tracking. */
    wp += emit_data_with_tracking(wp, wp_end, data, len, &emitted);

    /* Final reset to CS_NORMAL. */
    {
        co_ColorState normal = CO_CS_NORMAL;
        if (!co_cs_equal(&emitted, &normal)) {
            wp += emit_transition(wp, wp_end, &emitted, &normal);
        }
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_repeat ---- */

size_t co_repeat(unsigned char *out,
                 const unsigned char *data, size_t len,
                 size_t count)
{
    if (count == 0 || len == 0) {
        out[0] = '\0';
        return 0;
    }

    /* Check for overflow before multiplication. */
    if (count > (LBUF_SIZE - 1) / len) {
        out[0] = '\0';
        return 0;  /* too long */
    }
    size_t total = len * count;

    unsigned char *wp = out;
    for (size_t i = 0; i < count; i++) {
        memcpy(wp, data, len);
        wp += len;
    }

    *wp = '\0';
    return total;
}

/* ================================================================
 * Stage 4: Delete, splice, insert, sort, set operations.
 * ================================================================ */

/* ---- co_delete ---- */

size_t co_delete(unsigned char *out,
                 const unsigned char *data, size_t len,
                 size_t iStart, size_t nCount)
{
    const unsigned char *pe = data + len;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    if (nCount == 0) {
        size_t cb = len < LBUF_SIZE - 1 ? len : LBUF_SIZE - 1;
        memcpy(out, data, cb);
        out[cb] = '\0';
        return cb;
    }

    /* Copy everything before iStart (including color). */
    const unsigned char *p = data;
    if (iStart > 0) {
        size_t nb = co_copy_visible(wp, p, pe, iStart);
        wp += nb;
        p = co_visible_advance(data, pe, iStart, NULL);
    }

    /* Skip nCount visible code points (and their interleaved color). */
    size_t skipped = 0;
    while (skipped < nCount && p < pe) {
        p = co_skip_color(p, pe);
        if (p >= pe) break;
        p = co_visible_advance(p, pe, 1, NULL);
        skipped++;
    }

    /* Also skip trailing color that was part of the deleted region. */
    p = co_skip_color(p, pe);

    /* Copy remainder. */
    if (p < pe) {
        size_t cb = (size_t)(pe - p);
        wp += wp_safe_copy(wp, wp_end, p, cb);
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- word list helpers ---- */

/*
 * Split a delimited string into an array of (start, end) byte ranges.
 * Consecutive delimiters are compressed (empty words skipped).
 * Returns the number of words found.
 */
typedef struct { const unsigned char *start; const unsigned char *end; } word_range_t;

static size_t split_words(const unsigned char *data, size_t len,
                          unsigned char delim,
                          word_range_t *words, size_t max_words)
{
    const unsigned char *pe = data + len;
    const unsigned char *p = data;
    size_t count = 0;

    while (p < pe && count < max_words) {
        /* Skip delimiters and color. */
        const unsigned char *q = co_skip_color(p, pe);
        if (q >= pe) break;
        if (*q == delim) { p = q + 1; continue; }

        /* Start of word (include preceding color). */
        const unsigned char *word_start = p;
        const unsigned char *d = co_find_delim(q, pe, delim);
        const unsigned char *word_end = d ? d : pe;

        words[count].start = word_start;
        words[count].end = word_end;
        count++;

        if (!d) break;
        p = d + 1;
    }
    return count;
}

/* ---- co_splice ---- */

size_t co_splice(unsigned char *out,
                 const unsigned char *list1, size_t len1,
                 const unsigned char *list2, size_t len2,
                 const unsigned char *search, size_t slen,
                 unsigned char delim, unsigned char osep)
{
    word_range_t w1[LBUF_SIZE / 2], w2[LBUF_SIZE / 2];
    size_t n1 = split_words(list1, len1, delim, w1, LBUF_SIZE / 2);
    size_t n2 = split_words(list2, len2, delim, w2, LBUF_SIZE / 2);

    if (n1 != n2) {
        out[0] = '\0';
        return 0;  /* mismatched word counts */
    }

    /* Strip color from search word. */
    unsigned char splain[LBUF_SIZE];
    size_t sp_len = co_strip_color(splain, search, slen);

    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;
    unsigned char wplain[LBUF_SIZE];

    for (size_t i = 0; i < n1 && wp < wp_end; i++) {
        if (i > 0) WP_SAFE(wp, wp_end, osep);

        /* Strip color from word in list1 and compare. */
        size_t wlen = co_strip_color(wplain, w1[i].start,
                                      (size_t)(w1[i].end - w1[i].start));

        const unsigned char *src_start;
        const unsigned char *src_end;

        if (wlen == sp_len && memcmp(wplain, splain, sp_len) == 0) {
            /* Match: use word from list2. */
            src_start = w2[i].start;
            src_end = w2[i].end;
        } else {
            /* No match: use word from list1. */
            src_start = w1[i].start;
            src_end = w1[i].end;
        }

        size_t cb = (size_t)(src_end - src_start);
        wp += wp_safe_copy(wp, wp_end, src_start, cb);
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_insert_word ---- */

size_t co_insert_word(unsigned char *out,
                      const unsigned char *list, size_t llen,
                      const unsigned char *word, size_t wlen,
                      size_t iPos,
                      unsigned char delim, unsigned char osep)
{
    word_range_t words[LBUF_SIZE / 2];
    size_t nWords = split_words(list, llen, delim, words, LBUF_SIZE / 2);

    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    /* Convert 1-based to 0-based insertion point. */
    size_t ins = (iPos > 0) ? iPos - 1 : 0;
    if (ins > nWords) ins = nWords;

    size_t emitted = 0;

    for (size_t i = 0; i <= nWords && wp < wp_end; i++) {
        if (i == ins) {
            /* Insert the new word here. */
            if (emitted > 0) WP_SAFE(wp, wp_end, osep);
            wp += wp_safe_copy(wp, wp_end, word, wlen);
            emitted++;
        }
        if (i < nWords) {
            if (emitted > 0) WP_SAFE(wp, wp_end, osep);
            size_t cb = (size_t)(words[i].end - words[i].start);
            wp += wp_safe_copy(wp, wp_end, words[i].start, cb);
            emitted++;
        }
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- sort infrastructure ---- */

typedef struct {
    const unsigned char *start;
    const unsigned char *end;
    unsigned char plain[256]; /* stripped visible content for comparison */
    size_t plain_len;
} sort_elem_t;

static int cmp_ascii(const void *a, const void *b)
{
    const sort_elem_t *sa = (const sort_elem_t *)a;
    const sort_elem_t *sb = (const sort_elem_t *)b;
    size_t minlen = sa->plain_len < sb->plain_len ? sa->plain_len : sb->plain_len;
    int r = memcmp(sa->plain, sb->plain, minlen);
    if (r != 0) return r;
    return (sa->plain_len > sb->plain_len) - (sa->plain_len < sb->plain_len);
}

static int cmp_ascii_ci(const void *a, const void *b)
{
    const sort_elem_t *sa = (const sort_elem_t *)a;
    const sort_elem_t *sb = (const sort_elem_t *)b;
    size_t minlen = sa->plain_len < sb->plain_len ? sa->plain_len : sb->plain_len;
    for (size_t i = 0; i < minlen; i++) {
        unsigned char ca = sa->plain[i];
        unsigned char cb = sb->plain[i];
        if (ca >= 'A' && ca <= 'Z') ca += 32;
        if (cb >= 'A' && cb <= 'Z') cb += 32;
        if (ca != cb) return (int)ca - (int)cb;
    }
    return (sa->plain_len > sb->plain_len) - (sa->plain_len < sb->plain_len);
}

static long parse_long(const unsigned char *p, size_t len)
{
    char buf[64];
    size_t n = len < 63 ? len : 63;
    memcpy(buf, p, n);
    buf[n] = '\0';
    return atol(buf);
}

static int cmp_numeric(const void *a, const void *b)
{
    const sort_elem_t *sa = (const sort_elem_t *)a;
    const sort_elem_t *sb = (const sort_elem_t *)b;
    long la = parse_long(sa->plain, sa->plain_len);
    long lb = parse_long(sb->plain, sb->plain_len);
    return (la > lb) - (la < lb);
}

static int cmp_dbref(const void *a, const void *b)
{
    const sort_elem_t *sa = (const sort_elem_t *)a;
    const sort_elem_t *sb = (const sort_elem_t *)b;
    /* Skip leading '#' if present. */
    const unsigned char *pa = sa->plain;
    size_t la = sa->plain_len;
    const unsigned char *pb = sb->plain;
    size_t lb = sb->plain_len;
    if (la > 0 && *pa == '#') { pa++; la--; }
    if (lb > 0 && *pb == '#') { pb++; lb--; }
    long da = parse_long(pa, la);
    long db = parse_long(pb, lb);
    return (da > db) - (da < db);
}

typedef int (*cmp_fn)(const void *, const void *);

static cmp_fn get_cmp(char sort_type)
{
    switch (sort_type) {
        case 'i': case 'I': return cmp_ascii_ci;
        case 'n': case 'N': return cmp_numeric;
        case 'd': case 'D': return cmp_dbref;
        default:            return cmp_ascii;
    }
}

/* Build sort_elem_t array from word ranges. */
static size_t build_sort_elems(sort_elem_t *elems,
                               const word_range_t *words, size_t nWords)
{
    for (size_t i = 0; i < nWords; i++) {
        elems[i].start = words[i].start;
        elems[i].end = words[i].end;
        elems[i].plain_len = co_strip_color(
            elems[i].plain, words[i].start,
            (size_t)(words[i].end - words[i].start));
    }
    return nWords;
}

/* Emit sorted elements to output. */
static size_t emit_sorted(unsigned char *out,
                          const sort_elem_t *elems, size_t n,
                          unsigned char osep)
{
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;
    for (size_t i = 0; i < n && wp < wp_end; i++) {
        if (i > 0) WP_SAFE(wp, wp_end, osep);
        size_t cb = (size_t)(elems[i].end - elems[i].start);
        wp += wp_safe_copy(wp, wp_end, elems[i].start, cb);
    }
    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_sort_words ---- */

size_t co_sort_words(unsigned char *out,
                     const unsigned char *list, size_t llen,
                     unsigned char delim, unsigned char osep,
                     char sort_type)
{
    word_range_t words[LBUF_SIZE / 2];
    size_t nWords = split_words(list, llen, delim, words, LBUF_SIZE / 2);

    if (nWords <= 1) {
        if (nWords == 1) {
            size_t cb = (size_t)(words[0].end - words[0].start);
            if (cb > LBUF_SIZE - 1) cb = LBUF_SIZE - 1;
            memcpy(out, words[0].start, cb);
            out[cb] = '\0';
            return cb;
        }
        out[0] = '\0';
        return 0;
    }

    sort_elem_t elems[LBUF_SIZE / 2];
    build_sort_elems(elems, words, nWords);

    qsort(elems, nWords, sizeof(sort_elem_t), get_cmp(sort_type));

    return emit_sorted(out, elems, nWords, osep);
}

/* ---- set operation helpers ---- */

static int elems_equal(const sort_elem_t *a, const sort_elem_t *b,
                       cmp_fn cmp)
{
    return cmp(a, b) == 0;
}

/* ---- co_setunion ---- */

size_t co_setunion(unsigned char *out,
                   const unsigned char *list1, size_t len1,
                   const unsigned char *list2, size_t len2,
                   unsigned char delim, unsigned char osep,
                   char sort_type)
{
    word_range_t w1[LBUF_SIZE / 2], w2[LBUF_SIZE / 2];
    size_t n1 = split_words(list1, len1, delim, w1, LBUF_SIZE / 2);
    size_t n2 = split_words(list2, len2, delim, w2, LBUF_SIZE / 2);

    sort_elem_t e1[LBUF_SIZE / 2], e2[LBUF_SIZE / 2];
    build_sort_elems(e1, w1, n1);
    build_sort_elems(e2, w2, n2);

    cmp_fn cmp = get_cmp(sort_type);
    qsort(e1, n1, sizeof(sort_elem_t), cmp);
    qsort(e2, n2, sizeof(sort_elem_t), cmp);

    /* Merge sorted arrays, skipping duplicates. */
    sort_elem_t merged[LBUF_SIZE];
    size_t nm = 0;
    size_t i = 0, j = 0;

    while (i < n1 && j < n2) {
        int r = cmp(&e1[i], &e2[j]);
        if (r < 0) {
            if (nm == 0 || !elems_equal(&merged[nm-1], &e1[i], cmp))
                merged[nm++] = e1[i];
            i++;
        } else if (r > 0) {
            if (nm == 0 || !elems_equal(&merged[nm-1], &e2[j], cmp))
                merged[nm++] = e2[j];
            j++;
        } else {
            if (nm == 0 || !elems_equal(&merged[nm-1], &e1[i], cmp))
                merged[nm++] = e1[i];
            i++;
            j++;
        }
    }
    while (i < n1) {
        if (nm == 0 || !elems_equal(&merged[nm-1], &e1[i], cmp))
            merged[nm++] = e1[i];
        i++;
    }
    while (j < n2) {
        if (nm == 0 || !elems_equal(&merged[nm-1], &e2[j], cmp))
            merged[nm++] = e2[j];
        j++;
    }

    return emit_sorted(out, merged, nm, osep);
}

/* ---- co_setdiff ---- */

size_t co_setdiff(unsigned char *out,
                  const unsigned char *list1, size_t len1,
                  const unsigned char *list2, size_t len2,
                  unsigned char delim, unsigned char osep,
                  char sort_type)
{
    word_range_t w1[LBUF_SIZE / 2], w2[LBUF_SIZE / 2];
    size_t n1 = split_words(list1, len1, delim, w1, LBUF_SIZE / 2);
    size_t n2 = split_words(list2, len2, delim, w2, LBUF_SIZE / 2);

    sort_elem_t e1[LBUF_SIZE / 2], e2[LBUF_SIZE / 2];
    build_sort_elems(e1, w1, n1);
    build_sort_elems(e2, w2, n2);

    cmp_fn cmp = get_cmp(sort_type);
    qsort(e1, n1, sizeof(sort_elem_t), cmp);
    qsort(e2, n2, sizeof(sort_elem_t), cmp);

    /* Emit elements from e1 that are NOT in e2, skip duplicates. */
    sort_elem_t result[LBUF_SIZE / 2];
    size_t nr = 0;
    size_t i = 0, j = 0;

    while (i < n1) {
        /* Skip duplicates in e1. */
        if (nr > 0 && elems_equal(&result[nr-1], &e1[i], cmp)) {
            i++;
            continue;
        }

        /* Advance e2 past anything smaller than current e1. */
        while (j < n2 && cmp(&e2[j], &e1[i]) < 0) j++;

        if (j < n2 && elems_equal(&e1[i], &e2[j], cmp)) {
            /* In e2 — skip it. */
            i++;
        } else {
            result[nr++] = e1[i];
            i++;
        }
    }

    return emit_sorted(out, result, nr, osep);
}

/* ---- co_setinter ---- */

size_t co_setinter(unsigned char *out,
                   const unsigned char *list1, size_t len1,
                   const unsigned char *list2, size_t len2,
                   unsigned char delim, unsigned char osep,
                   char sort_type)
{
    word_range_t w1[LBUF_SIZE / 2], w2[LBUF_SIZE / 2];
    size_t n1 = split_words(list1, len1, delim, w1, LBUF_SIZE / 2);
    size_t n2 = split_words(list2, len2, delim, w2, LBUF_SIZE / 2);

    sort_elem_t e1[LBUF_SIZE / 2], e2[LBUF_SIZE / 2];
    build_sort_elems(e1, w1, n1);
    build_sort_elems(e2, w2, n2);

    cmp_fn cmp = get_cmp(sort_type);
    qsort(e1, n1, sizeof(sort_elem_t), cmp);
    qsort(e2, n2, sizeof(sort_elem_t), cmp);

    /* Emit elements present in both, skip duplicates. */
    sort_elem_t result[LBUF_SIZE / 2];
    size_t nr = 0;
    size_t i = 0, j = 0;

    while (i < n1 && j < n2) {
        int r = cmp(&e1[i], &e2[j]);
        if (r < 0) {
            i++;
        } else if (r > 0) {
            j++;
        } else {
            if (nr == 0 || !elems_equal(&result[nr-1], &e1[i], cmp))
                result[nr++] = e1[i];
            i++;
            j++;
        }
    }

    return emit_sorted(out, result, nr, osep);
}

/* ================================================================
 * Stage 5: Color collapse.
 * ================================================================ */

/* ---- XTERM 256-color palette ---- */

typedef struct { uint8_t r, g, b; } rgb_t;

static const rgb_t xterm_palette[256] = {
    /* 0-7: Standard colors */
    {0,0,0}, {128,0,0}, {0,128,0}, {128,128,0},
    {0,0,128}, {128,0,128}, {0,128,128}, {192,192,192},
    /* 8-15: Bright colors */
    {128,128,128}, {255,0,0}, {0,255,0}, {255,255,0},
    {0,0,255}, {255,0,255}, {0,255,255}, {255,255,255},
    /* 16-231: 6x6x6 color cube */
#define XC(v) ((v) == 0 ? 0 : 55 + 40 * (v))
#define XCUBE(r,g,b) {XC(r), XC(g), XC(b)}
#define XROW(r,g) XCUBE(r,g,0),XCUBE(r,g,1),XCUBE(r,g,2),XCUBE(r,g,3),XCUBE(r,g,4),XCUBE(r,g,5)
#define XPLANE(r) XROW(r,0),XROW(r,1),XROW(r,2),XROW(r,3),XROW(r,4),XROW(r,5)
    XPLANE(0), XPLANE(1), XPLANE(2), XPLANE(3), XPLANE(4), XPLANE(5),
#undef XPLANE
#undef XROW
#undef XCUBE
#undef XC
    /* 232-255: Grayscale ramp */
#define XG(i) {8+10*(i), 8+10*(i), 8+10*(i)}
    XG(0), XG(1), XG(2), XG(3), XG(4), XG(5),
    XG(6), XG(7), XG(8), XG(9), XG(10), XG(11),
    XG(12), XG(13), XG(14), XG(15), XG(16), XG(17),
    XG(18), XG(19), XG(20), XG(21), XG(22), XG(23)
#undef XG
};

/* ---- PUA byte parsing ---- */

/*
 * Parse a BMP PUA color sequence (3 bytes starting with EF).
 * Updates cs in place.  Returns 1 if parsed, 0 if not color.
 */
static int parse_bmp_color(const unsigned char *p, co_ColorState *cs)
{
    /* EF (94-9F) (80-BF) */
    if (p[1] < 0x94 || p[1] > 0x9F) return 0;

    /* Decode code point U+F500-F7FF from UTF-8. */
    unsigned int cp = ((p[0] & 0x0F) << 12)
                    | ((p[1] & 0x3F) << 6)
                    | (p[2] & 0x3F);

    if (cp == 0xF500) {
        /* RESET */
        *cs = CO_CS_NORMAL;
    } else if (cp == 0xF501) {
        cs->intense = 1;
    } else if (cp == 0xF504) {
        cs->underline = 1;
    } else if (cp == 0xF505) {
        cs->blink = 1;
    } else if (cp == 0xF507) {
        cs->inverse = 1;
    } else if (cp >= 0xF600 && cp <= 0xF6FF) {
        /* FG indexed color. */
        int idx = (int)(cp - 0xF600);
        cs->fg = (int16_t)idx;
        cs->fg_r = xterm_palette[idx].r;
        cs->fg_g = xterm_palette[idx].g;
        cs->fg_b = xterm_palette[idx].b;
    } else if (cp >= 0xF700 && cp <= 0xF7FF) {
        /* BG indexed color. */
        int idx = (int)(cp - 0xF700);
        cs->bg = (int16_t)idx;
        cs->bg_r = xterm_palette[idx].r;
        cs->bg_g = xterm_palette[idx].g;
        cs->bg_b = xterm_palette[idx].b;
    }
    return 1;
}

/*
 * Parse an SMP PUA color sequence (4 bytes starting with F3 (B0-B3)).
 * Updates cs in place.  Returns 1 if parsed, 0 if not color.
 */
static int parse_smp_color(const unsigned char *p, co_ColorState *cs)
{
    if (p[1] < 0xB0 || p[1] > 0xB3) return 0;

    unsigned int block = (unsigned int)(p[1] - 0xB0);
    unsigned int payload = ((unsigned int)(p[2] - 0x80) << 6)
                         | (unsigned int)(p[3] - 0x80);
    unsigned int hi = payload >> 8;
    unsigned int lo = payload & 0xFF;

    switch (block) {
        case 0: /* FG CP1: R high nibble + G */
            if (cs->fg >= 0 && cs->fg <= 255) cs->fg = -2;
            cs->fg_r = (uint8_t)(hi << 4);
            cs->fg_g = (uint8_t)lo;
            break;
        case 1: /* FG CP2: R low nibble + B */
            if (cs->fg >= 0 && cs->fg <= 255) cs->fg = -2;
            cs->fg_r = (uint8_t)((cs->fg_r & 0xF0) | hi);
            cs->fg_b = (uint8_t)lo;
            break;
        case 2: /* BG CP1: R high nibble + G */
            if (cs->bg >= 0 && cs->bg <= 255) cs->bg = -2;
            cs->bg_r = (uint8_t)(hi << 4);
            cs->bg_g = (uint8_t)lo;
            break;
        case 3: /* BG CP2: R low nibble + B */
            if (cs->bg >= 0 && cs->bg <= 255) cs->bg = -2;
            cs->bg_r = (uint8_t)((cs->bg_r & 0xF0) | hi);
            cs->bg_b = (uint8_t)lo;
            break;
    }
    return 1;
}

/* ---- Find nearest XTERM palette entry for an RGB color ---- */

static int find_nearest_palette(uint8_t r, uint8_t g, uint8_t b)
{
    int best = 0;
    int best_dist = 256 * 256 * 3;
    for (int i = 0; i < 256; i++) {
        int dr = (int)r - (int)xterm_palette[i].r;
        int dg = (int)g - (int)xterm_palette[i].g;
        int db = (int)b - (int)xterm_palette[i].b;
        int dist = dr*dr + dg*dg + db*db;
        if (dist < best_dist) {
            best_dist = dist;
            best = i;
            if (dist == 0) break;
        }
    }
    return best;
}

/* ---- Emit PUA bytes for a code point ---- */

static size_t emit_pua_bmp(unsigned char *wp, const unsigned char *wp_end,
                           unsigned int cp)
{
    /* U+F500-F7FF → 3-byte UTF-8 */
    if (wp + 3 > wp_end) return 0;
    wp[0] = 0xEF;
    wp[1] = (unsigned char)(0x80 | ((cp >> 6) & 0x3F));
    wp[2] = (unsigned char)(0x80 | (cp & 0x3F));
    return 3;
}

static size_t emit_pua_smp(unsigned char *wp, const unsigned char *wp_end,
                           unsigned int block, unsigned int payload)
{
    if (wp + 4 > wp_end) return 0;
    wp[0] = 0xF3;
    wp[1] = (unsigned char)(0xB0 + block);
    wp[2] = (unsigned char)(0x80 | ((payload >> 6) & 0x3F));
    wp[3] = (unsigned char)(0x80 | (payload & 0x3F));
    return 4;
}

/*
 * Emit minimal PUA transition from old_cs to new_cs.
 * Returns bytes written to wp.
 */
static size_t emit_transition(unsigned char *wp,
                              const unsigned char *wp_end,
                              const co_ColorState *old_cs,
                              const co_ColorState *new_cs)
{
    if (co_cs_equal(old_cs, new_cs)) return 0;

    unsigned char *start = wp;
    co_ColorState cur = *old_cs;

    /* Step 1: Do we need RESET?
     * RESET is required when:
     *   - Any attribute is being turned OFF
     *   - FG is going to default from non-default
     *   - BG is going to default from non-default
     */
    int need_reset = 0;
    if ((cur.intense && !new_cs->intense) ||
        (cur.underline && !new_cs->underline) ||
        (cur.blink && !new_cs->blink) ||
        (cur.inverse && !new_cs->inverse)) {
        need_reset = 1;
    }
    if (cur.fg != -1 && new_cs->fg == -1) need_reset = 1;
    if (cur.bg != -1 && new_cs->bg == -1) need_reset = 1;

    if (need_reset) {
        wp += emit_pua_bmp(wp, wp_end, 0xF500);
        cur = CO_CS_NORMAL;
    }

    /* Step 2: Emit changed attributes. */
    if (new_cs->intense && !cur.intense)
        wp += emit_pua_bmp(wp, wp_end, 0xF501);
    if (new_cs->underline && !cur.underline)
        wp += emit_pua_bmp(wp, wp_end, 0xF504);
    if (new_cs->blink && !cur.blink)
        wp += emit_pua_bmp(wp, wp_end, 0xF505);
    if (new_cs->inverse && !cur.inverse)
        wp += emit_pua_bmp(wp, wp_end, 0xF507);

    /* Step 3: Emit FG color if changed. */
    int fg_changed = (cur.fg != new_cs->fg ||
                      cur.fg_r != new_cs->fg_r ||
                      cur.fg_g != new_cs->fg_g ||
                      cur.fg_b != new_cs->fg_b);
    if (fg_changed && new_cs->fg != -1) {
        if (new_cs->fg >= 0 && new_cs->fg <= 255) {
            /* Indexed FG: emit palette code. */
            wp += emit_pua_bmp(wp, wp_end, 0xF600 + (unsigned int)new_cs->fg);
        } else if (new_cs->fg == -2) {
            /* RGB FG: nearest palette base + 2-code-point encoding. */
            int idx = find_nearest_palette(new_cs->fg_r, new_cs->fg_g,
                                           new_cs->fg_b);
            wp += emit_pua_bmp(wp, wp_end, 0xF600 + (unsigned int)idx);
            if (  new_cs->fg_r != xterm_palette[idx].r
               || new_cs->fg_g != xterm_palette[idx].g
               || new_cs->fg_b != xterm_palette[idx].b) {
                wp += emit_pua_smp(wp, wp_end, 0,
                    ((unsigned int)(new_cs->fg_r >> 4) << 8) | new_cs->fg_g);
                wp += emit_pua_smp(wp, wp_end, 1,
                    ((unsigned int)(new_cs->fg_r & 0xF) << 8) | new_cs->fg_b);
            }
        }
    }

    /* Step 4: Emit BG color if changed. */
    int bg_changed = (cur.bg != new_cs->bg ||
                      cur.bg_r != new_cs->bg_r ||
                      cur.bg_g != new_cs->bg_g ||
                      cur.bg_b != new_cs->bg_b);
    if (bg_changed && new_cs->bg != -1) {
        if (new_cs->bg >= 0 && new_cs->bg <= 255) {
            wp += emit_pua_bmp(wp, wp_end, 0xF700 + (unsigned int)new_cs->bg);
        } else if (new_cs->bg == -2) {
            int idx = find_nearest_palette(new_cs->bg_r, new_cs->bg_g,
                                           new_cs->bg_b);
            wp += emit_pua_bmp(wp, wp_end, 0xF700 + (unsigned int)idx);
            if (  new_cs->bg_r != xterm_palette[idx].r
               || new_cs->bg_g != xterm_palette[idx].g
               || new_cs->bg_b != xterm_palette[idx].b) {
                wp += emit_pua_smp(wp, wp_end, 2,
                    ((unsigned int)(new_cs->bg_r >> 4) << 8) | new_cs->bg_g);
                wp += emit_pua_smp(wp, wp_end, 3,
                    ((unsigned int)(new_cs->bg_r & 0xF) << 8) | new_cs->bg_b);
            }
        }
    }

    return (size_t)(wp - start);
}

/* ---- co_collapse_color ---- */

size_t co_collapse_color(unsigned char *out,
                         const unsigned char *data, size_t len)
{
    const unsigned char *pe = data + len;
    const unsigned char *p = data;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    co_ColorState emitted = CO_CS_NORMAL;
    co_ColorState pending = CO_CS_NORMAL;

    while (p < pe && wp < wp_end) {
        /* BMP PUA color: EF (94-9F) xx */
        if (p[0] == 0xEF && (p + 2) < pe
            && p[1] >= 0x94 && p[1] <= 0x9F) {
            parse_bmp_color(p, &pending);
            p += 3;
            continue;
        }

        /* SMP PUA color: F3 (B0-B3) xx xx */
        if (p[0] == 0xF3 && (p + 3) < pe
            && p[1] >= 0xB0 && p[1] <= 0xB3) {
            parse_smp_color(p, &pending);
            p += 4;
            continue;
        }

        /* Visible code point: emit transition + copy visible bytes. */
        wp += emit_transition(wp, wp_end, &emitted, &pending);
        emitted = pending;

        /* Copy visible code point bytes. */
        const unsigned char *after = co_visible_advance(p, pe, 1, NULL);
        while (p < after) WP_SAFE(wp, wp_end, *p++);
    }

    /* Emit trailing color if pending differs from emitted.
     * Typically a trailing RESET — preserve it so concatenation works. */
    if (!co_cs_equal(&pending, &emitted)) {
        wp += emit_transition(wp, wp_end, &emitted, &pending);
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_apply_color ---- */

size_t co_apply_color(unsigned char *out,
                      const unsigned char *data, size_t len,
                      co_ColorState cs)
{
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;
    co_ColorState normal = CO_CS_NORMAL;

    /* Emit transition from NORMAL to desired state. */
    wp += emit_transition(wp, wp_end, &normal, &cs);

    /* Copy the string. */
    if (len > 0) {
        wp += wp_safe_copy(wp, wp_end, data, len);
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_merge ---- */

/*
 * Consume PUA color codes at *pp, updating color state cs.
 * Advances *pp past any PUA codes.  Returns pointer to first visible byte.
 */
static const unsigned char *consume_pua(const unsigned char **pp,
                                        const unsigned char *pe,
                                        co_ColorState *cs)
{
    const unsigned char *p = *pp;
    for (;;) {
        if (p >= pe) break;
        if (p[0] == 0xEF && (p + 2) < pe
            && p[1] >= 0x94 && p[1] <= 0x9F) {
            parse_bmp_color(p, cs);
            p += 3;
            continue;
        }
        if (p[0] == 0xF3 && (p + 3) < pe
            && p[1] >= 0xB0 && p[1] <= 0xB3) {
            parse_smp_color(p, cs);
            p += 4;
            continue;
        }
        break;
    }
    *pp = p;
    return p;
}

size_t co_merge(unsigned char *out,
                const unsigned char *strA, size_t lenA,
                const unsigned char *strB, size_t lenB,
                const unsigned char *search, size_t slen)
{
    /* Strip color from search to get the match character. */
    unsigned char splain[LBUF_SIZE];
    size_t sp_len = co_strip_color(splain, search, slen);
    if (sp_len == 0) { splain[0] = ' '; sp_len = 1; }

    /* Verify visible lengths are equal. */
    size_t vlenA = co_visible_length(strA, lenA);
    size_t vlenB = co_visible_length(strB, lenB);
    if (vlenA != vlenB) {
        out[0] = '\0';
        return 0;
    }

    const unsigned char *pa = strA, *pae = strA + lenA;
    const unsigned char *pb = strB, *pbe = strB + lenB;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    co_ColorState stateA = CO_CS_NORMAL;
    co_ColorState stateB = CO_CS_NORMAL;
    co_ColorState emitted = CO_CS_NORMAL;

    while (pa < pae && pb < pbe && wp < wp_end) {
        /* Consume PUA from both, updating color states. */
        consume_pua(&pa, pae, &stateA);
        consume_pua(&pb, pbe, &stateB);
        if (pa >= pae || pb >= pbe) break;

        /* Get visible code point from A. */
        size_t cplenA;
        unsigned char chA = *pa;
        if (chA < 0x80)       cplenA = 1;
        else if (chA < 0xE0)  cplenA = 2;
        else if (chA < 0xF0)  cplenA = 3;
        else                  cplenA = 4;

        /* Get visible code point from B. */
        size_t cplenB;
        unsigned char chB = *pb;
        if (chB < 0x80)       cplenB = 1;
        else if (chB < 0xE0)  cplenB = 2;
        else if (chB < 0xF0)  cplenB = 3;
        else                  cplenB = 4;

        /* Check if A's code point matches search. */
        int match = (cplenA == sp_len && memcmp(pa, splain, cplenA) == 0);

        if (match) {
            /* Use B's color state and B's code point. */
            wp += emit_transition(wp, wp_end, &emitted, &stateB);
            emitted = stateB;
            for (size_t i = 0; i < cplenB && pb + i < pbe; i++)
                WP_SAFE(wp, wp_end, pb[i]);
        } else {
            /* Use A's color state and A's code point. */
            wp += emit_transition(wp, wp_end, &emitted, &stateA);
            emitted = stateA;
            for (size_t i = 0; i < cplenA && pa + i < pae; i++)
                WP_SAFE(wp, wp_end, pa[i]);
        }

        pa += cplenA;
        pb += cplenB;
    }

    /* Emit trailing reset to CS_NORMAL, matching export_TextColor. */
    {
        co_ColorState normal = CO_CS_NORMAL;
        if (!co_cs_equal(&emitted, &normal)) {
            wp += emit_transition(wp, wp_end, &emitted, &normal);
        }
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_escape ---- */

/*
 * Escape table: characters that need a preceding backslash.
 * Set: $%(),;[\]^{}
 */
static const unsigned char co_isescape[256] = {
/*  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 0 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 1 */
    0, 0, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 0, 0, 0,  /* 2: $%(),  */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0,  /* 3: ;      */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 4 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0,  /* 5: [\]^   */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 6 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 1, 0, 0,  /* 7: {}     */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 8 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* 9 */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* A */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* B */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* C */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* D */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,  /* E */
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0   /* F */
};

size_t co_escape(unsigned char *out,
                 const unsigned char *data, size_t len)
{
    const unsigned char *p = data;
    const unsigned char *pe = data + len;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;
    int bFirst = 1;

    while (p < pe && wp < wp_end) {
        /* Copy color PUA codes through to output. */
        const unsigned char *q = co_skip_color(p, pe);
        while (p < q && wp < wp_end) {
            *wp++ = *p++;
        }
        if (p >= pe) break;

        /* Determine visible code point length. */
        unsigned char ch = *p;
        size_t cplen;
        if (ch < 0x80)       cplen = 1;
        else if (ch < 0xE0)  cplen = 2;
        else if (ch < 0xF0)  cplen = 3;
        else                 cplen = 4;
        if (p + cplen > pe) break;

        /* Insert backslash before first visible char or escape chars. */
        if (bFirst || (ch < 0x80 && co_isescape[ch])) {
            WP_SAFE(wp, wp_end, '\\');
            bFirst = 0;
        }

        /* Copy the visible code point. */
        for (size_t i = 0; i < cplen; i++) {
            WP_SAFE(wp, wp_end, *p++);
        }
    }

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ================================================================
 * Grapheme cluster operations on PUA-encoded strings.
 *
 * Strategy: strip color to get plain text, run UAX #29 grapheme
 * segmentation on the plain buffer, then map the plain-text byte
 * count back to PUA-encoded positions by walking the original
 * string and skipping color codes.
 * ================================================================ */

/*
 * advance_pua_by_plain_bytes: given a PUA-encoded pointer and a count
 * of plain (visible) bytes consumed, advance the PUA pointer past
 * that many visible bytes, skipping any interleaved PUA color codes.
 * Returns pointer past the consumed visible bytes.
 */
static const unsigned char *advance_pua_by_plain_bytes(
    const unsigned char *p, const unsigned char *pe, size_t nPlainBytes)
{
    size_t consumed = 0;
    while (p < pe && consumed < nPlainBytes) {
        /* Skip color PUA. */
        const unsigned char *q = co_skip_color(p, pe);
        p = q;
        if (p >= pe) break;

        /* Visible code point. */
        unsigned char ch = *p;
        size_t cplen;
        if (ch < 0x80)       cplen = 1;
        else if (ch < 0xE0)  cplen = 2;
        else if (ch < 0xF0)  cplen = 3;
        else                 cplen = 4;
        if (p + cplen > pe) break;

        p += cplen;
        consumed += cplen;
    }
    return p;
}

size_t co_cluster_count(const unsigned char *data, size_t len)
{
    /* Strip color to get plain text. */
    unsigned char plain[LBUF_SIZE];
    size_t plen = co_strip_color(plain, data, len);

    /* Count grapheme clusters in plain text. */
    size_t nClusters = 0;
    size_t nConsumed = 0;
    while (nConsumed < plen) {
        size_t cb = next_grapheme_plain(plain + nConsumed, plen - nConsumed);
        if (0 == cb) break;
        nConsumed += cb;
        nClusters++;
    }
    return nClusters;
}

const unsigned char *co_cluster_advance(const unsigned char *data,
                                        const unsigned char *pe,
                                        size_t n, size_t *out_count)
{
    size_t len = (size_t)(pe - data);

    /* Strip color to get plain text. */
    unsigned char plain[LBUF_SIZE];
    size_t plen = co_strip_color(plain, data, len);

    /* Advance past n grapheme clusters in plain text. */
    size_t nConsumed = 0;
    size_t nClusters = 0;
    while (nConsumed < plen && nClusters < n) {
        size_t cb = next_grapheme_plain(plain + nConsumed, plen - nConsumed);
        if (0 == cb) break;
        nConsumed += cb;
        nClusters++;
    }

    if (out_count) *out_count = nClusters;

    /* Map nConsumed plain bytes back to PUA-encoded position. */
    return advance_pua_by_plain_bytes(data, pe, nConsumed);
}

size_t co_mid_cluster(unsigned char *out,
                      const unsigned char *data, size_t len,
                      size_t iStart, size_t nCount)
{
    const unsigned char *pe = data + len;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    /* Advance past iStart clusters to find the start position. */
    const unsigned char *pStart = co_cluster_advance(data, pe, iStart, NULL);

    /* Advance past nCount more clusters to find the end position. */
    const unsigned char *pEnd = co_cluster_advance(pStart, pe, nCount, NULL);

    /* Copy the range [pStart, pEnd) including any interleaved color. */
    size_t cb = (size_t)(pEnd - pStart);
    wp += wp_safe_copy(wp, wp_end, pStart, cb);

    *wp = '\0';
    return (size_t)(wp - out);
}

size_t co_delete_cluster(unsigned char *out,
                         const unsigned char *data, size_t len,
                         size_t iStart, size_t nCount)
{
    const unsigned char *pe = data + len;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    /* Copy everything before iStart clusters. */
    const unsigned char *pDelStart = co_cluster_advance(data, pe, iStart, NULL);
    size_t cb_before = (size_t)(pDelStart - data);
    wp += wp_safe_copy(wp, wp_end, data, cb_before);

    /* Skip past nCount clusters (the deleted range). */
    const unsigned char *pDelEnd = co_cluster_advance(pDelStart, pe, nCount, NULL);

    /* Copy everything after the deleted range. */
    size_t cb_after = (size_t)(pe - pDelEnd);
    wp += wp_safe_copy(wp, wp_end, pDelEnd, cb_after);

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ================================================================
 * Stage 6: Rendering — PUA to client output.
 * ================================================================ */

/* ASCII approximation DFA tables from utf8tables.
 * Declared here directly to avoid pulling in the C++ config.h chain.
 */
#define TR_ASCII_START_STATE (0)
#define TR_ASCII_ACCEPTING_STATES_START (99)
extern const unsigned char tr_ascii_itt[256];
extern const unsigned short tr_ascii_sot[99];
extern const unsigned char tr_ascii_sbt[3431];

/*
 * co_dfa_ascii — Run the ASCII approximation DFA on a single UTF-8 code point.
 *
 * The accepting state value IS the output byte (ASCII approximation).
 * Returns '?' if no approximation exists.
 */
unsigned char co_dfa_ascii(const unsigned char *p)
{
    int iState = TR_ASCII_START_STATE;
    do
    {
        unsigned char ch = *p++;
        unsigned char iColumn = tr_ascii_itt[ch];
        unsigned short iOffset = tr_ascii_sot[iState];
        for (;;)
        {
            int y = tr_ascii_sbt[iOffset];
            if (y < 128)
            {
                if (iColumn < y)
                {
                    iState = tr_ascii_sbt[iOffset+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset += 2;
                }
            }
            else
            {
                y = 256 - y;
                if (iColumn < y)
                {
                    iState = tr_ascii_sbt[iOffset+iColumn+1];
                    break;
                }
                else
                {
                    iColumn = (unsigned char)(iColumn - y);
                    iOffset = (unsigned short)(iOffset + y + 1);
                }
            }
        }
    } while (iState < TR_ASCII_ACCEPTING_STATES_START);

    unsigned char result = (unsigned char)(iState - TR_ASCII_ACCEPTING_STATES_START);
    return (result > 0 && result < 0x80) ? result : '?';
}

/* ---- co_render_ascii ---- */

%%{
    machine render_ascii;
    include color_scan;

    action mark { mark = p; }
    action emit_ascii {
        /* Run visible code point through tr_ascii DFA for approximation. */
        if (*mark < 0x80) {
            /* Pure ASCII — pass through. */
            WP_SAFE(wp, wp_end, *mark);
        } else {
            /* Multi-byte UTF-8 — approximate to ASCII. */
            unsigned char ch = co_dfa_ascii(mark);
            WP_SAFE(wp, wp_end, ch);
        }
    }

    main := ( color | visible >mark @emit_ascii )*;

    write data noerror nofinal;
}%%

size_t co_render_ascii(unsigned char *out,
                       const unsigned char *data, size_t len)
{
    int cs;
    const unsigned char *p = data;
    const unsigned char *pe = data + len;
    const unsigned char *mark = data;
    unsigned char *wp = out;
    const unsigned char *wp_end = out + LBUF_SIZE - 1;

    %% write init;
    %% write exec;

    *wp = '\0';
    return (size_t)(wp - out);
}
