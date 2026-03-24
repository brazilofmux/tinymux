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

#include <cstddef>
#include <cstdint>
#include <vector>

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
//
// 4-way set-associative: 1024 sets × 4 ways = 4096 entries.
// Layout: entries[set * WAYS + way].  Each set is 64 bytes (4 × 16).
// Lookup: set = hash(pc); scan ways 0..3 for guest_pc match.
//
struct block_entry_t {
    uint64_t guest_pc;      // guest start address (0 = empty)
    uint8_t *native_code;   // pointer into code buffer
};

static_assert(sizeof(block_entry_t) == 16, "block_entry_t must be 16 bytes");

static constexpr size_t BLOCK_CACHE_SETS = 1024;       // must be power of 2
static constexpr size_t BLOCK_CACHE_WAYS = 4;
static constexpr size_t BLOCK_CACHE_SIZE = BLOCK_CACHE_SETS * BLOCK_CACHE_WAYS;
static constexpr size_t BLOCK_CACHE_MASK = BLOCK_CACHE_SETS - 1;

static constexpr size_t CODE_BUF_SIZE = 1024 * 1024;   // 1 MB JIT buffer

// Block chaining: patch site for backpatching JMP targets.
// When a block exit targets a not-yet-translated PC, we record a patch
// site.  When that PC is later translated, we backpatch the JMP to go
// directly to the new native code, eliminating the trampoline round-trip.
//
struct patch_site_t {
    uint32_t jmp_offset;    // offset in code_buf of the JMP rel32 displacement
    uint32_t stub_offset;   // offset in code_buf of the slow-path stub
    uint64_t target_pc;     // guest PC this exit wants to reach
};

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
    std::vector<block_entry_t> cache;

    // JIT code buffer (mmap'd RWX).
    //
    uint8_t *code_buf;
    uint32_t code_used;
    uint32_t blob_code_end;     // code_used after blob pretranslation

    // Block chaining patch sites.
    //
    std::vector<patch_site_t> patches;

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
    uint64_t dispatch_count;     // dispatch loop iterations per dbt_run
    uint64_t superblock_count;   // blocks with self-loop detected
    uint64_t side_exits_total;   // total side exits across all superblocks
    uint64_t inline_calls;       // Tier 2 calls inlined via native CALL

    // ECALL callback — same signature as the interpreter.
    // Return >= 0 to exit, < 0 to continue.
    //
    int (*ecall_fn)(rv64_ctx_t *ctx, void *user);
    void *ecall_user;

    // Intrinsics: guest addresses the DBT replaces with native x86-64.
    // Data-driven table — each entry maps a guest address to an emitter.
    //
    static constexpr int MAX_INTRINSICS = 48;

    struct intrinsic_slot_t {
        uint64_t guest_addr;                       // 0 = empty slot
        void (*emitter)(void *e, void *fn);        // generic emitter (emit_t* cast to void*)
        void *host_fn;                             // host function to CALL
    };

    intrinsic_slot_t intrinsics[MAX_INTRINSICS];
    int              num_intrinsics;
    uint64_t         intrinsic_hits;     // stat: intrinsic calls emitted

    // Dispatch limit — 0 means unlimited.  When non-zero, dbt_run
    // returns -2 if the dispatch loop exceeds this count.  Turns
    // infinite-loop bugs into test failures instead of hangs.
    //
    uint64_t max_dispatch;

    // Inline CALL cold-exit diagnostics.
    //
    uint64_t cold_exit_count;
    uint64_t cold_exit_actual;    // last next_pc that caused cold exit
    uint64_t cold_exit_expected;  // what was expected
    uint64_t last_exit_from;      // guest_pc of last exit_indirect caller

    // Debug.
    //
    int trace;
    uint64_t trace_guest_pc;
    bool trace_guest_pc_filter;
};

// Public API.
//
int  dbt_init(dbt_state_t *dbt, uint8_t *memory, size_t memory_size,
              int (*ecall_fn)(rv64_ctx_t *, void *), void *ecall_user);
void dbt_reset(dbt_state_t *dbt, uint8_t *memory, size_t memory_size,
               int (*ecall_fn)(rv64_ctx_t *, void *), void *ecall_user);
void dbt_rerun(dbt_state_t *dbt,
               int (*ecall_fn)(rv64_ctx_t *, void *), void *ecall_user);
void dbt_pretranslate(dbt_state_t *dbt, uint64_t guest_pc);
void dbt_resolve_chains(dbt_state_t *dbt);
int  dbt_run(dbt_state_t *dbt, uint64_t entry_pc, uint64_t stack_top);
void dbt_cleanup(dbt_state_t *dbt);

// Intrinsic registration — called from dbt_compile.cpp before pretranslation.
//
// Emitter IDs for built-in intrinsic stubs:
enum dbt_emitter_id {
    DBT_EMIT_SLEN = 0,      // rv64_slen → host strlen
    DBT_EMIT_SCOPY,          // rv64_scopy → host strcpy+strlen
    DBT_EMIT_MEMCPY,         // memcpy → host memcpy
    DBT_EMIT_MEMCMP,         // memcmp → host memcmp
    DBT_EMIT_MEMSET,         // memset → host memset
    DBT_EMIT_MEMSWAP,        // memswap → inline qword swap

    // Generic co_* emitters — data-driven, parameterized by host_fn.
    // Signature encoded: number of args + which are guest pointers.
    //
    DBT_EMIT_CO_3P,          // 3 args: a0=ptr, a1-a2=int
    DBT_EMIT_CO_4PP,         // 4 args: a0=ptr, a1=ptr, a2-a3=int
    DBT_EMIT_CO_POS,         // 4 args: a0=ptr, a1=int, a2=ptr, a3=int
    DBT_EMIT_CO_5PP,         // 5 args: a0=ptr, a1=ptr, a2-a4=int
    DBT_EMIT_CO_MEMBER,      // 5 args: a0=ptr, a1=int, a2=ptr, a3-a4=int
    DBT_EMIT_CO_6PP,         // 6 args: a0=ptr, a1=ptr, a2-a5=int

    DBT_EMIT_CO_2P,          // 2 args: a0=ptr, a1=int
    DBT_EMIT_CO_3PP,         // 3 args: a0=ptr, a1=ptr, a2=int

    // Extended patterns — overflow args passed on x86-64 stack.
    //
    DBT_EMIT_CO_7PP,         // 7 args: a0=ptr, a1=ptr, a2-a6=int
    DBT_EMIT_CO_8PPP,        // 8 args: a0=ptr, a1=ptr, a3=ptr, rest=int

    // FP intrinsics — double→double and (double,double)→double.
    // Guest fa0/fa1 ↔ host xmm0/xmm1, call platform libm.
    //
    DBT_EMIT_FP_D_D,         // double fn(double): sin, cos, tan, etc.
    DBT_EMIT_FP_DD_D,        // double fn(double, double): pow, atan2, fmod
};

void dbt_register_intrinsic(dbt_state_t *dbt, uint64_t guest_addr,
                             dbt_emitter_id emitter_id, void *host_fn);

// Trace flags for dbt_state_t::trace.
//
static constexpr int DBT_TRACE_EXEC      = 1 << 0;  // dispatch/execution PCs
static constexpr int DBT_TRACE_TRANSLATE = 1 << 1;  // translation-time summaries

#endif // DBT_H
