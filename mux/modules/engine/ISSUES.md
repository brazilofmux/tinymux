# MUX Engine — Open Issues & Optimization Roadmap

## JIT/DBT Pipeline Optimization

These issues are based on the findings in `docs/JIT-PERF-INVESTIGATION.md`.

### 1. Level 2: Tier 2 Coverage Expansion

Move more frequently used builtin functions into pre-compiled RV64 guest code blobs to eliminate ECALL overhead.

**Completed:** 20 functions unblocked (BEFORE, AFTER, DELETE, ELEMENTS,
WORDPOS, REMOVE, REVWORDS, LNUM, LADD, LMAX, LMIN, LAND, LOR,
ISNUM, ISINT, DEC2HEX, HEX2DEC, ISDBREF, CHR, ORD) — parity-tested
via smoke suite.

**Note:** `time()`, `secs()`, `get()`, `xget()`, `name()`, `owner()`,
`flags()` cannot be Tier 2 — they require database access or engine
state. `member()`, `words()`, `extract()` were already Tier 2.
`ISDBREF` uses `ECALL_GOOD_OBJ` for database validation; `CHR`/`ORD`
use `ECALL_CHR`/`ECALL_ORD` for Unicode encoding + NFC normalization
and grapheme cluster extraction. All are leaf lookups with no
re-entrancy risk, keeping surrounding code in the JIT.

**No blocked functions remain.**  All previously blocked functions have
been unblocked via co_* wrappers or ECALLs:
SECURE (→ `co_secure_wrap`), SQUISH (→ `co_compress_wrap`),
TRANSLATE (→ `ECALL_TRANSLATE`), STRMATCH/MATCH/GRAB/GRABALL
(→ `ECALL_QUICK_WILD`), SORT (→ `ECALL_SORT` calling native
`sort_to_buffer()` with full DUCET collation support).

### 2. Phase 3: Concurrent Softcode Evaluation

Implement the framework for evaluating softcode on multiple cores simultaneously.

- **Tasks:**
  - Transition `mudstate`, JIT caches, and parser state to `thread_local` storage.
  - Implement optimistic read validation using the `mod_count` system.
  - Migrate `s_compile_cache` and `s_attr_mod_counts` to thread-safe structures (atomic/RW locks).
  - Ensure DBT contexts are thread-safe or per-thread.

### 5. Windows JIT/DBT: System V → Windows x64 Calling Convention

- **Status:** Windows is AST-only. `TINYMUX_JIT` compiles but crashes at runtime.
- **Root cause:** The DBT code generator (`dbt.cpp`) emits x86_64 native code
  using System V AMD64 calling convention (args in RDI, RSI, RDX, RCX, R8, R9).
  Windows x64 uses RCX, RDX, R8, R9 (4 register args + 32-byte shadow space).
- **Scope:** ~31 direct RDI/RSI references in emit_stub_* functions,
  `x64_arg_regs[]` table (line 829), `emit_trampoline()` (line 3046),
  all co_* generic stubs, strtod/strlen/memcpy call sites.
  RSI and RDI are callee-saved on Windows but caller-saved on System V.
  Windows requires 32 bytes of shadow space before every `call`.
- **Fix:** Ifdef `x64_arg_regs`, trampoline, stub prologue/epilogue,
  and shadow space allocation for `WIN32`. Significant but mechanical.

## COM & Module System Issues

### Memory Management: SIZE_HACK Pointer Arithmetic

- **File:** `db.cpp:2850-2868`
- **Issue:** Database growth code shifts `db` pointer backward by `SIZE_HACK` before `memcpy`, then reassigns. The sequence in the "else" (first allocation) branch sets `db = newdb` unshifted, calls `initialize_objects(0, SIZE_HACK)`, then overwrites with `db = newdb + SIZE_HACK`. The intermediate unshifted pointer creates a window where `db` is temporarily invalid.
- **Risk:** If `initialize_objects` accesses `db` during that window, it operates on the wrong base.

### Silent failure when storage interfaces fail to initialize

- **File:** `engine_com.cpp:2783-2834`
- **Issue:** `pComsysStorage` and `pMailStorage` creation via `mux_CreateInstance` can fail without any error log. The code checks `MUX_SUCCEEDED(mr)` for the inner block but has no "else" clause to report failure.
- **Impact:** Comsys or mail silently disabled with no diagnostic.

### exp3 module: unchecked interface acquisitions

- **File:** `../exp3/exp3.cpp:305-325`
- **Issue:** Five `mux_CreateInstance()` calls for core interfaces with no return value checks and no nullptr verification before use in `Call()`.

## Core Engine Issues

### 3. Platform Interface Driver

- **File:** `driver.cpp:384`, `modules.cpp:1172`
- **Issue:** Need to create a proper platform interface abstraction for the driver.
- **Goal:** Refactor `CPlatform` from `modules.cpp` into separate files (`platform_unix.cpp`, `platform_win32.cpp`). This will eliminate the current `#ifdef` tangle and allow for cleaner, OS-specific implementations of signals, helper processes, and file descriptor management.

### 4. Engine & LibMux Unit Testing

- **Status:** Backend logic has unit tests in `db/`, but the core server engine and foundational libraries (`mux/lib`) lack comprehensive unit tests.
- **Opportunity:** Add unit tests for:
  - `stringutil.cpp`: UTF-8 handling, case conversion, and buffer management.
  - `funmath.cpp`: Floating point and integer math edge cases.
  - `ast.cpp` & `eval.cpp`: AST construction and evaluation logic in isolation from the full server.

### ~~5. Session/Driver Separation~~

- **Fixed:** `access_list` stays in the driver (hot-path connection checks must not cross COM). Added `mux_IDriverControl::ListSiteInfo()` COM method so the engine can query the site list for `@list sites` display.
