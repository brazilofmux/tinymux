# WorldBuilder — Open Issues

## 1. Duplicated `mux_escape()` — FIXED

`mux_escape()` and `mux_unescape()` now live solely in `live_adapter.py`.
`worldbuilder.py` imports them from there.

## 2. Executor bypasses Operation pipeline — FIXED

`execute()` in `executor.py` now accepts an `operations` parameter. If not
provided, it plans operations via `plan_spec_operations()` or
`plan_incremental_operations()`. Execution is phased: room creates, room
updates, exit creates, thing creates, then destroys (exits before rooms).

## 3. No live snapshot query — FIXED

`live_adapter.py` now provides:
- `query_live_object(conn, dbref, query_fn)` — query one object
- `query_live_snapshot(conn, state, query_fn)` — query all tracked objects

Both return normalized dicts compatible with `reconcile_snapshots()`.

## 4. Importer output doesn't match reconciler format — FIXED

`importer.py` `generate_state()` now uses `normalize_imported_room()` and
`normalize_imported_exit()` from `live_adapter.py`. Flags are properly split
into sorted lists. Exits are included in the state snapshot.

## 5. No reconciler tests — FIXED

`test_reconciler.py`: 36 tests covering all seven reconciliation statuses
(unchanged, spec_modified, live_modified, conflict, converged, missing_live,
recycled_live, pending_destroy), plus new-in-spec, new-in-live, attribute-level
diffs, flag changes, multiple objects, summary counts, report formatting, and
empty inputs.
