/*! \file dbt_compile.h
 * \brief Shared declarations for the JIT compiler pipeline.
 *
 * Provides types and function declarations shared across:
 *   hir_lower.cpp    — AST → HIR lowering
 *   hir_codegen.cpp  — HIR → RV64 code generation
 *   jit_compiler.cpp — top-level JIT pipeline
 */

#ifndef DBT_COMPILE_H
#define DBT_COMPILE_H

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <memory>

#include "hir.h"

// ---------------------------------------------------------------
// No-init allocator for std::vector<uint8_t>.
//
// Skips value-initialization (zero-fill) on resize/construct.
// Used for guest memory buffers where we control exactly which
// regions are written before use.
// ---------------------------------------------------------------

template<typename Elem>
struct noinit_alloc : std::allocator<Elem> {
    using std::allocator<Elem>::allocator;

    template<typename U>
    struct rebind { using other = noinit_alloc<U>; };

    // construct() with no value: skip zero-initialization.
    void construct(Elem *p) noexcept {}

    // construct() with a value: placement-new as usual.
    template<typename... Args>
    void construct(Elem *p, Args&&... args) {
        ::new (static_cast<void *>(p)) Elem(std::forward<Args>(args)...);
    }
};

using guest_memory_t = std::vector<uint8_t, noinit_alloc<uint8_t>>;

// ---------------------------------------------------------------
// JIT statistics
// ---------------------------------------------------------------

struct jit_stats_t {
    uint64_t eval_attempts;       // jit_eval() called
    uint64_t eval_handled;        // JIT produced a result
    uint64_t eval_bailout;        // JIT returned false -> AST fallback

    uint64_t cache_hit_mem;       // memory LRU cache hit
    uint64_t cache_hit_sqlite;    // SQLite persistent cache hit
    uint64_t cache_miss;          // full compilation needed

    uint64_t compile_ok;          // compile_expression succeeded
    uint64_t compile_fail;        // compile_expression failed

    uint64_t bail_noeval;         // NOEVAL function forced bailout
    uint64_t bail_slots;          // output slot exhaustion

    uint64_t folded_total;        // constant-folded results (no JIT needed)
    uint64_t ecall_total;         // ECALL invocations at runtime
    uint64_t tier2_total;         // Tier 2 blob calls at runtime

    // Code size tracking.
    uint64_t code_bytes_total;    // total RV64 bytes emitted across all compiles
    uint64_t code_bytes_max;      // largest single program (bytes)
    uint64_t hir_insns_total;     // total HIR instructions across all compiles
    uint64_t hir_insns_max;       // most HIR instructions in one program
    uint64_t spills_total;        // register allocator spills

    // Top NOEVAL bailout functions.
    static constexpr int NOEVAL_TRACK_MAX = 16;
    struct { char name[32]; uint64_t count; } noeval_top[NOEVAL_TRACK_MAX];
    int noeval_top_used;

    void record_noeval_bail(const char *fname) {
        bail_noeval++;
        for (int i = 0; i < noeval_top_used; i++) {
            if (strcmp(noeval_top[i].name, fname) == 0) {
                noeval_top[i].count++;
                return;
            }
        }
        if (noeval_top_used < NOEVAL_TRACK_MAX) {
            snprintf(noeval_top[noeval_top_used].name, 32, "%s", fname);
            noeval_top[noeval_top_used].count = 1;
            noeval_top_used++;
        } else {
            int min_i = 0;
            for (int i = 1; i < NOEVAL_TRACK_MAX; i++) {
                if (noeval_top[i].count < noeval_top[min_i].count)
                    min_i = i;
            }
            snprintf(noeval_top[min_i].name, 32, "%s", fname);
            noeval_top[min_i].count = 1;
        }
    }
};

extern jit_stats_t s_jit_stats;

// ---------------------------------------------------------------
// Ragel co_* functions (in libmux.so) used by constant folding
// ---------------------------------------------------------------

extern "C" {
    size_t co_first(unsigned char *, const unsigned char *, size_t, unsigned char);
    size_t co_rest(unsigned char *, const unsigned char *, size_t, unsigned char);
    size_t co_last(unsigned char *, const unsigned char *, size_t, unsigned char);
    size_t co_words_count(const unsigned char *, size_t, unsigned char);
    size_t co_repeat(unsigned char *, const unsigned char *, size_t, size_t);
    size_t co_mid(unsigned char *, const unsigned char *, size_t, size_t, size_t);
    size_t co_pos(const unsigned char *, size_t, const unsigned char *, size_t);
    size_t co_member(const unsigned char *, size_t, const unsigned char *, size_t, unsigned char);
    size_t co_trim(unsigned char *, const unsigned char *, size_t, unsigned char, int);
    size_t co_delete(unsigned char *, const unsigned char *, size_t, size_t, unsigned char, unsigned char);
    size_t co_sort_words(unsigned char *, const unsigned char *, size_t, unsigned char, unsigned char, char);
    size_t co_extract(unsigned char *, const unsigned char *, size_t, size_t, size_t, unsigned char, unsigned char);
    size_t co_setunion(unsigned char *, const unsigned char *, size_t, const unsigned char *, size_t, unsigned char, unsigned char, char);
    size_t co_setdiff(unsigned char *, const unsigned char *, size_t, const unsigned char *, size_t, unsigned char, unsigned char, char);
    size_t co_setinter(unsigned char *, const unsigned char *, size_t, const unsigned char *, size_t, unsigned char, unsigned char, char);
    size_t co_cluster_count(const unsigned char *, size_t);
    size_t co_tolower(unsigned char *, const unsigned char *, size_t);
    size_t co_toupper(unsigned char *, const unsigned char *, size_t);
    size_t co_reverse(unsigned char *, const unsigned char *, size_t);
    size_t co_escape(unsigned char *, const unsigned char *, size_t);
    size_t co_left(unsigned char *, const unsigned char *, size_t, size_t);
    size_t co_right(unsigned char *, const unsigned char *, size_t, size_t);
    size_t co_compress(unsigned char *, const unsigned char *, size_t, unsigned char);
    size_t co_lpos(unsigned char *, const unsigned char *, size_t, unsigned char);
    size_t co_totitle(unsigned char *, const unsigned char *, size_t);
    size_t co_strip_color(unsigned char *, const unsigned char *, size_t);
    size_t co_visible_length(const unsigned char *, size_t);
    size_t co_mid_cluster(unsigned char *, const unsigned char *, size_t, size_t, size_t);
    size_t co_delete_cluster(unsigned char *, const unsigned char *, size_t, size_t, size_t);
    size_t co_edit(unsigned char *, const unsigned char *, size_t,
                   const unsigned char *, size_t, const unsigned char *, size_t);
}

// ---------------------------------------------------------------
// Tier 2: pre-compiled RV64 library blob
// ---------------------------------------------------------------

struct tier2_entry {
    uint32_t code_off;    // offset within blob code section
    uint32_t guest_addr;  // absolute guest address after loading
};

struct tier2_state {
    bool loaded;
    std::vector<uint8_t> image;          // flat image (code + rodata + data + zeroed BSS)
    uint32_t code_size;                  // initialized portion (code + rodata + data), excludes BSS
    uint32_t bss_size;                   // zero-fill size after data
    uint32_t data_image_offset;          // offset of writable data within image
    uint32_t data_image_size;            // size of writable data section
    std::map<std::string, tier2_entry> funcs;  // name → entry
    uint64_t guest_base;                 // where code is loaded in guest memory
};

extern tier2_state s_tier2;
extern std::string s_blob_version;

// Tier 2 functions used across translation units.
uint64_t tier2_lookup(const std::string &mux_name);
uint64_t tier2_sym_addr(const char *blob_name);
// Accepts both guest_memory_t and std::vector<uint8_t>.
template<typename Vec>
void tier2_install(Vec &memory, uint64_t guest_base);

// Explicit instantiation declarations (defined in jit_compiler.cpp).
extern template void tier2_install(guest_memory_t &, uint64_t);
extern template void tier2_install(std::vector<uint8_t> &, uint64_t);
void pretranslate_tier2(struct dbt_state_t *dbt);

// ---------------------------------------------------------------
// Compiler state
// ---------------------------------------------------------------

struct rv_compiler {
    guest_memory_t memory;
    std::vector<uint32_t> code;

    // Code base address in guest memory.  Defaults to CODE_BASE (0)
    // for backward compatibility.  In persistent VM mode, each compiled
    // function gets a different code_base from the code heap allocator.
    uint64_t code_base;

    // Pool allocators (bump pointers into guest memory).
    uint64_t str_pool;      // next free byte in string pool
    uint64_t str_pool_limit; // upper bound for string pool
    uint64_t fargs_pool;    // next free byte in fargs area
    uint64_t fargs_pool_limit; // upper bound for fargs pool
    uint64_t out_pool;      // next free byte in output area
    uint64_t final_out;     // guest addr or tagged frame-relative output ref

    // Statistics.
    int folds;              // number of constant-folded calls
    int ecalls;             // number of runtime ECALL calls
    int native_ops;         // number of native arithmetic ops
    int spills;             // register allocator spill slots used
    bool needs_jit;         // true if any runtime code was emitted

    static constexpr size_t MEM_SIZE     = 4 * 1024 * 1024;  // 4 MB
    static constexpr uint64_t CODE_BASE  = 0x0000;
    static constexpr uint64_t CODE_LIMIT = 0x1000;
    static constexpr uint64_t STR_BASE   = 0x1000;
    static constexpr uint64_t STR_LIMIT  = 0x4000;
    static constexpr uint64_t FARGS_BASE = 0x4000;
    static constexpr uint64_t FARGS_LIMIT= 0x8000;

    static constexpr uint64_t BLOB_BASE  = 0x10000;
    static constexpr uint64_t BLOB_LIMIT = 0x40000;

    // Output slots — sized to match LBUF_SIZE (32768).
    // Stack-allocated: output buffers grow downward from STACK_TOP.
    // The compiled code prologue decrements SP by the total output
    // frame size.  Each slot is STACK_TOP - 8 - (N+1)*OUT_SLOT.
    // Addresses are compile-time constants (STACK_TOP is fixed).
    //
    // Legacy OUT_BASE retained for backward compatibility with
    // cached programs and output clearing.
    //
    static constexpr uint64_t OUT_BASE       = 0x8000;   // legacy (unused by new alloc)
    static constexpr int      OUT_SLOT       = 32768;    // = LBUF_SIZE
    static constexpr uint64_t OUT_STACK_LIMIT = 0x81000; // lowest valid output addr
    // Bit 30, not bit 31: RV64 LUI sign-extends bit 31 to 64 bits,
    // which corrupts tagged values loaded via load_val.
    static constexpr uint64_t OUT_FRAME_TAG   = 0x40000000ULL;

    // Pinned Lua array region: 0x50000-0x60000 (64KB = 8192 int64 elements)
    // Shares address space with output range 2 — never used simultaneously.
    static constexpr uint64_t LUA_ARRAY_BASE  = 0x50000;
    static constexpr uint64_t LUA_ARRAY_LIMIT = 0x60000;
    static constexpr int      LUA_ARRAY_MAX   = 8192;

    static constexpr uint64_t CARGS_BASE = 0x68000;
    static constexpr int      CARGS_SLOT = 256;
    static constexpr int      MAX_CARGS  = 10;

    static constexpr uint64_t SUBST_BASE = 0x68A00;
    static constexpr int      SUBST_SLOT = 256;
    static constexpr int      SUBST_ENACTOR  = 0;
    static constexpr int      SUBST_EXECUTOR = 1;
    static constexpr int      SUBST_NAME     = 2;
    static constexpr int      SUBST_LOCATION = 3;
    static constexpr int      SUBST_QREG0    = 4;
    static constexpr int      SUBST_LASTCMD  = SUBST_QREG0 + MAX_GLOBAL_REGS;
    static constexpr int      SUBST_MONIKER  = SUBST_LASTCMD + 1;
    static constexpr int      SUBST_POUT     = SUBST_MONIKER + 1;
    static constexpr int      SUBST_NCARGS   = SUBST_POUT + 1;
    static constexpr int      SUBST_OBJID    = SUBST_NCARGS + 1;
    static constexpr int      SUBST_COUNT    = SUBST_OBJID + 1;

    // DMA windows (Tier C): zero-copy host interaction.
    // 4 windows of 16KB each at 0x70000-0x7FFFF.
    static constexpr uint64_t DMA_BASE         = 0x70000;
    static constexpr int      DMA_WINDOW_SIZE  = 16 * 1024;
    static constexpr int      DMA_WINDOW_COUNT = 4;
    static constexpr uint64_t DMA_DESC_BASE    = 0x80000; // descriptor rings (4KB)

    static constexpr uint64_t STACK_TOP  = 0x3FFFF0;    // top of 4MB space

    // Number of output slots allocated (for prologue frame size).
    int n_output_slots;

    rv_compiler() : code_base(CODE_BASE),
                    memory(MEM_SIZE),
                    str_pool(STR_BASE),
                    str_pool_limit(STR_LIMIT),
                    fargs_pool(FARGS_BASE),
                    fargs_pool_limit(FARGS_LIMIT),
                    out_pool(STACK_TOP - 8),
                    final_out(0),
                    folds(0),
                    ecalls(0),
                    native_ops(0),
                    spills(0),
                    needs_jit(false),
                    n_output_slots(0) {}

    // Persistent VM constructor: caller specifies code base, pool
    // starting positions, and pool limits.
    rv_compiler(uint64_t cb,
                uint64_t sp, uint64_t sp_lim,
                uint64_t fp, uint64_t fp_lim,
                uint64_t op)
        : code_base(cb),
          memory(MEM_SIZE),
          str_pool(sp),
          str_pool_limit(sp_lim),
          fargs_pool(fp),
          fargs_pool_limit(fp_lim),
          out_pool(op ? op : STACK_TOP - 8),
          final_out(0),
          folds(0),
          ecalls(0),
          native_ops(0),
          spills(0),
          needs_jit(false),
          n_output_slots(0) {}

    // Current guest PC of the next instruction to be emitted.
    uint64_t current_pc() const {
        return code_base + code.size() * 4;
    }

    uint64_t pool_str(const char *s, size_t len) {
        uint64_t addr = str_pool;
        if (addr + len + 1 > str_pool_limit) {
            pool_exhausted = true;
            return 0;
        }
        memcpy(memory.data() + addr, s, len);
        memory[addr + len] = '\0';
        str_pool = (addr + len + 1 + 7) & ~7ULL;
        return addr;
    }

    uint64_t pool_str(const std::string &s) {
        return pool_str(s.c_str(), s.size());
    }

    uint64_t alloc_fargs(const std::vector<uint64_t> &ptrs) {
        uint64_t addr = fargs_pool;
        size_t sz = ptrs.size() * 8;
        if (addr + sz > fargs_pool_limit) {
            pool_exhausted = true;
            return 0;
        }
        for (size_t i = 0; i < ptrs.size(); i++) {
            memcpy(memory.data() + addr + i * 8, &ptrs[i], 8);
        }
        fargs_pool = (addr + sz + 7) & ~7ULL;
        return addr;
    }

    bool out_exhausted = false;
    bool pool_exhausted = false;
    bool bail_was_noeval = false;

    static bool is_output_frame_ref(uint64_t addr) {
        return (addr & OUT_FRAME_TAG) != 0;
    }

    static uint64_t output_frame_delta(uint64_t addr) {
        return addr & ~OUT_FRAME_TAG;
    }

    static uint64_t make_output_frame_ref(uint64_t abs_addr) {
        return OUT_FRAME_TAG | (STACK_TOP - abs_addr);
    }

    static uint64_t resolve_output_addr(uint64_t addr, uint64_t entry_sp) {
        if (!is_output_frame_ref(addr)) {
            return addr;
        }
        return entry_sp - output_frame_delta(addr);
    }

    uint64_t alloc_output() {
        // Stack-allocated: grow downward from STACK_TOP.
        uint64_t addr = out_pool - OUT_SLOT;
        if (addr < OUT_STACK_LIMIT) {
            out_exhausted = true;
            return 0;
        }
        out_pool = addr;
        n_output_slots++;
        return make_output_frame_ref(addr);
    }
};

// ---------------------------------------------------------------
// Compiled program — output of compile_expression()
// ---------------------------------------------------------------

struct compiled_program {
    guest_memory_t memory;          // full 4MB — used by compile_expression(),
                                    // persistent_vm, run_compiled(); cleared
                                    // after compaction into cache.
    size_t memory_size;
    uint64_t out_addr;      // guest addr or tagged frame-relative output ref
    uint64_t out_used;      // bytes of output region actually allocated
    uint64_t entry_pc;      // guest PC of compiled code entry point
    uint64_t code_size;     // bytes of code emitted
    bool ok;
    int folds;
    int ecalls;
    int tier2_calls;
    int native_ops;
    bool needs_jit;         // true if JIT execution required

    // Pool high-water marks after compilation.
    // Persistent VM uses these to advance shared pool cursors so the
    // next compilation starts where this one left off.
    uint64_t str_pool_end;
    uint64_t fargs_pool_end;
    uint64_t out_pool_end;

    // Compact storage — only the occupied regions of guest memory.
    // Populated by compact_program(); used by materialize_program().
    std::vector<uint8_t> code_blob;     // CODE: entry_pc .. entry_pc+code_size
    std::vector<uint8_t> str_blob;      // STR:  STR_BASE .. str_pool_end
    std::vector<uint8_t> fargs_blob;    // FARGS: FARGS_BASE .. fargs_pool_end
    std::string folded_result;          // pre-extracted result for !needs_jit
    uint64_t program_id;                // unique ID for DBT cache invalidation

    // Tier 3 u()-inlining dependency tracking.
    // Each entry records an attr whose body was inlined at compile
    // time, along with its mod_count at that moment.  At runtime,
    // if any dep's current mod_count differs, the program is stale.
    //
    struct inline_dep {
        int32_t  obj;
        int32_t  attr_num;
        uint32_t mod_count;
    };
    std::vector<inline_dep> deps;
};

// ---------------------------------------------------------------
// ECALL handler context
// ---------------------------------------------------------------

struct eval_ctx {
    uint8_t *memory;
    size_t   memory_size;
    dbref    executor;
    dbref    caller;
    dbref    enactor;
    int      eval;
    const UTF8 **cargs;
    int        ncargs;

    // Lua JIT: opaque pointer to lua_State.
    // Non-null when running a JIT-compiled Lua program — allows ECALL
    // handlers to call back into the Lua VM for unsupported operations.
    // nullptr for softcode JIT.
    void    *lua_state;

    // DBT state pointer — enables ECALL_CALL_COMPILED to re-enter
    // the dispatch loop via dbt_resume for nested function calls.
    // nullptr disables re-entrant calls (falls back to ECALL fun_u).
    struct dbt_state_t *dbt;

    // Persistent VM pointer — enables ECALL_COMPILE_ATTR to compile
    // attribute bodies into the persistent code heap.
    // nullptr disables attribute compilation.
    void *pvm;
};

// ---------------------------------------------------------------
// Cross-file function declarations
// ---------------------------------------------------------------

// HIR lowering (hir_lower.cpp).
struct ASTNode;
int hir_lower_node(hir_program &h, rv_compiler &rc, const ASTNode *node);
void qreg_init();
bool returns_int(const std::string &upper);

// Compile-time eval flag tracking (defined in hir_lower.cpp).
extern int  s_compile_eval;
extern bool s_fcheck_available;

// Tier 3 compile-time state (defined in hir_lower.cpp).
extern std::vector<compiled_program::inline_dep> *s_compile_deps;
extern int s_inline_depth;

// HIR codegen (hir_codegen.cpp).
void hir_codegen(hir_program &h, rv_compiler &rc);
void hir_dump(const hir_program &h);

// JIT compiler (jit_compiler.cpp).
void dbt_compile_cleanup(void);
bool run_cached_program(compiled_program *prog,
                        dbref executor, dbref caller_db,
                        dbref enactor,
                        UTF8 *out, size_t out_size,
                        const UTF8 *cargs[] = nullptr,
                        int ncargs = 0,
                        int eval = EV_FCHECK | EV_EVAL,
                        void *lua_state = nullptr);

// Helper used by both hir_lower and hir_codegen.
static inline const UTF8 *u8(const std::string &s) {
    return reinterpret_cast<const UTF8 *>(s.c_str());
}

#endif // DBT_COMPILE_H
