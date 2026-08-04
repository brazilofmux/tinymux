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

static void run_all(unsigned int fuzz_iters)
{
    run_fixed();
    run_cap();
    run_fuzz(fuzz_iters);
    run_justify();
    run_justify_fuzz(fuzz_iters);
    o_str("DONE\n");
}

#endif /* CO_DIFF_CASES_H */
