/*! \file engine_api.h
 * \brief Engine API function index table for DBT/JIT dispatch.
 *
 * Provides O(1) function lookup by integer index, eliminating the
 * string-based hash lookup in the ECALL hot path.  The compiler
 * resolves function names to indices at compile time; the JIT
 * emits ECALL_CALL_INDEX (0x101) with a0 = function index.
 */

#ifndef ENGINE_API_H
#define ENGINE_API_H

#include <cstdint>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// Forward declarations.
struct tagFun;
struct compiled_program;
typedef struct tagFun FUN;

// ECALL numbers.
static constexpr uint64_t ECALL_EXIT       = 93;
static constexpr uint64_t ECALL_CALL_FUNC  = 0x100;  // string-based dispatch
static constexpr uint64_t ECALL_CALL_INDEX = 0x101;  // index-based dispatch
static constexpr uint64_t ECALL_SETQ       = 0x102;  // q-register write-through
static constexpr uint64_t ECALL_SETQ_PACK  = 0x130;  // efficient register sync

static constexpr uint64_t ECALL_ARENA_ALLOC   = 0x110; // a0=size -> a0=arena_id, a1=offset
static constexpr uint64_t ECALL_ARENA_REF     = 0x111; // a0=arena_id
static constexpr uint64_t ECALL_ARENA_RELEASE = 0x112; // a0=arena_id

static constexpr uint64_t ECALL_DMA_SUBMIT    = 0x120; // a0=window, a1=length, a2=op
static constexpr uint64_t ECALL_DMA_ACK       = 0x121; // -> a0=window (next free)

// Float/double conversion ECALLs.
static constexpr uint64_t ECALL_FTOA          = 0x140; // a0=double bits, a1=output addr
static constexpr uint64_t ECALL_ATOF          = 0x141; // a0=string addr → fa0=double
static constexpr uint64_t ECALL_LUA_FTOA      = 0x142; // a0=double bits, a1=output addr (Lua tostring rules)

// Database query ECALLs — leaf lookups, no softcode evaluation.
static constexpr uint64_t ECALL_GOOD_OBJ      = 0x150; // a0=dbref → a0=0/1

// Unicode ECALLs — heavy-weight operations that use host tables.
static constexpr uint64_t ECALL_CHR            = 0x160; // a0=input_addr, a1=output_addr → a0=0(ok)/-1(err)
static constexpr uint64_t ECALL_ORD            = 0x161; // a0=input_addr, a1=output_addr → a0=0(ok)/-1(err)
static constexpr uint64_t ECALL_TRANSLATE      = 0x162; // a0=input_addr, a1=type(0/1), a2=output_addr
static constexpr uint64_t ECALL_QUICK_WILD     = 0x163; // a0=pattern_addr, a1=data_addr → a0=0/1
static constexpr uint64_t ECALL_SORT           = 0x164; // a0=list_addr, a1=sort_type, a2=delim, a3=osep, a4=out_addr

// Persistent VM re-entrant call.
// Saves ctx, runs inner function via dbt_resume, restores ctx.
//   a0 = entry_pc of compiled function
//   a1 = guest addr of output buffer (for inner result)
// Returns: a0 = bytes written to output buffer (0 on failure)
//
static constexpr uint64_t ECALL_CALL_COMPILED = 0x200;

// Attribute resolution + compile.
// Resolves an attribute on an object, compiles the body into the
// persistent VM code heap, and returns the entry point.
//   a0 = dbref of target object
//   a1 = guest addr of attribute name string
// Returns: a0 = entry_pc (0 on failure)
//          a1 = out_addr (where the compiled function writes its result)
//          a2 = aflags (AF_NOEVAL etc.)
//
static constexpr uint64_t ECALL_COMPILE_ATTR  = 0x201;

// Lua VM ECALLs — operations that call back into the Lua interpreter.
// Require eval_ctx.lua_state != nullptr.
//
// Convention: a0-a3 carry operand guest addresses or values.
// Results written to guest memory; a0 returns status or integer result.
//
static constexpr uint64_t ECALL_LUA_NEWTABLE  = 0x301; // a0=narr, a1=nrec → a0=stack_idx
static constexpr uint64_t ECALL_LUA_GETI_INT = 0x308; // a0=tbl_idx, a1=key → a0=value, a1=ok
static constexpr uint64_t ECALL_LUA_SETI_INT = 0x309; // a0=tbl_idx, a1=key, a2=value
static constexpr uint64_t ECALL_LUA_LEN_INT  = 0x30A; // a0=tbl_idx → a0=len, a1=ok
static constexpr uint64_t ECALL_LUA_GETFIELD_INT = 0x30B; // a0=tbl_idx, a1=key addr → a0=value, a1=ok
static constexpr uint64_t ECALL_LUA_SETFIELD_INT = 0x30C; // a0=tbl_idx, a1=key addr, a2=value
static constexpr uint64_t ECALL_LUA_GETGLOBAL = 0x30D; // a0=key addr → a0=stack_idx, a1=ok
static constexpr uint64_t ECALL_LUA_GETFIELD_REF = 0x30E; // a0=tbl_idx, a1=key addr → a0=stack_idx, a1=ok
static constexpr uint64_t ECALL_LUA_CALL_INT = 0x30F; // a0=fn_idx, a1=nargs, a2=args addr → a0=int result, a1=ok
static constexpr uint64_t ECALL_LUA_CALL_STR = 0x310; // a0=fn_idx, a1=nargs|argkinds, a2..a4=args, a5=out addr, a6=out size → a0=len, a1=ok
static constexpr uint64_t ECALL_LUA_GETFIELD_FLT = 0x311; // a0=tbl_idx, a1=key addr → a0=double bits, a1=ok
static constexpr uint64_t ECALL_LUA_CALL_VOID = 0x312; // a0=fn_idx, a1=nargs|argkinds, a2..a4=args; result discarded → a1=ok
static constexpr uint64_t ECALL_LUA_LIMITED  = 0x313; // back-edge budget exhausted: aborts the run (declines to the interpreter)
static constexpr uint64_t ECALL_LUA_INSN_BUDGET = 0x314; // () -> a0 = current lua_instruction_limit (#1745 runtime rebinding)
// Typed call result: leave the first pcall result on the Lua stack and
// return its stack index (TY_LUA_HANDLE).  Marshal only at the softcode
// boundary (#1764 shape 2).
static constexpr uint64_t ECALL_LUA_CALL_VAL = 0x315; // a0=fn, a1=nargs|kinds, a2..a4=args → a0=stack_idx, a1=ok
static constexpr uint64_t ECALL_LUA_MARSHAL  = 0x316; // a0=stack_idx, a1=out addr, a2=out size → a0=len (fun_lua rules)
static constexpr uint64_t ECALL_LUA_TOBOOL   = 0x317; // a0=stack_idx → a0=0/1 (Lua truthiness: only nil/false falsy)

// Lua bridge ECALLs — reserved range for mux.* function dispatch.
static constexpr uint64_t ECALL_LUA_BRIDGE    = 0x380; // base for Lua bridge calls
static constexpr uint64_t ECALL_LUA_BRIDGE_MAX= 0x38F;

// Alarm check for JIT back-edge budgeting.
// Returns: a0 = 0 (ok, budget refilled) or 1 (alarmed, must exit).
static constexpr uint64_t ECALL_CHECK_ALARM   = 0x400;

// Maximum number of indexed functions.
static constexpr int ENGINE_API_MAX_FUNCS = 512;

// The function index table: engine_api_table[i] → FUN*.
// Populated by engine_api_init() during init_functab().
// Index 0 is reserved (invalid).
//
extern FUN *engine_api_table[ENGINE_API_MAX_FUNCS];
extern int   engine_api_count;  // number of valid entries (1..count-1)

// Initialize the function index table from builtin_functions.
// Must be called after init_functab().
//
void engine_api_init();

// Sync function aliases (from netmux.conf / alias.conf) into the
// JIT lookup map.  Must be called after cf_read().
//
void engine_api_sync_aliases();

// Look up a function index by uppercase name.
// Returns 0 if not found.
//
int engine_api_lookup(const char *name);

// Per-attribute modification counter for JIT cache invalidation.
// Incremented on every atr_add/atr_clr.  The JIT records mod_count
// at compile time and checks for staleness at runtime.
//
void     attr_mod_count_inc(dbref obj, int attrnum);
uint32_t attr_mod_count_get(dbref obj, int attrnum);
void     attr_mod_count_invalidate_all();

// Two-phase bulk invalidation for transactional object deletion.
// Phase 1: collect attr list + current counters (before delete, no mutation).
// Phase 2: apply increments (after commit only).
//
std::vector<std::pair<uint64_t, uint32_t>>
         attr_mod_count_collect_object(dbref obj);
void     attr_mod_count_apply_increments(
             const std::vector<std::pair<uint64_t, uint32_t>> &collected);

// Sort helper for ECALL_SORT — complete sort-and-format to buffer.
//
size_t sort_to_buffer(const UTF8 *list_in, char sort_type_char,
                      unsigned char delim, unsigned char osep,
                      UTF8 *out_buf, size_t out_max);

// SQLite code cache helpers (shared by softcode JIT and Lua JIT).
//
std::string jit_sha1_hex(const void *data, size_t len);
extern std::string s_blob_version;
void jit_store_to_sqlite(const std::string &key, const compiled_program &prog);
bool jit_load_from_sqlite(const std::string &key, compiled_program &out);
void jit_compact_program(compiled_program &prog);

// Lua JIT counters, owned by jit_lua.cpp.  Reported by jitstats() so the
// Lua path is observable at all: without this the counters are incremented
// and never read, and a Lua JIT that compiles but never runs looks exactly
// like a healthy one, because every failure falls back to the interpreter
// and still produces correct results (#1316).
//
struct lua_jit_counters {
    uint64_t compile_ok;
    uint64_t compile_fail;
    uint64_t run_ok;
    uint64_t run_fail;
    uint64_t cache_hits;
    uint64_t invalidations;
    uint64_t post_entry_decline;  // #1751 Phase 0; long-term target 0
};
void jit_lua_get_stats(lua_jit_counters *out);
void jit_lua_reset_stats(void);
// Count a post-entry decline that was committed outside RunCompiled
// (e.g. effect_refused in TryJIT).  #1751 Phase 0 / 0.5.
//
void jit_lua_note_post_entry_decline(void);
// Drop every in-process Lua compiled_program so a code_cache flush is not
// followed by runs that still hold native code from the previous build.
//
void jit_lua_clear_cache(void);

// Apply a jitstats(flush) that was deferred because a compiled program was
// still executing when it was requested.  Call before taking a pointer into
// either program cache; it is a no-op unless a flush is pending and the JIT
// is quiet (#1316).
//
void jit_flush_pending_caches(void);

#endif // ENGINE_API_H
