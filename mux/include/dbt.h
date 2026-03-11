/*! \file dbt.h
 * \brief RV64IMD dynamic binary translator — context and block cache.
 *
 * Convention during JIT execution:
 *   RBX = pointer to rv64_ctx_t (guest state)
 *   R12 = pointer to guest memory base
 *   R13 = pointer to block cache base
 *   RAX, RCX, RDX = scratch (not cached)
 *   RSI, RDI, R8-R11, R14, R15 = register cache (8 slots)
 */

#ifndef DBT_H
#define DBT_H

#include <cstdint>
#include <cstddef>

// Return Address Stack for call/return prediction.
//
static constexpr int RAS_SIZE = 32;
static constexpr int RAS_MASK = RAS_SIZE - 1;

// Guest CPU context — laid out for fast access from JIT code.
// RBX points to this struct during block execution.
//
// Offset map (bytes):
//   x[0..31]   :   0 .. 255  (32 x 8 = 256)
//   next_pc    : 256 .. 263
//   ras[0..31] : 264 .. 519  (32 x 8 = 256)
//   ras_top    : 520 .. 523
//   f[0..31]   : 528 .. 783  (32 x 8 = 256, aligned to 16)
//   fcsr       : 784 .. 787
//
struct rv64_ctx_t {
    uint64_t x[32];             // guest integer registers (x[0] always 0)
    uint64_t next_pc;           // set by translated block exits
    uint64_t ras[RAS_SIZE];     // return address stack predictions
    uint32_t ras_top;           // RAS circular buffer index
    uint32_t _pad0;
    double   f[32];             // FP registers (64-bit doubles, D extension only)
    uint32_t fcsr;              // FP control/status: frm[7:5], fflags[4:0]
    uint32_t _pad1;
};

// Context offsets for JIT emitter.
//
static constexpr int CTX_X_OFF       = 0;
static constexpr int CTX_NEXT_PC_OFF = 256;
static constexpr int CTX_RAS_OFF     = 264;
static constexpr int CTX_RAS_TOP_OFF = 520;
static constexpr int CTX_FP_OFF      = 528;
static constexpr int CTX_FCSR_OFF    = 784;

// Block cache entry — exactly 16 bytes for inline JIT lookup.
// R13 points to cache[0] during execution.
// Lookup: index = (pc >> 2) & MASK; entry = R13 + index * 16
//
struct block_entry_t {
    uint64_t guest_pc;      // guest start address (0 = empty)
    uint8_t *native_code;   // pointer into code buffer
};

static_assert(sizeof(block_entry_t) == 16, "block_entry_t must be 16 bytes");

static constexpr size_t BLOCK_CACHE_SIZE = 1024;       // must be power of 2
static constexpr size_t BLOCK_CACHE_MASK = BLOCK_CACHE_SIZE - 1;

static constexpr size_t CODE_BUF_SIZE = 256 * 1024;    // 256 KB JIT buffer

// Block chaining: patch site for backpatching JMP targets.
// When a block exit targets a not-yet-translated PC, we record a patch
// site.  When that PC is later translated, we backpatch the JMP to go
// directly to the new native code, eliminating the trampoline round-trip.
//
struct patch_site_t {
    uint32_t jmp_offset;    // offset in code_buf of the JMP rel32 displacement
    uint64_t target_pc;     // guest PC this exit wants to reach
};

static constexpr size_t MAX_PATCH_SITES = 8192;

// Max guest instructions per translated block.
//
static constexpr int MAX_BLOCK_INSNS = 64;

// DBT state.
//
struct dbt_state_t {
    rv64_ctx_t ctx;

    // Guest memory (borrowed pointer — not owned).
    //
    uint8_t *memory;
    size_t   memory_size;

    // Block cache.
    //
    block_entry_t *cache;

    // JIT code buffer (mmap'd RWX).
    //
    uint8_t *code_buf;
    uint32_t code_used;

    // Block chaining patch sites.
    //
    patch_site_t *patches;
    uint32_t num_patches;

    // Statistics.
    //
    uint64_t blocks_translated;
    uint64_t cache_hits;
    uint64_t cache_misses;
    uint64_t insns_translated;
    uint64_t ras_hits;
    uint64_t ras_misses;
    uint64_t chain_hits;
    uint64_t chain_misses;
    uint64_t insns_fused;

    // ECALL callback — same signature as the interpreter.
    // Return >= 0 to exit, < 0 to continue.
    //
    int (*ecall_fn)(rv64_ctx_t *ctx, void *user);
    void *ecall_user;

    // Debug.
    //
    int trace;
};

// Public API.
//
int  dbt_init(dbt_state_t *dbt, uint8_t *memory, size_t memory_size,
              int (*ecall_fn)(rv64_ctx_t *, void *), void *ecall_user);
void dbt_reset(dbt_state_t *dbt, uint8_t *memory, size_t memory_size,
               int (*ecall_fn)(rv64_ctx_t *, void *), void *ecall_user);
void dbt_rerun(dbt_state_t *dbt,
               int (*ecall_fn)(rv64_ctx_t *, void *), void *ecall_user);
int  dbt_run(dbt_state_t *dbt, uint64_t entry_pc, uint64_t stack_top);
void dbt_cleanup(dbt_state_t *dbt);

#endif // DBT_H
