# TinyMUX 2.14 JIT/DBT Next Steps

## 1. Stability & Bug Fixes
- [x] **Issue 1**: Fix `EV_FCHECK` persistence bug in `ast_eval_node` (SEQUENCE handler).
- [ ] **Issue 2**: Investigate lost `AST_BRACEGROUP` nodes during re-parse/evaluation. (Still pending investigation in production).
- [x] **Issue 3**: Fix `%q<` malformed angle substitution fallback regression.
- [x] **Issue 8**: Implement `ECALL_SETQ_PACK` to sync JIT q-registers with `mudstate.global_regs`.
- [x] **Issue 9**: Fix quadratic behavior in `hir_cse` ($O(N^2 D)$) with hash table.
- [x] **Issue 10**: Debug and fix intermittent `SIGABRT` on shutdown with JIT enabled.

## 2. Performance Scaling
- [x] **LBUF Phase 0**: Expand JIT output region to 256KB scratch ring (32 slots).
- [x] **LBUF Phase 0**: Liveness-based output buffer allocation (linear scan over string intervals).
- [ ] **SSA Optimizations**: Global Value Numbering (GVN). Postponed — hangs on deep linear idom chains in complex expressions. Needs HIR dump diagnostics first.
- [x] **Tier 2 Expansion**: Batches 1-8 enabled (string ops, case, reverse, escape, justify, space, wildcard, list aggregation, base conversion, etc.).
- [ ] **Intrinsics**: Map additional `co_*` functions as DBT intrinsics for native performance. ljust/rjust/center/edit/splice need new emitter patterns (7+ args with complex pointer layouts).
- [ ] **Tier 2 gaps**: `ISNUM` — rv64_isnum only handles integers; full isnum() needs floating-point support (not available in RV64 blob). Could become an ECALL. `SORT` — co_sort_words doesn't support DUCET collation, so SORT cannot be routed to Tier 2.

## 3. Observability & Debugging
- [x] **HIR Dump**: `TINYMUX_DUMP_HIR=1` env var dumps HIR after each compiler phase (lowering, SSA, optimization). `hir_dump()` and `hir_kind_name()` in hir_codegen.cpp.
- [ ] **JIT Stats Granularity**: Expose per-expression compile time and instruction counts through `jitstats()` so optimizer regressions are visible from softcode.

## 4. Architecture & Cleanup
- [x] **Code Quality**: Split `dbt_compile.cpp` into `hir_lower.cpp`, `hir_codegen.cpp`, `jit_compiler.cpp` + `dbt_compile.h`.
- [x] **LBUF Phase 1 (Tier B)**: JITArena class — bump allocator over lbuf_ref pools for ECALL_SETQ_PACK register packing. Reduces per-setq allocation overhead.
- [x] **LBUF Phase 2 (Tier C)**: DMA controller scaffolding — guest memory map expanded to 1MB, 4x16KB DMA windows at 0x70000, ECALL_DMA_SUBMIT/ACK handlers. Currently only DMA_OP_FINALIZE (copy to arena) is implemented.
- [x] **Guest memory**: Expanded from 512KB to 1MB to accommodate DMA windows and descriptor rings.
- [ ] **muxescape**: Move from `mux/src/` to its own `mux/muxescape/` subdir (like `mux/announce/`). Standalone tool — should NOT install to `mux/game/bin/`. Needs own Makefile.am, configure.ac SUBDIRS entry. Source lives in `mux/src/tools/muxescape.rl`; generated `.cpp` and binary stay in `mux/muxescape/`.
