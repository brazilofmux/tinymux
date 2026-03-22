# Strategic Roadmap: TinyMUX 2.14

**Date:** 2026-03-22
**Objective:** Focus future development on the high-performance JIT/DBT pipeline and modern Unicode compliance while resolving technical debt.

---

## Phase 1: Stability & Foundation (Weeks 1-4)
**Goal:** Eliminate the last major pieces of legacy C-style memory management.

- **[ ] Complete STL Conversion:**
    - Replace `objlist_block` / `objlist_stack` in `walkdb.cpp` with `std::vector<dbref>` and `std::stack`.
    - Replace `calloc`/fixed arrays in `dbt.cpp` with `std::vector`.
    - **Outcome:** -500 lines of code, improved safety, no more `MAX_PATCH_SITES` limit.
- **[ ] Unicode 16.0 Upgrade:**
    - Refresh all DFA state machines (`utf8tables.cpp`) with Unicode 16.0.0 data.
    - **Outcome:** Support for 8 major versions of new emoji and scripts.

---

## Phase 2: High Performance (Weeks 5-12)
**Goal:** Advance the JIT/DBT pipeline to its full potential.

- **[ ] Attribute Versioning (`mod_count`):**
    - Add a per-attribute `mod_count` to the SQLite database and in-memory cache.
    - Increment on every `atr_add()` / `atr_clr()`.
    - **Outcome:** Foundation for safe JIT inlining and optimistic read validation.
- **[ ] JIT Level 3 (u() Inlining):**
    - Implement inlining for `u()` and `ulocal()` calls in `hir_lower.cpp`.
    - Use `mod_count` for staleness checks and runtime permission guards.
    - **Outcome:** 2.8x speedup on hot softcode paths by eliminating ECALLs.
- **[ ] Tier 2 Coverage Expansion:**
    - Move frequently used functions (`time`, `get`, `member`) into RV64 guest code blobs.
    - **Outcome:** Minimal host/guest transitions.

---

## Phase 3: Internationalization (Weeks 13-20)
**Goal:** Full Unicode compliance and Grapheme-level string operations.

- **[ ] NFC Normalization:**
    - Implement the Layer 2 normalization pass at the network input boundary.
    - **Outcome:** Consistent text storage and comparison.
- **[ ] Grapheme Cluster Segmentation:**
    - Update `mux_string` and `mux_cursor` to operate on grapheme clusters.
    - Update all string functions (`strlen`, `mid`, etc.) to use user-perceived character counts.
    - **Outcome:** Modern emoji and combining characters "just work."

---

## Phase 4: Scalability (Future / Research)
**Goal:** Concurrent softcode evaluation.

- **[ ] Concurrent Evaluation Framework:**
    - Transition `mudstate` and JIT caches to `thread_local` storage.
    - Implement optimistic read validation using the `mod_count` system from Phase 2.
    - **Outcome:** Ability to evaluate softcode on multiple cores simultaneously.

---

## Success Metrics
- **Performance:** 2x overall increase in `muxscript` benchmarks.
- **Correctness:** 100% pass rate on updated Unicode 16.0 test suite.
- **Sustainability:** Zero manual `free`/`realloc` calls remaining in the "engine" module.
