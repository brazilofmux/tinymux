# Test Infrastructure — Open Issues

Updated: 2026-04-01

## Testing Levels

The project now has two complementary testing tiers:

- **`testcases/`** — Smoke tests: softcode-level integration tests that run inside a live MUX instance. 811 test cases across ~240 `.mux` files, exercising functions and commands end-to-end.
- **`tests/`** — Standalone unit test harnesses that link directly against built `.so` files and run without a MUX instance:
  - `tests/libmux/` — stringutil, mathutil, alloc (29 tests, links libmux.so)
  - `tests/color_ops/` — Ragel DFA color primitives (C, standalone)
  - `tests/db/` — SQLite storage backend (C++, standalone)

## High — Test Brittleness

### 84% of smoke tests use SHA1 hash comparison with no semantic assertions

- **Scope:** ~217 of ~240 test files, ~590 SHA1 references
- **Issue:** Tests compute `sha1(output)` and compare against a hardcoded hex hash. When a test fails, the hash mismatch gives zero clue about what actually changed. Adding or modifying any function behavior requires recalculating all dependent hashes.
- **Risk:** Function behavior could drift without visibility if a hash is copied incorrectly.
- **Opportunity:** Gradually introduce semantic assertions (`strmatch`, value comparisons) alongside or replacing hashes for the highest-risk functions.

## High — Coverage Gaps

### ~~Only 1 command-level test file~~ FIXED

- Now 7 command-level test files (1,101 lines total):
  `dolist_cmd.mux`, `if_cmd.mux`, `switch_cmd.mux`, `trigger_cmd.mux`,
  `wait_cmd.mux`, `assert_cmd.mux` (`@assert`/`@break`), `cmd_say.mux`.

### Minimal edge case testing across the board

- **Missing categories:**
  - Empty/null inputs
  - Maximum argument counts and string length limits
  - Numeric overflow and underflow
  - Nested depth limits
  - Invalid argument types and malformed syntax
  - Permission failure paths
  - Unicode: multi-byte characters, combining diacriticals, RTL text (only `accent_fn.mux` and `strdistance_fn.mux` touch this)
- **Note:** The `tests/` standalone harnesses are the right place for low-level edge case and boundary testing (overflow, LBUF limits, UTF-8 multi-byte). Smoke tests (`testcases/`) are better suited for end-to-end semantic correctness.

### Single test case per function is the norm

- **Example:** `abs_fn.mux` has 1 test case (lines 11-34). Most function test files follow this pattern.
- **Impact:** Happy-path coverage only; boundary and error cases untested.

## Medium — Infrastructure

### No automatic test discovery

- **File:** `smoke.mux:8-37`
- **Issue:** All 230+ test names are hardcoded in `&suite.list.*` attributes. Adding a new test requires manual registration in `smoke.mux`.
- **Opportunity:** Auto-discover `*.mux` files in `testcases/` or use a naming convention.

### Tests cannot run in isolation or parallel

- **Issue:** All tests run through the smoke harness sequentially, sharing a single database. A failure in one test can corrupt state for subsequent tests.

### No cleanup of orphaned test objects

- **Issue:** Every test file does `@create test_<name>` and `drop test_<name>`. If a test is interrupted or the drop fails, test objects accumulate in the database.

## Medium — Script Reliability (New, 2026-04-04)

### `mktemp` trap refresh invalidates cleanup in Makesmoke

- **File:** `testcases/tools/Makesmoke:25-26, 154`
- **Issue:** `TMP=$(mktemp)` with `trap 'rm -f "$TMP"' EXIT` is set early, but `TMP` is reassigned at line 154, making the trap clean up only the second temp file — the first is leaked.
- **Recommendation:** Save both filenames or use a cleanup function that tracks all temp files.

### Insufficient error context on dbconvert failure

- **File:** `testcases/tools/Smoke:129-133`
- **Issue:** When `dbconvert` fails, only the log is printed with no context about what command was run. Makes remote debugging harder.
- **Recommendation:** Print the command line and exit code alongside the log output.

### Optional omega validation silently skipped

- **File:** `testcases/tools/Makesmoke:159-170`
- **Issue:** If `omega` binary is not found, flatfile validation is silently skipped. A corrupt flatfile could be committed without detection.
- **Recommendation:** At minimum warn when omega is unavailable; ideally make it mandatory.

## Low — Hardcoded References

### Some tests reference `#0` directly

- **File:** `objid_fn.mux:19`
- **Issue:** Assumes `#0` exists with specific properties. Tests will fail on databases with different object numbering.
