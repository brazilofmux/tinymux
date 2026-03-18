/*! \file lua_mod.h
 * \brief Lua Module — Server-side Lua 5.4 scripting as a loadable module
 *
 * This module embeds a Lua 5.4 interpreter with a sandboxed environment.
 * Scripts are stored as LUA_* attributes on objects and executed via the
 * lua() softcode function or the @lua command.
 *
 * Core dependencies are accessed exclusively through COM interfaces:
 *   mux_INotify           - player notification
 *   mux_IObjectInfo       - object property queries
 *   mux_IAttributeAccess  - attribute read/write
 *   mux_IEvaluator        - softcode evaluation
 *   mux_IPermissions      - permission checks
 *   mux_ILog              - logging
 */

#ifndef LUA_MOD_H
#define LUA_MOD_H

#include <unordered_map>
#include <list>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

// Default resource limits.
//
#define LUA_DEFAULT_INSN_LIMIT   100000
#define LUA_DEFAULT_MEM_LIMIT    1048576   // 1 MB
#define LUA_DEFAULT_CACHE_SIZE   256

// Statistics.
//
struct lua_mod_stats
{
    size_t calls;
    size_t errors;
    size_t insn_limit_hits;
    size_t mem_limit_hits;
    size_t peak_mem_bytes;
    size_t cache_hits;
    size_t cache_misses;
};

class CLuaMod : public mux_ILuaControl, public mux_IServerEventsSink
{
private:
    mux_ILog                  *m_pILog;
    mux_IServerEventsControl  *m_pIServerEventsControl;
    mux_INotify               *m_pINotify;
    mux_IObjectInfo           *m_pIObjectInfo;
    mux_IAttributeAccess      *m_pIAttributeAccess;
    mux_IEvaluator            *m_pIEvaluator;
    mux_IPermissions          *m_pIPermissions;
    mux_IJITCompile           *m_pIJITCompile;

    // Lua state - global, shared across all executions.
    //
    lua_State *m_L;

    // Resource limits.
    //
    int m_nInsnLimit;
    int m_nMemLimit;

    // Memory tracking for custom allocator.
    //
    size_t m_nMemUsed;
    size_t m_nMemPeak;
    bool   m_bMemExceeded;

    // Execution statistics.
    //
    lua_mod_stats m_stats;

    // Bytecode cache — LRU keyed by source text.
    // Values are Lua registry references to compiled chunks.
    //
    struct cache_entry
    {
        int lua_ref;                         // LUA_REGISTRYINDEX ref
        std::list<std::string>::iterator lru_it;  // Position in LRU list
        uint64_t jit_key;                    // JIT compiled program key (0 = not compiled)
        bool jit_eligible;                   // true if JIT compilation was attempted
    };
    std::unordered_map<std::string, cache_entry> m_cache;
    std::list<std::string> m_cache_lru;      // Front = most recent
    int m_nCacheMaxSize;

    // Internal helpers.
    //
    bool CreateLuaState(void);
    void DestroyLuaState(void);
    void RegisterBridgeFunctions(void);
    bool SetupSandbox(lua_State *L);
    bool LoadCached(const char *source, size_t nSource, const char *chunkname);
    void CacheEvict(void);
    void CacheClear(void);
    bool TryJIT(cache_entry &entry, dbref executor, dbref caller,
        dbref enactor, const UTF8 *pArgs[], int nArgs,
        UTF8 *pResult, size_t nResultMax, size_t *pnResultLen);
    bool ExecuteChunk(lua_State *L, dbref executor, dbref caller,
        dbref enactor, const UTF8 *pArgs[], int nArgs,
        UTF8 *pResult, size_t nResultMax, size_t *pnResultLen);

    static void InsnCountHook(lua_State *L, lua_Debug *ar);
    static void *LuaAlloc(void *ud, void *ptr, size_t osize, size_t nsize);

public:
    // mux_IUnknown
    //
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    uint32_t   AddRef(void) override;
    uint32_t   Release(void) override;

    // mux_ILuaControl
    //
    MUX_RESULT CallAttr(dbref executor, dbref caller, dbref enactor,
        dbref obj, const UTF8 *pAttrName,
        const UTF8 *pArgs[], int nArgs,
        UTF8 *pResult, size_t nResultMax, size_t *pnResultLen) override;

    MUX_RESULT Eval(dbref executor, dbref caller, dbref enactor,
        const UTF8 *pSource, size_t nSource,
        UTF8 *pResult, size_t nResultMax, size_t *pnResultLen) override;

    MUX_RESULT GetStats(size_t *pnCalls, size_t *pnErrors,
        size_t *pnInsnLimitHits, size_t *pnMemLimitHits,
        size_t *pnBytesUsed,
        size_t *pnCacheHits, size_t *pnCacheMisses,
        size_t *pnCacheEntries) override;

    MUX_RESULT SetLimits(int nInsnLimit, int nMemLimit) override;

    // mux_IServerEventsSink
    //
    void startup(void) override;
    void presync_database(void) override;
    void presync_database_sigsegv(void) override;
    void dump_database(int dump_type) override;
    void dump_complete_signal(void) override;
    void shutdown(void) override;
    void dbck(void) override;
    void connect(dbref player, int isnew, int num) override;
    void disconnect(dbref player, int num) override;
    void data_create(dbref object) override;
    void data_clone(dbref clone, dbref source) override;
    void data_free(dbref object) override;

    CLuaMod(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CLuaMod();

private:
    uint32_t m_cRef;
};

// Factory function for engine_com.cpp registration.
MUX_RESULT lua_mod_create_instance(MUX_IID iid, void **ppv);

#endif // LUA_MOD_H
