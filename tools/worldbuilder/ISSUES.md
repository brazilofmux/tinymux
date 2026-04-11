# WorldBuilder — Open Issues

Updated: 2026-04-10

## Bugs

### ~~`test_worldbuilder.py` only works when run from `tools/worldbuilder/`~~ FIXED

- Test harness now calls `os.chdir()` to its own directory at startup, so fixture paths resolve correctly regardless of the caller's working directory.

## Additional Issues Found (2026-03-27 Survey)

### ~~File handle leak in executor.py~~ FALSE ALARM

- On inspection, `executor.py:593-595` already closes the log file in a `finally` block.

### ~~Softcode lint KNOWN_FUNCTIONS is incomplete and hand-maintained~~ FIXED

- Replaced hand-maintained ~100-entry set with authoritative 482-entry set extracted from `builtin_function_list[]` in `mux/modules/engine/functions.cpp`. Regeneration command documented in the source comment.

### ~~Dangerous pattern detection is incomplete~~ FIXED

- `softcode_lint.py` now flags additional high-risk patterns for review: `@pemit` with `%#`, `@trigger`, dynamic `%(...)` substitution, `setr()`, and `mail*()` calls. `test_worldbuilder.py` includes focused coverage for each new heuristic.

### ~~Executor SSL validation disabled~~ FIXED

- **File:** `executor.py:37-40`
- `MuxConnection` now verifies certificates and hostnames by default. `executor.py` and `importer.py` expose an explicit `--insecure` flag for self-signed or internal development servers that still need the previous behavior.

### ~~Live adapter undocumented server format assumptions~~ FIXED

- **File:** `live_adapter.py`, `executor.py`
- Replaced the free-form response-parsing path with a sentinel-wrapped `think` round-trip. `live_adapter.think_expr()` wraps a softcode expression as `think WB1[<expr>]WB2`; `parse_think_response()` recovers the evaluated value from the response buffer (taking the *last* sentinel pair so we land on the `think` output rather than the echoed command); `query_think()` and `query_last_created()` are the high-level helpers. `executor.py` now uses `lastcreate(me, R|E|T)` for room/exit/thing dbref capture after `@dig`/`@open`/`@create`, eliminating the regex scan of `@dig`/`@open` output text via `extract_dbref()`. Thing creation also now captures its dbref (previously silently dropped). The module docstring in `live_adapter.py` documents the server-format contract the tool still depends on (`%L` for current location; `lastcreate` + `think` sentinel for everything else). `extract_dbref()` was removed as dead code.

## Verified Checks

- `python3 tools/worldbuilder/test_diff_v3.py` passes.
- `python3 tools/worldbuilder/test_reconciler.py` passes.

### ~~Attribute writes are emitted without MUX escaping~~ FIXED

- `executor.py` and `live_adapter.py` now route room/thing attribute payloads through `mux_escape()` before emitting `&attr=` commands, so `%`, tabs, and newlines are serialized safely instead of being interpreted by the server. `mux_escape()` itself now actually converts `\t` to `%t`, matching its documented contract. `test_worldbuilder.py` includes focused coverage for escaped room and thing attributes.

### ~~`verify()` can report success while exits and things are wrong~~ FIXED

- `executor.verify()` now verifies exits by stored dbref/object id, checks exit names, and compares `home(<exit>)` against the expected destination. It also verifies managed things with the same object-id/name/description/attr checks used for rooms, plus a `loc()` check for placement. `test_worldbuilder.py` now exercises both exit-destination drift and thing drift.
