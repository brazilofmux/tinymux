# MUX Engine — Open Issues & Optimization Roadmap

## JIT/DBT Pipeline Optimization

These issues are based on the findings in `docs/JIT-PERF-INVESTIGATION.md`.

### 1. Level 2: Tier 2 Coverage Expansion

Move more frequently used builtin functions into pre-compiled RV64 guest code blobs to eliminate ECALL overhead.

**Completed:** 17 functions unblocked (BEFORE, AFTER, DELETE, ELEMENTS,
WORDPOS, REMOVE, REVWORDS, LNUM, LADD, LMAX, LMIN, LAND, LOR,
ISNUM, ISINT, DEC2HEX, HEX2DEC) — parity-tested via smoke suite.

**Note:** `time()`, `secs()`, `get()`, `xget()`, `name()`, `owner()`,
`flags()` cannot be Tier 2 — they require database access or engine
state. `member()`, `words()`, `extract()` were already Tier 2.

**Remaining blocked (diverge from server):**
- `ISDBREF` — format-only check vs database lookup (requires Good_obj)
- `SORT` — Shellsort vs DUCET collation
- `CHR/ORD` — ASCII-only vs Unicode/grapheme-aware
- `SECURE/SQUISH/TRANSLATE` — byte-level vs Unicode
- `STRMATCH/MATCH/GRAB/GRABALL` — may diverge on Unicode

### 2. Phase 3: Concurrent Softcode Evaluation

Implement the framework for evaluating softcode on multiple cores simultaneously.

- **Tasks:**
  - Transition `mudstate`, JIT caches, and parser state to `thread_local` storage.
  - Implement optimistic read validation using the `mod_count` system.
  - Migrate `s_compile_cache` and `s_attr_mod_counts` to thread-safe structures (atomic/RW locks).
  - Ensure DBT contexts are thread-safe or per-thread.

## Core Engine Issues

### 3. Platform Interface Driver

- **File:** `driver.cpp:384`
- **Issue:** Need to create a proper platform interface abstraction for the driver to better separate OS-specific logic from core server lifecycle management.

### 4. Session/Driver Separation

- **File:** `session.cpp:339`
- **Issue:** `access_list` currently lives in the driver, but session-related access checks might need a better abstraction or to be moved.
