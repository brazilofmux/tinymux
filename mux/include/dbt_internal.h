/*! \file dbt_internal.h
 * \brief Internal interface between shared DBT code and per-platform backends.
 *
 * This header declares:
 *   - Struct definitions shared between dbt.cpp and the backend
 *   - Functions exported by the shared code (called by backends)
 *   - Functions exported by backends (called by shared code)
 *
 * Not part of the public API — only included by dbt.cpp and dbt_*_*.cpp.
 */

#ifndef DBT_INTERNAL_H
#define DBT_INTERNAL_H

#include "dbt.h"
#include "dbt_decoder.h"

// ---------------------------------------------------------------
// Register cache
// ---------------------------------------------------------------

static constexpr int RC_NUM_SLOTS = 8;
static constexpr int RC_NUM_PINNED = 4;

struct rc_slot_t {
    int guest_reg;  // -1 = free
    int dirty;
    int last_use;
    int pinned;     // if true, never evict
};

struct reg_cache_t {
    rc_slot_t slots[RC_NUM_SLOTS];
    int clock;
};

// ---------------------------------------------------------------
// Warm-loop register-pressure analysis (shared by all backends)
// ---------------------------------------------------------------
//
// A "warm-loop" superblock keeps the loop body's guest registers resident
// in host registers across the back-edge (warm_entry) instead of flushing
// and reloading through ctx every iteration.  The register cache has only
// (RC_NUM_SLOTS - RC_NUM_PINNED) free slots for non-pinned guest registers
// (a0-a3 are pinned).  If a loop references more non-pinned registers than
// that, the cache cannot hold a consistent guest->host mapping across
// iterations: a loop-invariant, never-dirty register (e.g. the magic
// reciprocal divisor in an itoa /10 loop) is pre-loaded, read at the top
// with no reload, then evicted mid-body.  rc_flush at the back-edge saves
// only dirty registers, so later iterations read a stale host register and
// corrupt the result.  When the loop over-commits, the backend falls back
// to ordinary per-iteration dispatch, which is always correct.
//
// These helpers live in the shared header so every backend computes the
// preload set, the referenced-register set, and the over-commit decision
// identically — the guard was originally added to only one backend and
// silently missing from the others (see ISSUES.md /
// dbt_test.cpp::test_selfloop_register_pressure).

// Mark the guest source registers (rs1/rs2) read by one instruction in
// used[1..31].  This set drives the warm-loop preload: the registers worth
// pre-loading at warm_entry are the ones *read* early in the loop, so it
// must track sources only.  Marking destinations here would preload
// write-only registers and skew the warm cache layout.
static inline void rc_mark_used(const rv64_insn_t &si, int used[32]) {
    if (si.rs1) used[si.rs1] = 1;
    if ((si.opcode == OP_REG || si.opcode == OP_BRANCH || si.opcode == OP_STORE)
        && si.rs2)
        used[si.rs2] = 1;
}

// Mark every guest register one instruction references — sources AND the
// integer destination — in referenced[1..31].  This drives the over-commit
// guard, which must count a cache slot for each distinct register the loop
// touches: a destination-only register still occupies a slot, so omitting it
// under-counts the loop's register pressure.  rd is taken only for opcodes
// that actually write an integer register — for STORE/BRANCH/SYSTEM the rd
// field holds immediate bits, not a destination.
static inline void rc_mark_referenced(const rv64_insn_t &si, int referenced[32]) {
    rc_mark_used(si, referenced);
    switch (si.opcode) {
    case OP_LUI: case OP_AUIPC: case OP_JAL: case OP_JALR:
    case OP_LOAD: case OP_IMM: case OP_REG: case OP_IMM32: case OP_REG32:
        if (si.rd) referenced[si.rd] = 1;
        break;
    default:
        break;
    }
}

// Return true if a self-loop body whose referenced registers are recorded
// in referenced[1..31] needs more non-pinned cache slots than are free,
// meaning a warm superblock would over-commit the cache.
static inline bool rc_loop_overcommits(const int referenced[32],
                                       const int *pinned_guest,
                                       int num_pinned) {
    const int free_slots = RC_NUM_SLOTS - num_pinned;
    int nonpinned_used = 0;
    for (int r = 1; r < 32; r++) {
        if (!referenced[r]) continue;
        bool pinned = false;
        for (int p = 0; p < num_pinned; p++)
            if (pinned_guest[p] == r) { pinned = true; break; }
        if (!pinned) nonpinned_used++;
    }
    return nonpinned_used > free_slots;
}

// ---------------------------------------------------------------
// FP register cache
// ---------------------------------------------------------------

static constexpr int FC_NUM_SLOTS = 6;

struct fc_slot_t {
    int guest_freg;  // -1 = free
    int dirty;
    int last_use;
};

struct fp_cache_t {
    fc_slot_t slots[FC_NUM_SLOTS];
    int clock;
};

// ---------------------------------------------------------------
// Superblock side exits
// ---------------------------------------------------------------

static constexpr int MAX_SIDE_EXITS = 8;

struct side_exit_t {
    uint32_t jcc_patch;         // offset of Jcc rel32 displacement
    uint64_t target_pc;         // guest PC of the taken path
    uint64_t expected_next_pc;  // expected next_pc for inline CALL cold exits
    rc_slot_t snapshot[RC_NUM_SLOTS];
};

// ---------------------------------------------------------------
// Direct JALR flow result
// ---------------------------------------------------------------

enum class direct_jalr_flow_t {
    inline_call_done,
    tail_call,
    chained_exit,
};

// ---------------------------------------------------------------
// Functions exported by the SHARED code (dbt.cpp)
// Called by backends.
// ---------------------------------------------------------------

// Block cache.
block_entry_t *dbt_cache_lookup(dbt_state_t *dbt, uint64_t pc);
void dbt_cache_insert(dbt_state_t *dbt, uint64_t pc, uint8_t *code);

// Block chain resolution.
void dbt_backpatch_chains(dbt_state_t *dbt, uint64_t guest_pc,
                           uint8_t *native_code);

// Direct JALR target resolution (pure computation).
bool dbt_resolve_direct_jalr_target(uint64_t pc,
                                     const rv64_insn_t &insn,
                                     const rv64_insn_t &next,
                                     uint64_t *target_out,
                                     uint64_t *return_pc_out);

// Trace helpers.
bool dbt_trace_translate_enabled(const dbt_state_t *dbt, uint64_t guest_pc);
void dbt_trace_translate_pc(dbt_state_t *dbt, uint64_t guest_pc,
                             const char *fmt, ...);
void dbt_trace_translate(dbt_state_t *dbt, const char *fmt, ...);
void dbt_trace_fusion(dbt_state_t *dbt, uint64_t pc, const char *kind);

// ---------------------------------------------------------------
// Functions exported by the BACKEND (dbt_x64_sysv.cpp, etc.)
// Called by shared code.
// ---------------------------------------------------------------

// Emit the trampoline at the start of the code buffer.
void dbt_backend_emit_trampoline(dbt_state_t *dbt);

// Translate a single RV64 block to native host code.
// Returns pointer to native code, or nullptr on failure.
uint8_t *dbt_backend_translate_block(dbt_state_t *dbt, uint64_t guest_pc);

// Backpatch a single JMP/branch instruction in the code buffer.
// Platform-specific because the patch format differs (x86-64 rel32
// vs AArch64 B offset field).
void dbt_backend_backpatch_jmp(uint8_t *code_buf, uint32_t patch_offset,
                                uint8_t *target);

#endif // DBT_INTERNAL_H
