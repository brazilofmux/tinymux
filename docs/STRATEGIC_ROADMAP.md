# Strategic Roadmap: TinyMUX 2.14

**Date:** 2026-03-22
**Objective:** Focus future development on the high-performance JIT/DBT pipeline while resolving technical debt.

## Phase 1: Stability & Foundation (Weeks 1-4)
**Goal:** Eliminate the last major pieces of legacy C-style memory management.

- **[ ] Finish DBT STL conversion:**
    - Replace `calloc`/fixed arrays in `dbt.cpp` with `std::vector`.
    - **Outcome:** Improved safety and no more `MAX_PATCH_SITES` limit.

## Phase 2: High Performance (Weeks 5-12)
**Goal:** Advance the JIT/DBT pipeline to its full potential.

- **[ ] Tier 2 Coverage Expansion:**
    - Move frequently used functions (`time`, `get`, `member`) into RV64 guest code blobs.
    - **Outcome:** Minimal host/guest transitions.

## Phase 3: Scalability (Future / Research)
**Goal:** Concurrent softcode evaluation.

- **[ ] Concurrent Evaluation Framework:**
    - Transition `mudstate` and JIT caches to `thread_local` storage.
    - Implement optimistic read validation using the `mod_count` system from Phase 2.
    - **Outcome:** Ability to evaluate softcode on multiple cores simultaneously.
