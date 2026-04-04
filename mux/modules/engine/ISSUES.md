# Core Engine (mux/modules/engine/) — Open Issues

Updated: 2026-04-04

## High — AST/JIT Safety & Performance

### Stack overflow risk from AST recursion
- **File:** `mux/modules/engine/ast.cpp`
- **Issue:** `ast_eval_node()` and `ast_dump()` use recursion to traverse the AST. While `mudconf.func_nest_lim` provides some protection, extremely deep ASTs (especially from generated or malicious softcode) could still overflow the stack if the per-frame overhead is high.
- **Impact:** Server crash due to stack overflow.
- **Recommendation:** Implement an iterative evaluator or use an explicit stack to limit recursion depth more rigorously.

### O(n) lookup in `persistent_vm_t::attr_cache`
- **File:** `mux/modules/engine/jit_compiler.cpp`
- **Issue:** The attribute cache used by the JIT compiler uses a `std::vector` and O(n) linear search for lookups.
- **Impact:** Performance degradation as the number of compiled attributes grows.
- **Recommendation:** Use a `std::unordered_map` with `(obj, attr_num)` as the key for O(1) lookups.

## Medium — Memory Management

### Continued use of manual `alloc_lbuf`/`free_lbuf`
- **File:** Multiple files in `mux/modules/engine/`
- **Issue:** Many functions still use manual memory management for large buffers, which is prone to leaks in error paths.
- **Impact:** Potential memory leaks and use-after-free bugs.
- **Recommendation:** Accelerate migration to `LBuf` RAII wrapper or `std::string`.

## High — Buffer Safety (New, 2026-04-04)

### ~~Unsafe `strcat()` in fun_rxlevel() and fun_txlevel()~~ FIXED

- Replaced the local `strcat()` list building with bounded `safe_str()`/`safe_chr()` writes into `levelbuff`, then copied the result out with `safe_str()`. Reverified with `g++ -std=c++17 -fsyntax-only -I mux/include -I mux/sqlite -I mux/modules/engine mux/modules/engine/functions.cpp`.

## Medium — SQLite Error Handling (New, 2026-04-04)

### ~~Missing `sqlite3_reset()` in CodeCachePut() error path~~ FIXED

- Added `sqlite3_reset(m_stmtCodeCachePut)` before the error return from `CodeCachePut()`. Reverified with `make -C tests/db test`.

### ~~No null pointer check for `sqlite3_column_blob()` results~~ FIXED

- `CodeCacheGet()` now rejects rows where any blob column reports `len > 0` but returns a null data pointer, resets the statement, and returns failure instead of passing invalid pointers to callers. Reverified with `make -C tests/db test`.

## Medium — JIT Safety (New, 2026-04-04)

### Unsafe global state in JIT compiler

- **File:** `mux/modules/engine/jit_compiler.cpp:120, 2266, 2317-2323`
- **Issue:** Static variables `s_current`, `s_arenas`, `s_current_ecall_ctx` and the save/restore pattern for eval context have no synchronization. Safe under current single-threaded evaluation but blocks any future multi-threading.
- **Progress:** `s_current_ecall_ctx` and the JIT arena cursor `s_current` are now `thread_local`, which removes the most direct per-thread context/cursor hazards. The arena registry (`s_arenas`) and related global bookkeeping are still process-global and still block broader concurrent evaluation.

### ~~Unchecked `jit_alloc()` in `dbt_reset()`~~ FALSE ALARM

- Current `dbt_reset()` does not call `jit_alloc()`; the only `jit_alloc()` call in `dbt.cpp` is in `dbt_init()`, and that path already checks for null and returns an error.

## Low — Technical Debt

### Missing JIT support for dynamic `ulambda` args
- **File:** `mux/modules/engine/ast.cpp:1150` (approx)
- **Issue:** `ast_noeval_ulambda()` bypasses the JIT compiler because the JIT does not currently support dynamically-provided `cargs`.
- **Impact:** Reduced performance for complex anonymous functions evaluated via `ulambda()`.

### ~~`MigrateSchema()` logs versions 8 and 9 as "upgraded" before the migration succeeds~~ FIXED
- Removed the pre-success `fprintf()` calls from the v8/v9 branches so `RunMigration()` is the single source of success logging. Reverified with `make -C tests/db test`; schema versions 8 and 9 now log once per fresh database open.
