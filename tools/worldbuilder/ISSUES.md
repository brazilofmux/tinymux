# WorldBuilder — Open Issues

Updated: 2026-03-27

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

### Live adapter undocumented server format assumptions

- **File:** `live_adapter.py` (throughout)
- **Issue:** Assumes MUX responds in specific format to `@dig`, `@open`, `@set`. Small changes to server output format will break state file generation. No version/capability detection.

## Verified Checks

- `python3 tools/worldbuilder/test_diff_v3.py` passes.
- `python3 tools/worldbuilder/test_reconciler.py` passes.
