# TinyMUX 2.14 — Open Work Items

## Lua Bridge Permission Fixes (ISSUES #1)
- [ ] **mux.notify/pemit**: Add @pemit permission checks (nearby/Long_Fingers/Controls/page_check).
- [ ] **mux.location**: Add `locatable()` check (UNFINDABLE).
- [ ] **mux.name**: Add `read_rem_name` / `nearby_or_control` check.
- [ ] **mux.controls**: Restrict `who` arg to executor or controlled-by-executor.
- [ ] **Error consistency**: Unify mux.get/mux.set to return `nil, "reason"` on failure.

## Backport
- [ ] Backport strip_fancy_quotes %0 fix to master.

## Performance
- [ ] **SSA Optimizations**: Global Value Numbering (GVN). Postponed — hangs on deep linear idom chains.
- [ ] **Intrinsics**: Map additional `co_*` functions as DBT intrinsics.
- [ ] **Tier 2 gaps**: `ISNUM` float support, `SORT` DUCET collation.

## Observability
- [ ] **JIT Stats**: Expose per-expression compile time and instruction counts through `jitstats()`.

## Architecture
- [ ] **muxescape**: Move to its own `mux/muxescape/` subdir with separate Makefile.am.
