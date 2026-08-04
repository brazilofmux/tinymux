/* cases.h -- the shared case battery for the three-way color_ops differential.
 *
 * Included by both the RV64 guest program (guest_main.c) and the host
 * reference (host_main.c) so that all four routes -- host, qemu, our
 * interpreter, our DBT -- run byte-identical inputs and emit byte-identical
 * output formats.  Any difference in the transcripts is a real divergence.
 *
 * Deliberately free of libc: the guest side is -nostdlib -ffreestanding, so
 * everything here is spelled with the primitives softlib.c provides.
 */

#ifndef CO_DIFF_CASES_H
#define CO_DIFF_CASES_H

/* ---- output primitives (provided by whichever main includes us) ---- */

static void o_str(const char *s);
static void o_uint(unsigned long long v);
static void o_bytes(const unsigned char *p, unsigned long n);

static void o_case(const char *name, unsigned long long ret,
                   const unsigned char *out, unsigned long outlen)
{
    o_str(name);
    o_str(" ret=");
    o_uint(ret);
    o_str(" len=");
    o_uint(outlen);
    o_str(" [");
    o_bytes(out, outlen);
    o_str("]\n");
}

/* ---- the battery ---- */

#define OUTCAP 32768

static unsigned char g_out[OUTCAP];

/* A tiny deterministic LCG so the fuzz leg is identical on every route. */
static unsigned long long g_rng = 0x2002BEEFULL;
static unsigned int rnd(unsigned int n)
{
    g_rng = g_rng * 6364136223846793005ULL + 1442695040888963407ULL;
    return (unsigned int)((g_rng >> 33) % (n ? n : 1));
}

static unsigned long slen_of(const char *s)
{
    unsigned long n = 0;
    while (s[n]) n++;
    return n;
}

/*
 * Fixed cases.  The first block is Macbook's reported failure on #2002
 * (insert(||,-2,Pear,|) came out as a runaway of ~130 separators in the
 * blob while the host produced ||Pear|), then the neighbours of it: empty
 * lists, negative and duplicate positions, and the max_words cap where
 * split_words()'s final word absorbs the remainder.
 */
static void run_fixed(void)
{
    const unsigned char *L2   = (const unsigned char *)"||";
    const unsigned char *PEAR = (const unsigned char *)"Pear";
    unsigned long r;
    int pos[8];

    /* --- co_insert_at --- */
    pos[0] = -2;
    r = co_insert_at(g_out, L2, 2, pos, 1, PEAR, 4, '|', '|');
    o_case("insert_at/||/-2", r, g_out, r);

    pos[0] = -1;
    r = co_insert_at(g_out, L2, 2, pos, 1, PEAR, 4, '|', '|');
    o_case("insert_at/||/-1", r, g_out, r);

    pos[0] = 1;
    r = co_insert_at(g_out, L2, 2, pos, 1, PEAR, 4, '|', '|');
    o_case("insert_at/||/1", r, g_out, r);

    pos[0] = 1;
    r = co_insert_at(g_out, (const unsigned char *)"", 0, pos, 1,
                     PEAR, 4, '|', '|');
    o_case("insert_at/empty/1", r, g_out, r);

    pos[0] = 5;
    r = co_insert_at(g_out, (const unsigned char *)"", 0, pos, 1,
                     PEAR, 4, '|', '|');
    o_case("insert_at/empty/5", r, g_out, r);

    pos[0] = 2; pos[1] = 2;
    r = co_insert_at(g_out, (const unsigned char *)"a|b|c", 5, pos, 2,
                     PEAR, 4, '|', '|');
    o_case("insert_at/abc/2,2", r, g_out, r);

    pos[0] = -1; pos[1] = 1;
    r = co_insert_at(g_out, (const unsigned char *)"a b c", 5, pos, 2,
                     PEAR, 4, ' ', ' ');
    o_case("insert_at/spc/-1,1", r, g_out, r);

    /* --- co_replace_at --- */
    pos[0] = -2;
    r = co_replace_at(g_out, L2, 2, pos, 1, PEAR, 4, '|', '|');
    o_case("replace_at/||/-2", r, g_out, r);

    pos[0] = 2; pos[1] = 2;
    r = co_replace_at(g_out, (const unsigned char *)"a|b|c", 5, pos, 2,
                      PEAR, 4, '|', '|');
    o_case("replace_at/abc/2,2", r, g_out, r);

    pos[0] = 9;
    r = co_replace_at(g_out, (const unsigned char *)"a|b|c", 5, pos, 1,
                      PEAR, 4, '|', '|');
    o_case("replace_at/abc/9", r, g_out, r);

    /* --- co_delete_at --- */
    pos[0] = -2;
    r = co_delete_at(g_out, L2, 2, pos, 1, '|', '|');
    o_case("delete_at/||/-2", r, g_out, r);

    pos[0] = 1; pos[1] = 3;
    r = co_delete_at(g_out, (const unsigned char *)"a|b|c", 5, pos, 2,
                     '|', '|');
    o_case("delete_at/abc/1,3", r, g_out, r);

    /* --- co_insert_word --- */
    r = co_insert_word(g_out, L2, 2, PEAR, 4, 2, '|', '|');
    o_case("insert_word/||/2", r, g_out, r);

    r = co_insert_word(g_out, (const unsigned char *)"a|b|c", 5,
                       PEAR, 4, 0, '|', '|');
    o_case("insert_word/abc/0", r, g_out, r);

    /* --- co_splice --- */
    r = co_splice(g_out, (const unsigned char *)"a|b|c", 5,
                  (const unsigned char *)"x|y|z", 5,
                  (const unsigned char *)"b", 1, '|', '|');
    o_case("splice/abc/xyz", r, g_out, r);

    r = co_splice(g_out, L2, 2, L2, 2,
                  (const unsigned char *)"", 0, '|', '|');
    o_case("splice/||/||", r, g_out, r);
}

/*
 * Cap-boundary cases.  split_words() caps at LBUF_SIZE/2 = 16384 words and
 * the FINAL word then swallows the whole remainder -- the semantics a
 * separately-written counting loop gets wrong.  Small random inputs never
 * reach it, so it is spelled out.
 */
static unsigned char g_big[OUTCAP];

static void run_cap(void)
{
    unsigned long i, n;
    unsigned long r;
    int pos[4];

    /* 20000 empty words: "||||..." -- past the 16384 cap. */
    n = 20000;
    if (n > OUTCAP - 1) n = OUTCAP - 1;
    for (i = 0; i < n; i++) g_big[i] = '|';

    pos[0] = 1;
    r = co_insert_at(g_out, g_big, n, pos, 1,
                     (const unsigned char *)"Q", 1, '|', '|');
    o_case("cap/insert_at/1", r, g_out, r);

    pos[0] = -1;
    r = co_insert_at(g_out, g_big, n, pos, 1,
                     (const unsigned char *)"Q", 1, '|', '|');
    o_case("cap/insert_at/-1", r, g_out, r);

    pos[0] = -1;
    r = co_delete_at(g_out, g_big, n, pos, 1, '|', '|');
    o_case("cap/delete_at/-1", r, g_out, r);

    pos[0] = -1;
    r = co_replace_at(g_out, g_big, n, pos, 1,
                      (const unsigned char *)"Q", 1, '|', '|');
    o_case("cap/replace_at/-1", r, g_out, r);

    /* Dense single-char words: "a|a|a|..." also past the cap. */
    for (i = 0; i + 1 < n; i += 2) { g_big[i] = 'a'; g_big[i + 1] = '|'; }
    pos[0] = -2;
    r = co_insert_at(g_out, g_big, n, pos, 1,
                     (const unsigned char *)"Q", 1, '|', '|');
    o_case("cap/insert_at/dense", r, g_out, r);
}

/*
 * Randomised leg.  Same LCG on every route, so the inputs are identical
 * and any transcript difference is the code, not the generator.
 */
static void run_fuzz(unsigned int iters)
{
    static const char *delims = "| ,x";
    unsigned int k;

    for (k = 0; k < iters; k++) {
        unsigned char delim = (unsigned char)delims[rnd(4)];
        unsigned char osep  = (unsigned char)delims[rnd(4)];
        unsigned int  llen  = rnd(48);
        unsigned int  i;
        int pos[4];
        int npos = (int)(1 + rnd(3));
        unsigned long r;

        for (i = 0; i < llen; i++) {
            unsigned int c = rnd(6);
            g_big[i] = (unsigned char)(c < 3 ? 'a' + c
                                             : (c == 3 ? delim
                                             : (c == 4 ? ' ' : '|')));
        }
        for (i = 0; i < (unsigned int)npos; i++) {
            pos[i] = (int)rnd(20) - 10;
        }

        o_str("f");
        o_uint(k);
        o_str(" d=");
        o_uint(delim);
        o_str(" s=");
        o_uint(osep);
        o_str(" in=[");
        o_bytes(g_big, llen);
        o_str("]\n");

        {
            int p[4];
            for (i = 0; i < 4; i++) p[i] = pos[i];
            r = co_insert_at(g_out, g_big, llen, p, npos,
                             (const unsigned char *)"Q", 1, delim, osep);
            o_case("  ins", r, g_out, r);
        }
        {
            int p[4];
            for (i = 0; i < 4; i++) p[i] = pos[i];
            r = co_replace_at(g_out, g_big, llen, p, npos,
                              (const unsigned char *)"Q", 1, delim, osep);
            o_case("  rep", r, g_out, r);
        }
        {
            int p[4];
            for (i = 0; i < 4; i++) p[i] = pos[i];
            r = co_delete_at(g_out, g_big, llen, p, npos, delim, osep);
            o_case("  del", r, g_out, r);
        }
        {
            r = co_insert_word(g_out, g_big, llen,
                               (const unsigned char *)"Q", 1,
                               rnd(8), delim, osep);
            o_case("  isw", r, g_out, r);
        }
        {
            r = co_splice(g_out, g_big, llen, g_big, llen,
                          (const unsigned char *)"a", 1, delim, osep);
            o_case("  spl", r, g_out, r);
        }
    }
}

/*
 * Justification cases: co_center / co_ljust / co_rjust.
 *
 * These three hold the largest surviving frames in color_ops.c (~43 KB each,
 * #2002), and the bulk of that is fill_buf[LBUF_SIZE] -- a copy of the fill
 * pattern that exists only so strip_crnltab() can mutate it in place before
 * parse_fill_chars() reads it.  Any attempt to remove that copy changes when
 * CR/NL/TAB are removed relative to when PUA colour codes are consumed, so
 * the battery leans on exactly that seam:
 *
 *   - CR/NL/TAB alone, and interleaved with visible characters
 *   - CR/NL/TAB immediately before, inside and after a PUA colour sequence,
 *     which is where stripping-before and skipping-during could disagree
 *   - fills longer than CO_FILL_CHARS_MAX (256) visible chars, where the
 *     pattern wraps -- a shorter fill buffer would silently change the wrap
 *   - multi-byte UTF-8 fills, where a byte-length bound is not a char bound
 *   - the empty fill (defaults to space) and the width<=str_width paths
 *
 * PUA colour codes are spelled as raw bytes rather than via a helper: the
 * guest has no libc, and the encoding is what is under test.
 */

/* BMP colour code: EF 94-9F xx.  SMP colour code: F3 B0-B3 xx xx. */
#define PUA_BMP "\xEF\x94\x81"
#define PUA_SMP "\xF3\xB0\x80\x81"

static unsigned char g_fill[OUTCAP];

static void justify_triple(const char *tag,
                           const unsigned char *data, unsigned long dlen,
                           unsigned long width,
                           const unsigned char *fill, unsigned long flen,
                           int bTrunc)
{
    unsigned long r;
    o_str(tag);
    o_str("/ctr ");
    r = co_center(g_out, data, dlen, width, fill, flen, bTrunc);
    o_case("", r, g_out, r);
    o_str(tag);
    o_str("/ljs ");
    r = co_ljust(g_out, data, dlen, width, fill, flen, bTrunc);
    o_case("", r, g_out, r);
    o_str(tag);
    o_str("/rjs ");
    r = co_rjust(g_out, data, dlen, width, fill, flen, bTrunc);
    o_case("", r, g_out, r);
}

static void run_justify(void)
{
    unsigned long i, n;
    const unsigned char *AB = (const unsigned char *)"ab";

    /* Plain fill, and the empty fill that defaults to space. */
    justify_triple("jx/plain", AB, 2, 10, (const unsigned char *)"-", 1, 0);
    justify_triple("jx/empty", AB, 2, 10, (const unsigned char *)"", 0, 0);
    justify_triple("jx/multi", AB, 2, 11, (const unsigned char *)".oO", 3, 0);

    /* Width at and below the string width, both truncation modes. */
    justify_triple("jx/exact", AB, 2, 2, (const unsigned char *)"-", 1, 0);
    justify_triple("jx/narrow0", (const unsigned char *)"abcdef", 6, 3,
                   (const unsigned char *)"-", 1, 0);
    justify_triple("jx/narrow1", (const unsigned char *)"abcdef", 6, 3,
                   (const unsigned char *)"-", 1, 1);

    /* CR/NL/TAB in the fill -- the strip_crnltab path. */
    justify_triple("jx/crlf", AB, 2, 12,
                   (const unsigned char *)"\r\n\t", 3, 0);
    justify_triple("jx/crlfmix", AB, 2, 12,
                   (const unsigned char *)"-\r=\n+\t", 6, 0);

    /* CR/NL/TAB against PUA boundaries: before, between and after a colour
     * sequence.  If a rewrite skipped these during the parse rather than
     * stripping them first, this is where the two would part company. */
    justify_triple("jx/pua_pre", AB, 2, 12,
                   (const unsigned char *)"\r" PUA_BMP "x", 5, 0);
    justify_triple("jx/pua_post", AB, 2, 12,
                   (const unsigned char *)PUA_BMP "\rx", 5, 0);
    /* 3 (BMP) + 3 ("x\ry") + 4 (SMP) + 1 ("z") = 11. */
    justify_triple("jx/pua_mid", AB, 2, 12,
                   (const unsigned char *)PUA_BMP "x\ry" PUA_SMP "z", 11, 0);
    /* A NUL inside the fill, deliberately rather than by miscounting: it is
     * not strippable and not a PUA lead, so it must survive as a visible
     * byte wherever the fill is parsed. */
    justify_triple("jx/pua_nul", AB, 2, 12,
                   (const unsigned char *)PUA_BMP "x\0y", 6, 0);
    justify_triple("jx/pua_smp", AB, 2, 12,
                   (const unsigned char *)PUA_SMP "\t" PUA_BMP "q", 9, 0);

    /* Coloured data as well as coloured fill, so the emitted transitions and
     * the final reset to NORMAL are exercised, not just the padding. */
    justify_triple("jx/pua_data", (const unsigned char *)PUA_BMP "ab", 5, 12,
                   (const unsigned char *)PUA_SMP "-", 5, 0);

    /* Multi-byte UTF-8 fill: a byte bound is not a character bound. */
    justify_triple("jx/utf8", AB, 2, 12,
                   (const unsigned char *)"\xC3\xA9\xE2\x98\x85", 5, 0);

    /* Fill longer than CO_FILL_CHARS_MAX (256) visible characters, so the
     * pattern wraps at the cap.  A smaller fill buffer would move this. */
    n = 0;
    for (i = 0; i < 400 && n < OUTCAP - 8; i++) {
        g_fill[n++] = (unsigned char)('a' + (i % 26));
    }
    justify_triple("jx/wrap", AB, 2, 300, g_fill, n, 0);

    /* The same, with CR/NL/TAB sprinkled through, so the cap is reached
     * only if the strip happens -- the two effects interacting. */
    n = 0;
    for (i = 0; i < 400 && n < OUTCAP - 8; i++) {
        g_fill[n++] = (unsigned char)('a' + (i % 26));
        if ((i % 7) == 0) g_fill[n++] = '\n';
        if ((i % 11) == 0) g_fill[n++] = '\t';
    }
    justify_triple("jx/wrapstrip", AB, 2, 300, g_fill, n, 0);

    /* A fill that is nothing but strippable bytes: after the strip the fill
     * is empty and the space default must kick in. */
    n = 0;
    for (i = 0; i < 40; i++) {
        g_fill[n++] = "\r\n\t"[i % 3];
    }
    justify_triple("jx/allstrip", AB, 2, 10, g_fill, n, 0);
}

/*
 * Randomised justification.  Fill bytes are drawn from a small alphabet that
 * includes the strippable characters and PUA lead bytes, so sequences land in
 * combinations the fixed cases do not spell out.
 */
static void run_justify_fuzz(unsigned int iters)
{
    unsigned int it;
    unsigned long i, flen, dlen, width;
    unsigned char data[64];

    for (it = 0; it < iters; it++) {
        /* Emit only WELL-FORMED sequences.  An earlier version drew bare
         * 0xEF/0x94/0xF3 lead bytes, which produces malformed UTF-8 -- and
         * the server does not admit that: net.cpp validates the incoming
         * encoding and normalises Unicode to NFC at the boundary, and the
         * database and the outbound path are maintained in NFC from there.
         *
         * Pinning behaviour on input the boundary excludes is worse than
         * useless: it makes a legitimate refactor read as a regression.
         * This battery is meant to hold color_ops to its contract, not to
         * freeze whatever it happens to do with bytes it can never see.
         */
        flen = 0;
        {
            unsigned long want = rnd(24);
            /* +4 of headroom so a multi-byte sequence started near the
             * limit always completes -- a truncated one would be exactly
             * the malformed input this is avoiding. */
            while (flen < want && flen + 4 < sizeof(g_fill)) {
                unsigned int k = rnd(10);
                if (k == 0)      g_fill[flen++] = '\r';
                else if (k == 1) g_fill[flen++] = '\n';
                else if (k == 2) g_fill[flen++] = '\t';
                else if (k == 3) {
                    /* Complete 3-byte BMP colour code. */
                    g_fill[flen++] = 0xEF;
                    g_fill[flen++] = (unsigned char)(0x94 + rnd(12));
                    g_fill[flen++] = (unsigned char)(0x80 + rnd(64));
                } else if (k == 4) {
                    /* Complete 4-byte SMP colour code. */
                    g_fill[flen++] = 0xF3;
                    g_fill[flen++] = (unsigned char)(0xB0 + rnd(4));
                    g_fill[flen++] = (unsigned char)(0x80 + rnd(64));
                    g_fill[flen++] = (unsigned char)(0x80 + rnd(64));
                } else {
                    g_fill[flen++] = (unsigned char)('a' + rnd(26));
                }
            }
        }
        dlen = rnd(12);
        for (i = 0; i < dlen; i++) {
            data[i] = (unsigned char)('A' + rnd(26));
        }
        width = rnd(40);

        {
            /* Draw the truncation flags first: evaluating rnd() inside the
             * call arguments would make the RNG sequence depend on argument
             * evaluation order, which is unspecified and need not match
             * between the host compiler and the RV64 one.  That would
             * desynchronise the routes and report a divergence that is an
             * artefact of this file rather than of color_ops.c. */
            int t0 = (int)rnd(2);
            int t1 = (int)rnd(2);
            int t2 = (int)rnd(2);
            unsigned long r;

            o_str("jf/ctr ");
            r = co_center(g_out, data, dlen, width, g_fill, flen, t0);
            o_case("", r, g_out, r);
            o_str("jf/ljs ");
            r = co_ljust(g_out, data, dlen, width, g_fill, flen, t1);
            o_case("", r, g_out, r);
            o_str("jf/rjs ");
            r = co_rjust(g_out, data, dlen, width, g_fill, flen, t2);
            o_case("", r, g_out, r);
        }
    }
}

/*
 * co_transform (tr) cases.  It holds the largest surviving frame in
 * color_ops.c at ~131 KB (#2002): fplain/tplain/splain at LBUF_SIZE each
 * plus fspans/tspans at MAX_TR_CLUSTERS spans each.  Any reduction has to
 * choose between its two paths, so the battery must exercise both:
 *
 *   fast path  -- every cluster in BOTH sets is one ASCII byte, so a
 *                 256-byte table is built and splain is never touched
 *   slow path  -- anything else: clusters are matched by linear scan
 *                 through a colour-stripped copy of the input
 *
 * The seam between them is is_all_single_byte_ascii(), so cases sit either
 * side of it deliberately -- a set that is ASCII except for one cluster
 * must take the slow path, and that is easy to get wrong when restructuring.
 */
static void run_transform(void)
{
    unsigned long i, n, r;
    const unsigned char *S = (const unsigned char *)"hello world";

    /* Fast path: pure ASCII sets. */
    r = co_transform(g_out, S, 11, (const unsigned char *)"lo",  2,
                     (const unsigned char *)"01", 2);
    o_case("tr/ascii", r, g_out, r);

    /* Sets of unequal length: the shorter one bounds the mapping. */
    r = co_transform(g_out, S, 11, (const unsigned char *)"abcde", 5,
                     (const unsigned char *)"xy", 2);
    o_case("tr/short_to", r, g_out, r);
    r = co_transform(g_out, S, 11, (const unsigned char *)"lo", 2,
                     (const unsigned char *)"0123", 4);
    o_case("tr/short_from", r, g_out, r);

    /* Empty sets, both ways -- nothing should map. */
    r = co_transform(g_out, S, 11, (const unsigned char *)"", 0,
                     (const unsigned char *)"", 0);
    o_case("tr/empty_both", r, g_out, r);
    r = co_transform(g_out, S, 11, (const unsigned char *)"lo", 2,
                     (const unsigned char *)"", 0);
    o_case("tr/empty_to", r, g_out, r);

    /* Empty subject. */
    r = co_transform(g_out, (const unsigned char *)"", 0,
                     (const unsigned char *)"a", 1,
                     (const unsigned char *)"b", 1);
    o_case("tr/empty_str", r, g_out, r);

    /* Colour in the SETS must be stripped before cluster parsing, so this
     * must behave exactly like tr/ascii above. */
    r = co_transform(g_out, S, 11,
                     (const unsigned char *)PUA_BMP "lo", 5,
                     (const unsigned char *)"01", 2);
    o_case("tr/colour_set", r, g_out, r);

    /* Colour in the SUBJECT must be preserved through the mapping. */
    r = co_transform(g_out, (const unsigned char *)PUA_BMP "hello", 8,
                     (const unsigned char *)"lo", 2,
                     (const unsigned char *)"01", 2);
    o_case("tr/colour_str", r, g_out, r);

    /* Slow path: one non-ASCII cluster is enough to leave the fast path.
     * e-acute (C3 A9) and a star (E2 98 85). */
    r = co_transform(g_out, (const unsigned char *)"caf\xC3\xA9", 5,
                     (const unsigned char *)"\xC3\xA9", 2,
                     (const unsigned char *)"\xE2\x98\x85", 3);
    o_case("tr/utf8_cluster", r, g_out, r);

    /* Mixed: ASCII and non-ASCII in the same set -- still slow path. */
    r = co_transform(g_out, (const unsigned char *)"abc\xC3\xA9", 5,
                     (const unsigned char *)"a\xC3\xA9", 3,
                     (const unsigned char *)"X\xE2\x98\x85", 4);
    o_case("tr/mixed_set", r, g_out, r);

    /* Combining mark: base + U+0301 is ONE grapheme cluster, so this
     * exercises next_grapheme_plain rather than a bare codepoint walk. */
    r = co_transform(g_out, (const unsigned char *)"e\xCC\x81x", 4,
                     (const unsigned char *)"e\xCC\x81", 3,
                     (const unsigned char *)"Z", 1);
    o_case("tr/combining", r, g_out, r);

    /* Subject longer than the sets, repeated matches. */
    n = 0;
    for (i = 0; i < 200 && n < OUTCAP - 4; i++) {
        g_big[n++] = (unsigned char)('a' + (i % 5));
    }
    r = co_transform(g_out, g_big, n, (const unsigned char *)"abc", 3,
                     (const unsigned char *)"XYZ", 3);
    o_case("tr/repeat", r, g_out, r);

    /* Past MAX_TR_CLUSTERS (1024): the set is truncated at the cap, so
     * clusters beyond it must not map.  A narrower fplain/tplain would
     * move exactly this. */
    n = 0;
    for (i = 0; i < 1200 && n < OUTCAP - 4; i++) {
        g_fill[n++] = (unsigned char)(0x20 + (i % 90));
    }
    r = co_transform(g_out, S, 11, g_fill, n, g_fill, n);
    o_case("tr/cap_identity", r, g_out, r);
}

/*
 * Grapheme-cluster addressing: co_cluster_count / co_mid_cluster (#2045).
 *
 * scramble() emits clusters in shuffled order by calling co_mid_cluster()
 * once per cluster, and each call re-walks the whole string -- so it is
 * O(n^2) and a CA_PUBLIC call occupies the server for ~19s at LBUF length.
 * The fix is a single pass recording every cluster's byte range, so these
 * cases pin what "the cluster at index i" currently means before that
 * changes: the answer must be identical whether it is reached by re-walking
 * or from a precomputed table.
 *
 * Colour is the part that makes it interesting.  co_mid_cluster copies the
 * byte range VERBATIM including interleaved PUA codes, so where a colour run
 * sits relative to a cluster boundary decides which bytes come out.
 */
static void run_clusters(void)
{
    unsigned long i, n, r;
    const unsigned char *PLAIN = (const unsigned char *)"abcde";
    /* e + U+0301 is one cluster; the string is 3 clusters, 6 bytes. */
    const unsigned char *COMB  = (const unsigned char *)"xe\xCC\x81y";

    o_str("cl/count/plain ");
    o_uint(co_cluster_count(PLAIN, 5)); o_str("\n");
    o_str("cl/count/comb ");
    o_uint(co_cluster_count(COMB, 5)); o_str("\n");
    o_str("cl/count/empty ");
    o_uint(co_cluster_count((const unsigned char *)"", 0)); o_str("\n");
    o_str("cl/count/colour ");
    o_uint(co_cluster_count((const unsigned char *)PUA_BMP "ab", 5)); o_str("\n");

    /* Every index of a plain string, plus one past the end. */
    for (i = 0; i < 6; i++) {
        r = co_mid_cluster(g_out, PLAIN, 5, i, 1);
        o_str("cl/mid/plain/"); o_uint(i); o_str(" ");
        o_case("", r, g_out, r);
    }

    /* Every index of the combining-mark string: index 1 must yield BOTH
     * bytes of the cluster, not just the base. */
    for (i = 0; i < 4; i++) {
        r = co_mid_cluster(g_out, COMB, 5, i, 1);
        o_str("cl/mid/comb/"); o_uint(i); o_str(" ");
        o_case("", r, g_out, r);
    }

    /* Multi-cluster spans, including one that runs off the end. */
    r = co_mid_cluster(g_out, PLAIN, 5, 1, 3);
    o_case("cl/mid/span/1,3", r, g_out, r);
    r = co_mid_cluster(g_out, PLAIN, 5, 3, 9);
    o_case("cl/mid/span/3,9", r, g_out, r);
    r = co_mid_cluster(g_out, PLAIN, 5, 0, 0);
    o_case("cl/mid/span/0,0", r, g_out, r);

    /* Colour interleaved with clusters.  Where the PUA run sits relative to
     * the boundary decides which bytes the range carries. */
    {
        const unsigned char *C1 = (const unsigned char *)"a" PUA_BMP "bc";
        for (i = 0; i < 3; i++) {
            r = co_mid_cluster(g_out, C1, 6, i, 1);
            o_str("cl/mid/colour/"); o_uint(i); o_str(" ");
            o_case("", r, g_out, r);
        }
        r = co_mid_cluster(g_out, C1, 6, 0, 3);
        o_case("cl/mid/colour/all", r, g_out, r);
    }

    /* Colour immediately before a combining mark -- the boundary case a
     * single-pass rewrite is most likely to move. */
    {
        const unsigned char *C2 = (const unsigned char *)"e" PUA_BMP "\xCC\x81z";
        o_str("cl/count/colcomb ");
        o_uint(co_cluster_count(C2, 7)); o_str("\n");
        for (i = 0; i < 3; i++) {
            r = co_mid_cluster(g_out, C2, 7, i, 1);
            o_str("cl/mid/colcomb/"); o_uint(i); o_str(" ");
            o_case("", r, g_out, r);
        }
    }

    /* Long input: scramble's actual shape, walked end to end one cluster at
     * a time.  This is the loop whose cost #2045 is about, so the transcript
     * must be identical after it is made linear. */
    n = 0;
    for (i = 0; i < 300; i++) {
        g_big[n++] = (unsigned char)('a' + (i % 26));
    }
    {
        unsigned long total = 0;
        for (i = 0; i < 300; i++) {
            r = co_mid_cluster(g_out, g_big, n, i, 1);
            total += r;
        }
        o_str("cl/walk/300 total="); o_uint(total); o_str("\n");
    }
}

static void run_all(unsigned int fuzz_iters)
{
    run_fixed();
    run_cap();
    run_fuzz(fuzz_iters);
    run_justify();
    run_justify_fuzz(fuzz_iters);
    run_transform();
    run_clusters();
    o_str("DONE\n");
}

#endif /* CO_DIFF_CASES_H */
