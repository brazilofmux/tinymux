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

## Core Engine Issues

### 3. Platform Interface Driver

- **File:** `driver.cpp:384`
- **Issue:** Need to create a proper platform interface abstraction for the driver to better separate OS-specific logic from core server lifecycle management.

### ~~4. Session/Driver Separation~~

- **Fixed:** `access_list` stays in the driver (hot-path connection checks must not cross COM). Added `mux_IDriverControl::ListSiteInfo()` COM method so the engine can query the site list for `@list sites` display.
