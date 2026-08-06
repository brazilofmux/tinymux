/*! \file selftest.cpp
 * \brief Proves the poison shim works, before anything trusts a green run under it.
 *
 * A suite that passes under a shim which silently failed to load looks exactly
 * like a suite that passes under a working one -- and is worth nothing.  That
 * is the same failure #1946 addressed for build configuration and #2133 for
 * the benchmarking instruments: an instrument must not report an answer it
 * cannot stand behind.  So run.sh runs these modes first and refuses to
 * continue if any of them says the wrong thing.
 *
 * Modes:
 *   --check-fill   Is a large allocation actually poisoned?  Detects a shim
 *                  that failed to load (wrong arch, missing -ldl, LD_PRELOAD
 *                  dropped by an intervening exec) -- the vacuous-pass case.
 *   --past-read    The injected defect: a table written to N entries and read
 *                  at N, exactly as a list builtin would after splitting a
 *                  short list.  Must exit 0 unpoisoned and die poisoned.
 *   --recycle      Informational: shows what a past-count read really yields
 *                  once the allocator recycles a filled block.  Never fails
 *                  the run; it is the justification for the other two.
 */

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <memory>

typedef unsigned char UTF8;

// Matches the shape of the tables in question: LBUF_SIZE/2 pointers, which is
// far above the shim's 4096-byte threshold.  Kept local on purpose -- this
// test must not depend on engine headers to describe an allocator property.
static const int  TABLE_ENTRIES  = 16384;
static const int  WRITTEN_COUNT  = 3;
static const unsigned char POISON_BYTE = 0xAA;

static char words[8][16];

static void init_words()
{
    for (int i = 0; i < 8; i++)
    {
        snprintf(words[i], sizeof(words[i]), "word%d", i);
    }
}

// Is a freshly allocated large block poison-filled?
static int check_fill()
{
    std::unique_ptr<unsigned char[]> p(new unsigned char[TABLE_ENTRIES * 8]);
    size_t n = static_cast<size_t>(TABLE_ENTRIES) * 8;
    size_t poisoned = 0, zeroed = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (POISON_BYTE == p[i]) poisoned++;
        else if (0 == p[i])      zeroed++;
    }
    if (poisoned == n)
    {
        printf("poisoned\n");
        return 0;
    }
    if (zeroed == n)
    {
        printf("clean\n");
        return 0;
    }
    printf("mixed (poisoned=%zu zeroed=%zu of %zu)\n", poisoned, zeroed, n);
    return 0;
}

// The defect #2146 could have introduced, reproduced deliberately: write
// WRITTEN_COUNT entries, then read one slot past.  The `if (past)` guard
// mirrors safe_str(), which tolerates a null -- so unpoisoned this is a
// no-op on fresh zeroed pages, which is precisely why a plain suite cannot
// see it.  Poisoned, the slot is non-canonical and the dereference faults.
static int past_read()
{
    std::unique_ptr<UTF8*[]> table(new UTF8*[TABLE_ENTRIES]);
    for (int i = 0; i < WRITTEN_COUNT; i++)
    {
        table[i] = reinterpret_cast<UTF8 *>(words[i]);
    }

    UTF8 *volatile past = table[WRITTEN_COUNT];
    if (nullptr != past)
    {
        printf("past-count slot = %p, dereferencing...\n", (void *)past);
        fflush(stdout);
        printf("  content=\"%s\"\n", reinterpret_cast<char *>(past));
        return 0;
    }
    printf("past-count slot = null (benign; indistinguishable from correct code)\n");
    return 0;
}

// Why the shim is needed at all: once a filled table is freed and the
// allocator hands the same block back, the past-count slot holds a stale but
// perfectly valid pointer.  It dereferences cleanly -- silent wrong output,
// not a crash.  Informational only; allocator behaviour is not a contract.
static int recycle()
{
    int nullish = 0, nonnull = 0, deref_ok = 0;
    void *last = nullptr;
    bool same_block = false;

    for (int round = 0; round < 200; round++)
    {
        {   // a call that splits a long list and fills the table
            std::unique_ptr<UTF8*[]> t(new UTF8*[TABLE_ENTRIES]);
            for (int i = 0; i < 500; i++)
            {
                t[i] = reinterpret_cast<UTF8 *>(words[i % 8]);
            }
            last = t.get();
        }
        {   // a call that splits a short list, then reads one past
            std::unique_ptr<UTF8*[]> t(new UTF8*[TABLE_ENTRIES]);
            if (t.get() == last) same_block = true;
            for (int i = 0; i < WRITTEN_COUNT; i++)
            {
                t[i] = reinterpret_cast<UTF8 *>(words[i]);
            }
            UTF8 *past = t[WRITTEN_COUNT];
            if (nullptr == past)
            {
                nullish++;
            }
            else
            {
                nonnull++;
                if (strlen(reinterpret_cast<char *>(past)) < 100) deref_ok++;
            }
        }
    }
    printf("recycled the same block: %s\n", same_block ? "yes" : "no");
    printf("200 rounds: past-count read was null %d times, NON-NULL %d times\n",
           nullish, nonnull);
    printf("  of the non-null reads, %d dereferenced without faulting\n", deref_ok);
    return 0;
}

int main(int argc, char *argv[])
{
    init_words();
    if (2 != argc)
    {
        fprintf(stderr, "usage: %s --check-fill|--past-read|--recycle\n", argv[0]);
        return 2;
    }
    if (0 == strcmp(argv[1], "--check-fill")) return check_fill();
    if (0 == strcmp(argv[1], "--past-read"))  return past_read();
    if (0 == strcmp(argv[1], "--recycle"))    return recycle();
    fprintf(stderr, "unknown mode: %s\n", argv[1]);
    return 2;
}
