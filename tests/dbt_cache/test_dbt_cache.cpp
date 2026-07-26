// Unit tests for the DBT block cache: dbt_cache_insert / dbt_cache_lookup.
//
// The cache is 4-way set associative with a FIFO way-0 eviction. Insert had
// no dedupe by guest_pc, and intrinsic blocks are inserted *twice*:
// try_emit_intrinsic() in the backend inserts before returning, and every
// caller of dbt_backend_translate_block() inserts the result again. So one
// intrinsic occupied two of the four ways in its set, and a set holding
// duplicates could FIFO-evict a live block while it still had room (#1153).
//
// The tests deliberately do not reimplement cache_set(): it is static in
// dbt.cpp, and a test that recomputed the hash would pass even if the hash
// and the test drifted apart together. Instead set membership is discovered
// empirically -- insert a marker and see which set it lands in -- so these
// tests keep working if the hash is ever retuned.
//
// Build/run: make test   (no dependency on a built netmux; dbt.cpp is
// compiled directly)

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>

#include "dbt.h"
#include "dbt_internal.h"

// ---------------------------------------------------------------
// Backend stubs
// ---------------------------------------------------------------
//
// Linking dbt.cpp pulls in the whole translation unit, so the four backend
// symbols it references must resolve. The cache pair touches none of them;
// each stub aborts so that a refactor which routes cache work through the
// backend fails loudly instead of quietly testing something else.
//
static void die_unreachable(const char *who) {
    fprintf(stderr, "test_dbt_cache: %s was called from the cache path.\n"
            "These stubs are no longer safe.\n", who);
    abort();
}

void dbt_backend_emit_trampoline(dbt_state_t *) {
    die_unreachable("dbt_backend_emit_trampoline");
}

uint8_t *dbt_backend_translate_block(dbt_state_t *, uint64_t) {
    die_unreachable("dbt_backend_translate_block");
    return nullptr;
}

void dbt_backend_backpatch_jmp(uint8_t *, uint32_t, uint8_t *) {
    die_unreachable("dbt_backend_backpatch_jmp");
}

uint32_t dbt_backend_decode_jmp_target(const uint8_t *, uint32_t) {
    die_unreachable("dbt_backend_decode_jmp_target");
    return 0;
}

// ---------------------------------------------------------------
// Test framework
// ---------------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

static void check(bool ok, const char *what) {
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
        printf("FAIL: %s\n", what);
    }
}

// Distinct non-null code pointers. Never dereferenced -- the cache only
// stores and compares them.
static uint8_t *code_ptr(int n) {
    return reinterpret_cast<uint8_t *>(static_cast<uintptr_t>(0x100000)
                                       + static_cast<uintptr_t>(n) * 0x40);
}

static dbt_state_t *make_state(void) {
    dbt_state_t *dbt = new dbt_state_t();
    dbt->cache.assign(BLOCK_CACHE_SIZE, block_entry_t{});
    return dbt;
}

static void clear_cache(dbt_state_t *dbt) {
    dbt->cache.assign(BLOCK_CACHE_SIZE, block_entry_t{});
}

// Which set does pc map to? Discovered by probing, not by recomputing the
// hash -- see the file header.
static uint32_t set_of(dbt_state_t *scratch, uint64_t pc) {
    clear_cache(scratch);
    uint8_t *mark = code_ptr(999);
    dbt_cache_insert(scratch, pc, mark);
    for (size_t i = 0; i < scratch->cache.size(); i++) {
        if (scratch->cache[i].native_code == mark) {
            return static_cast<uint32_t>(i / BLOCK_CACHE_WAYS);
        }
    }
    return UINT32_MAX;
}

// Count ways in a set that hold a live entry.
static int occupied(dbt_state_t *dbt, uint32_t set) {
    int n = 0;
    for (size_t w = 0; w < BLOCK_CACHE_WAYS; w++) {
        if (dbt->cache[set * BLOCK_CACHE_WAYS + w].native_code) {
            n++;
        }
    }
    return n;
}

// Find `want` distinct guest PCs that all map to the same set, so the tests
// can fill one set exactly. Guest PCs are 4-aligned (RV64 instructions).
static bool find_colliding(dbt_state_t *scratch, size_t want,
                           std::vector<uint64_t> &out) {
    std::vector<std::vector<uint64_t> > by_set(BLOCK_CACHE_SETS);
    for (uint64_t pc = 0x1000; pc < 0x1000 + 4 * 40000; pc += 4) {
        uint32_t s = set_of(scratch, pc);
        if (s == UINT32_MAX) {
            continue;
        }
        by_set[s].push_back(pc);
        if (by_set[s].size() >= want) {
            out = by_set[s];
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------

int main(void) {
    printf("=== DBT block cache ===\n\n");

    dbt_state_t *scratch = make_state();
    dbt_state_t *dbt = make_state();

    std::vector<uint64_t> pcs;
    if (!find_colliding(scratch, BLOCK_CACHE_WAYS + 1, pcs)) {
        printf("FAIL: could not find %zu guest PCs sharing a set\n",
               BLOCK_CACHE_WAYS + 1);
        return 1;
    }
    const uint32_t SET = set_of(scratch, pcs[0]);
    printf("  using set %u, colliding PCs:", SET);
    for (size_t i = 0; i < pcs.size(); i++) {
        printf(" 0x%llX", static_cast<unsigned long long>(pcs[i]));
    }
    printf("\n\n");

    // --- 1. The #1153 regression -------------------------------------
    // The intrinsic shape: the same guest_pc inserted twice with the same
    // code pointer. It must occupy one way, not two.
    {
        clear_cache(dbt);
        dbt_cache_insert(dbt, pcs[0], code_ptr(0));
        dbt_cache_insert(dbt, pcs[0], code_ptr(0));   // caller's second insert
        check(1 == occupied(dbt, SET),
              "double insert of one pc must occupy exactly one way");
        block_entry_t *be = dbt_cache_lookup(dbt, pcs[0]);
        check(be && be->native_code == code_ptr(0),
              "double-inserted pc must still look up to its code");
    }

    // --- 2. The consequence the issue names --------------------------
    // Capacity loss needs TWO intrinsics in one set, not one. With a single
    // duplicate the way-0 FIFO evicts the original and the copy at way 1
    // keeps that pc reachable, so nothing is lost -- an earlier draft of
    // this test used one intrinsic and passed with the bug still in.
    //
    // Two duplicates fill all four ways with two distinct blocks. The next
    // two distinct blocks then evict each other, so a set that was asked to
    // hold only four distinct blocks drops one.
    {
        clear_cache(dbt);
        dbt_cache_insert(dbt, pcs[0], code_ptr(0));
        dbt_cache_insert(dbt, pcs[0], code_ptr(0));   // intrinsic A, twice
        dbt_cache_insert(dbt, pcs[1], code_ptr(1));
        dbt_cache_insert(dbt, pcs[1], code_ptr(1));   // intrinsic B, twice
        dbt_cache_insert(dbt, pcs[2], code_ptr(2));
        dbt_cache_insert(dbt, pcs[3], code_ptr(3));

        check(BLOCK_CACHE_WAYS == static_cast<size_t>(occupied(dbt, SET)),
              "a set holding two intrinsics must still fill four ways");
        bool all_resident = true;
        for (size_t i = 0; i < BLOCK_CACHE_WAYS; i++) {
            block_entry_t *be = dbt_cache_lookup(dbt, pcs[i]);
            if (!be || be->native_code != code_ptr(static_cast<int>(i))) {
                all_resident = false;
                printf("       pc 0x%llX was evicted\n",
                       static_cast<unsigned long long>(pcs[i]));
            }
        }
        check(all_resident,
              "four distinct blocks must all fit a 4-way set, "
              "however many times each was inserted");
    }

    // --- 3. Re-insert updates in place -------------------------------
    // Retranslation to a new address must move the entry, not duplicate it.
    {
        clear_cache(dbt);
        dbt_cache_insert(dbt, pcs[0], code_ptr(1));
        dbt_cache_insert(dbt, pcs[0], code_ptr(2));
        check(1 == occupied(dbt, SET),
              "re-insert at a new address must not add a way");
        block_entry_t *be = dbt_cache_lookup(dbt, pcs[0]);
        check(be && be->native_code == code_ptr(2),
              "re-insert must publish the new code pointer");
    }

    // --- 4. Distinct PCs are still distinct ---------------------------
    // The dedupe must key on guest_pc, not collapse a whole set.
    {
        clear_cache(dbt);
        for (size_t i = 0; i < BLOCK_CACHE_WAYS; i++) {
            dbt_cache_insert(dbt, pcs[i], code_ptr(static_cast<int>(i)));
        }
        check(BLOCK_CACHE_WAYS == static_cast<size_t>(occupied(dbt, SET)),
              "four distinct pcs must occupy four ways");
        bool ok = true;
        for (size_t i = 0; i < BLOCK_CACHE_WAYS; i++) {
            block_entry_t *be = dbt_cache_lookup(dbt, pcs[i]);
            if (!be || be->native_code != code_ptr(static_cast<int>(i))) {
                ok = false;
            }
        }
        check(ok, "each distinct pc must look up to its own code");
    }

    // --- 5. Eviction still happens when genuinely full ----------------
    // The fix must not turn a full set into a set that refuses new blocks.
    {
        clear_cache(dbt);
        for (size_t i = 0; i <= BLOCK_CACHE_WAYS; i++) {
            dbt_cache_insert(dbt, pcs[i], code_ptr(static_cast<int>(i)));
        }
        check(BLOCK_CACHE_WAYS == static_cast<size_t>(occupied(dbt, SET)),
              "an over-full set must still hold exactly 4 ways");
        block_entry_t *be = dbt_cache_lookup(dbt, pcs[BLOCK_CACHE_WAYS]);
        check(be && be->native_code
                    == code_ptr(static_cast<int>(BLOCK_CACHE_WAYS)),
              "the newest block must be resident after eviction");
        check(nullptr == dbt_cache_lookup(dbt, pcs[0]),
              "way 0 must be the one evicted (FIFO)");
    }

    // --- 6. guest_pc 0 is the empty sentinel --------------------------
    // The dedupe match mirrors dbt_cache_lookup (native_code non-null) so an
    // empty way is never mistaken for an entry for pc 0. Registration skips
    // guest_addr 0, but the cache must not corrupt itself if it ever sees it.
    {
        clear_cache(dbt);
        const uint32_t s0 = set_of(scratch, 0);
        dbt_cache_insert(dbt, 0, code_ptr(7));
        check(1 == occupied(dbt, s0),
              "insert of pc 0 must occupy exactly one way");
        dbt_cache_insert(dbt, 0, code_ptr(8));
        check(1 == occupied(dbt, s0),
              "re-insert of pc 0 must dedupe, not consume a second way");
        block_entry_t *be = dbt_cache_lookup(dbt, 0);
        check(be && be->native_code == code_ptr(8),
              "pc 0 must look up to its latest code");
    }

    delete dbt;
    delete scratch;

    printf("\n=== dbt cache: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
