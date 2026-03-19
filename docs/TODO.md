# TinyMUX 2.14 — Open Work Items

## Stability & Bug Fixes
- [ ] **ISSUES #2**: Investigate lost `AST_BRACEGROUP` nodes during re-parse/evaluation.
- [ ] **ISSUES #3**: Fix strip_fancy_quotes leaking into `%0` (backport to master).
- [ ] **ISSUES #4-5**: Backport GANL fixes (pure virtual, double-free) to master.

## JIT Parity
- [ ] **ISSUES #6**: ECALL-based sync for setq/setr q-registers (currently guarded out).
- [ ] **ISSUES #7**: Investigate compiler hang on expressions > ~1200 bytes.
- [ ] **ISSUES #8**: Reproduce and debug intermittent SIGABRT on shutdown with JIT.

## Performance
- [ ] **SSA Optimizations**: Global Value Numbering (GVN). Postponed — hangs on deep linear idom chains. Needs HIR dump diagnostics first.
- [ ] **Intrinsics**: Map additional `co_*` functions as DBT intrinsics (ljust/rjust/center/edit/splice — 7+ args with complex pointer layouts).
- [ ] **Tier 2 gaps**: `ISNUM` needs floating-point support (ECALL candidate). `SORT` needs DUCET collation for Tier 2 routing.

## Observability
- [ ] **JIT Stats**: Expose per-expression compile time and instruction counts through `jitstats()`.

## Architecture
- [ ] **muxescape**: Move from `mux/src/` to its own `mux/muxescape/` subdir with separate Makefile.am.

## Design Gaps
- [ ] **ISSUES #9**: Define Lua privilege model (ownership, FN_PRIV, queued execution).
- [ ] **ISSUES #10**: Add cache-version metadata for Lua VM compatibility.
