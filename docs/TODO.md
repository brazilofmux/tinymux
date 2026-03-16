# TinyMUX 2.14 JIT/DBT Next Steps

## 1. Stability & Bug Fixes (High Priority)
- [x] **Issue 1**: Fix `EV_FCHECK` persistence bug in `ast_eval_node` (SEQUENCE handler).
- [ ] **Issue 2**: Investigate lost `AST_BRACEGROUP` nodes during re-parse/evaluation. (Still pending investigation in production).
- [x] **Issue 3**: Fix `%q<` malformed angle substitution fallback regression.
- [x] **Issue 9**: Fix quadratic behavior in `hir_cse` ($O(N^2 D)$) with hash table.
- [x] **Issue 10**: Debug and fix intermittent `SIGABRT` on shutdown with JIT enabled.
- [x] **Issue 8**: Implement `ECALL_SETQ_PACK` to sync JIT q-registers with `mudstate.global_regs`.

## 2. Performance Scaling
- [x] **LBUF Phase 0**: Expand JIT output region to 256KB scratch ring (32 slots).
- [ ] **LBUF Phase 0**: Implement compile-time liveness-based address assignment for scratch buffers.
- [ ] **SSA Optimizations**: Global Value Numbering (GVN). (Postponed due to hangs in complex expressions).
- [x] **Tier 2 Expansion**: Enable Batches 4 & 5 (ESCAPE, SECURE, wildcard matching, etc.).
- [ ] **Intrinsics**: Map more `co_*` functions as DBT intrinsics for native performance.

## 3. Observability & Debugging
- [ ] **HIR Dump**: Add `--dump-hir` / `--dump-blocks` debug output to the JIT compiler. Should print the block structure (block_first/block_last, idom[], successors), HIR instructions per block, and dominator tree shape. This makes degenerate block structures (e.g., deep linear idom chains that cause GVN to hang) diagnosable from a dump rather than requiring a debugger. Any LLM should be able to read the dump and spot the problem.
- [ ] **JIT Stats Granularity**: Expose per-expression compile time and instruction counts through `jitstats()` so optimizer regressions are visible from softcode.

## 4. Architecture & Cleanup
- [ ] **LBUF Phase 1**: Implement Arena-based allocation (Tier B) for long-lived values.
- [ ] **LBUF Phase 2**: Implement DMA windows for large value transfers (Phase 2).
- [x] **Code Quality**: Refactor `dbt_compile.cpp` (214KB) into smaller modules (hir_lower, hir_codegen, jit_compiler).
- [ ] **muxescape**: Move from `mux/src/` to its own `mux/muxescape/` subdir (like `mux/announce/`). Standalone tool — should NOT install to `mux/game/bin/`. Needs own Makefile.am, configure.ac SUBDIRS entry. Source lives in `mux/src/tools/muxescape.rl`; generated `.cpp` and binary stay in `mux/muxescape/`.
