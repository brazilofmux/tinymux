# MUX Engine — Open Issues & Optimization Roadmap

## JIT/DBT Pipeline Optimization

These issues are based on the findings in `docs/JIT-PERF-INVESTIGATION.md`.

### 1. Level 2: Tier 2 Coverage Expansion

Move more frequently used builtin functions into pre-compiled RV64 guest code blobs to eliminate ECALL overhead.

**Priority targets:**
- `time()`, `secs()`
- `get()`, `xget()`
- `name()`, `owner()`, `flags()`
- `member()`, `words()`, `extract()`

### 2. Phase 3: Concurrent Softcode Evaluation

Implement the framework for evaluating softcode on multiple cores simultaneously.

- **Tasks:**
  - Transition `mudstate`, JIT caches, and parser state to `thread_local` storage.
  - Implement optimistic read validation using the `mod_count` system.
  - Migrate `s_compile_cache` and `s_attr_mod_counts` to thread-safe structures (atomic/RW locks).
  - Ensure DBT contexts are thread-safe or per-thread.

## Core Engine Issues

### 3. `MakeCanonicalUserFunctionName` Truncation

- **File:** `functions.cpp:14544`
- **Issue:** The function uses `memcpy` into a static buffer without properly handling potential truncation logic or notifying the caller if truncation occurred beyond just cutting it off.

### 4. Platform Interface Driver

- **File:** `driver.cpp:384`
- **Issue:** Need to create a proper platform interface abstraction for the driver to better separate OS-specific logic from core server lifecycle management.

### 5. Output Queue Threshold

- **File:** `net.cpp:144`
- **Issue:** The threshold for spending time to push output when the queue is full might be too high, potentially causing latency. It needs tuning and possibly a better way to predict if a write would be productive.

## Other Engine Issues

### 6. HTML Escaping for Exit Names

- **File:** `look.cpp:583`
- **Issue:** Exit names need to be HTML escaped when rendered in certain contexts.

### 7. Command Deletion Logic

- **File:** `conf.cpp:1378`
- **Issue:** Investigate and fix issues related to deleting commands, especially when they might be referenced or have specific flags.

### 8. ANSI Support in funceval

- **File:** `funceval.cpp:2002`
- **Issue:** Support ANSI in output separators and padding for functions that generate lists or formatted output.

### 9. Comsys Truncation

- **File:** `comsys.cpp:2549`
- **Issue:** Fix truncation issues in comsys-related functions.

### 10. Predicates Cleanup

- **File:** `predicates.cpp:1260`
- **Issue:** Delete everything related to 'old' in predicates (legacy code cleanup).

### 11. Session/Driver Separation

- **File:** `session.cpp:339`
- **Issue:** `access_list` currently lives in the driver, but session-related access checks might need a better abstraction or to be moved.
