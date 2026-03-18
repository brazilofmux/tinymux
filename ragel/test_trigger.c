/*
 * test_trigger.c — Test harness for the trigger matching engine.
 */

#include "trigger_match.h"
#include <stdio.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) \
    do { printf("  %-55s ", name); } while (0)

#define PASS() \
    do { printf("PASS\n"); g_pass++; } while (0)

#define FAIL(fmt, ...) \
    do { printf("FAIL " fmt "\n", ##__VA_ARGS__); g_fail++; } while (0)

/* ---- Helpers ---- */

static int search_one(trigger_set *ts, const char *text)
{
    trigger_match_t results[TRIGGER_MAX];
    return trigger_set_search(ts, text, strlen(text),
                              results, TRIGGER_MAX);
}

static int search_has_id(trigger_set *ts, const char *text, int id)
{
    trigger_match_t results[TRIGGER_MAX];
    int n = trigger_set_search(ts, text, strlen(text),
                               results, TRIGGER_MAX);
    for (int i = 0; i < n; i++) {
        if (results[i].id == id) return 1;
    }
    return 0;
}

/* ---- Tests ---- */

static void test_single_literal(void)
{
    printf("\n--- Single literal trigger ---\n");

    trigger_set *ts = trigger_set_create();
    trigger_set_add(ts, 0, "tells you", TRIGGER_ICASE);
    trigger_set_compile(ts);

    TEST("matches substring");
    if (search_one(ts, "Bob tells you hello")) { PASS(); }
    else { FAIL("expected match"); }

    TEST("no match");
    if (search_one(ts, "Bob greets you") == 0) { PASS(); }
    else { FAIL("expected no match"); }

    TEST("exact match");
    if (search_one(ts, "tells you")) { PASS(); }
    else { FAIL("expected match"); }

    TEST("case insensitive");
    if (search_one(ts, "Bob TELLS YOU hello")) { PASS(); }
    else { FAIL("expected match"); }

    trigger_set_free(ts);
}

static void test_multiple_literals(void)
{
    printf("\n--- Multiple literal triggers ---\n");

    trigger_set *ts = trigger_set_create();
    trigger_set_add(ts, 0, "tells you",      TRIGGER_ICASE);
    trigger_set_add(ts, 1, "are hungry",     TRIGGER_ICASE);
    trigger_set_add(ts, 2, "drops gold",     TRIGGER_ICASE);
    trigger_set_compile(ts);

    TEST("match first only");
    if (search_has_id(ts, "Bob tells you hi", 0) &&
        !search_has_id(ts, "Bob tells you hi", 1)) { PASS(); }
    else { FAIL("wrong match set"); }

    TEST("match second only");
    if (search_has_id(ts, "You are hungry", 1) &&
        !search_has_id(ts, "You are hungry", 0)) { PASS(); }
    else { FAIL("wrong match set"); }

    TEST("match two in one line");
    if (search_has_id(ts, "tells you that you are hungry", 0) &&
        search_has_id(ts, "tells you that you are hungry", 1)) { PASS(); }
    else { FAIL("expected both triggers"); }

    TEST("match none");
    if (search_one(ts, "nothing interesting") == 0) { PASS(); }
    else { FAIL("expected no match"); }

    trigger_set_free(ts);
}

static void test_glob_star(void)
{
    printf("\n--- Glob * wildcards ---\n");

    trigger_set *ts = trigger_set_create();
    trigger_set_add(ts, 0, "drops*gold", TRIGGER_ICASE);
    trigger_set_compile(ts);

    TEST("matches with gap");
    if (search_one(ts, "The orc drops 50 gold coins")) { PASS(); }
    else { FAIL("expected match"); }

    TEST("matches adjacent");
    if (search_one(ts, "drops gold here")) { PASS(); }
    else { FAIL("expected match"); }

    TEST("matches with star=empty");
    if (search_one(ts, "dropsgold")) { PASS(); }
    else { FAIL("expected match"); }

    TEST("no match (missing gold)");
    if (search_one(ts, "drops silver coins") == 0) { PASS(); }
    else { FAIL("expected no match"); }

    TEST("no match (wrong order)");
    if (search_one(ts, "gold drops here") == 0) { PASS(); }
    else { FAIL("expected no match"); }

    trigger_set_free(ts);
}

static void test_glob_question(void)
{
    printf("\n--- Glob ? wildcard ---\n");

    trigger_set *ts = trigger_set_create();
    trigger_set_add(ts, 0, "h?llo", TRIGGER_ICASE);
    trigger_set_compile(ts);

    TEST("matches hello");
    if (search_one(ts, "say hello")) { PASS(); }
    else { FAIL("expected match"); }

    TEST("matches hallo");
    if (search_one(ts, "say hallo")) { PASS(); }
    else { FAIL("expected match"); }

    TEST("no match (hllo, missing char)");
    if (search_one(ts, "say hllo") == 0) { PASS(); }
    else { FAIL("expected no match"); }

    trigger_set_free(ts);
}

static void test_glob_dot(void)
{
    printf("\n--- Glob . wildcard ---\n");

    trigger_set *ts = trigger_set_create();
    trigger_set_add(ts, 0, "c.t", TRIGGER_ICASE);
    trigger_set_compile(ts);

    TEST("matches cat");
    if (search_one(ts, "the cat sat")) { PASS(); }
    else { FAIL("expected match"); }

    TEST("matches cot");
    if (search_one(ts, "a cot")) { PASS(); }
    else { FAIL("expected match"); }

    TEST("no match (ct)");
    if (search_one(ts, "act quickly") == 0 ||
        search_one(ts, "ct")) { /* 'act' contains 'act' which has a.t? No — c.t needs exactly 3 chars c?t */ }
    /* Let me just test 'ct' alone */
    if (search_one(ts, "only ct here") == 0) { PASS(); }
    else { FAIL("expected no match"); }

    trigger_set_free(ts);
}

static void test_anchors(void)
{
    printf("\n--- Anchors ^ and $ ---\n");

    trigger_set *ts = trigger_set_create();
    trigger_set_add(ts, 0, "^Hello", TRIGGER_ICASE);
    trigger_set_compile(ts);

    TEST("^ matches at start");
    if (search_one(ts, "Hello world")) { PASS(); }
    else { FAIL("expected match"); }

    TEST("^ no match in middle");
    if (search_one(ts, "Say Hello") == 0) { PASS(); }
    else { FAIL("expected no match"); }

    trigger_set_free(ts);

    ts = trigger_set_create();
    trigger_set_add(ts, 0, "world$", TRIGGER_ICASE);
    trigger_set_compile(ts);

    TEST("$ matches at end");
    if (search_one(ts, "Hello world")) { PASS(); }
    else { FAIL("expected match"); }

    TEST("$ no match in middle");
    if (search_one(ts, "world Hello") == 0) { PASS(); }
    else { FAIL("expected no match"); }

    trigger_set_free(ts);
}

static void test_escape(void)
{
    printf("\n--- Escaped special characters ---\n");

    trigger_set *ts = trigger_set_create();
    trigger_set_add(ts, 0, "price\\*gold", TRIGGER_ICASE);
    trigger_set_compile(ts);

    TEST("escaped * is literal");
    if (search_one(ts, "the price*gold is high")) { PASS(); }
    else { FAIL("expected match"); }

    TEST("escaped * does not glob");
    if (search_one(ts, "the price of gold") == 0) { PASS(); }
    else { FAIL("expected no match"); }

    trigger_set_free(ts);
}

static void test_mixed_literal_glob(void)
{
    printf("\n--- Mixed literal and glob triggers ---\n");

    trigger_set *ts = trigger_set_create();
    trigger_set_add(ts, 0, "tells you",      TRIGGER_ICASE);  /* literal → AC */
    trigger_set_add(ts, 1, "drops*gold",     TRIGGER_ICASE);  /* glob */
    trigger_set_add(ts, 2, "You are hungry", TRIGGER_ICASE);  /* literal → AC */
    trigger_set_compile(ts);

    TEST("literal match only");
    {
        int got = search_has_id(ts, "Bob tells you hi", 0);
        int no1 = !search_has_id(ts, "Bob tells you hi", 1);
        if (got && no1) { PASS(); } else { FAIL("wrong"); }
    }

    TEST("glob match only");
    {
        int got = search_has_id(ts, "orc drops 5 gold", 1);
        int no0 = !search_has_id(ts, "orc drops 5 gold", 0);
        if (got && no0) { PASS(); } else { FAIL("wrong"); }
    }

    TEST("both literal and glob");
    {
        int g0 = search_has_id(ts, "tells you orc drops gold", 0);
        int g1 = search_has_id(ts, "tells you orc drops gold", 1);
        if (g0 && g1) { PASS(); } else { FAIL("expected both"); }
    }

    trigger_set_free(ts);
}

static void test_remove_and_recompile(void)
{
    printf("\n--- Remove and recompile ---\n");

    trigger_set *ts = trigger_set_create();
    trigger_set_add(ts, 0, "hello", TRIGGER_ICASE);
    trigger_set_add(ts, 1, "world", TRIGGER_ICASE);
    trigger_set_compile(ts);

    TEST("both match before remove");
    if (search_has_id(ts, "hello world", 0) &&
        search_has_id(ts, "hello world", 1)) { PASS(); }
    else { FAIL("expected both"); }

    trigger_set_remove(ts, 0);
    trigger_set_compile(ts);

    TEST("only world matches after remove");
    if (!search_has_id(ts, "hello world", 0) &&
        search_has_id(ts, "hello world", 1)) { PASS(); }
    else { FAIL("wrong match set"); }

    trigger_set_free(ts);
}

static void test_needs_regex(void)
{
    printf("\n--- trigger_needs_regex ---\n");

    TEST("literal: no regex needed");
    if (!trigger_needs_regex("tells you")) { PASS(); }
    else { FAIL("should not need regex"); }

    TEST("glob *: no regex needed");
    if (!trigger_needs_regex("drops*gold")) { PASS(); }
    else { FAIL("should not need regex"); }

    TEST("alternation |: needs regex");
    if (trigger_needs_regex("cat|dog")) { PASS(); }
    else { FAIL("should need regex"); }

    TEST("group (): needs regex");
    if (trigger_needs_regex("(hello)")) { PASS(); }
    else { FAIL("should need regex"); }

    TEST("quantifier +: needs regex");
    if (trigger_needs_regex("he+llo")) { PASS(); }
    else { FAIL("should need regex"); }

    TEST("char class []: needs regex");
    if (trigger_needs_regex("[abc]")) { PASS(); }
    else { FAIL("should need regex"); }

    TEST("escaped special: no regex needed");
    if (!trigger_needs_regex("price\\+tax")) { PASS(); }
    else { FAIL("escape should prevent regex flag"); }
}

static void test_empty_and_edge_cases(void)
{
    printf("\n--- Edge cases ---\n");

    trigger_set *ts = trigger_set_create();
    trigger_set_add(ts, 0, "x", TRIGGER_ICASE);
    trigger_set_compile(ts);

    TEST("empty text matches nothing");
    if (search_one(ts, "") == 0) { PASS(); }
    else { FAIL("expected no match"); }

    TEST("single char pattern");
    if (search_one(ts, "x")) { PASS(); }
    else { FAIL("expected match"); }

    TEST("single char in longer text");
    if (search_one(ts, "abcxdef")) { PASS(); }
    else { FAIL("expected match"); }

    trigger_set_free(ts);

    /* Overlapping patterns. */
    ts = trigger_set_create();
    trigger_set_add(ts, 0, "he",    TRIGGER_ICASE);
    trigger_set_add(ts, 1, "hello", TRIGGER_ICASE);
    trigger_set_compile(ts);

    TEST("overlapping: both match");
    if (search_has_id(ts, "hello world", 0) &&
        search_has_id(ts, "hello world", 1)) { PASS(); }
    else { FAIL("expected both"); }

    trigger_set_free(ts);
}

static void test_case_sensitive(void)
{
    printf("\n--- Case-sensitive matching (no ICASE) ---\n");

    trigger_set *ts = trigger_set_create();
    trigger_set_add(ts, 0, "Hello", 0);  /* No TRIGGER_ICASE */
    trigger_set_compile(ts);

    TEST("exact case matches");
    if (search_one(ts, "say Hello there")) { PASS(); }
    else { FAIL("expected match"); }

    TEST("wrong case does not match");
    if (search_one(ts, "say hello there") == 0) { PASS(); }
    else { FAIL("expected no match"); }

    trigger_set_free(ts);
}

/* ---- Main ---- */

int main(void)
{
    printf("=== Trigger Match Engine Tests ===\n");

    test_single_literal();
    test_multiple_literals();
    test_glob_star();
    test_glob_question();
    test_glob_dot();
    test_anchors();
    test_escape();
    test_mixed_literal_glob();
    test_remove_and_recompile();
    test_needs_regex();
    test_empty_and_edge_cases();
    test_case_sensitive();

    printf("\n=== Results: %d passed, %d failed ===\n",
           g_pass, g_fail);

    return g_fail ? 1 : 0;
}
