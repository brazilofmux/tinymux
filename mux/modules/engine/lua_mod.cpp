/*! \file lua_mod.cpp
 * \brief Lua 5.4 scripting — embedded in engine.so.
 *
 * Embeds a sandboxed Lua 5.4 interpreter.  Scripts live as LUA_* attributes
 * on objects.  The lua() softcode function dispatches through this module.
 *
 * All server access goes through COM interfaces — this code never touches
 * db[] or networking internals directly.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "libmux.h"
#include "modules.h"
#include "lua_mod.h"

#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <vector>

// Global pointer for bridge functions to reach the module instance.
// Safe because there is exactly one CLuaMod instance per process.
//
static CLuaMod *g_pLuaMod = nullptr;

// =========================================================================
// Per-execution context stored in Lua registry.
// =========================================================================

struct lua_exec_ctx
{
    dbref executor;
    dbref caller;
    dbref enactor;
    const UTF8 *pArgs[10];
    int nArgs;
};

#define LUA_EXEC_CTX_KEY "mux_exec_ctx"
#define LUA_MOD_KEY      "mux_lua_mod"

static lua_exec_ctx *get_exec_ctx(lua_State *L)
{
    lua_getfield(L, LUA_REGISTRYINDEX, LUA_EXEC_CTX_KEY);
    lua_exec_ctx *ctx = static_cast<lua_exec_ctx *>(lua_touserdata(L, -1));
    lua_pop(L, 1);
    return ctx;
}

static CLuaMod *get_lua_mod(lua_State *L)
{
    (void)L;
    return g_pLuaMod;
}

// =========================================================================
// mux.* bridge functions (Lua C functions)
// =========================================================================

// mux.notify(dbref, message) — send text to a player.
//
static int bridge_notify(lua_State *L)
{
    CLuaMod *mod = get_lua_mod(L);
    if (nullptr == mod) return 0;

    lua_exec_ctx *ctx = get_exec_ctx(L);
    if (nullptr == ctx) return 0;

    int target = static_cast<int>(luaL_checkinteger(L, 1));
    const char *msg = luaL_checkstring(L, 2);

    mux_INotify *pNotify = nullptr;
    MUX_RESULT mr = mux_CreateInstance(CID_Notify, nullptr, UseSameProcess,
        IID_INotify, reinterpret_cast<void **>(&pNotify));
    if (MUX_SUCCEEDED(mr) && nullptr != pNotify)
    {
        pNotify->Notify(static_cast<dbref>(target),
            reinterpret_cast<const UTF8 *>(msg));
        pNotify->Release();
    }
    return 0;
}

// mux.name(dbref) — return object name.
//
static int bridge_name(lua_State *L)
{
    int obj = static_cast<int>(luaL_checkinteger(L, 1));

    mux_IObjectInfo *pOI = nullptr;
    MUX_RESULT mr = mux_CreateInstance(CID_ObjectInfo, nullptr,
        UseSameProcess, IID_IObjectInfo,
        reinterpret_cast<void **>(&pOI));
    if (MUX_SUCCEEDED(mr) && nullptr != pOI)
    {
        const UTF8 *pName = nullptr;
        mr = pOI->GetName(static_cast<dbref>(obj), &pName);
        pOI->Release();
        if (MUX_SUCCEEDED(mr) && nullptr != pName)
        {
            lua_pushstring(L, reinterpret_cast<const char *>(pName));
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// mux.owner(dbref) — return owner dbref.
//
static int bridge_owner(lua_State *L)
{
    int obj = static_cast<int>(luaL_checkinteger(L, 1));

    mux_IObjectInfo *pOI = nullptr;
    MUX_RESULT mr = mux_CreateInstance(CID_ObjectInfo, nullptr,
        UseSameProcess, IID_IObjectInfo,
        reinterpret_cast<void **>(&pOI));
    if (MUX_SUCCEEDED(mr) && nullptr != pOI)
    {
        dbref owner;
        mr = pOI->GetOwner(static_cast<dbref>(obj), &owner);
        pOI->Release();
        if (MUX_SUCCEEDED(mr))
        {
            lua_pushinteger(L, owner);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// mux.location(dbref) — return location dbref.
//
static int bridge_location(lua_State *L)
{
    int obj = static_cast<int>(luaL_checkinteger(L, 1));

    mux_IObjectInfo *pOI = nullptr;
    MUX_RESULT mr = mux_CreateInstance(CID_ObjectInfo, nullptr,
        UseSameProcess, IID_IObjectInfo,
        reinterpret_cast<void **>(&pOI));
    if (MUX_SUCCEEDED(mr) && nullptr != pOI)
    {
        dbref loc;
        mr = pOI->GetLocation(static_cast<dbref>(obj), &loc);
        pOI->Release();
        if (MUX_SUCCEEDED(mr))
        {
            lua_pushinteger(L, loc);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// mux.get(dbref, attrname) — read an attribute value.
//
static int bridge_get(lua_State *L)
{
    lua_exec_ctx *ctx = get_exec_ctx(L);
    if (nullptr == ctx) { lua_pushnil(L); return 1; }

    int obj = static_cast<int>(luaL_checkinteger(L, 1));
    const char *attrname = luaL_checkstring(L, 2);

    mux_IAttributeAccess *pAA = nullptr;
    MUX_RESULT mr = mux_CreateInstance(CID_AttributeAccess, nullptr,
        UseSameProcess, IID_IAttributeAccess,
        reinterpret_cast<void **>(&pAA));
    if (MUX_SUCCEEDED(mr) && nullptr != pAA)
    {
        UTF8 value[8000];
        size_t nValue = 0;
        mr = pAA->GetAttribute(ctx->executor, static_cast<dbref>(obj),
            reinterpret_cast<const UTF8 *>(attrname),
            value, sizeof(value), &nValue);
        pAA->Release();
        if (MUX_SUCCEEDED(mr))
        {
            lua_pushlstring(L, reinterpret_cast<const char *>(value), nValue);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// mux.set(dbref, attrname, value) — write an attribute value.
//
static int bridge_set(lua_State *L)
{
    lua_exec_ctx *ctx = get_exec_ctx(L);
    if (nullptr == ctx) return luaL_error(L, "no execution context");

    int obj = static_cast<int>(luaL_checkinteger(L, 1));
    const char *attrname = luaL_checkstring(L, 2);
    const char *value = luaL_checkstring(L, 3);

    mux_IAttributeAccess *pAA = nullptr;
    MUX_RESULT mr = mux_CreateInstance(CID_AttributeAccess, nullptr,
        UseSameProcess, IID_IAttributeAccess,
        reinterpret_cast<void **>(&pAA));
    if (MUX_SUCCEEDED(mr) && nullptr != pAA)
    {
        mr = pAA->SetAttribute(ctx->executor, static_cast<dbref>(obj),
            reinterpret_cast<const UTF8 *>(attrname),
            reinterpret_cast<const UTF8 *>(value));
        pAA->Release();
        if (MUX_FAILED(mr))
        {
            return luaL_error(L, "permission denied");
        }
    }
    return 0;
}

// mux.eval(expression) — evaluate softcode expression.
//
static int bridge_eval(lua_State *L)
{
    lua_exec_ctx *ctx = get_exec_ctx(L);
    if (nullptr == ctx) { lua_pushnil(L); return 1; }

    const char *expr = luaL_checkstring(L, 1);

    mux_IEvaluator *pEval = nullptr;
    MUX_RESULT mr = mux_CreateInstance(CID_Evaluator, nullptr,
        UseSameProcess, IID_IEvaluator,
        reinterpret_cast<void **>(&pEval));
    if (MUX_SUCCEEDED(mr) && nullptr != pEval)
    {
        UTF8 result[8000];
        size_t nResult = 0;
        mr = pEval->Eval(ctx->executor, ctx->caller, ctx->enactor,
            reinterpret_cast<const UTF8 *>(expr),
            result, sizeof(result), &nResult);
        pEval->Release();
        if (MUX_SUCCEEDED(mr))
        {
            lua_pushlstring(L, reinterpret_cast<const char *>(result),
                nResult);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// mux.type(dbref) — return object type string.
//
static int bridge_type(lua_State *L)
{
    int obj = static_cast<int>(luaL_checkinteger(L, 1));

    mux_IObjectInfo *pOI = nullptr;
    MUX_RESULT mr = mux_CreateInstance(CID_ObjectInfo, nullptr,
        UseSameProcess, IID_IObjectInfo,
        reinterpret_cast<void **>(&pOI));
    if (MUX_SUCCEEDED(mr) && nullptr != pOI)
    {
        int type = -1;
        mr = pOI->GetType(static_cast<dbref>(obj), &type);
        pOI->Release();
        if (MUX_SUCCEEDED(mr))
        {
            // Type constants: 0=ROOM, 1=THING, 2=EXIT, 3=PLAYER
            static const char *types[] = {"ROOM", "THING", "EXIT", "PLAYER"};
            if (type >= 0 && type <= 3)
            {
                lua_pushstring(L, types[type]);
            }
            else
            {
                lua_pushstring(L, "UNKNOWN");
            }
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// mux.flags(dbref) — return flag string.
//
static int bridge_flags(lua_State *L)
{
    lua_exec_ctx *ctx = get_exec_ctx(L);
    int obj = static_cast<int>(luaL_checkinteger(L, 1));

    mux_IObjectInfo *pOI = nullptr;
    MUX_RESULT mr = mux_CreateInstance(CID_ObjectInfo, nullptr,
        UseSameProcess, IID_IObjectInfo,
        reinterpret_cast<void **>(&pOI));
    if (MUX_SUCCEEDED(mr) && nullptr != pOI)
    {
        UTF8 *pFlags = nullptr;
        dbref looker = (ctx != nullptr) ? ctx->executor : static_cast<dbref>(1);
        mr = pOI->DecodeFlags(looker, static_cast<dbref>(obj), &pFlags);
        pOI->Release();
        if (MUX_SUCCEEDED(mr) && nullptr != pFlags)
        {
            lua_pushstring(L, reinterpret_cast<const char *>(pFlags));
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// mux.isplayer(dbref) — check if object is a player.
//
static int bridge_isplayer(lua_State *L)
{
    int obj = static_cast<int>(luaL_checkinteger(L, 1));

    mux_IObjectInfo *pOI = nullptr;
    MUX_RESULT mr = mux_CreateInstance(CID_ObjectInfo, nullptr,
        UseSameProcess, IID_IObjectInfo,
        reinterpret_cast<void **>(&pOI));
    if (MUX_SUCCEEDED(mr) && nullptr != pOI)
    {
        bool bPlayer = false;
        mr = pOI->IsPlayer(static_cast<dbref>(obj), &bPlayer);
        pOI->Release();
        if (MUX_SUCCEEDED(mr))
        {
            lua_pushboolean(L, bPlayer ? 1 : 0);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

// mux.isconnected(dbref) — check if player is connected.
//
static int bridge_isconnected(lua_State *L)
{
    int obj = static_cast<int>(luaL_checkinteger(L, 1));

    mux_IObjectInfo *pOI = nullptr;
    MUX_RESULT mr = mux_CreateInstance(CID_ObjectInfo, nullptr,
        UseSameProcess, IID_IObjectInfo,
        reinterpret_cast<void **>(&pOI));
    if (MUX_SUCCEEDED(mr) && nullptr != pOI)
    {
        bool bConn = false;
        mr = pOI->IsConnected(static_cast<dbref>(obj), &bConn);
        pOI->Release();
        if (MUX_SUCCEEDED(mr))
        {
            lua_pushboolean(L, bConn ? 1 : 0);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

// mux.pennies(dbref) — return object pennies.
//
static int bridge_pennies(lua_State *L)
{
    int obj = static_cast<int>(luaL_checkinteger(L, 1));

    mux_IObjectInfo *pOI = nullptr;
    MUX_RESULT mr = mux_CreateInstance(CID_ObjectInfo, nullptr,
        UseSameProcess, IID_IObjectInfo,
        reinterpret_cast<void **>(&pOI));
    if (MUX_SUCCEEDED(mr) && nullptr != pOI)
    {
        int pennies = 0;
        mr = pOI->GetPennies(static_cast<dbref>(obj), &pennies);
        pOI->Release();
        if (MUX_SUCCEEDED(mr))
        {
            lua_pushinteger(L, pennies);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

// mux.iswizard(dbref) — check if object is a wizard.
//
static int bridge_iswizard(lua_State *L)
{
    int obj = static_cast<int>(luaL_checkinteger(L, 1));

    mux_IPermissions *pPerms = nullptr;
    MUX_RESULT mr = mux_CreateInstance(CID_Permissions, nullptr,
        UseSameProcess, IID_IPermissions,
        reinterpret_cast<void **>(&pPerms));
    if (MUX_SUCCEEDED(mr) && nullptr != pPerms)
    {
        bool bWizard = false;
        mr = pPerms->IsWizard(static_cast<dbref>(obj), &bWizard);
        pPerms->Release();
        if (MUX_SUCCEEDED(mr))
        {
            lua_pushboolean(L, bWizard ? 1 : 0);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

// mux.controls(who, what) — check if who controls what.
//
static int bridge_controls(lua_State *L)
{
    int who = static_cast<int>(luaL_checkinteger(L, 1));
    int what = static_cast<int>(luaL_checkinteger(L, 2));

    mux_IPermissions *pPerms = nullptr;
    MUX_RESULT mr = mux_CreateInstance(CID_Permissions, nullptr,
        UseSameProcess, IID_IPermissions,
        reinterpret_cast<void **>(&pPerms));
    if (MUX_SUCCEEDED(mr) && nullptr != pPerms)
    {
        bool bControls = false;
        mr = pPerms->HasControl(static_cast<dbref>(who),
            static_cast<dbref>(what), &bControls);
        pPerms->Release();
        if (MUX_SUCCEEDED(mr))
        {
            lua_pushboolean(L, bControls ? 1 : 0);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

// Bridge function table.
//
static const luaL_Reg bridge_funcs[] = {
    {"notify",      bridge_notify},
    {"pemit",       bridge_notify},
    {"name",        bridge_name},
    {"owner",       bridge_owner},
    {"location",    bridge_location},
    {"type",        bridge_type},
    {"flags",       bridge_flags},
    {"isplayer",    bridge_isplayer},
    {"isconnected", bridge_isconnected},
    {"pennies",     bridge_pennies},
    {"get",         bridge_get},
    {"set",         bridge_set},
    {"eval",        bridge_eval},
    {"iswizard",    bridge_iswizard},
    {"controls",    bridge_controls},
    {nullptr,       nullptr}
};

// =========================================================================
// Custom memory allocator with limit enforcement.
// =========================================================================

void *CLuaMod::LuaAlloc(void *ud, void *ptr, size_t osize, size_t nsize)
{
    CLuaMod *self = static_cast<CLuaMod *>(ud);

    if (nsize == 0)
    {
        // Free.
        if (ptr != nullptr)
        {
            self->m_nMemUsed -= osize;
            free(ptr);
        }
        return nullptr;
    }

    // Check memory limit.
    //
    size_t delta = nsize - (ptr ? osize : 0);
    if (self->m_nMemUsed + delta > static_cast<size_t>(self->m_nMemLimit))
    {
        self->m_bMemExceeded = true;
        return nullptr;  // Allocation denied — triggers Lua OOM error.
    }

    void *newptr = realloc(ptr, nsize);
    if (newptr != nullptr)
    {
        self->m_nMemUsed += delta;
        if (self->m_nMemUsed > self->m_nMemPeak)
        {
            self->m_nMemPeak = self->m_nMemUsed;
        }
    }
    return newptr;
}

// =========================================================================
// Instruction count hook — enforces execution limits.
// =========================================================================

void CLuaMod::InsnCountHook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    luaL_error(L, "instruction limit exceeded");
}

// =========================================================================
// Lua state creation and sandbox setup.
// =========================================================================

bool CLuaMod::CreateLuaState(void)
{
    m_nMemUsed = 0;
    m_nMemPeak = 0;
    m_bMemExceeded = false;

    m_L = lua_newstate(LuaAlloc, this);
    if (nullptr == m_L)
    {
        return false;
    }

    // Open whitelisted libraries.
    //
    luaL_requiref(m_L, "_G", luaopen_base, 1);
    lua_pop(m_L, 1);
    luaL_requiref(m_L, "string", luaopen_string, 1);
    lua_pop(m_L, 1);
    luaL_requiref(m_L, "table", luaopen_table, 1);
    lua_pop(m_L, 1);
    luaL_requiref(m_L, "math", luaopen_math, 1);
    lua_pop(m_L, 1);
    luaL_requiref(m_L, "utf8", luaopen_utf8, 1);
    lua_pop(m_L, 1);
    luaL_requiref(m_L, "coroutine", luaopen_coroutine, 1);
    lua_pop(m_L, 1);

    // Remove dangerous functions from the global table.
    //
    static const char *blocked[] = {
        "load", "loadfile", "dofile", "require",
        "rawget", "rawset", "rawequal", "rawlen",
        "collectgarbage", nullptr
    };
    for (int i = 0; blocked[i] != nullptr; i++)
    {
        lua_pushnil(m_L);
        lua_setglobal(m_L, blocked[i]);
    }

    // Remap print to do nothing (scripts should use mux.notify).
    //
    lua_pushcfunction(m_L, [](lua_State *) -> int { return 0; });
    lua_setglobal(m_L, "print");

    // Register mux.* bridge table.
    //
    luaL_newlib(m_L, bridge_funcs);
    lua_setglobal(m_L, "mux");

    // Store module pointer in registry for bridge function access.
    //
    lua_pushlightuserdata(m_L, this);
    lua_setfield(m_L, LUA_REGISTRYINDEX, LUA_MOD_KEY);

    return true;
}

void CLuaMod::DestroyLuaState(void)
{
    if (nullptr != m_L)
    {
        lua_close(m_L);
        m_L = nullptr;
    }
}

// =========================================================================
// Chunk execution with sandbox.
// =========================================================================

bool CLuaMod::ExecuteChunk(lua_State *L, dbref executor, dbref caller,
    dbref enactor, const UTF8 *pArgs[], int nArgs,
    UTF8 *pResult, size_t nResultMax, size_t *pnResultLen)
{
    // The compiled chunk is on top of the Lua stack.  Set up the execution
    // context and call it.

    // Store execution context in the registry.
    //
    lua_exec_ctx ctx;
    ctx.executor = executor;
    ctx.caller = caller;
    ctx.enactor = enactor;
    ctx.nArgs = (nArgs > 10) ? 10 : nArgs;
    for (int i = 0; i < ctx.nArgs; i++)
    {
        ctx.pArgs[i] = (pArgs != nullptr) ? pArgs[i] : nullptr;
    }
    lua_pushlightuserdata(L, &ctx);
    lua_setfield(L, LUA_REGISTRYINDEX, LUA_EXEC_CTX_KEY);

    // Inject mux.executor, mux.caller, mux.enactor, mux.args into
    // the global mux table.
    //
    lua_getglobal(L, "mux");
    lua_pushinteger(L, executor);
    lua_setfield(L, -2, "executor");
    lua_pushinteger(L, caller);
    lua_setfield(L, -2, "caller");
    lua_pushinteger(L, enactor);
    lua_setfield(L, -2, "enactor");

    // Build mux.args table.
    //
    int nSafe = (nArgs > 0 && pArgs != nullptr) ? nArgs : 0;
    lua_createtable(L, nSafe, 0);
    for (int i = 0; i < nSafe; i++)
    {
        if (pArgs[i] != nullptr)
        {
            lua_pushstring(L, reinterpret_cast<const char *>(pArgs[i]));
        }
        else
        {
            lua_pushstring(L, "");
        }
        lua_rawseti(L, -2, i + 1);
    }
    lua_setfield(L, -2, "args");
    lua_pop(L, 1);  // pop mux table

    // Set instruction count hook.
    //
    m_bMemExceeded = false;
    lua_sethook(L, InsnCountHook, LUA_MASKCOUNT, m_nInsnLimit);

    // Call the chunk (it's below the mux table stuff we just popped).
    //
    int status = lua_pcall(L, 0, 1, 0);

    // Remove the hook.
    //
    lua_sethook(L, nullptr, 0, 0);

    // Clear execution context.
    //
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, LUA_EXEC_CTX_KEY);

    if (status != LUA_OK)
    {
        // Error.
        const char *errmsg = lua_tostring(L, -1);
        if (nullptr == errmsg) errmsg = "unknown error";

        if (m_bMemExceeded)
        {
            m_stats.mem_limit_hits++;
            errmsg = "memory limit exceeded";
        }
        else if (strstr(errmsg, "instruction limit") != nullptr)
        {
            m_stats.insn_limit_hits++;
        }

        m_stats.errors++;

        int n = snprintf(reinterpret_cast<char *>(pResult), nResultMax,
            "#-1 LUA ERROR: %s", errmsg);
        if (n < 0) n = 0;
        if (static_cast<size_t>(n) >= nResultMax) n = static_cast<int>(nResultMax - 1);
        pResult[n] = '\0';
        *pnResultLen = static_cast<size_t>(n);

        lua_pop(L, 1);
        return false;
    }

    // Success — convert return value to string.
    //
    size_t len = 0;
    const char *result = nullptr;

    if (lua_isnil(L, -1) || lua_isnone(L, -1))
    {
        result = "";
        len = 0;
    }
    else if (lua_isboolean(L, -1))
    {
        result = lua_toboolean(L, -1) ? "1" : "0";
        len = 1;
    }
    else
    {
        result = lua_tolstring(L, -1, &len);
        if (nullptr == result)
        {
            result = "";
            len = 0;
        }
    }

    if (len >= nResultMax)
    {
        len = nResultMax - 1;
    }
    memcpy(pResult, result, len);
    pResult[len] = '\0';
    *pnResultLen = len;

    lua_pop(L, 1);
    return true;
}

// =========================================================================
// CLuaMod — main module class.
// =========================================================================

CLuaMod::CLuaMod(void) : m_cRef(1),
    m_pILog(nullptr),
    m_pIServerEventsControl(nullptr),
    m_pINotify(nullptr),
    m_pIObjectInfo(nullptr),
    m_pIAttributeAccess(nullptr),
    m_pIEvaluator(nullptr),
    m_pIPermissions(nullptr),
    m_pIJITCompile(nullptr),
    m_L(nullptr),
    m_nInsnLimit(LUA_DEFAULT_INSN_LIMIT),
    m_nMemLimit(LUA_DEFAULT_MEM_LIMIT),
    m_nMemUsed(0),
    m_nMemPeak(0),
    m_bMemExceeded(false),
    m_nCacheMaxSize(LUA_DEFAULT_CACHE_SIZE)
{
    memset(&m_stats, 0, sizeof(m_stats));
    g_pLuaMod = this;
}

MUX_RESULT CLuaMod::FinalConstruct(void)
{
    MUX_RESULT mr;

    // Acquire logging interface.
    //
    mr = mux_CreateInstance(CID_Log, nullptr, UseSameProcess,
        IID_ILog, reinterpret_cast<void **>(&m_pILog));
    if (MUX_FAILED(mr))
    {
        return mr;
    }

    // Register for server events.
    //
    mux_IServerEventsSink *pSink = nullptr;
    mr = QueryInterface(IID_IServerEventsSink,
        reinterpret_cast<void **>(&pSink));
    if (MUX_SUCCEEDED(mr))
    {
        mr = mux_CreateInstance(CID_ServerEventsSource, nullptr,
            UseSameProcess, IID_IServerEventsControl,
            reinterpret_cast<void **>(&m_pIServerEventsControl));
        if (MUX_SUCCEEDED(mr))
        {
            m_pIServerEventsControl->Advise(pSink);
        }
        pSink->Release();
    }

    // Acquire core interfaces.
    //
    mux_CreateInstance(CID_Notify, nullptr, UseSameProcess,
        IID_INotify, reinterpret_cast<void **>(&m_pINotify));

    mux_CreateInstance(CID_ObjectInfo, nullptr, UseSameProcess,
        IID_IObjectInfo, reinterpret_cast<void **>(&m_pIObjectInfo));

    mux_CreateInstance(CID_AttributeAccess, nullptr, UseSameProcess,
        IID_IAttributeAccess,
        reinterpret_cast<void **>(&m_pIAttributeAccess));

    mux_CreateInstance(CID_Evaluator, nullptr, UseSameProcess,
        IID_IEvaluator, reinterpret_cast<void **>(&m_pIEvaluator));

    mux_CreateInstance(CID_Permissions, nullptr, UseSameProcess,
        IID_IPermissions, reinterpret_cast<void **>(&m_pIPermissions));

    // Acquire JIT compile interface (optional — graceful degradation).
    mux_CreateInstance(CID_JITCompile, nullptr, UseSameProcess,
        IID_IJITCompile, reinterpret_cast<void **>(&m_pIJITCompile));

    // Create the Lua state.
    //
    if (!CreateLuaState())
    {
        if (nullptr != m_pILog)
        {
            bool fStarted;
            m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("ERR"));
            if (fStarted)
            {
                m_pILog->log_text(T("Lua module: failed to create Lua state."));
                m_pILog->end_log();
            }
        }
        return MUX_E_FAIL;
    }

    // Log that we are alive.
    //
    if (nullptr != m_pILog)
    {
        bool fStarted;
        m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO"));
        if (fStarted)
        {
            m_pILog->log_text(T("Lua module loaded (Lua 5.4)."));
            m_pILog->end_log();
        }
    }

    return MUX_S_OK;
}

CLuaMod::~CLuaMod()
{
    CacheClear();
    DestroyLuaState();

    if (nullptr != m_pILog)
    {
        bool fStarted;
        m_pILog->start_log(&fStarted, LOG_ALWAYS, T("INI"), T("INFO"));
        if (fStarted)
        {
            m_pILog->log_text(T("Lua module unloading."));
            m_pILog->end_log();
        }
        m_pILog->Release();
        m_pILog = nullptr;
    }

    if (nullptr != m_pIServerEventsControl)
    {
        m_pIServerEventsControl->Release();
        m_pIServerEventsControl = nullptr;
    }

    if (nullptr != m_pINotify)
    {
        m_pINotify->Release();
        m_pINotify = nullptr;
    }

    if (nullptr != m_pIObjectInfo)
    {
        m_pIObjectInfo->Release();
        m_pIObjectInfo = nullptr;
    }

    if (nullptr != m_pIAttributeAccess)
    {
        m_pIAttributeAccess->Release();
        m_pIAttributeAccess = nullptr;
    }

    if (nullptr != m_pIEvaluator)
    {
        m_pIEvaluator->Release();
        m_pIEvaluator = nullptr;
    }

    if (nullptr != m_pIPermissions)
    {
        m_pIPermissions->Release();
        m_pIPermissions = nullptr;
    }

    if (nullptr != m_pIJITCompile)
    {
        m_pIJITCompile->Release();
        m_pIJITCompile = nullptr;
    }

    if (g_pLuaMod == this)
    {
        g_pLuaMod = nullptr;
    }
}

MUX_RESULT CLuaMod::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_ILuaControl *>(this);
    }
    else if (IID_ILuaControl == iid)
    {
        *ppv = static_cast<mux_ILuaControl *>(this);
    }
    else if (IID_IServerEventsSink == iid)
    {
        *ppv = static_cast<mux_IServerEventsSink *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CLuaMod::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CLuaMod::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

// =========================================================================
// Bytecode cache — LRU keyed by source text.
// =========================================================================

// LoadCached: try the cache first, compile on miss.
// On success, the compiled chunk is on top of the Lua stack.
// Returns true on success, false on compile error (error string on stack).
//
bool CLuaMod::LoadCached(const char *source, size_t nSource,
    const char *chunkname)
{
    std::string key(source, nSource);

    auto it = m_cache.find(key);
    if (it != m_cache.end())
    {
        // Cache hit — push the cached chunk.
        //
        m_stats.cache_hits++;
        lua_rawgeti(m_L, LUA_REGISTRYINDEX, it->second.lua_ref);

        // Move to front of LRU.
        //
        m_cache_lru.erase(it->second.lru_it);
        m_cache_lru.push_front(key);
        it->second.lru_it = m_cache_lru.begin();
        return true;
    }

    // Cache miss — compile.
    //
    m_stats.cache_misses++;
    int status = luaL_loadbufferx(m_L, source, nSource, chunkname, "t");
    if (status != LUA_OK)
    {
        return false;  // Error string is on top of stack.
    }

    // Store in cache: push a copy, get a registry reference.
    //
    lua_pushvalue(m_L, -1);  // duplicate the chunk
    int ref = luaL_ref(m_L, LUA_REGISTRYINDEX);

    // Evict if full.
    //
    if (static_cast<int>(m_cache.size()) >= m_nCacheMaxSize)
    {
        CacheEvict();
    }

    // Insert.
    //
    m_cache_lru.push_front(key);
    cache_entry entry;
    entry.lua_ref = ref;
    entry.lru_it = m_cache_lru.begin();
    entry.jit_key = 0;
    entry.jit_eligible = false;
    m_cache[key] = entry;

    return true;
}

void CLuaMod::CacheEvict(void)
{
    if (m_cache_lru.empty())
    {
        return;
    }

    // Remove the least recently used entry (back of list).
    //
    const std::string &oldest = m_cache_lru.back();
    auto it = m_cache.find(oldest);
    if (it != m_cache.end())
    {
        luaL_unref(m_L, LUA_REGISTRYINDEX, it->second.lua_ref);
        if (it->second.jit_key != 0 && nullptr != m_pIJITCompile)
        {
            m_pIJITCompile->Invalidate(it->second.jit_key);
        }
        m_cache.erase(it);
    }
    m_cache_lru.pop_back();
}

void CLuaMod::CacheClear(void)
{
    for (auto &pair : m_cache)
    {
        if (nullptr != m_L)
        {
            luaL_unref(m_L, LUA_REGISTRYINDEX, pair.second.lua_ref);
        }
        if (pair.second.jit_key != 0 && nullptr != m_pIJITCompile)
        {
            m_pIJITCompile->Invalidate(pair.second.jit_key);
        }
    }
    m_cache.clear();
    m_cache_lru.clear();
}

// =========================================================================
// lua_dump writer callback — accumulates bytecode into a vector.
// =========================================================================

struct dump_buffer {
    std::vector<uint8_t> data;
};

static int dump_writer(lua_State *L, const void *p, size_t sz, void *ud) {
    (void)L;
    dump_buffer *buf = static_cast<dump_buffer *>(ud);
    const uint8_t *bytes = static_cast<const uint8_t *>(p);
    buf->data.insert(buf->data.end(), bytes, bytes + sz);
    return 0;
}

// =========================================================================
// TryJIT: attempt to JIT-compile a cached chunk.
// The chunk must be on top of the Lua stack.
// Returns true if JIT succeeded and result is in pResult.
// Returns false on JIT failure (caller should fall through to lua_pcall).
// Does NOT pop the chunk from the stack.
// =========================================================================

bool CLuaMod::TryJIT(cache_entry &entry, dbref executor, dbref caller,
    dbref enactor, const UTF8 *pArgs[], int nArgs,
    UTF8 *pResult, size_t nResultMax, size_t *pnResultLen)
{
    if (nullptr == m_pIJITCompile) return false;

    // Already tried and failed?
    if (entry.jit_eligible) {
        // Already have a compiled key? Run it.
        if (entry.jit_key != 0) {
            MUX_RESULT mr = m_pIJITCompile->RunCompiled(entry.jit_key,
                executor, caller, enactor, pArgs, nArgs,
                pResult, nResultMax, pnResultLen);
            return MUX_SUCCEEDED(mr);
        }
        return false;  // Previously failed to compile.
    }

    // First attempt: dump the chunk to bytecode and try JIT compilation.
    entry.jit_eligible = true;

    // lua_dump expects the function on top of stack.  We have it there
    // from LoadCached.  Push a copy so we don't consume it.
    lua_pushvalue(m_L, -1);

    dump_buffer buf;
    int dump_status = lua_dump(m_L, dump_writer, &buf, 0);
    lua_pop(m_L, 1);  // pop the copy

    if (dump_status != 0 || buf.data.empty()) {
        return false;
    }

    // Try to compile.
    uint64_t key = 0;
    MUX_RESULT mr = m_pIJITCompile->CompileLuaBytecode(
        buf.data.data(), buf.data.size(), &key);
    if (MUX_FAILED(mr) || key == 0) {
        return false;  // JIT doesn't support this bytecode; fall through.
    }

    entry.jit_key = key;

    // Run the compiled program.
    mr = m_pIJITCompile->RunCompiled(key, executor, caller, enactor,
        pArgs, nArgs, pResult, nResultMax, pnResultLen);
    return MUX_SUCCEEDED(mr);
}

// =========================================================================
// mux_ILuaControl implementation.
// =========================================================================

MUX_RESULT CLuaMod::CallAttr(dbref executor, dbref caller, dbref enactor,
    dbref obj, const UTF8 *pAttrName,
    const UTF8 *pArgs[], int nArgs,
    UTF8 *pResult, size_t nResultMax, size_t *pnResultLen)
{
    if (nullptr == m_L)
    {
        return MUX_E_FAIL;
    }

    // Read the attribute via COM interface (permission-checked).
    //
    if (nullptr == m_pIAttributeAccess)
    {
        return MUX_E_FAIL;
    }

    UTF8 source[8000];
    size_t nSource = 0;
    MUX_RESULT mr = m_pIAttributeAccess->GetAttribute(executor, obj,
        pAttrName, source, sizeof(source), &nSource);
    if (MUX_FAILED(mr))
    {
        int n = snprintf(reinterpret_cast<char *>(pResult), nResultMax,
            "#-1 LUA ERROR: cannot read attribute");
        if (n < 0) n = 0;
        if (static_cast<size_t>(n) >= nResultMax) n = static_cast<int>(nResultMax - 1);
        pResult[n] = '\0';
        *pnResultLen = static_cast<size_t>(n);
        return MUX_S_OK;
    }

    if (nSource == 0)
    {
        pResult[0] = '\0';
        *pnResultLen = 0;
        return MUX_S_OK;
    }

    m_stats.calls++;

    // Compile (or load from cache).
    //
    char chunkname[128];
    snprintf(chunkname, sizeof(chunkname), "@#%d/%s",
        static_cast<int>(obj),
        reinterpret_cast<const char *>(pAttrName));

    if (!LoadCached(reinterpret_cast<const char *>(source), nSource,
                    chunkname))
    {
        const char *errmsg = lua_tostring(m_L, -1);
        if (nullptr == errmsg) errmsg = "compile error";
        m_stats.errors++;
        int n = snprintf(reinterpret_cast<char *>(pResult), nResultMax,
            "#-1 LUA ERROR: %s", errmsg);
        if (n < 0) n = 0;
        if (static_cast<size_t>(n) >= nResultMax) n = static_cast<int>(nResultMax - 1);
        pResult[n] = '\0';
        *pnResultLen = static_cast<size_t>(n);
        lua_pop(m_L, 1);
        return MUX_S_OK;
    }

    // Try JIT execution.  The compiled chunk is on top of the Lua stack.
    // If JIT succeeds, pop the chunk and return.
    //
    std::string cache_key(reinterpret_cast<const char *>(source), nSource);
    auto cache_it = m_cache.find(cache_key);
    if (cache_it != m_cache.end())
    {
        if (TryJIT(cache_it->second, executor, caller, enactor,
                   pArgs, nArgs, pResult, nResultMax, pnResultLen))
        {
            lua_pop(m_L, 1);  // pop the chunk
            return MUX_S_OK;
        }
    }

    // Fall through to Lua VM execution.
    //
    ExecuteChunk(m_L, executor, caller, enactor, pArgs, nArgs,
        pResult, nResultMax, pnResultLen);
    return MUX_S_OK;
}

MUX_RESULT CLuaMod::Eval(dbref executor, dbref caller, dbref enactor,
    const UTF8 *pSource, size_t nSource,
    UTF8 *pResult, size_t nResultMax, size_t *pnResultLen)
{
    if (nullptr == m_L)
    {
        return MUX_E_FAIL;
    }

    // Wizard-only check.
    //
    if (nullptr != m_pIPermissions)
    {
        bool bWizard = false;
        m_pIPermissions->IsWizard(executor, &bWizard);
        if (!bWizard)
        {
            int n = snprintf(reinterpret_cast<char *>(pResult), nResultMax,
                "#-1 LUA ERROR: wizard-only");
            if (n < 0) n = 0;
            if (static_cast<size_t>(n) >= nResultMax) n = static_cast<int>(nResultMax - 1);
            pResult[n] = '\0';
            *pnResultLen = static_cast<size_t>(n);
            return MUX_S_OK;
        }
    }

    m_stats.calls++;

    if (!LoadCached(reinterpret_cast<const char *>(pSource), nSource,
                    "@inline"))
    {
        const char *errmsg = lua_tostring(m_L, -1);
        if (nullptr == errmsg) errmsg = "compile error";
        m_stats.errors++;
        int n = snprintf(reinterpret_cast<char *>(pResult), nResultMax,
            "#-1 LUA ERROR: %s", errmsg);
        if (n < 0) n = 0;
        if (static_cast<size_t>(n) >= nResultMax) n = static_cast<int>(nResultMax - 1);
        pResult[n] = '\0';
        *pnResultLen = static_cast<size_t>(n);
        lua_pop(m_L, 1);
        return MUX_S_OK;
    }

    // Try JIT execution.
    //
    std::string cache_key(reinterpret_cast<const char *>(pSource), nSource);
    auto cache_it = m_cache.find(cache_key);
    if (cache_it != m_cache.end())
    {
        if (TryJIT(cache_it->second, executor, caller, enactor,
                   nullptr, 0, pResult, nResultMax, pnResultLen))
        {
            lua_pop(m_L, 1);  // pop the chunk
            return MUX_S_OK;
        }
    }

    // Fall through to Lua VM execution.
    //
    ExecuteChunk(m_L, executor, caller, enactor, nullptr, 0,
        pResult, nResultMax, pnResultLen);
    return MUX_S_OK;
}

MUX_RESULT CLuaMod::GetStats(size_t *pnCalls, size_t *pnErrors,
    size_t *pnInsnLimitHits, size_t *pnMemLimitHits,
    size_t *pnBytesUsed,
    size_t *pnCacheHits, size_t *pnCacheMisses,
    size_t *pnCacheEntries)
{
    *pnCalls = m_stats.calls;
    *pnErrors = m_stats.errors;
    *pnInsnLimitHits = m_stats.insn_limit_hits;
    *pnMemLimitHits = m_stats.mem_limit_hits;
    *pnBytesUsed = m_nMemUsed;
    *pnCacheHits = m_stats.cache_hits;
    *pnCacheMisses = m_stats.cache_misses;
    *pnCacheEntries = m_cache.size();
    return MUX_S_OK;
}

MUX_RESULT CLuaMod::SetLimits(int nInsnLimit, int nMemLimit)
{
    if (nInsnLimit > 0)
    {
        m_nInsnLimit = nInsnLimit;
    }
    if (nMemLimit > 0)
    {
        m_nMemLimit = nMemLimit;
    }
    return MUX_S_OK;
}

// =========================================================================
// mux_IServerEventsSink stubs.
// =========================================================================

void CLuaMod::startup(void) { }
void CLuaMod::presync_database(void) { }
void CLuaMod::presync_database_sigsegv(void) { }
void CLuaMod::dump_database(int dump_type) { (void)dump_type; }
void CLuaMod::dump_complete_signal(void) { }
void CLuaMod::shutdown(void) { CacheClear(); DestroyLuaState(); }
void CLuaMod::dbck(void) { }
void CLuaMod::connect(dbref player, int isnew, int num) { (void)player; (void)isnew; (void)num; }
void CLuaMod::disconnect(dbref player, int num) { (void)player; (void)num; }
void CLuaMod::data_create(dbref object) { (void)object; }
void CLuaMod::data_clone(dbref clone, dbref source) { (void)clone; (void)source; }
void CLuaMod::data_free(dbref object) { (void)object; }

// =========================================================================
// =========================================================================
// Factory function — called from engine_com.cpp's CLuaModFactory.
// =========================================================================

MUX_RESULT lua_mod_create_instance(MUX_IID iid, void **ppv) {
    CLuaMod *pLuaMod = nullptr;
    try { pLuaMod = new CLuaMod; } catch (...) { ; }
    if (nullptr == pLuaMod) return MUX_E_OUTOFMEMORY;

    MUX_RESULT mr = pLuaMod->FinalConstruct();
    if (MUX_SUCCEEDED(mr)) {
        mr = pLuaMod->QueryInterface(iid, ppv);
    }
    pLuaMod->Release();
    return mr;
}
