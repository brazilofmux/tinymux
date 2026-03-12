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
 *   SMP PUA U+F0000-F05FF (RGB deltas):
 *     F3 B0 80 80..BF     (U+F0000-F003F)   red FG low
 *     F3 B0 81 80..BF     (U+F0040-F007F)   red FG mid
 *     F3 B0 82 80..BF     (U+F0080-F00BF)   red FG high
 *     F3 B0 83 80..BF     (U+F00C0-F00FF)   red FG top
 *     ...through...
 *     F3 B0 97 80..BF     (U+F05C0-F05FF)   blue BG top
 *
 *   So SMP color = F3 B0 (80..97) (80..BF)
 */

#include "color_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------- Ragel machine definitions ---------- */

%%{
    machine color_scan;
    alphtype unsigned char;

    # BMP PUA color: U+F500-F7FF
    # UTF-8: EF (94-9F) (80-BF)
    color_bmp = 0xEF (0x94..0x9F) (0x80..0xBF);

    # SMP PUA color: U+F0000-F05FF (RGB deltas)
    # UTF-8: F3 B0 (80-97) (80-BF)
    color_smp = 0xF3 0xB0 (0x80..0x97) (0x80..0xBF);

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

    # 4-byte UTF-8 — excluding the F3 B0 prefix that overlaps color_smp.
    # F0: F0 (90-BF) (80-BF) (80-BF)
    # F1-F2: (F1-F2) (80-BF) (80-BF) (80-BF)
    # F3 with non-color second byte: F3 (80-AF|B1-BF) (80-BF) (80-BF)
    # F3 B0 with non-color third byte: F3 B0 (98-BF) (80-BF)
    # F4: F4 (80-8F) (80-BF) (80-BF)
    vis_4_f0 = 0xF0 (0x90..0xBF) (0x80..0xBF) (0x80..0xBF);
    vis_4_f1_f2 = (0xF1..0xF2) (0x80..0xBF) (0x80..0xBF) (0x80..0xBF);
    vis_4_f3_other = 0xF3 (0x80..0xAF | 0xB1..0xBF) (0x80..0xBF) (0x80..0xBF);
    vis_4_f3_b0 = 0xF3 0xB0 (0x98..0xBF) (0x80..0xBF);
    vis_4_f4 = 0xF4 (0x80..0x8F) (0x80..0xBF) (0x80..0xBF);
    vis_4 = vis_4_f0 | vis_4_f1_f2 | vis_4_f3_other | vis_4_f3_b0 | vis_4_f4;

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
        /* SMP PUA color: F3 B0 (80-97) xx */
        if (p[0] == 0xF3 && (p + 3) < pe
            && p[1] == 0xB0
            && p[2] >= 0x80 && p[2] <= 0x97) {
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
        *wp++ = fc;
    }

    action vis_copied {
        nCopied++;
        if (nCopied >= limit) {
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
 */
static size_t raw_copy(unsigned char *out, const unsigned char *start,
                       const unsigned char *end)
{
    size_t n = (size_t)(end - start);
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

    while (p < pe) {
        /* Skip leading delimiters (with color pass-through). */
        p = co_skip_color(p, pe);
        if (p >= pe) break;
        if (*p == delim) { p++; continue; }

        /* We are at the start of a word. */
        count++;

        /* Advance to next delimiter or end. */
        const unsigned char *d = co_find_delim(p, pe, delim);
        if (!d) break;
        p = d + 1;
    }
    return count;
}

/* ---- co_first ---- */

size_t co_first(unsigned char *out, const unsigned char *data, size_t len,
                unsigned char delim)
{
    const unsigned char *pe = data + len;
    const unsigned char *p = data;

    /* Skip leading delimiters (but remember where content starts
     * including any color that precedes the first visible char). */
    const unsigned char *content_start = data;
    while (p < pe) {
        const unsigned char *q = co_skip_color(p, pe);
        if (q >= pe) break;
        if (*q == delim) { p = q + 1; content_start = p; continue; }
        break;
    }

    /* Find end of first word. */
    const unsigned char *d = co_find_delim(content_start, pe, delim);
    const unsigned char *end = d ? d : pe;

    return raw_copy(out, content_start, end);
}

/* ---- co_rest ---- */

size_t co_rest(unsigned char *out, const unsigned char *data, size_t len,
               unsigned char delim)
{
    const unsigned char *pe = data + len;

    /* Skip leading delimiters + color. */
    const unsigned char *p = data;
    while (p < pe) {
        const unsigned char *q = co_skip_color(p, pe);
        if (q >= pe) break;
        if (*q == delim) { p = q + 1; continue; }
        p = q;
        break;
    }

    /* Find end of first word. */
    const unsigned char *d = co_find_delim(p, pe, delim);
    if (!d) {
        out[0] = '\0';
        return 0;
    }

    /* Skip the delimiter itself, then copy rest. */
    const unsigned char *rest_start = d + 1;
    return raw_copy(out, rest_start, pe);
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

    if (iFirst == 0) iFirst = 1;
    if (nWords == 0) {
        out[0] = '\0';
        return 0;
    }

    size_t iWord = 0;
    size_t copied = 0;
    const unsigned char *p = data;

    while (p < pe) {
        /* Skip leading delimiters. */
        const unsigned char *q = co_skip_color(p, pe);
        if (q >= pe) {
            /* Only color left — copy it if we are mid-extraction. */
            if (copied > 0 && q > p) {
                size_t cb = (size_t)(q - p);
                memcpy(wp, p, cb);
                wp += cb;
            }
            break;
        }
        if (*q == delim) { p = q + 1; continue; }

        /* Start of a word. */
        iWord++;
        const unsigned char *word_start = p;  /* include preceding color */
        const unsigned char *d = co_find_delim(q, pe, delim);
        const unsigned char *word_end = d ? d : pe;

        if (iWord >= iFirst && iWord < iFirst + nWords) {
            if (copied > 0) {
                *wp++ = osep;
            }
            size_t cb = (size_t)(word_end - word_start);
            memcpy(wp, word_start, cb);
            wp += cb;
            copied++;
            if (copied >= nWords) break;
        }

        if (!d) break;
        p = d + 1;
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
    if (color_prefix && end > start) {
        memcpy(wp, color_prefix, color_prefix_len);
        wp += color_prefix_len;
    }
    if (end > start) {
        size_t content_len = (size_t)(end - start);
        memcpy(wp, start, content_len);
        wp += content_len;
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
        while (s <= p) *wp++ = *s++;
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

    %% write init;
    %% write exec;

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_toupper ---- */

%%{
    machine toupper_machine;
    include color_scan;

    action mark { mark = p; }
    action emit_color {
        const unsigned char *s = mark;
        while (s <= p) *wp++ = *s++;
    }
    action emit_upper {
        if (mark == p && *p >= 'a' && *p <= 'z') {
            *wp++ = *p - 32;
        } else {
            const unsigned char *s = mark;
            while (s <= p) {
                unsigned char c = *s;
                if (c >= 'a' && c <= 'z') c -= 32;
                *wp++ = c;
                s++;
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

    %% write init;
    %% write exec;

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_tolower ---- */

%%{
    machine tolower_machine;
    include color_scan;

    action mark { mark = p; }
    action emit_color {
        const unsigned char *s = mark;
        while (s <= p) *wp++ = *s++;
    }
    action emit_lower {
        if (mark == p && *p >= 'A' && *p <= 'Z') {
            *wp++ = *p + 32;
        } else {
            const unsigned char *s = mark;
            while (s <= p) {
                unsigned char c = *s;
                if (c >= 'A' && c <= 'Z') c += 32;
                *wp++ = c;
                s++;
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

    %% write init;
    %% write exec;

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_totitle ---- */

size_t co_totitle(unsigned char *out, const unsigned char *data, size_t len)
{
    const unsigned char *pe = data + len;

    /* Skip any leading color. */
    const unsigned char *p = co_skip_color(data, pe);

    /* Copy any leading color. */
    unsigned char *wp = out;
    if (p > data) {
        memcpy(wp, data, (size_t)(p - data));
        wp += (size_t)(p - data);
    }

    if (p < pe) {
        /* Uppercase first visible byte (ASCII only). */
        unsigned char c = *p;
        if (c >= 'a' && c <= 'z') c -= 32;
        *wp++ = c;
        p++;
    }

    /* Copy remainder unchanged. */
    if (p < pe) {
        size_t remaining = (size_t)(pe - p);
        memcpy(wp, p, remaining);
        wp += remaining;
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

    /* Emit leading color. */
    if (leading_end > data) {
        size_t cb = (size_t)(leading_end - data);
        memcpy(wp, data, cb);
        wp += cb;
    }

    /* Emit elements in reverse order. */
    for (size_t i = nElems; i > 0; i--) {
        size_t cb = (size_t)(elems[i-1].end - elems[i-1].start);
        memcpy(wp, elems[i-1].start, cb);
        wp += cb;
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

    /* Empty pattern: return original unchanged. */
    if (flen == 0) {
        memcpy(out, str, slen);
        out[slen] = '\0';
        return slen;
    }

    /* Strip color from pattern for matching. */
    unsigned char fplain[LBUF_SIZE];
    size_t fplain_len = co_strip_color(fplain, from, flen);
    if (fplain_len == 0) {
        memcpy(out, str, slen);
        out[slen] = '\0';
        return slen;
    }

    /* Count visible code points in pattern (for advancing past match). */
    size_t fvis = co_visible_length(from, flen);

    const unsigned char *p = str;

    while (p < pe) {
        /* Search for pattern from current position. */
        const unsigned char *match = co_search(p, (size_t)(pe - p), from, flen);

        if (!match) {
            /* No more matches — copy remainder. */
            size_t cb = (size_t)(pe - p);
            memcpy(wp, p, cb);
            wp += cb;
            break;
        }

        /* Copy everything before the match. */
        if (match > p) {
            size_t cb = (size_t)(match - p);
            memcpy(wp, p, cb);
            wp += cb;
        }

        /* Emit replacement. */
        if (tlen > 0) {
            memcpy(wp, to, tlen);
            wp += tlen;
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

    while (p < pe) {
        /* Copy color bytes unchanged. */
        const unsigned char *q = co_skip_color(p, pe);
        while (p < q) { *wp++ = *p++; }
        if (p >= pe) break;

        /* Visible code point: transform if single-byte ASCII. */
        if (*p < 0x80) {
            *wp++ = table[*p++];
        } else {
            /* Multi-byte visible: copy unchanged. */
            const unsigned char *after = co_visible_advance(p, pe, 1, NULL);
            while (p < after) { *wp++ = *p++; }
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
    int in_run = 0;

    while (p < pe) {
        /* Copy color bytes. */
        const unsigned char *q = co_skip_color(p, pe);
        if (!in_run) {
            while (p < q) { *wp++ = *p++; }
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
                *wp++ = *p;
                in_run = 1;
            }
            /* Skip this char (either first copied or subsequent). */
            p++;
        } else {
            in_run = 0;
            /* Copy visible code point. */
            const unsigned char *after = co_visible_advance(p, pe, 1, NULL);
            while (p < after) { *wp++ = *p++; }
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
    size_t vis_idx = 0;
    int first = 1;

    while (p < pe) {
        p = co_skip_color(p, pe);
        if (p >= pe) break;

        /* Check if this visible byte matches. */
        if (*p < 0x80 && *p == pattern) {
            if (!first) *wp++ = ' ';
            /* Write the 0-based index as decimal. */
            char num[20];
            int n = snprintf(num, sizeof(num), "%zu", vis_idx);
            memcpy(wp, num, (size_t)n);
            wp += n;
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

/* ---- padding helper ---- */

/*
 * Emit nPad visible code points from a fill pattern (cycled).
 * fill/fill_len is the fill string.  If fill_len is 0, uses spaces.
 * Returns bytes written to wp.
 */
static size_t emit_fill(unsigned char *wp,
                        size_t nPad,
                        const unsigned char *fill, size_t fill_len)
{
    if (nPad == 0) return 0;

    unsigned char *start = wp;

    if (fill_len == 0 || (fill_len == 1 && fill[0] == ' ')) {
        /* Fast path: space fill. */
        memset(wp, ' ', nPad);
        return nPad;
    }

    /* Count visible code points in fill pattern. */
    size_t fill_vis = co_visible_length(fill, fill_len);
    if (fill_vis == 0) {
        memset(wp, ' ', nPad);
        return nPad;
    }

    /* Cycle through the fill pattern. */
    size_t emitted = 0;
    while (emitted < nPad) {
        size_t remaining = nPad - emitted;
        if (remaining >= fill_vis) {
            /* Copy entire fill pattern. */
            memcpy(wp, fill, fill_len);
            wp += fill_len;
            emitted += fill_vis;
        } else {
            /* Partial fill — copy only 'remaining' visible code points. */
            size_t nb = co_copy_visible(wp, fill, fill + fill_len, remaining);
            wp += nb;
            emitted += remaining;
        }
    }

    return (size_t)(wp - start);
}

/* ---- co_center ---- */

size_t co_center(unsigned char *out,
                 const unsigned char *data, size_t len,
                 size_t width,
                 const unsigned char *fill, size_t fill_len)
{
    size_t str_vis = co_visible_length(data, len);

    if (str_vis >= width) {
        /* Truncate to width. */
        return co_copy_visible(out, data, data + len, width);
    }

    size_t total_pad = width - str_vis;
    size_t left_pad = total_pad / 2;
    size_t right_pad = total_pad - left_pad;

    unsigned char *wp = out;

    /* Left padding. */
    wp += emit_fill(wp, left_pad, fill, fill_len);

    /* Content. */
    memcpy(wp, data, len);
    wp += len;

    /* Right padding. */
    wp += emit_fill(wp, right_pad, fill, fill_len);

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_ljust ---- */

size_t co_ljust(unsigned char *out,
                const unsigned char *data, size_t len,
                size_t width,
                const unsigned char *fill, size_t fill_len)
{
    size_t str_vis = co_visible_length(data, len);

    if (str_vis >= width) {
        return co_copy_visible(out, data, data + len, width);
    }

    unsigned char *wp = out;

    /* Content. */
    memcpy(wp, data, len);
    wp += len;

    /* Right padding. */
    wp += emit_fill(wp, width - str_vis, fill, fill_len);

    *wp = '\0';
    return (size_t)(wp - out);
}

/* ---- co_rjust ---- */

size_t co_rjust(unsigned char *out,
                const unsigned char *data, size_t len,
                size_t width,
                const unsigned char *fill, size_t fill_len)
{
    size_t str_vis = co_visible_length(data, len);

    if (str_vis >= width) {
        return co_copy_visible(out, data, data + len, width);
    }

    unsigned char *wp = out;

    /* Left padding. */
    wp += emit_fill(wp, width - str_vis, fill, fill_len);

    /* Content. */
    memcpy(wp, data, len);
    wp += len;

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

    size_t total = len * count;
    if (total > LBUF_SIZE - 1) {
        out[0] = '\0';
        return 0;  /* too long */
    }

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

    if (nCount == 0) {
        memcpy(out, data, len);
        out[len] = '\0';
        return len;
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
        memcpy(wp, p, cb);
        wp += cb;
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
    unsigned char wplain[LBUF_SIZE];

    for (size_t i = 0; i < n1; i++) {
        if (i > 0) *wp++ = osep;

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
        memcpy(wp, src_start, cb);
        wp += cb;
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

    /* Convert 1-based to 0-based insertion point. */
    size_t ins = (iPos > 0) ? iPos - 1 : 0;
    if (ins > nWords) ins = nWords;

    size_t emitted = 0;

    for (size_t i = 0; i <= nWords; i++) {
        if (i == ins) {
            /* Insert the new word here. */
            if (emitted > 0) *wp++ = osep;
            memcpy(wp, word, wlen);
            wp += wlen;
            emitted++;
        }
        if (i < nWords) {
            if (emitted > 0) *wp++ = osep;
            size_t cb = (size_t)(words[i].end - words[i].start);
            memcpy(wp, words[i].start, cb);
            wp += cb;
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
    for (size_t i = 0; i < n; i++) {
        if (i > 0) *wp++ = osep;
        size_t cb = (size_t)(elems[i].end - elems[i].start);
        memcpy(wp, elems[i].start, cb);
        wp += cb;
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
