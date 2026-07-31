/*! \file jit_lua.cpp
 * \brief CJITCompile COM class — Lua bytecode → native JIT compilation.
 *
 * Implements mux_IJITCompile.  Deserializes Lua 5.4 bytecode,
 * lowers through HIR/RV64/x86-64 pipeline, caches compiled programs.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "dbt_compile.h"
#include "engine_api.h"
#include "lua_bytecode.h"
#include "hir_lower_lua.h"

#include <atomic>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <vector>
#include <string>

// ---------------------------------------------------------------
// Compile cache
// ---------------------------------------------------------------

static std::unordered_map<uint64_t, compiled_program> s_lua_cache;
static std::atomic<uint64_t> s_next_key{1};

// ---------------------------------------------------------------
// Statistics
// ---------------------------------------------------------------

struct lua_jit_stats {
    uint64_t compile_ok;
    uint64_t compile_fail;
    uint64_t run_ok;
    uint64_t run_fail;
    uint64_t cache_hits;
    uint64_t invalidations;
    // #1751 Phase 0: post-entry ECALL_DECLINE committed as loud error
    // (no interpreter re-run).  Correct long-term value is zero.
    //
    uint64_t post_entry_decline;
};

static lua_jit_stats s_lua_jit_stats = {};

// Published to jitstats() (#1316).  These counters are the only way to tell
// a Lua JIT that runs from one that compiles and then silently falls back.
//
void jit_lua_get_stats(lua_jit_counters *out) {
    if (nullptr == out) return;
    out->compile_ok          = s_lua_jit_stats.compile_ok;
    out->compile_fail        = s_lua_jit_stats.compile_fail;
    out->run_ok              = s_lua_jit_stats.run_ok;
    out->run_fail            = s_lua_jit_stats.run_fail;
    out->cache_hits          = s_lua_jit_stats.cache_hits;
    out->invalidations       = s_lua_jit_stats.invalidations;
    out->post_entry_decline  = s_lua_jit_stats.post_entry_decline;
}

void jit_lua_note_post_entry_decline(void) {
    s_lua_jit_stats.post_entry_decline++;
    s_lua_jit_stats.run_fail++;
    // A successful RunCompiled may have already counted run_ok; reverse it
    // when the outer path commits a post-entry fail instead.
    //
    if (s_lua_jit_stats.run_ok > 0) {
        s_lua_jit_stats.run_ok--;
    }
}

void jit_lua_reset_stats(void) {
    s_lua_jit_stats = {};
}

void jit_lua_clear_cache(void) {
    const size_t n = s_lua_cache.size();
    s_lua_cache.clear();
    // Count each dropped entry as an invalidation so jitstats() shows the
    // flush happened; operators comparing before/after can see the drop.
    //
    s_lua_jit_stats.invalidations += n;
}

// ---------------------------------------------------------------
// Compile a Lua bytecode blob to a compiled_program.
// ---------------------------------------------------------------

static bool compile_lua_bytecode(const uint8_t *data, size_t len,
                                  compiled_program *out) {
    // TINYMUX_DUMP_HIR covered only the softcode JIT, so nothing on this path
    // was visible: neither the block layout nor, more basically, whether a
    // chunk compiled at all.  A declined chunk runs on the interpreter and so
    // agrees with it trivially, which reads as a pass when comparing the two
    // — the reason the declines below are reported and not just the dumps.
    //
    const char *dump_env = getenv("TINYMUX_DUMP_HIR");
    bool bDump = (dump_env && *dump_env != '0');

    // Softcode entry points load the Tier 2 blob; the Lua path used not to.
    // With jit_eval_brackets 0 (required for Lua JIT under #1326) softcode
    // never hits jit_eval, so the blob stayed unloaded and every `^` that
    // needs tier2_sym_addr("pow") declined at lowering (#1561).  Ensure once
    // here so native FP lowers can resolve symbols; missing softlib still
    // leaves s_tier2.loaded false and those ops decline cleanly.
    //
    tier2_ensure();

    // Deserialize.
    lua_bc_chunk chunk;
    if (!lua_bc_load(data, len, &chunk)) {
        if (bDump) {
            printf("\n--- Lua JIT: declined, bytecode failed to load ---\n");
        }
        return false;
    }

    // Fast eligibility check — reject before allocating HIR/RV64 state.
    lua_bc_reject reason = lua_bc_eligible(&chunk.main);
    if (reason != LUA_BC_ELIGIBLE) {
        if (bDump) {
            printf("\n--- Lua JIT: %s declined by eligibility: %s ---\n",
                   chunk.main.source.c_str(), lua_bc_reject_name(reason));
        }
        return false;
    }

    // Create HIR program and RV64 compiler state.
    hir_program *h = new hir_program;
    h->init();

    rv_compiler rc_state;

    // Lower Lua bytecode to HIR.
    int result = hir_lower_lua_proto(*h, rc_state, &chunk.main);
    if (result < 0) {
        if (bDump) {
            printf("\n--- Lua JIT: %s declined by lowering"
                   " (%d bytecode insns) ---\n",
                   chunk.main.source.c_str(),
                   static_cast<int>(chunk.main.code.size()));
        }
        delete h;
        return false;
    }

    // The Lua path never consulted h->overflowed (#1501).  It checked only the
    // -1 returned by hir_lower_lua_proto, which catches a refusal the lowering
    // propagated all the way out and misses one a consumer swallowed -- and
    // then ran SSA, the optimizer and codegen over a program whose lowering had
    // stopped partway.  compile_expression has had this check since #859; the
    // Lua entry point is the same pipeline and needs it too, plus the
    // consumer-side refused_index() half.
    //
    if (h->overflowed || h->refused_index()) {
        if (bDump) {
            printf("\n--- Lua JIT: %s declined by overflow/refused index"
                   " (%d bytecode insns) ---\n",
                   chunk.main.source.c_str(),
                   static_cast<int>(chunk.main.code.size()));
        }
        delete h;
        return false;
    }

    if (bDump) {
        printf("\n--- Lua JIT Compilation: %s (%d bytecode insns) ---\n",
               chunk.main.source.c_str(),
               static_cast<int>(chunk.main.code.size()));
        printf("Phase 1: HIR Lowering\n");
        hir_dump(*h);
    }

    // Always build block ranges (block_last starts at -1 until CFG is
    // computed).  Without this, single-block Lua programs skip every HIR
    // insn in hir_codegen and leave final_out=0 (#1309 empty folds).
    //
    hir_build_cfg(*h);
    if (bDump) {
        printf("Phase 2: CFG (%d blocks)\n", h->n_blocks);
        hir_dump(*h);
    }
    if (h->n_blocks > 1) {
        hir_ssa_construct(*h);
        if (bDump) {
            printf("Phase 2b: SSA Construction\n");
            hir_dump(*h);
        }
        hir_optimize(*h);
    } else {
        // Single block: just constant folding.
        hir_const_fold(*h);
    }
    if (bDump) {
        printf("Phase 3: %s\n",
               h->n_blocks > 1 ? "SSA Optimization" : "Constant Folding");
        hir_dump(*h);
    }

    // SSA construction and the optimizer can exhaust capacity themselves, after
    // the check above has already run -- the same reason compile_expression
    // re-checks between phases (#1149).
    //
    if (h->overflowed || h->refused_index()) {
        delete h;
        return false;
    }

    // Code generation: HIR → RV64.
    hir_codegen(*h, rc_state);

    // Copy code into guest memory at code_base — same as softcode
    // compile_expression.  Without this, code lives only in rc_state.code
    // while memory's code region stays zero; compact then materializes
    // zeros at entry_pc and the DBT spins forever at guest PC 0
    // (unknown opcode → exit_with_pc(same) → #1309 hang).
    //
    if (rc_state.code.size() * 4 > rv_compiler::CODE_LIMIT) {
        delete h;
        return false;
    }
    for (size_t i = 0; i < rc_state.code.size(); i++) {
        memcpy(rc_state.memory.data() + rc_state.code_base + i * 4,
               &rc_state.code[i], 4);
    }
    if (rc_state.out_exhausted || rc_state.pool_exhausted) {
        delete h;
        return false;
    }

    // Build compiled_program output.
    // Include rc_state.needs_jit: codegen may force runtime for results that
    // live only in registers (ITOA path), even when lowering saw no ecalls.
    //
    out->memory = std::move(rc_state.memory);
    out->memory_size = rv_compiler::MEM_SIZE;
    out->out_addr = rc_state.final_out;
    out->out_used = rc_state.out_pool;
    out->entry_pc = rc_state.code_base;
    out->code_size = rc_state.code.size() * 4;
    out->str_pool_end = rc_state.str_pool;
    out->fargs_pool_end = rc_state.fargs_pool;
    out->out_pool_end = rc_state.out_pool;
    out->ok = true;
    out->folds = h->folds;
    out->ecalls = h->ecalls;
    out->tier2_calls = 0;
    out->native_ops = h->native_ops;
    out->needs_jit = h->needs_jit || rc_state.needs_jit;

    // Classify CARGS/SUBST refs (mux.args[N] → emit_sref) so
    // run_cached_program populates the guest slots this program reads.
    // Same pass as softcode compile_expression.
    //
    out->subst_mask = 0;
    out->cargs_used = 0;
    for (uint64_t a : h->sref_addrs) {
        if (  a >= rv_compiler::CARGS_BASE
           && a < rv_compiler::CARGS_BASE
                  + static_cast<uint64_t>(rv_compiler::MAX_CARGS)
                    * rv_compiler::CARGS_SLOT) {
            int idx = static_cast<int>(
                (a - rv_compiler::CARGS_BASE) / rv_compiler::CARGS_SLOT);
            if (idx + 1 > out->cargs_used) {
                out->cargs_used = idx + 1;
            }
        } else if (  a >= rv_compiler::SUBST_BASE
                  && a < rv_compiler::SUBST_BASE
                         + static_cast<uint64_t>(rv_compiler::SUBST_COUNT)
                           * rv_compiler::SUBST_SLOT) {
            int slot = static_cast<int>(
                (a - rv_compiler::SUBST_BASE) / rv_compiler::SUBST_SLOT);
            out->subst_mask |= (UINT64_C(1) << slot);
        }
    }

    delete h;
    return true;
}

// ---------------------------------------------------------------
// CJITCompile COM class
// ---------------------------------------------------------------

class CJITCompile : public mux_IJITCompile
{
public:
    CJITCompile(void) : m_cRef(1) {}
    virtual ~CJITCompile() {}

    // mux_IUnknown
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override {
        if (mux_IID_IUnknown == iid) {
            *ppv = static_cast<mux_IUnknown *>(static_cast<mux_IJITCompile *>(this));
        } else if (IID_IJITCompile == iid) {
            *ppv = static_cast<mux_IJITCompile *>(this);
        } else {
            *ppv = nullptr;
            return MUX_E_NOINTERFACE;
        }
        AddRef();
        return MUX_S_OK;
    }

    uint32_t AddRef(void) override { return ++m_cRef; }
    uint32_t Release(void) override {
        uint32_t n = --m_cRef;
        if (0 == n) delete this;
        return n;
    }

    // mux_IJITCompile
    MUX_RESULT CompileLuaBytecode(const uint8_t *pData, size_t nData,
        uint64_t *pKey) override
    {
        if (nullptr == pData || nullptr == pKey) return MUX_E_INVALIDARG;

        // Persistent cache key: "lua:" + sha1(bytecodes).
        std::string cache_key = "lua:" + jit_sha1_hex(pData, nData);

        // Check SQLite cache first.
        compiled_program prog;
        bool from_cache = jit_load_from_sqlite(cache_key, prog);

        if (!from_cache) {
            // Cache miss — compile from bytecodes.
            if (!compile_lua_bytecode(pData, nData, &prog)) {
                s_lua_jit_stats.compile_fail++;
                *pKey = 0;
                return MUX_E_FAIL;
            }
            // Persist to SQLite while prog.memory still exists.
            jit_store_to_sqlite(cache_key, prog);
            // Compact: extract blobs, release 4MB memory.
            jit_compact_program(prog);
        }

        // Drain before inserting, not after: a flush deferred from earlier in
        // this command would otherwise wipe the program we just compiled and
        // hand back a key that no longer resolves.
        //
        jit_flush_pending_caches();

        uint64_t key = s_next_key.fetch_add(1, std::memory_order_relaxed);
        s_lua_cache[key] = std::move(prog);
        *pKey = key;
        s_lua_jit_stats.compile_ok++;
        return MUX_S_OK;
    }

    MUX_RESULT RunCompiled(uint64_t key,
        dbref executor, dbref caller, dbref enactor,
        const UTF8 *pArgs[], int nArgs,
        UTF8 *pResult, size_t nResultMax, size_t *pnResultLen,
        void *pLuaState) override
    {
        // Apply a deferred flush before taking a pointer into the cache:
        // &it->second below is held by run_cached_program for the whole
        // execution, and a flush arriving mid-run would free it (#1316).
        // A drop here is a miss, and the caller falls back to the Lua VM.
        //
        jit_flush_pending_caches();

        auto it = s_lua_cache.find(key);
        if (it == s_lua_cache.end()) return MUX_E_NOTFOUND;

        s_lua_jit_stats.cache_hits++;

        // #1751 Phase 4: run_cached_program with a Lua state never returns
        // false after entry (CPU LIMITED, LUA ERROR, residual POST-ENTRY,
        // and generic RUN FAIL are all committed handled=true).  A false
        // here is only a pre-entry/setup failure — depth/watermarks,
        // oversize carg (#1055), get_dbt.  Nothing ran; the interpreter
        // must answer (#1837).  Do not commit #-1 LUA JIT RUN FAIL.
        //
        bool ok = run_cached_program(&it->second, executor, caller, enactor,
            pResult, nResultMax, pArgs, nArgs,
            EV_FCHECK | EV_EVAL, pLuaState);

        if (ok && nullptr != pResult
            && 0 == strncmp(reinterpret_cast<const char *>(pResult),
                            "#-1 LUA JIT POST-ENTRY DECLINE", 30)) {
            s_lua_jit_stats.post_entry_decline++;
            s_lua_jit_stats.run_fail++;
            if (pnResultLen) {
                *pnResultLen = strlen(reinterpret_cast<const char *>(pResult));
            }
            return MUX_S_OK;
        }

        if (!ok) {
            // Pre-entry only.  Leave pResult empty; caller falls back.
            //
            return MUX_E_FAIL;
        }

        if (pnResultLen) {
            *pnResultLen = strlen(reinterpret_cast<const char *>(pResult));
        }
        // Handled run (success or committed LUA ERROR / CPU LIMITED).
        // POST-ENTRY arm above already counted run_fail.
        //
        s_lua_jit_stats.run_ok++;
        return MUX_S_OK;
    }

    MUX_RESULT IsCompiled(uint64_t key, bool *pCompiled) override {
        if (nullptr == pCompiled) return MUX_E_INVALIDARG;
        *pCompiled = (s_lua_cache.find(key) != s_lua_cache.end());
        return MUX_S_OK;
    }

    MUX_RESULT Invalidate(uint64_t key) override {
        auto it = s_lua_cache.find(key);
        if (it != s_lua_cache.end()) {
            s_lua_cache.erase(it);
            s_lua_jit_stats.invalidations++;
        }
        return MUX_S_OK;
    }

private:
    uint32_t m_cRef;
};

// ---------------------------------------------------------------
// Factory creation function — called from engine_com.cpp.
// ---------------------------------------------------------------

MUX_RESULT jit_compile_create_instance(MUX_IID iid, void **ppv) {
    CJITCompile *pObj = nullptr;
    try { pObj = new CJITCompile; } catch (...) { ; }
    if (nullptr == pObj) return MUX_E_OUTOFMEMORY;

    MUX_RESULT mr = pObj->QueryInterface(iid, ppv);
    pObj->Release();
    return mr;
}
