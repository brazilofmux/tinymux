# WorldBuilder — Open Issues

Updated: 2026-03-27

## Bugs

### `test_worldbuilder.py` only works when run from `tools/worldbuilder/`

- **Evidence:** The header says `Run: python test_worldbuilder.py` at `tools/worldbuilder/test_worldbuilder.py:18`, but the test bodies open fixtures via relative paths like `tests/park.yaml` at `tools/worldbuilder/test_worldbuilder.py:69-87`.
- **Observed failure:** Running `python3 tools/worldbuilder/test_worldbuilder.py` from the repository root raises `FileNotFoundError: tests/park.yaml`.
- **Why it happens:** `tools/worldbuilder/worldbuilder.py:114` opens the given path directly, and the harness only adjusts `sys.path`, not its fixture base directory.
- **Impact:** The main offline WorldBuilder regression suite is easy to invoke incorrectly from the repo root, which makes test results less trustworthy and automation harder to script.

## Additional Issues Found (2026-03-27 Survey)

### File handle leak in executor.py

- **File:** `executor.py:557`
- **Issue:** `log_file = open(args.log, 'w') if args.log else None` — opened but never explicitly closed. No `finally` block or context manager.
- **Risk:** Resource leak if execution fails or is interrupted.
- **Fix:** Use `with open(args.log, 'w') as log_file:` pattern.

### Softcode lint KNOWN_FUNCTIONS is incomplete and hand-maintained

- **File:** `softcode_lint.py:23-87`
- **Issue:** Only ~100 function names in the dictionary. The engine supports 200+ functions. Many modern functions (e.g., `encode64`, `decode64`, `url_escape`, `json`, `hmac`, `crc32`, `lua`) are missing.
- **Impact:** Linter flags valid modern softcode as "unknown function."
- **Opportunity:** Auto-generate the function list from engine source headers.

### Dangerous pattern detection is incomplete

- **File:** `softcode_lint.py:90-101`
- **Issue:** Only 9 dangerous patterns. Missing: `@pemit` with `%#`, `@trig` without delays, unescaped user input in `%()` or `setr()`, mail functions without auth, debug attributes that expose internals.

### Executor SSL validation disabled

- **File:** `executor.py:37-40`
- **Issue:** `ctx.verify_mode = ssl.CERT_NONE` — accepts any certificate, no hostname checking.
- **Scope:** Suitable for development/internal use only.

### Live adapter undocumented server format assumptions

- **File:** `live_adapter.py` (throughout)
- **Issue:** Assumes MUX responds in specific format to `@dig`, `@open`, `@set`. Small changes to server output format will break state file generation. No version/capability detection.

## Verified Checks

- `python3 tools/worldbuilder/test_diff_v3.py` passes.
- `python3 tools/worldbuilder/test_reconciler.py` passes.
