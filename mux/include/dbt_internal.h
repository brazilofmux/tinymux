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
// Out-of-range guest pointer sink (#1151)
// ---------------------------------------------------------------
//
// Defined in dbt.cpp; backends bake its address into the intrinsic stubs.
// Its address is stable for the process lifetime, unlike memory_size,
// which dbt_reset can change — that is why the bound is read from ctx at
// run time while this pointer is an immediate.
//
static constexpr size_t DBT_SAFE_PAGE_SIZE = 4096;
extern uint8_t g_dbt_safe_page[DBT_SAFE_PAGE_SIZE];

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
// OP_REG32 (ADDW/SUBW/SLLW/M*W) reads rs2 as an integer register exactly
// as OP_REG does; leaving it out under-counted the loop's pressure by one
// slot per *W instruction, which is enough to admit a superblock that then
// evicts a preloaded register.  The FP opcodes are deliberately absent:
// their rs2 names an FP register, which the integer cache does not hold.
static inline void rc_mark_used(const rv64_insn_t &si, int used[32]) {
    if (si.rs1) used[si.rs1] = 1;
    if ((si.opcode == OP_REG || si.opcode == OP_REG32
         || si.opcode == OP_BRANCH || si.opcode == OP_STORE)
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
    case OP_FP:
        // Most OP_FP forms write an FP register, which costs no integer
        // slot.  Three families write an *integer* rd and do occupy one:
        // the comparisons (FEQ/FLT/FLE), the float→int converts
        // (FCVT.W/WU/L/LU) and FCLASS/FMV.X.  Omitting them under-counts
        // the same way OP_REG32's rs2 did.
        switch (si.funct7 >> 2) {
        case FP_FCMP: case FP_FCVTW: case FP_FCLASS:
            if (si.rd) referenced[si.rd] = 1;
            break;
        default:
            break;
        }
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
// May a block exit to target_pc be chained straight into native code?
//
// No, when the target is at or below the block being translated: that is a
// back-edge, and chaining it closes a loop the dispatch loop never re-enters.
// Both watchdogs -- max_dispatch and alarm_flag -- are read only at the top of
// dbt_run's loop, so such a loop is bounded by nothing at all (#1571).
//
// Forward chaining, which is what makes straight-line guest code fast, is
// untouched.  Only the branch that closes a loop pays a dispatch, and that is
// the one place a bound is worth having.
//
// Inline in the header because test_chain links the three backends without
// dbt.cpp, and a one-line predicate should not force that binary to grow a
// dependency on the whole dispatcher.
//
static inline bool dbt_chain_allowed(const dbt_state_t *dbt,
                                     uint64_t target_pc) {
    return target_pc > dbt->cur_block_pc;
}

void dbt_backpatch_chains(dbt_state_t *dbt, uint64_t guest_pc,
                           uint8_t *native_code);

// Drop patch sites recorded during a failed translate_block so their
// absolute code_buf offsets are not backpatched into later live code
// that reuses the same arena region (#1147).
static inline void dbt_rollback_patches(dbt_state_t *dbt,
                                        size_t patches_before) {
    if (dbt->patches.size() <= patches_before) {
        return;
    }
    for (size_t i = patches_before; i < dbt->patches.size(); i++) {
        uint64_t target = dbt->patches[i].target_pc;
        auto it = dbt->pending_patch_targets.find(target);
        if (it == dbt->pending_patch_targets.end()) {
            continue;
        }
        std::vector<size_t> &idxs = it->second;
        size_t w = 0;
        for (size_t r = 0; r < idxs.size(); r++) {
            if (idxs[r] < patches_before) {
                idxs[w++] = idxs[r];
            }
        }
        idxs.resize(w);
        if (idxs.empty()) {
            dbt->pending_patch_targets.erase(it);
        }
    }
    dbt->patches.resize(patches_before);
}

// Direct JALR target resolution (pure computation).
bool dbt_resolve_direct_jalr_target(uint64_t pc,
                                     const rv64_insn_t &insn,
                                     const rv64_insn_t &next,
                                     uint64_t *target_out,
                                     uint64_t *return_pc_out);

// Apply a CSR access against ctx->fcsr.  Matches the interpreter's
// fflags (0x001) / frm (0x002) / fcsr (0x003) support (#1333).
// src is rs1 for CSRRW/CSRRS/CSRRC, or the zimm for CSRRWI/CSRRSI/CSRRCI.
// On success writes the prior CSR value into ctx->x[rd] (if rd != 0)
// and updates fcsr; returns 0.  Returns -1 for unsupported CSR numbers
// or illegal funct3 (backends must refuse those at translate time).
//
// Inline so the multi-backend `test_chain` binary (which does not link
// dbt.cpp) still resolves the symbol when backends emit host calls to it.
//
static inline int dbt_csr_apply(rv64_ctx_t *ctx, uint32_t csr_addr,
                                uint32_t funct3, uint64_t src, uint32_t rd)
{
    uint64_t csr_val = 0;
    switch (csr_addr)
    {
    case 0x001: csr_val = ctx->fcsr & 0x1Fu; break;
    case 0x002: csr_val = (ctx->fcsr >> 5) & 0x7u; break;
    case 0x003: csr_val = ctx->fcsr & 0xFFu; break;
    default:
        return -1;
    }

    uint64_t new_val = csr_val;
    switch (funct3)
    {
    case 1: // CSRRW
    case 5: // CSRRWI
        new_val = src;
        break;
    case 2: // CSRRS
    case 6: // CSRRSI
        new_val = csr_val | src;
        break;
    case 3: // CSRRC
    case 7: // CSRRCI
        new_val = csr_val & ~src;
        break;
    default:
        return -1;
    }

    if (rd)
    {
        ctx->x[rd] = csr_val;
    }
    switch (csr_addr)
    {
    case 0x001:
        ctx->fcsr = (ctx->fcsr & ~0x1Fu)
                  | (static_cast<uint32_t>(new_val) & 0x1Fu);
        break;
    case 0x002:
        ctx->fcsr = (ctx->fcsr & ~0xE0u)
                  | ((static_cast<uint32_t>(new_val) & 0x7u) << 5);
        break;
    case 0x003:
        ctx->fcsr = static_cast<uint32_t>(new_val) & 0xFFu;
        break;
    }
    ctx->x[0] = 0;
    return 0;
}

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
// On nullptr the backend must set dbt->xlate_fail to XLATE_FULL (capacity)
// or XLATE_REFUSE (unhandled insn); dbt_run only reclaims on FULL (#1331).
//
uint8_t *dbt_backend_translate_block(dbt_state_t *dbt, uint64_t guest_pc);

// Helpers for backends: set fail reason and return nullptr.
//
static inline uint8_t *dbt_xlate_full(dbt_state_t *dbt)
{
    dbt->xlate_fail = dbt_state_t::XLATE_FULL;
    return nullptr;
}
static inline uint8_t *dbt_xlate_refuse(dbt_state_t *dbt)
{
    dbt->xlate_fail = dbt_state_t::XLATE_REFUSE;
    return nullptr;
}

// Backpatch a single JMP/branch instruction in the code buffer.
// Platform-specific because the patch format differs (x86-64 rel32
// vs AArch64 B offset field).
void dbt_backend_backpatch_jmp(uint8_t *code_buf, uint32_t patch_offset,
                                uint8_t *target);

// Read back where a patch site currently branches to, as a code-buffer
// offset.  The inverse of dbt_backend_backpatch_jmp, and platform
// specific for the same reason: x86-64 stores a rel32 measured from the
// end of the displacement, AArch64 a B imm26 in words measured from the
// instruction itself.
//
// dbt_resolve_chains used to decode the site as rel32 unconditionally,
// so on AArch64 it compared a B imm26 word against a byte displacement.
// Sites never matched their stub offset and were all counted
// already_ok, leaving that pass unable to resolve anything (#1152).
//
uint32_t dbt_backend_decode_jmp_target(const uint8_t *code_buf,
                                        uint32_t patch_offset);

#endif // DBT_INTERNAL_H
