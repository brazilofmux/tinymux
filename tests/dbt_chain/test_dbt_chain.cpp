// Unit tests for the DBT block-chaining patch encode/decode pair:
// dbt_backend_backpatch_jmp() and dbt_backend_decode_jmp_target().
//
// dbt_resolve_chains() decides whether a recorded patch site is still
// pointing at its slow-path stub by decoding the site and comparing the
// result against stub_offset.  That decode has to be the exact inverse of
// the backpatch that wrote the site.  It was not: the decode was an x86-64
// rel32 computation with no architecture guard, so on AArch64 (a B imm26
// word, displacement in words, measured from the instruction rather than
// from the end of a displacement field) it produced noise, never equalled
// stub_offset, and every site took the already_ok branch (#1152, PR #1244).
//
// A bug in one backend's encoding survived because nothing exercised that
// backend.  So this harness does not test only the host's backend: all
// three (a64_sysv, x64_sysv, x64_win64) are compiled into this one binary
// with their colliding strong symbols renamed on the compiler command line
// (see the Makefile).  Every developer, on every host, tests every
// encoding -- including x64_win64, which no non-Windows build otherwise
// touches.
//
// Round-trip identity alone would be too weak: it still holds if the
// encoder and decoder are wrong in the same way.  The golden vectors below
// are the independent anchor -- each was checked against a real
// disassembler (aarch64 as/objdump for the B imm26 forms,
// x86_64-linux-gnu-objdump for the JMP rel32 forms), not derived from the
// code under test.
//
// Build/run: make test   (no dependency on a built netmux; the backend
// sources are compiled directly)

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>

#include "dbt.h"
#include "dbt_internal.h"

// ---------------------------------------------------------------
// Backend entry points
// ---------------------------------------------------------------
//
// Renamed per translation unit by -D on the command line, so the three
// backends can coexist.  Signatures must match dbt_internal.h exactly or
// the mangled names will not resolve.
//
#define DECLARE_BACKEND(p)                                                  \
    void p##_backpatch_jmp(uint8_t *code_buf, uint32_t patch_offset,        \
                            uint8_t *target);                               \
    uint32_t p##_decode_jmp_target(const uint8_t *code_buf,                 \
                                    uint32_t patch_offset)

DECLARE_BACKEND(a64);
DECLARE_BACKEND(x64sysv);
DECLARE_BACKEND(win64);

// ---------------------------------------------------------------
// Shared-code stubs
// ---------------------------------------------------------------
//
// Linking a backend object pulls in the whole translation unit, so the
// shared-code symbols it references have to resolve.  The chaining pair is
// leaf arithmetic over a byte buffer and reaches none of them.  Each stub
// aborts rather than returning a plausible value: if a future refactor
// makes backpatch or decode call into shared code, this test dies loudly
// instead of quietly testing something else.
//
static void die_unreachable(const char *who) {
    fprintf(stderr, "test_dbt_chain: %s was called from the chaining path.\n"
            "The encode/decode pair is no longer leaf arithmetic; these "
            "stubs are no longer safe.\n", who);
    abort();
}

block_entry_t *dbt_cache_lookup(dbt_state_t *, uint64_t) {
    die_unreachable("dbt_cache_lookup");
    return nullptr;
}

void dbt_cache_insert(dbt_state_t *, uint64_t, uint8_t *) {
    die_unreachable("dbt_cache_insert");
}

void dbt_trace_fusion(dbt_state_t *, uint64_t, const char *) {
    die_unreachable("dbt_trace_fusion");
}

void dbt_trace_translate_pc(dbt_state_t *, uint64_t, const char *, ...) {
    die_unreachable("dbt_trace_translate_pc");
}

bool dbt_trace_translate_enabled(const dbt_state_t *, uint64_t) {
    die_unreachable("dbt_trace_translate_enabled");
    return false;
}

bool dbt_resolve_direct_jalr_target(uint64_t, const rv64_insn_t &,
                                    const rv64_insn_t &, uint64_t *,
                                    uint64_t *) {
    die_unreachable("dbt_resolve_direct_jalr_target");
    return false;
}

// ---------------------------------------------------------------
// Test framework
// ---------------------------------------------------------------

static int g_pass = 0;
static int g_fail = 0;

// The sweeps run millions of round trips.  A broken backend would print a
// line per failure and bury the summary, so report the first few in full
// and count the rest.
static const int MAX_REPORTED = 8;

// The arena is larger than CODE_BUF_SIZE.  The tail is a canary: the
// out-of-range guard test drives backpatch past the end of the code buffer,
// and without a working guard that is a real out-of-bounds write.  The
// padding turns "the guard is gone" into a detected failure rather than
// memory corruption in the test itself.
static const size_t CANARY = 64;
static const uint8_t CANARY_BYTE = 0xA5;
static uint8_t g_arena[CODE_BUF_SIZE + CANARY];
static uint8_t *const g_buf = g_arena;

static void arena_reset(void) {
    memset(g_arena, 0, CODE_BUF_SIZE);
    memset(g_arena + CODE_BUF_SIZE, CANARY_BYTE, CANARY);
}

static bool canary_intact(void) {
    for (size_t i = 0; i < CANARY; i++) {
        if (g_arena[CODE_BUF_SIZE + i] != CANARY_BYTE) {
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------
// Backend table
// ---------------------------------------------------------------

// Structural check on the four bytes a backpatch wrote.  AArch64 writes a
// whole instruction, so the opcode field is checkable; the x86-64 backends
// write a bare rel32 displacement field (the 0xE9 opcode was emitted
// earlier), so every bit pattern is legal and there is nothing to check.
static bool a64_is_uncond_branch(const uint8_t *at) {
    uint32_t inst;
    memcpy(&inst, at, 4);
    return (inst & 0xFC000000u) == 0x14000000u;
}

struct backend_t {
    const char *name;
    void (*backpatch)(uint8_t *, uint32_t, uint8_t *);
    uint32_t (*decode)(const uint8_t *, uint32_t);

    // Displacement granularity the encoding can represent.  AArch64 stores
    // the displacement in words (byte_diff >> 2), so only 4-aligned
    // displacements survive a round trip -- which is the whole domain, since
    // every AArch64 instruction and every block entry point is 4-aligned.
    // The x86-64 rel32 is a byte count, and the DBT's patch sites are not
    // aligned at all (the field starts one byte after the 0xE9), so the
    // contract there is every displacement.
    uint32_t align;

    bool (*well_formed)(const uint8_t *);  // nullptr = nothing to check
};

static const backend_t BACKENDS[] = {
    { "a64_sysv",  a64_backpatch_jmp,     a64_decode_jmp_target,
      4, a64_is_uncond_branch },
    { "x64_sysv",  x64sysv_backpatch_jmp, x64sysv_decode_jmp_target,
      1, nullptr },
    { "x64_win64", win64_backpatch_jmp,   win64_decode_jmp_target,
      1, nullptr },
};
static const size_t NUM_BACKENDS = sizeof(BACKENDS) / sizeof(BACKENDS[0]);

// ---------------------------------------------------------------
// 1. Golden vectors
// ---------------------------------------------------------------
//
// The non-circular anchor.  Round-trip identity cannot catch an encoder and
// decoder that are wrong the same way; these exact bytes can.  Every vector
// was produced by a real disassembler, not by this code:
//
//   aarch64:  $ as; objdump -d
//       b .+0x100  ->  14000040
//       b .-0x100  ->  17ffffc0
//       b .        ->  14000000
//
//   x86-64:   $ x86_64-linux-gnu-objdump -D -b binary -m i386:x86-64
//       e9 fc 00 00 00  at 0x0ff  ->  jmp 0x200   (disp field at 0x100)
//       e9 fc fe ff ff  at 0x1ff  ->  jmp 0x100   (disp field at 0x200)
//       e9 fc ff ff ff  at 0x03f  ->  jmp 0x40    (disp field at 0x040)
//
// Note the x86 offsets: dbt_backend_backpatch_jmp is handed the offset of
// the displacement field, one byte past the 0xE9 opcode, which is why the
// disassembly addresses above are one lower.

struct golden_t {
    uint32_t patch_off;
    uint32_t target_off;
    uint8_t want[4];
    const char *note;
};

static const golden_t GOLDEN_A64[] = {
    { 0x100, 0x200, { 0x40, 0x00, 0x00, 0x14 }, "b .+0x100" },
    { 0x200, 0x100, { 0xC0, 0xFF, 0xFF, 0x17 }, "b .-0x100" },
    { 0x040, 0x040, { 0x00, 0x00, 0x00, 0x14 }, "b .       (self)" },
};

static const golden_t GOLDEN_X64[] = {
    { 0x100, 0x200, { 0xFC, 0x00, 0x00, 0x00 }, "jmp forward 0x100" },
    { 0x200, 0x100, { 0xFC, 0xFE, 0xFF, 0xFF }, "jmp back 0x100" },
    { 0x040, 0x040, { 0xFC, 0xFF, 0xFF, 0xFF }, "jmp to self" },
};

static void test_golden(const backend_t &be, const golden_t *gold,
                        size_t n) {
    for (size_t i = 0; i < n; i++) {
        const golden_t &g = gold[i];
        arena_reset();
        be.backpatch(g_buf, g.patch_off, g_buf + g.target_off);

        if (0 != memcmp(g_buf + g.patch_off, g.want, 4)) {
            g_fail++;
            printf("FAIL: %s golden %s: patch 0x%X -> 0x%X encoded "
                   "%02X %02X %02X %02X, want %02X %02X %02X %02X\n",
                   be.name, g.note, g.patch_off, g.target_off,
                   g_buf[g.patch_off], g_buf[g.patch_off + 1],
                   g_buf[g.patch_off + 2], g_buf[g.patch_off + 3],
                   g.want[0], g.want[1], g.want[2], g.want[3]);
            continue;
        }

        // The decode has to agree with the disassembler too, not merely
        // with the encoder.
        uint32_t got = be.decode(g_buf, g.patch_off);
        if (got != g.target_off) {
            g_fail++;
            printf("FAIL: %s golden %s: decode of the correct bytes gave "
                   "0x%X, want 0x%X\n", be.name, g.note, got, g.target_off);
            continue;
        }
        g_pass++;
    }
}

// ---------------------------------------------------------------
// 2. Round trip across the displacement range
// ---------------------------------------------------------------
//
// Exhaustive at the backend's own granularity: for each base, every legal
// target in the 1 MB code buffer.  Covers forward, backward and zero
// displacement, and both extremes -- offset 0 reaching the far end and the
// last legal offset reaching back to 0.

static void test_roundtrip(const backend_t &be) {
    static const uint32_t BASES[] = {
        0,
        4,
        0x1000,
        static_cast<uint32_t>(CODE_BUF_SIZE / 2),
        static_cast<uint32_t>(CODE_BUF_SIZE) - 4,   // last legal site
    };
    static const size_t NUM_BASES = sizeof(BASES) / sizeof(BASES[0]);

    int reported = 0;
    long failures = 0;
    long checks = 0;

    arena_reset();
    for (size_t b = 0; b < NUM_BASES; b++) {
        const uint32_t off = BASES[b];
        for (uint32_t tgt = 0;
             tgt < static_cast<uint32_t>(CODE_BUF_SIZE);
             tgt += be.align) {
            be.backpatch(g_buf, off, g_buf + tgt);
            uint32_t got = be.decode(g_buf, off);
            checks++;

            bool ok = (got == tgt);
            if (ok && nullptr != be.well_formed) {
                ok = be.well_formed(g_buf + off);
            }
            if (ok) {
                continue;
            }

            failures++;
            if (reported < MAX_REPORTED) {
                reported++;
                printf("FAIL: %s round trip: site 0x%X -> target 0x%X "
                       "decoded 0x%X (bytes %02X %02X %02X %02X)\n",
                       be.name, off, tgt, got,
                       g_buf[off], g_buf[off + 1],
                       g_buf[off + 2], g_buf[off + 3]);
            }
        }
    }

    if (!canary_intact()) {
        failures++;
        printf("FAIL: %s round trip wrote past the end of the code buffer\n",
               be.name);
    }

    if (0 == failures) {
        g_pass++;
        printf("  %-9s round trip: %ld/%ld exact (step %u)\n",
               be.name, checks, checks, be.align);
    } else {
        g_fail++;
        printf("FAIL: %s round trip: %ld of %ld failed (%d shown)\n",
               be.name, failures, checks, reported);
    }
}

// ---------------------------------------------------------------
// 3. The predicate dbt_resolve_chains actually depends on
// ---------------------------------------------------------------
//
// resolve_chains does exactly this:
//
//     uint32_t cur_target = dbt_backend_decode_jmp_target(code_buf, jmp_off);
//     if (cur_target != patches[i].stub_offset) { already_ok++; continue; }
//
// So the decode has to distinguish "still on its stub" from "already
// backpatched to a real block".  #1152 was the first half failing on
// AArch64: the site was still on its stub, the decode said otherwise, and
// the site was never resolved.  The second half matters just as much in the
// other direction -- a decode that spuriously equalled stub_offset would
// re-patch a live site.

static void test_resolve_predicate(const backend_t &be) {
    struct site_t {
        uint32_t jmp_off;
        uint32_t stub_off;
        uint32_t real_off;
        const char *note;
    };
    // Shapes the DBT actually emits: the stub sits just past the jump site
    // in the same block, the real target is another block either later or
    // earlier in the arena.
    static const site_t SITES[] = {
        { 0x1000,  0x1004,  0x40000, "stub adjacent, target forward" },
        { 0x40000, 0x40010, 0x1000,  "stub adjacent, target backward" },
        { 0x800,   0xF0000, 0x804,   "stub far, target adjacent" },
        { 0,       4,       static_cast<uint32_t>(CODE_BUF_SIZE) - 4,
          "first site, target at the far end" },
        { static_cast<uint32_t>(CODE_BUF_SIZE) - 4, 0x20, 0,
          "last site, target at zero" },
    };
    static const size_t NUM_SITES = sizeof(SITES) / sizeof(SITES[0]);

    for (size_t i = 0; i < NUM_SITES; i++) {
        const site_t &s = SITES[i];
        arena_reset();

        // Unresolved: emitted pointing at the slow-path stub.
        be.backpatch(g_buf, s.jmp_off, g_buf + s.stub_off);
        uint32_t cur = be.decode(g_buf, s.jmp_off);
        if (cur != s.stub_off) {
            g_fail++;
            printf("FAIL: %s resolve predicate (%s): site on its stub 0x%X "
                   "decoded 0x%X -- resolve_chains would skip it as "
                   "already_ok\n", be.name, s.note, s.stub_off, cur);
            continue;
        }

        // Resolved: backpatched to the real block.  Must no longer look
        // like the stub, and must decode to the block itself.
        be.backpatch(g_buf, s.jmp_off, g_buf + s.real_off);
        cur = be.decode(g_buf, s.jmp_off);
        if (cur == s.stub_off) {
            g_fail++;
            printf("FAIL: %s resolve predicate (%s): resolved site still "
                   "decodes as stub 0x%X -- resolve_chains would re-patch "
                   "live code\n", be.name, s.note, s.stub_off);
            continue;
        }
        if (cur != s.real_off) {
            g_fail++;
            printf("FAIL: %s resolve predicate (%s): resolved site decoded "
                   "0x%X, want 0x%X\n", be.name, s.note, cur, s.real_off);
            continue;
        }
        g_pass++;
    }
}

// ---------------------------------------------------------------
// 4. The out-of-range guard (#1147)
// ---------------------------------------------------------------
//
// Stale patch sites from a failed translate can carry offsets past the end
// of the arena.  backpatch refuses those rather than writing out of bounds.
// (decode has no such guard by design -- dbt_resolve_chains range-checks
// jmp_off before calling it.  If that caller-side check is ever removed,
// this is where to add the matching guard.)

static void test_oob_guard(const backend_t &be) {
    const uint32_t SIZE = static_cast<uint32_t>(CODE_BUF_SIZE);
    // Only just past the edge: an unguarded write from a wildly out of
    // range offset would land outside the canary and corrupt the process
    // rather than being detected.
    static const uint32_t BAD[] = { SIZE - 3, SIZE - 2, SIZE - 1, SIZE,
                                    SIZE + 4, SIZE + 32 };
    static const size_t NUM_BAD = sizeof(BAD) / sizeof(BAD[0]);

    for (size_t i = 0; i < NUM_BAD; i++) {
        arena_reset();
        // Mark the tail so a partial write inside the buffer shows up too.
        memset(g_buf + SIZE - 8, CANARY_BYTE, 8);

        be.backpatch(g_buf, BAD[i], g_buf + 0x1000);

        bool tail_ok = true;
        for (uint32_t k = SIZE - 8; k < SIZE; k++) {
            if (g_buf[k] != CANARY_BYTE) {
                tail_ok = false;
            }
        }
        if (!tail_ok || !canary_intact()) {
            g_fail++;
            printf("FAIL: %s out-of-range guard: backpatch at offset 0x%X "
                   "(buffer is 0x%X) wrote to the buffer%s\n",
                   be.name, BAD[i], SIZE,
                   canary_intact() ? "" : " and past the end of it");
            continue;
        }
        g_pass++;
    }

    // The last in-range offset must still be accepted -- an off-by-one in
    // the guard that rejected it would silently stop chaining the final
    // site in the arena.
    arena_reset();
    be.backpatch(g_buf, SIZE - 4, g_buf + 0x1000);
    if (be.decode(g_buf, SIZE - 4) != 0x1000) {
        g_fail++;
        printf("FAIL: %s out-of-range guard rejected the last legal site "
               "at 0x%X\n", be.name, SIZE - 4);
    } else {
        g_pass++;
    }
}

// ---------------------------------------------------------------

int main(void) {
    printf("=== DBT chain patch encode/decode, all %zu backends ===\n\n",
           NUM_BACKENDS);

    for (size_t i = 0; i < NUM_BACKENDS; i++) {
        const backend_t &be = BACKENDS[i];
        printf("%s:\n", be.name);

        // Golden vectors are per-encoding; the two x86-64 backends share
        // the rel32 form.
        if (0 == strcmp(be.name, "a64_sysv")) {
            test_golden(be, GOLDEN_A64,
                        sizeof(GOLDEN_A64) / sizeof(GOLDEN_A64[0]));
        } else {
            test_golden(be, GOLDEN_X64,
                        sizeof(GOLDEN_X64) / sizeof(GOLDEN_X64[0]));
        }

        test_roundtrip(be);
        test_resolve_predicate(be);
        test_oob_guard(be);
        printf("\n");
    }

    printf("=== dbt chain: %d passed, %d failed ===\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
