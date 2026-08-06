/*! \file test_emit_bounds.cpp
 * \brief Emitter backpatches must not write past a full code buffer.
 *
 * The emitters run in dropped-write mode on overflow: emit_byte/emit_bytes
 * skip the store when offset >= capacity but keep advancing `offset`, and
 * translate_block bails post-hoc on offset > capacity, discarding the
 * overflowed block.  That makes every RAW backpatch (a direct e->buf[pos]
 * store rather than an emit_* call) a landmine: its patch site can sit past
 * capacity, and an unguarded store there writes out of bounds.
 *
 * #830 added the guard to emit_patch_rel32.  emit_loop_budget_check (#1571)
 * arrived later with a hand-rolled short-jump backpatch and no guard — the
 * one raw backpatch in the x64 emitter without one.  It was latent while
 * every program switch reset the DBT (the buffer stayed far from full), and
 * became a live SIGSEGV the day #2129's code slots let translations
 * accumulate toward the cap: deterministic crash at 32 distinct programs
 * round-robin, faulting in dbt_backend_translate_block on Linux/x86-64.
 *
 * The a64 emitter routes its budget-check backpatch through emit_patch_b19,
 * which has carried the #830 guard from the start — which is why only
 * x86-64 hosts could crash, and why an arm64 box could not find this.
 * (Not exercised here: both emit headers define the same file-static
 * emitter symbols, so one TU cannot include both, and namespace tricks
 * around transitive system includes are not worth risking the MSVC build
 * this suite also runs under.)
 *
 * Host-independent: emitters only write bytes into a caller-supplied
 * buffer; nothing is executed.  Runs on every host like test_chain, and
 * that is the point — an arm64 box compiling this file still checks the
 * x64 emitter it cannot execute.
 */

#include "dbt_emit_x64.h"

#include <cstdio>
#include <cstring>

static int failures = 0;

#define CHECK(cond, name)                                                  \
    do {                                                                   \
        if (cond) {                                                        \
            printf("  ok   %s\n", name);                                   \
        } else {                                                           \
            printf("  FAIL %s\n", name);                                   \
            failures++;                                                    \
        }                                                                  \
    } while (0)

// A capacity-sized window inside a larger allocation, with sentinel bytes
// after it.  Any store past capacity lands in the sentinel and is detected
// byte-for-byte — the unit-test analogue of the mmap'd buffer edge the live
// crash fell off.
struct guarded_buf {
    static const uint32_t CAP = 64;
    static const uint32_t SENTINEL = 64;
    uint8_t raw[CAP + SENTINEL];

    emit_t make() {
        memset(raw, 0xAA, sizeof(raw));
        emit_t e;
        e.buf = raw;
        e.offset = 0;
        e.capacity = CAP;
        return e;
    }
    bool sentinel_intact() const {
        for (uint32_t i = CAP; i < CAP + SENTINEL; i++) {
            if (raw[i] != 0xAA) return false;
        }
        return true;
    }
};

int main() {
    printf("test_emit_bounds: raw backpatches vs a full code buffer\n");

    // 1. The regression: budget check emitted entirely past capacity.
    //    Before the guard, the jnz backpatch stored through e->buf
    //    unconditionally — here into the sentinel; on the live buffer,
    //    past the mapping.
    {
        guarded_buf g;
        emit_t e = g.make();
        e.offset = guarded_buf::CAP;          // buffer already full
        emit_loop_budget_check(&e, 0x1234);
        CHECK(g.sentinel_intact(),
              "budget check at offset==capacity writes nothing");
        CHECK(e.offset > e.capacity,
              "offset still advances (translate_block's bail signal)");
    }

    // 2. Patch site exactly at the boundary: the jnz placeholder is the
    //    first dropped write, so jnz_disp == capacity.  The guard's strict
    //    `<` must refuse it.
    {
        guarded_buf g;
        emit_t e = g.make();
        // jnz_disp = entry_offset + 8 (7 bytes of dec, 1 of jnz opcode).
        e.offset = guarded_buf::CAP - 8;
        emit_loop_budget_check(&e, 0x1234);
        CHECK(g.sentinel_intact(),
              "budget check with patch site at capacity writes nothing OOB");
    }

    // 3. In-bounds behaviour unchanged: the short-jump displacement is
    //    patched over the placeholder and spans exactly the exit sequence.
    {
        guarded_buf g;
        emit_t e = g.make();
        emit_loop_budget_check(&e, 0x1234);
        const uint32_t jnz_disp = 8;          // after 7-byte dec + jnz opcode
        uint8_t disp = g.raw[jnz_disp];
        CHECK(disp != 0
              && disp == static_cast<uint8_t>(e.offset - jnz_disp - 1),
              "in-bounds budget check patches the short jump over the exit");
        CHECK(g.sentinel_intact(), "in-bounds budget check stays in bounds");
    }

    // 4. The #830 guard on rel32 patches holds (regression pin for the
    //    guard this fix copies).
    {
        guarded_buf g;
        emit_t e = g.make();
        e.offset = guarded_buf::CAP - 2;      // room for opcode + 1 byte
        emit_byte(&e, 0xE9);
        uint32_t patch = emit_pos(&e);
        emit_u32(&e, 0);                      // rel32 placeholder straddles cap
        emit_patch_rel32(&e, patch, emit_pos(&e));
        CHECK(g.sentinel_intact(), "rel32 patch straddling capacity is skipped");
    }

    if (failures) {
        printf("test_emit_bounds: %d FAILED\n", failures);
        return 1;
    }
    printf("test_emit_bounds: all passed\n");
    return 0;
}
