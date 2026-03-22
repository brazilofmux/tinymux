# Code Review Report: TinyMUX 2.14

**Date:** 2026-03-22
**Status:** Comprehensive review of `mux/modules/engine/` and JIT/Unicode documentation.

## 1. Technical Debt & Modernization

TinyMUX is mid-transition from a C-style legacy codebase to modern C++17. While significant progress has been made (Comsys, Mail, and Pool Allocator conversions), several high-impact areas remain.

### 1.1 Manual Resource Management (The "Last Mile" of STL)

STL conversions for the remaining engine helpers (`walkdb.cpp`, `dbt.cpp`) are complete, eliminating bespoke object blocks and `MAX_PATCH_SITES` ceilings. Keep auditing new code for regressions into manual allocation patterns.

### 1.2 Performance Bottlenecks

- **ECALL Overhead:** The transition between JIT guest code (RV64) and host C++ code (ECALL) is the primary performance tax. 
    - *Opportunity:* Expand "Tier 2" coverage (pre-compiled guest blobs) for frequently used builtins like `time()`, `get()`, and list operations (`extract`, `member`). This keeps execution in the guest environment.

## 2. Potential Bugs & Risks

### 2.1 JIT Tier 3 (Inlining) Hazards
The proposed JIT Level 3 (inlining `u()` calls) introduces several correctness risks that must be addressed before deployment:

- **Cache Staleness:** If `u(obj/attr)` is inlined and `attr` is later modified, the JIT caller remains stale.
    - *Fix Required:* Implement a per-attribute `mod_count` (versioning) system and a runtime dependency check.
- **Permission Bypass:** Inlining at compile-time (using `GOD` permissions) could allow a player to execute code they shouldn't see.
    - *Fix Required:* Emit a runtime permission ECALL (`_check_u_perm`) before the inlined body.
- **Resource Leaks:** `ulocal()` and CARGS (`%0-%9`) must be scoped. Inlining risks "leaking" register or argument state into the caller.
    - *Fix Required:* Helper functions (`_save_qregs`, `_write_carg`) to manage scope transitions.

## 3. Concurrency & Scalability

### 3.1 Global State Contention
Softcode evaluation is currently single-threaded and relies heavily on global state (`mudstate`). 

- **Risk:** Transitioning to concurrent evaluation (multiple parser threads) will be blocked by non-thread-local storage (TLS) for things like `s_jit_depth`, `qreg[]`, and the JIT compilation cache.

## 4. Summary of Opportunities

| Category | Priority | Impact | Effort |
|----------|----------|--------|--------|
| JIT Level 3 (Inlining) | High | 2-3x Performance | High |
| Concurrent Evaluation | Low | Scalability | Very High |
