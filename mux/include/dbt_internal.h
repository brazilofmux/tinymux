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
