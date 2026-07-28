/*! \file test_plural.cpp
 * \brief Unit test for the built-in reader's Plural-Forms evaluator (#1702).
 *
 * Why this exists as a unit test rather than a server scenario:
 *
 * #1702 made the built-in MO reader the only catalogue path on every
 * platform, which meant implementing Plural-Forms here instead of borrowing
 * ngettext(3) from libintl.  That moved plural selection for EVERY platform
 * onto one expression evaluator -- so a bug in it is no longer a Windows
 * bug, it is everybody's.
 *
 * tests/nls/run.sh drives plurals through the server, but it can only reach
 * the counts a scenario can produce (@entrances with one or two exits) and
 * only the rule the shipped catalogues carry (nplurals=2, plural=(n!=1), and
 * ko's nplurals=1).  Neither exercises ternary chaining, %, && / ||, or the
 * out-of-range and malformed cases.  Russian needs n=21 to be interesting,
 * and digging 21 exits to test an expression parser is the wrong shape.
 *
 * Reaching plural_eval() means being in its translation unit, since it lives
 * in an anonymous namespace -- the same reason tests/dbt/test_interp.cpp
 * includes dbt_interp.cpp to reach its file-static mem_check.
 */

#define HAVE_NLS 1
#include "mux_nls.cpp"

#include <cstdio>
#include <cstring>

namespace
{
    int g_pass = 0;
    int g_fail = 0;

    // Point the evaluator at a rule, as plural_load() would after reading a
    // catalogue header.
    //
    void use_rule(unsigned long nplurals, const char *expr)
    {
        s_nplurals = nplurals;
        if (nullptr == expr)
        {
            s_plural_expr = nullptr;
            return;
        }
        snprintf(s_plural_buf, sizeof(s_plural_buf), "%s", expr);
        s_plural_expr = s_plural_buf;
    }

    void expect(const char *label, unsigned long n, unsigned long want)
    {
        const unsigned long got = plural_eval(n);
        if (got == want)
        {
            g_pass++;
        }
        else
        {
            g_fail++;
            printf("not ok - %s: n=%lu wanted form %lu, got %lu\n",
                   label, n, want, got);
        }
    }
}

int main(void)
{
    // ---- English: the .pot's own rule, and the fallback shape ----
    use_rule(2, "(n != 1)");
    expect("en", 0, 1);
    expect("en", 1, 0);
    expect("en", 2, 1);
    expect("en", 21, 1);

    // ---- No rule at all: must behave as English, not as form 0 ----
    //
    // An untranslated or header-less catalogue still has to pick sensibly,
    // and "always singular" would render "1 entrances found" for every count.
    //
    use_rule(2, nullptr);
    expect("no-rule", 1, 0);
    expect("no-rule", 2, 1);

    // ---- Korean / Japanese / Chinese: one form, always ----
    //
    // This is the case the old built-in reader got wrong in the other
    // direction: it returned the English plural for n>1, so ko saw a form
    // its catalogue does not even define.
    //
    use_rule(1, "0");
    expect("ko", 1, 0);
    expect("ko", 2, 0);
    expect("ko", 11, 0);

    // ---- French: plural starts at 2, and 0 is singular ----
    use_rule(2, "(n > 1)");
    expect("fr", 0, 0);
    expect("fr", 1, 0);
    expect("fr", 2, 1);

    // ---- Russian: three forms, chained ternaries, % and && and || ----
    //
    // The rule this evaluator exists for.  Hand-checked against the standard
    // ru Plural-Forms: form 0 for 1/21/31 (but not 11), form 1 for 2-4/22-24
    // (but not 12-14), form 2 otherwise.
    //
    use_rule(3,
        "(n%10==1 && n%100!=11 ? 0 : n%10>=2 && n%10<=4 "
        "&& (n%100<10 || n%100>=20) ? 1 : 2)");
    expect("ru", 1, 0);
    expect("ru", 2, 1);
    expect("ru", 4, 1);
    expect("ru", 5, 2);
    expect("ru", 11, 2);      // 11 is the exception to the n%10==1 rule
    expect("ru", 12, 2);      // 12-14 are the exception to the 2-4 rule
    expect("ru", 14, 2);
    expect("ru", 21, 0);
    expect("ru", 22, 1);
    expect("ru", 25, 2);
    expect("ru", 101, 0);
    expect("ru", 111, 2);
    expect("ru", 0, 2);

    // ---- Polish: a different three-form split, to catch a rule fitted
    //      to Russian by accident ----
    use_rule(3,
        "(n==1 ? 0 : n%10>=2 && n%10<=4 && (n%100<10 || n%100>=20) ? 1 : 2)");
    expect("pl", 1, 0);
    expect("pl", 2, 1);
    expect("pl", 5, 2);
    expect("pl", 21, 2);      // unlike ru, 21 is NOT form 0 in pl
    expect("pl", 22, 1);

    // ---- Arabic: six forms, the widest rule in common use ----
    use_rule(6,
        "(n==0 ? 0 : n==1 ? 1 : n==2 ? 2 : n%100>=3 && n%100<=10 ? 3 "
        ": n%100>=11 ? 4 : 5)");
    expect("ar", 0, 0);
    expect("ar", 1, 1);
    expect("ar", 2, 2);
    expect("ar", 5, 3);
    expect("ar", 15, 4);
    expect("ar", 102, 5);

    // ---- Hostile and malformed input: must be total, never trap ----
    //
    // A .mo is data the operator may not have written.  Every one of these
    // has to yield some form rather than crash or read past the buffer.
    //
    use_rule(2, "n/0");
    expect("div-by-zero", 5, 0);

    use_rule(2, "n%0");
    expect("mod-by-zero", 5, 0);

    use_rule(2, "(n");            // unbalanced
    expect("unbalanced", 1, 0);

    use_rule(2, "n ? 1");         // ternary with no ':'
    expect("truncated-ternary", 1, 0);

    use_rule(2, "@@@");           // not the grammar at all
    expect("garbage", 1, 0);

    use_rule(2, "");              // empty expression
    expect("empty", 1, 0);

    // A rule selecting a form the catalogue does not carry must clamp to 0
    // rather than index off the end of the translation entry.
    //
    use_rule(2, "5");
    expect("out-of-range", 1, 0);

    printf("=== tests/nls plural: %d passed, %d failed ===\n", g_pass, g_fail);
    return (0 == g_fail) ? 0 : 1;
}
