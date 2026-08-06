/*! \file poison.c
 * \brief LD_PRELOAD shim that hands back DIRTY heap memory.
 *
 * The bug class this exists to find: a buffer handed over uninitialized
 * (#2145 made eleven list-builtin tables uninitialized by contract) that is
 * then read past the count something actually wrote.  That read is
 * structurally invisible to a clean test run, because large allocations
 * normally come back as fresh kernel-zeroed pages -- so the stale slot reads
 * as zero and behaves exactly like the value-initialized code it replaced.
 *
 * It bites later, nondeterministically, once the allocator recycles a block
 * that a previous call already filled.  Measured, alternating a table-filling
 * call with a short-list call reading one slot past a count of three:
 *
 *     200 rounds: past-count read was nullptr 1 times, NON-NULL 199 times
 *       of the non-null reads, 199 dereferenced successfully (no fault)
 *
 * That is the real failure mode, and note what it is NOT: a crash.  It is a
 * stale-but-valid pointer that dereferences cleanly into whatever the last
 * caller left there -- silent wrong output.  This shim's whole job is to turn
 * that quiet failure into a loud one, by filling every large allocation with
 * 0xAA.  Repeated, 0xAAAA_AAAA_AAAA_AAAA is non-canonical on x86-64 and
 * outside any mapping on aarch64, so dereferencing one faults immediately
 * instead of half-working.
 *
 * Deliberately NOT poisoned:
 *   - calloc(), which is contractually zeroed; poisoning it breaks correct
 *     code rather than exposing incorrect code.
 *   - realloc(), whose grown tail is a genuine uninitialized region but whose
 *     preserved prefix must survive; the added coverage is not worth the risk
 *     of a shim bug being mistaken for an engine bug.
 *   - anything under POISON_MIN_BYTES, so the pool allocator's small,
 *     high-frequency traffic keeps its normal cost.
 *
 * Build and use via `make test-poison`; see run.sh, which verifies this shim
 * is working before it trusts a green result from it.
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stddef.h>
#include <string.h>

#define POISON_BYTE      0xAA
#define POISON_MIN_BYTES 4096

static void *(*real_malloc)(size_t);

/* dlsym() may allocate while we are still resolving malloc.  Serving those
 * few bytes from a static arena keeps the bootstrap from recursing; it is
 * never freed, which is correct for a shim that lives as long as the process.
 */
static char   boot_arena[16384];
static size_t boot_used;

static void *boot_alloc(size_t n)
{
    size_t aligned = (n + 15u) & ~(size_t)15u;
    if (sizeof(boot_arena) - boot_used < aligned)
    {
        return NULL;
    }
    void *p = boot_arena + boot_used;
    boot_used += aligned;
    return p;
}

int poison_is_active(void)
{
    return 1;
}

void *malloc(size_t n)
{
    static int resolving;

    if (NULL == real_malloc)
    {
        if (resolving)
        {
            return boot_alloc(n);
        }
        resolving = 1;
        real_malloc = dlsym(RTLD_NEXT, "malloc");
        resolving = 0;
        if (NULL == real_malloc)
        {
            return boot_alloc(n);
        }
    }

    void *p = real_malloc(n);
    if (NULL != p && POISON_MIN_BYTES <= n)
    {
        memset(p, POISON_BYTE, n);
    }
    return p;
}
