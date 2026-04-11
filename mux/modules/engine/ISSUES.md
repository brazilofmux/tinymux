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

## Critical — JIT Codegen Bugs (New, 2026-04-10)

### RV64 JAL offsets not range-checked against 21-bit signed immediate
- **File:** `mux/modules/engine/hir_codegen.cpp:578, 1726, 1750, 1772`
- **Issue:** Four Tier-2 call sites compute `int32_t offset = static_cast<int32_t>(target - pc);` and hand the raw value to `rv_JAL(rd, offset)`. RV64's JAL immediate is 21-bit signed (±1 MiB). The encoder silently drops the high bits; there is no check that the offset fits. Today the blob heap lives in the same JIT arena as the compiled code so offsets stay small, but if the arena grows past ~1 MiB (or the blob pool is relocated), every Tier-2 call — including the `rv64_strtod` fast path and all FP intrinsic stubs (`HIR_FCALL1`/`HIR_FCALL2`) — silently jumps to a wrong address.
- **Fix:** Clip/assert on `offset`, or fall back to `AUIPC`+`JALR` (32-bit PC-relative) when `|offset| > 0xFFFFC`.

### Unchecked `iv.value` index into `result.addr[]` / `result.reg[]` / `result.spill_slot[]`
- **File:** `mux/modules/engine/hir_codegen.cpp:696, 954, 968`
- **Issue:** The linear-scan allocator writes `result.addr[iv.value]`, `result.reg[iv.value]`, and `result.spill_slot[iv.value]` without checking `iv.value < HIR_MAX_INSNS`. `iv.value` is a HIR instruction index and is normally bounded by the HIR builder, but any later pass that synthesizes extra virtuals (e.g., PHI rewrites, HIR_FCALL expansion) could push past `HIR_MAX_INSNS` and corrupt the adjacent allocator state. Add an explicit bounds assertion.

## Medium — JIT Memory & Offset Hygiene (New, 2026-04-10)

### Stale compiled entry leaked on `persistent_vm_t::attr_cache` replacement
- **File:** `mux/modules/engine/jit_compiler.cpp:1139-1140`
- **Issue:** When an attribute's `mod_count` changes, `compile_attr()` overwrites the cache entry in place without reclaiming the old code region. A comment already says "stale code heap space is leaked; future: reclaim". Under a write-heavy softcode workload this causes unbounded code-heap growth until the pool exhausts.
- **Opportunity:** Maintain a free-list of abandoned code regions per arena, or bump `arena_id` on invalidation so the pool can be reused wholesale when the old generation has no live references.

### Branch offset backpatching assumes `code.size() * 4` fits in 21 bits
- **File:** `mux/modules/engine/hir_codegen.cpp` (multiple backpatch sites)
- **Issue:** All backpatch arithmetic casts `(target - source) * 4` to `int32_t` then pipes through the B-type/JAL encoders. No check that the B-type branch (12-bit signed, ±4 KiB) or JAL (21-bit signed, ±1 MiB) range is honored. A compiled attribute larger than ~4 KiB for branches or ~1 MiB for jumps would silently produce invalid instructions. Add an assertion and fall back to trampolines where needed.

## High — Lua Module Refcount & Concurrency (New, 2026-04-10)

### Non-atomic `CLuaMod::m_cRef` / factory `m_cRef`
- **File:** `mux/modules/engine/lua_mod.h:159`, `mux/modules/engine/lua_mod.cpp:1004-1016`
- **Issue:** `m_cRef` is a plain `uint32_t` and `AddRef`/`Release` use `m_cRef++` / `m_cRef--`. The comsys/mail modules already converted to `std::atomic<uint32_t>` (see fixed entries above) to close the decrement/zero-check race for double-delete. The Lua module was not converted.
- **Fix:** Change `m_cRef` to `std::atomic<uint32_t>` in both `CLuaMod` and its factory, matching the comsys/mail pattern.

### `s_next_key++` in Lua JIT cache is not atomic
- **File:** `mux/modules/engine/jit_lua.cpp:168` (approx)
- **Issue:** The Lua JIT-compiled chunk cache mints keys via `uint64_t key = s_next_key++;` on a plain static. Two concurrent `CompileLuaBytecode()` calls can collide on the same key, and the cache map can lose the earlier entry — producing a dangling reference if another evaluator is still executing against it.
- **Fix:** `std::atomic<uint64_t> s_next_key{0}; uint64_t key = s_next_key.fetch_add(1, std::memory_order_relaxed);`

### `LuaAlloc` can drive `m_nMemUsed` negative on realloc failure
- **File:** `mux/modules/engine/lua_mod.cpp:556-571`
- **Issue:** `LuaAlloc()` accounts `m_nMemUsed -= osize` on free and `m_nMemUsed += delta` on grow. On `realloc()` failure the grow path leaves `m_nMemUsed` unchanged, but the subsequent free of the *original* pointer still subtracts `osize`. If any prior grow failed, the running total drifts and eventually wraps the unsigned counter to a huge value, defeating the memory limit check.
- **Fix:** Only update `m_nMemUsed` after a successful allocation; update the decrement path to match the actual allocator bookkeeping.

### `int n = snprintf(...)` cast to `size_t` without error check
- **File:** `mux/modules/engine/lua_mod.cpp:758` (approx)
- **Issue:** `int n = snprintf(buf, sz, ...); if ((size_t)n < nResultMax) ...` — a negative return (encoding error) casts to a huge `size_t` that passes the comparison. The same pattern appears in websocket.cpp (already reported). Guard with `n >= 0` first.

## Medium — SQLite Backend Error Handling (New, 2026-04-10)

### `GetAttribute` blob returned without null-check
- **File:** `mux/modules/engine/sqlitedb.cpp` (approximately `GetAttribute()` around line 1364)
- **Issue:** After a successful `sqlite3_step() == SQLITE_ROW`, the attribute-fetch path reads `sqlite3_column_bytes()` as `blobLen` and then `memcpy`s from `sqlite3_column_blob()` without checking that the blob pointer is non-null when `blobLen > 0`. Per the SQLite docs, `sqlite3_column_blob()` may return `NULL` even when `bytes > 0` in OOM conditions. Add a null guard and treat as "not found".

### `GetAllAttributes` callback receives unvalidated blob pointers
- **File:** `mux/modules/engine/sqlitedb.cpp` (approximately `GetAllAttributes()` around line 1562)
- **Issue:** Same pattern as `GetAttribute` — the row callback hands `sqlite3_column_blob(stmt, 1)` directly to the consumer without a null guard. An OOM-triggered null blob becomes a crash in the consumer.

### `CodeCacheFlush` reset ordering on `sqlite3_step` error
- **File:** `mux/modules/engine/sqlitedb.cpp` (approximately `CodeCacheFlush()` around line 1972)
- **Issue:** The flush path calls `sqlite3_reset()` *before* `sqlite3_step()`. If `sqlite3_step()` returns an error (`SQLITE_IOERR`, `SQLITE_FULL`, …) the statement is left with unreset error state; the next use fails spuriously. Reset on the error return path.

### `RunMigration` rollback silently ignored on error
- **File:** `mux/modules/engine/sqlitedb.cpp` (approximately `RunMigration()` around line 356)
- **Issue:** The schema-migration failure path calls `sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr)` with a null `errmsg`. If `ROLLBACK` itself fails (busy, I/O error, …) the failure is dropped on the floor and the migration aborts with the DB in an indeterminate transaction state. Log the rollback result and escalate to `abort_dump_no_restart()` if it fails.

### WAL checkpoint does not retry on `SQLITE_BUSY`
- **File:** `mux/modules/engine/sqlitedb.cpp` (approximately `RunCheckpoint()` around line 2116)
- **Issue:** `sqlite3_wal_checkpoint_v2()` returns immediately on busy. `@dump` then reports success while the WAL file keeps growing because the checkpoint was never completed. Retry a few times with short backoff (the engine is single-threaded, so busy should be rare but is possible under `dbconvert -m`).

## Low — SQLite Defensive Hygiene (New, 2026-04-10)

### `sqlite3_column_text()` consumed without null-check in `LoadAllAttrNames`
- **File:** `mux/modules/engine/sqlitedb.cpp` (approximately `LoadAllAttrNames()` around line 1656)
- **Issue:** Attribute-name loading passes `(const char*)sqlite3_column_text(stmt, 1)` directly into callers expecting a valid C string. A NULL text column (possible after OOM or constraint violation) crashes. Skip rows with null name columns.

## Low — Lua Bytecode Hardening (New, 2026-04-10)

### `read_size()` varint loop lacks length cap
- **File:** `mux/modules/engine/lua_bytecode.cpp:62-68` (approx)
- **Issue:** `while ((b & 0x80) == 0)` loops over varint bytes accumulating into `size_t`. A malformed bytecode with >10 continuation bytes can overflow `size_t` before the caller's bounds check catches the resulting length. Cap iterations at `sizeof(size_t) * 8 / 7 + 1`.

## Low — Technical Debt
