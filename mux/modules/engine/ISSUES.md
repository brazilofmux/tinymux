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

### Unsafe `strcat()` in fun_rxlevel() and fun_txlevel()

- **File:** `mux/modules/engine/functions.cpp:5608-5609, 5641-5642`
- **Issue:** Both functions build a space-separated level list using `strcat()` into a fixed `levelbuff[2048]` with no bounds checking. While the current max (32 levels x 9 bytes) fits, this pattern is fragile and inconsistent with the project's use of `safe_str()`/`safe_chr()` elsewhere.
- **Impact:** Buffer overflow if level names or count change.
- **Recommendation:** Replace `strcat()` with `safe_str()` or `strncat()` with explicit remaining-space tracking.

## Medium — SQLite Error Handling (New, 2026-04-04)

### Missing `sqlite3_reset()` in CodeCachePut() error path

- **File:** `mux/modules/engine/sqlitedb.cpp:1952-1958`
- **Issue:** If `sqlite3_step()` fails, the prepared statement is not reset before returning false. Other functions (CodeCacheGet, InsertObject) properly reset on error paths.
- **Recommendation:** Add `sqlite3_reset(m_stmtCodeCachePut)` before `return false`.

### No null pointer check for `sqlite3_column_blob()` results

- **File:** `mux/modules/engine/sqlitedb.cpp:1890-1913`
- **Issue:** `sqlite3_column_blob()` can return NULL for NULL column values. The returned pointers (`memory_blob`, `code_blob`, `deps_blob`) are stored without null checks; callers may assume non-null.
- **Recommendation:** Validate critical blobs are non-null before returning success.

## Medium — JIT Safety (New, 2026-04-04)

### Unsafe global state in JIT compiler

- **File:** `mux/modules/engine/jit_compiler.cpp:120, 2266, 2317-2323`
- **Issue:** Static variables `s_current`, `s_arenas`, `s_current_ecall_ctx` and the save/restore pattern for eval context have no synchronization. Safe under current single-threaded evaluation but blocks any future multi-threading.
- **Recommendation:** Use `thread_local` for `s_current_ecall_ctx`; document single-threaded assumption.

### Unchecked `jit_alloc()` in `dbt_reset()`

- **File:** `mux/modules/engine/dbt.cpp:~201-212`
- **Issue:** `dbt_init()` checks `jit_alloc()` for null, but `dbt_reset()` does not. A null return would cause subsequent null dereference.
- **Recommendation:** Add the same null check as `dbt_init()`.

## Low — Technical Debt

### Missing JIT support for dynamic `ulambda` args
- **File:** `mux/modules/engine/ast.cpp:1150` (approx)
- **Issue:** `ast_noeval_ulambda()` bypasses the JIT compiler because the JIT does not currently support dynamically-provided `cargs`.
- **Impact:** Reduced performance for complex anonymous functions evaluated via `ulambda()`.

### ~~`MigrateSchema()` logs versions 8 and 9 as "upgraded" before the migration succeeds~~ FIXED
- Removed the pre-success `fprintf()` calls from the v8/v9 branches so `RunMigration()` is the single source of success logging. Reverified with `make -C tests/db test`; schema versions 8 and 9 now log once per fresh database open.
