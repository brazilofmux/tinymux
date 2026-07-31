// test_format.cpp — differential tests for mux_vsnprintf.
//
// mux_vsnprintf implements printf's conversions by hand.  Anything it did not
// implement fell through to mux_assert(0) and aborted the process, so a
// caller reaching for a standard conversion took the server down: that was
// #1382 in fun_astbench and, independently, the same shape in @list.  A grep
// of the tree found no third instance, but "no third instance today" is not a
// property anyone can maintain by hand -- so %i, %o and the floating-point
// conversions are implemented rather than forbidden, and what remains
// unimplemented is echoed literally rather than being fatal (#1429).  The two
// halves matter together: implementing conversions shrinks the surface, and
// not aborting means the next gap found is a formatting bug instead of an
// outage.
//
// Floating point is assembled here from mux_dtoa digits (the same correctly
// rounded generator mux_ftoa and fval use) rather than delegated to the host
// libc.  Hand-assembled float formatting is exactly the kind of code that
// looks right and is subtly wrong at the boundaries, so the platform's
// snprintf is used as an oracle across a wide corpus of values, precisions,
// widths and flags.
//
// The oracle is only valid for conversions where mux_vsnprintf intends to
// match printf.  It does, for everything tested here.

#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cfloat>
#include <string>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

static void report(const char *what, const char *fmt,
                   const std::string &got, const std::string &want)
{
    if (got == want)
    {
        g_pass++;
        return;
    }
    g_fail++;
    if (g_fail <= 40)
    {
        printf("not ok - %s  fmt=%-14s mux=%-28s libc=%s\n",
               what, fmt, ("[" + got + "]").c_str(), ("[" + want + "]").c_str());
    }
}

static std::string mux_fmt(const char *fmt, ...)
{
    UTF8 buf[LBUF_SIZE];
    va_list ap;
    va_start(ap, fmt);
    size_t n = mux_vsnprintf(buf, sizeof(buf), (const UTF8 *)fmt, ap);
    va_end(ap);
    return std::string((const char *)buf, n);
}

static std::string libc_fmt_d(const char *fmt, double v)
{
    char buf[4096];
    snprintf(buf, sizeof(buf), fmt, v);
    return std::string(buf);
}

static std::string libc_fmt_i(const char *fmt, int v)
{
    char buf[4096];
    snprintf(buf, sizeof(buf), fmt, v);
    return std::string(buf);
}

static std::string libc_fmt_u(const char *fmt, unsigned int v)
{
    char buf[4096];
    snprintf(buf, sizeof(buf), fmt, v);
    return std::string(buf);
}

static std::string libc_fmt_ull(const char *fmt, unsigned long long v)
{
    char buf[4096];
    snprintf(buf, sizeof(buf), fmt, v);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// Floating point: the conversions that used to abort.
// ---------------------------------------------------------------------------

static void test_floats(void)
{
    const double values[] = {
        0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 3.14159265358979,
        2.5, 3.5, 0.125, 0.1, 1.0/3.0, 100.0, -100.0,
        1e-5, 1e-4, 1e-3, 1e5, 1e6, 1e15, 1e16, 1e17,
        123456789.0, 0.000123456789, 9.999999999,
        1e300, 1e-300, DBL_MAX, DBL_MIN,
        // Ties, where round-half-even vs round-half-away actually differs.
        0.005, 0.015, 0.025, 0.045, 1.005, 2.675, 8.835,
        // Values that stress the integer/fraction boundary.
        0.9999999, 9.9999999, 99.999999,
    };
    const char *convs[] = { "f", "F", "e", "E", "g", "G" };
    const char *precs[] = { "", ".0", ".1", ".2", ".3", ".6", ".9", ".15", ".17" };
    const char *widths[] = { "", "1", "8", "15", "20" };
    const char *flags[]  = { "", "-", "0" };

    for (double v : values)
    for (const char *c : convs)
    for (const char *p : precs)
    for (const char *w : widths)
    for (const char *f : flags)
    {
        // "%-0..." is contradictory; skip rather than pin a corner printf
        // itself treats as implementation-flavoured.
        std::string fmt = std::string("%") + f + w + p + c;
        report("float", fmt.c_str(), mux_fmt(fmt.c_str(), v),
               libc_fmt_d(fmt.c_str(), v));
    }
}

static void test_float_specials(void)
{
    const double specials[] = {
        HUGE_VAL, -HUGE_VAL, std::nan(""),
    };
    const char *fmts[] = {
        "%f", "%F", "%e", "%E", "%g", "%G", "%.3f", "%10f", "%-10f",
    };
    for (double v : specials)
    for (const char *fmt : fmts)
    {
        report("special", fmt, mux_fmt(fmt, v), libc_fmt_d(fmt, v));
    }
}

// ---------------------------------------------------------------------------
// %i and %o: standard conversions that also used to abort.
// ---------------------------------------------------------------------------

static void test_i_and_o(void)
{
    const int ivals[] = { 0, 1, -1, 42, -42, 12345, -12345, 2147483647,
                          -2147483647 - 1 };
    const char *ifmts[] = { "%i", "%8i", "%-8i", "%08i", "%1i" };
    for (int v : ivals)
    for (const char *fmt : ifmts)
    {
        report("int-i", fmt, mux_fmt(fmt, v), libc_fmt_i(fmt, v));
    }

    const unsigned int uvals[] = { 0u, 1u, 7u, 8u, 63u, 64u, 511u, 512u,
                                   4294967295u, 123456u };
    const char *ofmts[] = { "%o", "%12o", "%-12o", "%012o" };
    for (unsigned int v : uvals)
    for (const char *fmt : ofmts)
    {
        report("int-o", fmt, mux_fmt(fmt, v), libc_fmt_u(fmt, v));
    }

    // 64-bit octal is 22 digits — one more than LONGEST_I64 allows for
    // decimal, which is why the scratch buffer had to grow.
    const unsigned long long lvals[] = {
        0ull, 1ull, 0xFFFFFFFFFFFFFFFFull, 0x8000000000000000ull,
        1234567890123456789ull,
    };
    for (unsigned long long v : lvals)
    {
        report("int-llo", "%llo", mux_fmt("%llo", v), libc_fmt_ull("%llo", v));
        report("int-llu", "%llu", mux_fmt("%llu", v), libc_fmt_ull("%llu", v));
        report("int-llx", "%llx", mux_fmt("%llx", v), libc_fmt_ull("%llx", v));
    }

    // #1809: size_t / SOCKET-width counters must not truncate at 32 bits.
    // SESSION and log prefixes use %zu / %llu for full-width values.
    //
    {
        const size_t zbig = static_cast<size_t>(0x100000000ull); // 2^32
        char libc_buf[64];
        std::snprintf(libc_buf, sizeof(libc_buf), "%zu", zbig);
        report("size_t-zu-above-u32", "%zu", mux_fmt("%zu", zbig), libc_buf);

        const unsigned long long sockish = 0x100000001ull;
        report("socket-llu-above-u32", "%llu",
               mux_fmt("%llu", sockish), libc_fmt_ull("%llu", sockish));

        // Shape used by dump_users SESSION after #1809.
        std::string got = mux_fmt(
            "%5llu%5zu%6zu%10zu",
            sockish, zbig, zbig + 1, zbig + 2);
        char want_buf[128];
        std::snprintf(want_buf, sizeof(want_buf), "%5llu%5zu%6zu%10zu",
                      sockish, zbig, zbig + 1, zbig + 2);
        report("session-row-widths", "mix", got, want_buf);
    }
}

// ---------------------------------------------------------------------------
// Regressions on what already worked, so the new arms cannot disturb them.
// ---------------------------------------------------------------------------

static void test_existing(void)
{
    report("d",   "%d",     mux_fmt("%d", -42),          libc_fmt_i("%d", -42));
    report("08d", "%08d",   mux_fmt("%08d", -42),        libc_fmt_i("%08d", -42));
    report("-8d", "%-8d",   mux_fmt("%-8d", -42),        libc_fmt_i("%-8d", -42));
    report("u",   "%u",     mux_fmt("%u", 4294967295u),  libc_fmt_u("%u", 4294967295u));
    report("x",   "%x",     mux_fmt("%x", 48879u),       libc_fmt_u("%x", 48879u));
    report("X",   "%X",     mux_fmt("%X", 48879u),       libc_fmt_u("%X", 48879u));

    // Strings and literal percent are not covered by the numeric oracle.
    report("s",   "%s",     mux_fmt("%s", (const UTF8 *)"hello"), "hello");
    report("pct", "%%",     mux_fmt("100%%"),            "100%");
    report("mix", "%d/%s",  mux_fmt("%d/%s", 7, (const UTF8 *)"x"), "7/x");
}

// ---------------------------------------------------------------------------
// Truncation: a format that does not fit must truncate, never overrun and
// never abort.
// ---------------------------------------------------------------------------

static void test_truncation(void)
{
    UTF8 small[8];
    memset(small, 0xAA, sizeof(small));
    UTF8 canary = small[7];
    (void)canary;

    va_list dummy;
    (void)dummy;

    UTF8 buf[16];
    size_t n;
    {
        // 12 chars into an 8-byte buffer.
        struct Local {
            static size_t go(UTF8 *b, size_t nb, const char *fmt, ...)
            {
                va_list ap;
                va_start(ap, fmt);
                size_t r = mux_vsnprintf(b, nb, (const UTF8 *)fmt, ap);
                va_end(ap);
                return r;
            }
        };
        n = Local::go(buf, 8, "%f", 123456.789);
        if (n <= 7 && buf[n] == '\0')
        {
            g_pass++;
        }
        else
        {
            g_fail++;
            printf("not ok - truncation: n=%zu buf=[%s]\n", n, (const char *)buf);
        }

        // A precision large enough to exceed the float scratch must also
        // truncate rather than overrun or abort.
        n = Local::go(buf, sizeof(buf), "%.2000f", 1.0);
        if (n < sizeof(buf))
        {
            g_pass++;
        }
        else
        {
            g_fail++;
            printf("not ok - huge precision: n=%zu\n", n);
        }
    }
}

// Conversions mux_vsnprintf still does not implement must not kill the
// process (#1429).
//
// snprintf is not an oracle here: the required behaviour deliberately differs
// from printf's.  An unimplemented spec is echoed literally, so the evidence
// lands in the output where it is visible, and formatting then STOPS.  Before
// this, each of these reached mux_assert(0), which is an unconditional abort()
// in the shipping build -- mux_assert has no NDEBUG guard -- so an ordinary
// looking T("%+d") took the whole game down.
//
// Stopping rather than continuing is what test_unimplemented_stops below
// pins.  Recovery consumes no va_arg, so anything interpreted afterwards
// reads a shifted argument list (#1445).
//
// Reaching this function at all is the test.  If the recovery regresses to an
// abort, the harness dies here rather than reporting a failure, which is
// itself an unambiguous signal.
//
static void test_unimplemented(void)
{
    static const struct
    {
        const char *fmt;
        const char *want;
    } cases[] =
    {
        // The seven forms named in #1429.  Each is a perfectly ordinary
        // printf spec that no one would expect to be fatal.
        //
        { "%+d",  "%+d"  },
        { "% d",  "% d"  },
        { "%#o",  "%#o"  },
        { "%#x",  "%#x"  },
        { "%+f",  "%+f"  },
        { "%hd",  "%hd"  },
        { "%a",   "%a"   },

        // Surrounding text must survive on both sides, and a spec must not
        // swallow what follows it.
        //
        { "before %+d after", "before %+d after" },
        { "[%a]",             "[%a]"             },

        // A trailing bare '%' and a doubled '%' are already handled; included
        // so the recovery path cannot break them.
        //
        { "100%% sure",       "100% sure"        },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        // Deliberately passing an argument the spec will not consume: the
        // recovery does not call va_arg, and must not read one either.
        //
        std::string got = mux_fmt(cases[i].fmt, 42);
        report("unimplemented", cases[i].fmt, got, cases[i].want);
    }
}

// Recovery must STOP at the first spec it cannot parse (#1445).
//
// No va_arg is consumed for an unimplemented spec, so a later conversion in
// the same format string reads the wrong argument.  Continuing was measured
// before this was fixed:
//
//   "%+d|%d"  11, 22      ->  "%+d|11"    silently the wrong argument
//   "%+d|%s"  11, "tail"  ->  SIGSEGV     an int dereferenced as UTF8*
//
// The %s case is why this is a crash test and not a cosmetic one, and it is
// why "echo and carry on" is not a safe recovery: there is no way to
// resynchronise without knowing what the unknown spec would have taken.
//
// Conversions BEFORE the bad spec must still be honoured -- stopping is not
// the same as discarding the whole format.
//
static void test_unimplemented_stops(void)
{
    static const struct
    {
        const char *fmt;
        const char *want;
    } cases[] =
    {
        // Everything from the bad spec onward is echoed verbatim.
        //
        { "%+d|%d",   "%+d|%d"   },
        { "a%#xb%sc", "a%#xb%sc" },
        { "%hd tail", "%hd tail" },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
    {
        std::string got = mux_fmt(cases[i].fmt, 11, 22);
        report("stops", cases[i].fmt, got, cases[i].want);
    }

    // The %s tail: this is the shape that segfaulted.  Reaching the report
    // at all is most of the assertion.
    //
    report("stops", "%+d|%s", mux_fmt("%+d|%s", 11, "tail"), "%+d|%s");

    // A conversion before the bad spec is still performed.
    //
    report("stops", "%s|%+d", mux_fmt("%s|%+d", "head", 11), "head|%+d");
    report("stops", "%d then %a", mux_fmt("%d then %a", 7, 1.5), "7 then %a");
}

// POSIX %N$ positional conversions (#1623).  gettext catalogues reorder
// multi-conversion msgids this way; the sequential path cannot.
//
static void test_positional(void)
{
    // Same argument order as the call site; format reorders them.
    //
    report("positional", "%2$s %1$d",
           mux_fmt("%2$s %1$d", 7, "name"), "name 7");
    report("positional", "%1$s-%2$d-%3$s",
           mux_fmt("%1$s-%2$d-%3$s", "a", 2, "b"), "a-2-b");
    report("positional", "%3$s %2$s %1$d",
           mux_fmt("%3$s %2$s %1$d", 9, "mid", "end"), "end mid 9");

    // The Korean create message shape from tests/nls/run_ko.sh.
    //
    report("positional", "사물 #%2$d(으)로 %1$s을(를) 만들었습니다",
           mux_fmt("사물 #%2$d(으)로 %1$s을(를) 만들었습니다",
                   "kowidget", 12),
           "사물 #12(으)로 kowidget을(를) 만들었습니다");

    // Sequential formats must still work (no $).
    //
    report("positional", "seq %s %d",
           mux_fmt("%s %d", "x", 3), "x 3");
}

int main(void)
{
    printf("=== mux_vsnprintf differential tests (vs snprintf) ===\n");

    test_existing();
    test_i_and_o();
    test_floats();
    test_float_specials();
    test_truncation();
    test_unimplemented();
    test_unimplemented_stops();
    test_positional();

    printf("=== %d passed, %d failed ===\n", g_pass, g_fail);
    return (0 == g_fail) ? 0 : 1;
}
