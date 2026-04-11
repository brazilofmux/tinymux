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

## Bugs (New, 2026-04-10)

### Attribute writes are emitted without MUX escaping

- **Files:** `tools/worldbuilder/executor.py:234-235`, `tools/worldbuilder/executor.py:254-255`, `tools/worldbuilder/executor.py:346-347`, `tools/worldbuilder/live_adapter.py:393-394`, `tools/worldbuilder/live_adapter.py:412-414`, `tools/worldbuilder/live_adapter.py:435-436`
- **Issue:** Descriptions are sent through `mux_escape()`, but room/thing attribute values are written raw. Any attribute containing `%`, newlines, tabs, or trailing spaces is therefore interpreted by the server instead of being stored literally.
- **Impact:** Applied state can diverge from the spec without the planner noticing. Multi-line attr text is especially fragile because it is split across telnet commands rather than serialized as a single MUX-safe value.
- **Fix:** Escape attribute payloads with the same MUX-aware escaping used for descriptions, and add regression coverage for `%`, `%r`, and embedded newlines in attrs.

### `verify()` can report success while exits and things are wrong

- **File:** `tools/worldbuilder/executor.py:443-454`
- **Issue:** The verifier only performs detailed checks for rooms. Exit validation does not append any discrepancy when the destination is missing, and thing objects are not verified at all.
- **Impact:** `verify()` can finish with "Verification passed" even when exits drifted or managed things were deleted/renamed. That makes it unsafe as a post-apply confidence check.
- **Fix:** Turn the exit pass into real assertions, and add thing verification using the same `objid`/name/attr checks already used for rooms.
