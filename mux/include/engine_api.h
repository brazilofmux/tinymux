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

// Forward declaration (full definition in functions.h).
struct tagFun;
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

#endif // ENGINE_API_H
