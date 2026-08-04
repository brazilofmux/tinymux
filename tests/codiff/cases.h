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

static void run_all(unsigned int fuzz_iters)
{
    run_fixed();
    run_cap();
    run_fuzz(fuzz_iters);
    o_str("DONE\n");
}

#endif /* CO_DIFF_CASES_H */
