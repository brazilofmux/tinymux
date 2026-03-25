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

// Database query ECALLs — leaf lookups, no softcode evaluation.
static constexpr uint64_t ECALL_GOOD_OBJ      = 0x150; // a0=dbref → a0=0/1

// Unicode ECALLs — heavy-weight operations that use host tables.
static constexpr uint64_t ECALL_CHR            = 0x160; // a0=input_addr, a1=output_addr → a0=0(ok)/-1(err)
static constexpr uint64_t ECALL_ORD            = 0x161; // a0=input_addr, a1=output_addr → a0=0(ok)/-1(err)
static constexpr uint64_t ECALL_TRANSLATE      = 0x162; // a0=input_addr, a1=type(0/1), a2=output_addr
static constexpr uint64_t ECALL_QUICK_WILD     = 0x163; // a0=pattern_addr, a1=data_addr → a0=0/1
static constexpr uint64_t ECALL_SORT           = 0x164; // a0=list_addr, a1=sort_type, a2=delim, a3=osep, a4=out_addr

// Lua VM ECALLs — operations that call back into the Lua interpreter.
// Require eval_ctx.lua_state != nullptr.
//
// Convention: a0-a3 carry operand guest addresses or values.
// Results written to guest memory; a0 returns status or integer result.
//
static constexpr uint64_t ECALL_LUA_LEN       = 0x300; // a0=src_addr → a0=length (integer)
static constexpr uint64_t ECALL_LUA_NEWTABLE  = 0x301; // a0=narr, a1=nrec → a0=stack_idx
static constexpr uint64_t ECALL_LUA_GETI      = 0x302; // a0=table_stk_idx, a1=int_key, a2=out_addr
static constexpr uint64_t ECALL_LUA_SETI      = 0x303; // a0=table_stk_idx, a1=int_key, a2=val_addr
static constexpr uint64_t ECALL_LUA_GETFIELD  = 0x304; // a0=table_stk_idx, a1=key_addr, a2=out_addr
static constexpr uint64_t ECALL_LUA_SETFIELD  = 0x305; // a0=table_stk_idx, a1=key_addr, a2=val_addr
static constexpr uint64_t ECALL_LUA_POPTABLE  = 0x306; // a0=table_stk_idx (cleanup)
static constexpr uint64_t ECALL_LUA_GETI_INT = 0x308; // a0=tbl_idx, a1=key → a0=value, a1=ok
static constexpr uint64_t ECALL_LUA_SETI_INT = 0x309; // a0=tbl_idx, a1=key, a2=value
static constexpr uint64_t ECALL_LUA_PIN_ARRAY= 0x30A; // a0=tbl_idx, a1=dest, a2=max → a0=count
static constexpr uint64_t ECALL_LUA_UNPIN    = 0x30B; // a0=tbl_idx, a1=src, a2=count (write-back)

// Lua bridge ECALLs — reserved range for mux.* function dispatch.
static constexpr uint64_t ECALL_LUA_BRIDGE    = 0x380; // base for Lua bridge calls
static constexpr uint64_t ECALL_LUA_NAME      = 0x380; // mux.name(dbref)
static constexpr uint64_t ECALL_LUA_OWNER     = 0x381; // mux.owner(dbref)
static constexpr uint64_t ECALL_LUA_LOCATION  = 0x382; // mux.location(dbref)
static constexpr uint64_t ECALL_LUA_GET       = 0x383; // mux.get(dbref, attr)
static constexpr uint64_t ECALL_LUA_SET       = 0x384; // mux.set(dbref, attr, val)
static constexpr uint64_t ECALL_LUA_NOTIFY    = 0x385; // mux.notify(dbref, msg)
static constexpr uint64_t ECALL_LUA_EVAL      = 0x386; // mux.eval(expr)
static constexpr uint64_t ECALL_LUA_BRIDGE_MAX= 0x38F;

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

#endif // ENGINE_API_H
