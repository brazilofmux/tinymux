/*! \file wild_test.cpp
 * \brief Standalone unit harness for the wildcard matcher (wild.cpp).
 *
 * The wild() capturing path ($-command / ^-listen %0..%9 captures) is not
 * reachable through muxscript's REPL or the smoke suite, so this harness links
 * the real wild.eo (same code as engine.so) against libmux and calls wild() /
 * quick_wild() directly, asserting both match results and capture contents.
 *
 * Provides the two driver globals (mudstate, mudconf) wild.cpp references and
 * initializes the LBUF pool; everything else (mux_strlwr, mux_tolower tables,
 * the LBUF pool) comes from libmux.
 *
 * Compile (from mux/modules/engine):
 *   g++ -std=c++17 -g -O2 -fPIC -DREALITY_LVLS -DTINYMUX_JIT -DWOD_REALMS \
 *       -I../../include -I../../ganl/include -I../../sqlite -I../../lua54 \
 *       -o wild_test wild_test.cpp wild.eo \
 *       -L../../lib -lmux -Wl,-rpath,'$ORIGIN/../../lib' -lssl -lcrypto -lm \
 *       -Wl,--unresolved-symbols=ignore-in-object-files
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

// Driver globals referenced by wild.cpp (wild_invk_ctr / wild_invk_lim).
STATEDATA mudstate;
CONFDATA  mudconf;

static int g_pass = 0;
static int g_fail = 0;

// Join the non-null capture args with '|' separators.
static void join_caps(UTF8 *args[], int n, char *out)
{
    out[0] = '\0';
    for (int i = 0; i < n; i++)
    {
        if (nullptr != args[i])
        {
            strcat(out, "<");
            strcat(out, reinterpret_cast<char *>(args[i]));
            strcat(out, ">");
        }
    }
}

// Assert a capturing wild() match and its %0..%9 contents.
//   want_caps: concatenated "<cap0><cap1>..." of the non-null args, or "".
static void tcap(const char *pat, const char *data, bool want_match,
                 const char *want_caps)
{
    UTF8 p[LBUF_SIZE], d[LBUF_SIZE];
    strncpy(reinterpret_cast<char *>(p), pat, LBUF_SIZE - 1);
    p[LBUF_SIZE - 1] = '\0';
    strncpy(reinterpret_cast<char *>(d), data, LBUF_SIZE - 1);
    d[LBUF_SIZE - 1] = '\0';

    UTF8 *args[NUM_ENV_VARS];
    bool r = wild(p, d, args, NUM_ENV_VARS);

    char got[LBUF_SIZE];
    join_caps(args, NUM_ENV_VARS, got);

    bool ok = (r == want_match) && (0 == strcmp(got, want_caps));
    printf("%s  wild(\"%s\",\"%s\") -> %d caps=%s%s\n",
           ok ? "ok  " : "FAIL", pat, data, r ? 1 : 0, got,
           ok ? "" : (r != want_match ? "  [match mismatch]" : "  [caps mismatch]"));
    if (!ok)
    {
        printf("        expected match=%d caps=%s\n", want_match ? 1 : 0, want_caps);
        g_fail++;
    }
    else
    {
        g_pass++;
    }

    for (int i = 0; i < NUM_ENV_VARS; i++)
    {
        if (nullptr != args[i])
        {
            free_lbuf(args[i]);
        }
    }
}

// Assert a non-capturing quick_wild() match result.
static void tq(const char *pat, const char *data, bool want)
{
    bool r = quick_wild(reinterpret_cast<const UTF8 *>(pat),
                        reinterpret_cast<const UTF8 *>(data));
    bool ok = (r == want);
    printf("%s  quick_wild(\"%s\",\"%s\") -> %d\n",
           ok ? "ok  " : "FAIL", pat, data, r ? 1 : 0);
    if (ok) g_pass++; else g_fail++;
}

int main()
{
    pool_init(POOL_LBUF, LBUF_SIZE);
    pool_init(POOL_MBUF, MBUF_SIZE);
    pool_init(POOL_SBUF, SBUF_SIZE);
    mudconf.wild_invk_lim = 100000;

    printf("=== quick_wild (non-capturing) ===\n");
    tq("f*r", "foobar", true);
    tq("*z*", "foobar", false);
    tq("foo", "Foo", true);                 // ASCII case-insensitive
    tq("café", "CAFÉ", true);               // UTF-8 fold (pattern upper-cased side)
    tq("CAFÉ", "café", true);               // UTF-8 fold (data upper-cased side, #836)
    tq("????", "café", true);               // '?' = whole character (#835)
    tq("?????", "café", false);

    printf("\n=== wild (capturing) — ASCII ===\n");
    tcap("* stuff", "HELLO stuff", true, "<HELLO>");
    tcap("* and *", "foo and bar", true, "<foo><bar>");
    tcap("?", "x", true, "<x>");
    tcap("Test *", "tEsT World", true, "<World>");   // case-insensitive literal
    tcap("pre * post", "pre middle post", true, "<middle>");
    tcap("*", "anything here", true, "<anything here>");
    tcap("no match", "different", false, "");

    printf("\n=== wild (capturing) — UTF-8 ===\n");
    tcap("?", "é", true, "<é>");                       // '?' captures whole char
    tcap("x *", "x naïve", true, "<naïve>");           // capture preserves original
    tcap("*café", "naïve café", true, "<naïve >");     // '*' span, multibyte anchor
    tcap("café *", "CAFÉ münchen", true, "<münchen>"); // case-insensitive UTF-8 literal (#837)
    tcap("* CAFÉ", "münchen café", true, "<münchen>"); // case-insensitive UTF-8 anchor (#837)
    tcap("pre * post", "pre ñoño post", true, "<ñoño>");

    // '?' immediately after '*' (numextra path).  Empty captures are freed to
    // nullptr by wild(), so they do not appear in the joined output.
    printf("\n=== wild (capturing) — '*?' adjacency (numextra), ASCII ===\n");
    tcap("*?", "abc", true, "<ab><c>");
    tcap("*?x", "abx", true, "<a><b>");
    tcap("*??", "abcd", true, "<ab><c><d>");
    tcap("*?*", "abc", true, "<a><bc>");          // empty '*' dropped

    printf("\n=== wild (capturing) — '*?' adjacency (numextra), UTF-8 ===\n");
    tcap("*?x", "éx", true, "<é>");                // empty '*' dropped; '?' = é
    tcap("*?", "aé", true, "<a><é>");
    tcap("*??", "éàx", true, "<é><à><x>");
    tcap("*?*", "éàx", true, "<é><àx>");           // empty '*' dropped

    printf("\n=== %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
