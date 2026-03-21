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

#include "hir.h"

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
    std::vector<uint8_t> code;           // code section bytes
    std::vector<uint8_t> rodata;         // rodata section bytes
    std::map<std::string, tier2_entry> funcs;  // name → entry
    uint64_t guest_base;                 // where code is loaded in guest memory
};

extern tier2_state s_tier2;
extern std::string s_blob_version;

// Tier 2 functions used across translation units.
uint64_t tier2_lookup(const std::string &mux_name);
void tier2_install(std::vector<uint8_t> &memory, uint64_t guest_base);
void pretranslate_tier2(struct dbt_state_t *dbt);

// ---------------------------------------------------------------
// Compiler state
// ---------------------------------------------------------------

struct rv_compiler {
    std::vector<uint8_t> memory;
    std::vector<uint32_t> code;

    // Pool allocators (bump pointers into guest memory).
    uint64_t str_pool;      // next free byte in string pool
    uint64_t fargs_pool;    // next free byte in fargs area
    uint64_t out_pool;      // next free byte in output area
    uint64_t final_out;     // guest addr of final result

    // Statistics.
    int folds;              // number of constant-folded calls
    int ecalls;             // number of runtime ECALL calls
    int native_ops;         // number of native arithmetic ops
    bool needs_jit;         // true if any runtime code was emitted

    static constexpr size_t MEM_SIZE     = 1024 * 1024;
    static constexpr uint64_t CODE_BASE  = 0x0000;
    static constexpr uint64_t CODE_LIMIT = 0x1000;
    static constexpr uint64_t STR_BASE   = 0x1000;
    static constexpr uint64_t STR_LIMIT  = 0x4000;
    static constexpr uint64_t FARGS_BASE = 0x4000;
    static constexpr uint64_t FARGS_LIMIT= 0x8000;

    static constexpr uint64_t BLOB_BASE  = 0x10000;

    static constexpr uint64_t OUT_BASE   = 0x8000;
    static constexpr uint64_t OUT_GAP_LO = 0x10000;
    static constexpr uint64_t OUT_GAP_HI = 0x30000;
    static constexpr uint64_t OUT_LIMIT  = 0x68000;
    static constexpr int      OUT_SLOT   = 8000;

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

    static constexpr uint64_t STACK_TOP  = 0xFFFF0;
    static constexpr uint64_t BLOB_LIMIT = 0x40000;

    rv_compiler() : memory(MEM_SIZE, 0),
                    str_pool(STR_BASE),
                    fargs_pool(FARGS_BASE),
                    out_pool(OUT_BASE),
                    final_out(0),
                    folds(0),
                    ecalls(0),
                    native_ops(0),
                    needs_jit(false) {}

    uint64_t pool_str(const char *s, size_t len) {
        uint64_t addr = str_pool;
        if (addr + len + 1 > STR_LIMIT) return 0;
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
        if (addr + sz > FARGS_LIMIT) return 0;
        for (size_t i = 0; i < ptrs.size(); i++) {
            memcpy(memory.data() + addr + i * 8, &ptrs[i], 8);
        }
        fargs_pool = (addr + sz + 7) & ~7ULL;
        return addr;
    }

    bool out_exhausted = false;
    bool bail_was_noeval = false;

    uint64_t alloc_output() {
        uint64_t addr = out_pool;
        if (addr >= OUT_GAP_LO && addr < OUT_GAP_HI) {
            addr = OUT_GAP_HI;
            out_pool = addr;
        }
        if (addr + OUT_SLOT > OUT_LIMIT) {
            out_exhausted = true;
            return 0;
        }
        out_pool = addr + OUT_SLOT;
        return addr;
    }
};

// ---------------------------------------------------------------
// Compiled program — output of compile_expression()
// ---------------------------------------------------------------

struct compiled_program {
    std::vector<uint8_t> memory;
    size_t memory_size;
    uint64_t out_addr;      // where the final result lives
    uint64_t out_used;      // bytes of output region actually allocated
    bool ok;
    int folds;
    int ecalls;
    int tier2_calls;
    int native_ops;
    bool needs_jit;         // true if JIT execution required

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
