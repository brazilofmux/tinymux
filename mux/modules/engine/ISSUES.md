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

### ~~5. Windows JIT/DBT: System V → Windows x64 Calling Convention~~ DONE

- **Resolved:** Separate `dbt_x64_win64.cpp` backend (2,992 lines) with proper
  Win64 ABI: RCX/RDX/R8/R9 argument registers, 32-byte shadow space, RSI/RDI
  callee-saved. Selected automatically by `configure.ac` on mingw/cygwin/msys.

### 6. Apple Silicon DBT: W^X Memory Model

- **Status:** Not yet implemented. `configure` errors out on `aarch64-*-darwin*`.
- **Planned file:** `dbt_a64_apple.cpp`
- **Issue:** Apple Silicon enforces W^X (Write XOR Execute) for JIT memory.
  The existing `dbt_a64_sysv.cpp` uses standard `mprotect()` which doesn't
  work on macOS. Needs `MAP_JIT`, `pthread_jit_write_protect_np()` toggle,
  and `sys_icache_invalidate()` for cache coherency.
- **Calling convention:** Identical AAPCS64 — only the JIT memory model differs
  from the Linux AArch64 backend.
- **Blocked on:** Lack of Apple Silicon hardware for testing.

### 7. RISC-V Native Passthrough DBT

- **Status:** Not yet implemented. `configure` errors out on `riscv64`.
- **Planned file:** `dbt_rv64_native.cpp` (~100 lines)
- **Issue:** Trivial backend — guest RV64 code IS native code. Translation
  block cache points directly into guest memory. Just needs `mprotect()`
  and fence.i for I-cache sync.
- **Blocked on:** Lack of RV64 hardware for testing.

## COM & Module System Issues

### ~~Memory Management: SIZE_HACK Pointer Arithmetic~~ FALSE ALARM

- On inspection, the code is correct. In the first-allocation "else" branch, `db = newdb` (unshifted) is set before `initialize_objects(0, SIZE_HACK)` fills the `#-1` padding slot at `db[0]`. Then `db = newdb + SIZE_HACK` shifts so `db[0]` addresses the first real object and `db[-1]` is the padding. No invalid window exists.

### ~~Silent failure when storage interfaces fail to initialize~~ FIXED

- Added `else` clauses with `log_printf` diagnostics when `CID_ComsysStorage` or `CID_MailStorage` creation fails.

### ~~exp3 module: unchecked interface acquisitions~~ FIXED

- All five `mux_CreateInstance()` calls in exp3 `FinalConstruct()` now check return values and bail early on failure.

## Core Engine Issues

### ~~3. Platform Interface Driver~~ DONE

- **Resolved:** Extracted CPlatform and CPlatformFactory (~440 lines) from `modules.cpp`
  into `platform.cpp`. Added to `DRIVER_SRC` in `Makefile.am`. `modules.cpp` now
  contains only COM dispatch and CConnectionManager/CDriverControl. Future
  per-platform files (`platform_apple.cpp`, etc.) can replace or supplement
  `platform.cpp` cleanly.

### 4. Engine & LibMux Unit Testing — IN PROGRESS

- **Status:** `tests/libmux/` harness created with 29 tests covering mux_atol,
  mux_atof, mux_i64toa, safe buffer writing, StringClone, mux_stricmp,
  mux_strupr/mux_strlwr, trim_spaces, mux_strncpy. DB tests moved to `tests/db/`.
- **Remaining:** UTF-8 multi-byte edge cases, numeric overflow/underflow,
  LBUF boundary behavior, and AST/eval isolation tests.

### ~~5. Session/Driver Separation~~

- **Fixed:** `access_list` stays in the driver (hot-path connection checks must not cross COM). Added `mux_IDriverControl::ListSiteInfo()` COM method so the engine can query the site list for `@list sites` display.
