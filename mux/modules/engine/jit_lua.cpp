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

#include <cstring>
#include <cstdio>
#include <unordered_map>
#include <vector>
#include <string>

// ---------------------------------------------------------------
// Compile cache
// ---------------------------------------------------------------

static std::unordered_map<uint64_t, compiled_program> s_lua_cache;
static uint64_t s_next_key = 1;

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
};

static lua_jit_stats s_lua_jit_stats = {};

// ---------------------------------------------------------------
// Compile a Lua bytecode blob to a compiled_program.
// ---------------------------------------------------------------

static bool compile_lua_bytecode(const uint8_t *data, size_t len,
                                  compiled_program *out) {
    // Deserialize.
    lua_bc_chunk chunk;
    if (!lua_bc_load(data, len, &chunk)) {
        return false;
    }

    // Create HIR program and RV64 compiler state.
    hir_program *h = new hir_program;
    h->init();

    rv_compiler rc_state;

    // Lower Lua bytecode to HIR.
    int result = hir_lower_lua_proto(*h, rc_state, &chunk.main);
    if (result < 0) {
        delete h;
        return false;
    }

    // For multi-block programs, run SSA construction and optimization.
    if (h->n_blocks > 1) {
        hir_build_cfg(*h);
        hir_ssa_construct(*h);
        hir_optimize(*h);
    } else {
        // Single block: just constant folding.
        hir_const_fold(*h);
    }

    // Code generation: HIR → RV64.
    hir_codegen(*h, rc_state);

    // Build compiled_program output.
    out->memory = std::move(rc_state.memory);
    out->memory_size = rv_compiler::MEM_SIZE;
    out->out_addr = rc_state.final_out;
    out->out_used = rc_state.out_pool;
    out->ok = true;
    out->folds = h->folds;
    out->ecalls = h->ecalls;
    out->tier2_calls = 0;
    out->native_ops = h->native_ops;
    out->needs_jit = h->needs_jit;

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

        compiled_program prog;
        if (!compile_lua_bytecode(pData, nData, &prog)) {
            s_lua_jit_stats.compile_fail++;
            *pKey = 0;
            return MUX_E_FAIL;
        }

        uint64_t key = s_next_key++;
        s_lua_cache[key] = std::move(prog);
        *pKey = key;
        s_lua_jit_stats.compile_ok++;
        return MUX_S_OK;
    }

    MUX_RESULT RunCompiled(uint64_t key,
        dbref executor, dbref caller, dbref enactor,
        const UTF8 *pArgs[], int nArgs,
        UTF8 *pResult, size_t nResultMax, size_t *pnResultLen) override
    {
        auto it = s_lua_cache.find(key);
        if (it == s_lua_cache.end()) return MUX_E_NOTFOUND;

        s_lua_jit_stats.cache_hits++;

        bool ok = run_cached_program(&it->second, executor, caller, enactor,
            pResult, nResultMax, pArgs, nArgs);
        if (!ok) {
            s_lua_jit_stats.run_fail++;
            return MUX_E_FAIL;
        }

        if (pnResultLen) {
            *pnResultLen = strlen(reinterpret_cast<const char *>(pResult));
        }
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
