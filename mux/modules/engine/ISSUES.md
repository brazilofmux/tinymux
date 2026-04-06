# Core Engine (mux/modules/engine/) — Open Issues

Updated: 2026-04-04

## High — AST/JIT Safety & Performance

### ~~Stack overflow risk from AST recursion~~ FIXED
- **File:** `mux/modules/engine/ast.cpp`
- `ast_eval_node()` now carries an `AstEvalDepthGuard` RAII object backed by a `thread_local` counter capped at `AST_EVAL_MAX_DEPTH = 400`. Overflow sets `mudstate.bStackLimitReached` and returns, so adversarial deep ASTs (e.g., `[[[[...x]]]]` with thousands of layers) cannot blow the native C stack before the softcode limits (`func_nest_lim`, `nStackLimit`) trip on re-entry. `ast_dump()` also gained an `indent`-based cap so debug logging on pathological ASTs truncates rather than recurses unbounded.

### ~~O(n) lookup in `persistent_vm_t::attr_cache`~~ FIXED
- **File:** `mux/modules/engine/jit_compiler.cpp:1056-1170`
- The attribute cache is now a `std::unordered_map<uint64_t, attr_cache_entry>` keyed by a packed `(obj, attr_num)` word, replacing the linear `std::vector` scan on every `compile_attr()` lookup and update path. Reverified with `g++ -std=c++17 -fsyntax-only` on `jit_compiler.cpp`.

## Medium — Memory Management

### Continued use of manual `alloc_lbuf`/`free_lbuf` — MOSTLY RESOLVED
- **File:** Multiple files in `mux/modules/engine/`
- **Progress:** ~216 of 305 sites converted to `LBuf` RAII (`LBuf_Src` for fresh allocations, `LBuf_Adopt` for caller-owned `atr_get`/`atr_pget` returns) across 38 source files. Move semantics added to `LBuf` for adopt-by-value.
- **Remaining:** ~90 sites that resist mechanical conversion: `fargs[]` array stores, `did_it()` charge/runout swap patterns, `unparse_object()` returns, ping-pong buffers, and cross-function lifetimes. These would need structural refactoring or a separate `LBufPtr` type.

## High — Buffer Safety (New, 2026-04-04)

### ~~Unsafe `strcat()` in fun_rxlevel() and fun_txlevel()~~ FIXED

- Replaced the local `strcat()` list building with bounded `safe_str()`/`safe_chr()` writes into `levelbuff`, then copied the result out with `safe_str()`. Reverified with `g++ -std=c++17 -fsyntax-only -I mux/include -I mux/sqlite -I mux/modules/engine mux/modules/engine/functions.cpp`.

## Medium — SQLite Error Handling (New, 2026-04-04)

### ~~Missing `sqlite3_reset()` in CodeCachePut() error path~~ FIXED

- Added `sqlite3_reset(m_stmtCodeCachePut)` before the error return from `CodeCachePut()`. Reverified with `make -C tests/db test`.

### ~~No null pointer check for `sqlite3_column_blob()` results~~ FIXED

- `CodeCacheGet()` now rejects rows where any blob column reports `len > 0` but returns a null data pointer, resets the statement, and returns failure instead of passing invalid pointers to callers. Reverified with `make -C tests/db test`.

## Medium — JIT Safety (New, 2026-04-04)

### ~~Unsafe global state in JIT compiler~~ FIXED

- **File:** `mux/modules/engine/jit_compiler.cpp:119-121`
- All three `JITArena` static members (`s_next_id`, `s_current`, `s_arenas`) are now `thread_local`. Combined with the earlier `thread_local` conversion of `s_current_ecall_ctx`, no JIT compiler state is shared across threads. Arena IDs are per-thread (they only flow through the RV64 VM context, which is per-evaluation).

### ~~Unchecked `jit_alloc()` in `dbt_reset()`~~ FALSE ALARM

- Current `dbt_reset()` does not call `jit_alloc()`; the only `jit_alloc()` call in `dbt.cpp` is in `dbt_init()`, and that path already checks for null and returns an error.

## Low — Technical Debt

### Missing JIT support for dynamic `ulambda` args
- **File:** `mux/modules/engine/ast.cpp:1150` (approx)
- **Issue:** `ast_noeval_ulambda()` bypasses the JIT compiler because the JIT does not currently support dynamically-provided `cargs`.
- **Impact:** Reduced performance for complex anonymous functions evaluated via `ulambda()`.

### ~~`MigrateSchema()` logs versions 8 and 9 as "upgraded" before the migration succeeds~~ FIXED
- Removed the pre-success `fprintf()` calls from the v8/v9 branches so `RunMigration()` is the single source of success logging. Reverified with `make -C tests/db test`; schema versions 8 and 9 now log once per fresh database open.
