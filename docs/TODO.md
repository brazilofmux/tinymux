# TinyMUX 2.14 — Open Work Items

## Stability & Bug Fixes
- [ ] **ISSUES #1**: Backport strip_fancy_quotes %0 fix to master.

## Performance
- [ ] **SSA Optimizations**: Global Value Numbering (GVN). Postponed — hangs on deep linear idom chains. Needs HIR dump diagnostics first.
- [ ] **Intrinsics**: Map additional `co_*` functions as DBT intrinsics (ljust/rjust/center/edit/splice — 7+ args with complex pointer layouts).
- [ ] **Tier 2 gaps**: `ISNUM` needs floating-point support (ECALL candidate). `SORT` needs DUCET collation for Tier 2 routing.

## Observability
- [ ] **JIT Stats**: Expose per-expression compile time and instruction counts through `jitstats()`.

## Architecture
- [ ] **muxescape**: Move from `mux/src/` to its own `mux/muxescape/` subdir with separate Makefile.am.

## Design Gaps
- [ ] **ISSUES #2**: Define Lua privilege model (ownership, FN_PRIV, queued execution).
- [ ] **ISSUES #3**: Add cache-version metadata for Lua VM compatibility.
