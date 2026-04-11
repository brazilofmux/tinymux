# Test Infrastructure — Open Issues

Updated: 2026-04-10

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

### ~~No automatic test discovery~~ FIXED

- `testcases/tools/generate_smoke_suite.py` now discovers top-level `testcases/*.mux` files automatically (excluding harness/setup files), emits generated `&suite.list.1` / `&suite.list.2` overrides, and `testcases/tools/Makesmoke` appends that generated suite file after the checked-in `.mux` corpus before unformatting. Adding a new smoke test no longer requires hand-editing `smoke.mux`. Reverified with `python3 testcases/tools/generate_smoke_suite.py` and `bash -n testcases/tools/Makesmoke`.

### ~~Tests cannot run in isolation or parallel~~ PARTLY FIXED

- `testcases/tools/Makesmoke` now accepts optional test names and passes them through to `generate_smoke_suite.py`, so you can build a smoke database for a single testcase or selected subset (`./tools/Makesmoke abs_fn`, etc.). That closes the "cannot run in isolation" part of this item.
- **Remaining:** the smoke harness still executes the selected suite sequentially inside one database, so parallel execution/isolation between multiple tests is still not implemented.

### ~~No cleanup of orphaned test objects~~ FIXED

- `testcases/tools/generate_smoke_suite.py` now emits per-test `&suite.cleanup.<name>` hooks that destroy named helper fixtures, stored dbref fixtures, and transient channels both before and after each smoke test. `testcases/smoke.mux` runs those hooks around every testcase, so interrupted reruns no longer inherit stale route rooms/exits, search fixtures, temporary channels, or similar leftovers from prior attempts.
- Remaining runtime-only leaks are handled at the testcase level where needed; `clone_fn.mux` now destroys the cloned thing it creates during assertion.

## Medium — Script Reliability (New, 2026-04-04)

### ~~`mktemp` trap refresh invalidates cleanup in Makesmoke~~ FIXED

- `Makesmoke` now tracks temp files in `TMP_FILES[]` with a `cleanup_temps()` trap, so both the initial unformat temp file and the later scrub temp file are removed on exit.

### ~~Insufficient error context on dbconvert failure~~ FIXED

- `Smoke` now prints the exact `dbconvert` command line and exit code before dumping `netmux.log`.

### ~~Optional omega validation silently skipped~~ FIXED

- `Makesmoke` now emits an explicit warning when `$REPO_ROOT/mux/convert/omega` is unavailable, so validation skips are visible in CI and local runs.

## Low — Hardcoded References

### ~~Some tests reference `#0` directly~~ FIXED

- **File:** `objid_fn.mux`
- `objid_fn.mux` still uses `#0` (the TinyMUX master room, which is a database invariant), but no longer hashes `name(objid(#0))` — the brittleness was the SHA1 capture of a configurable room name. Converted to a 14-bit semantic bitstring assertion that compares `name(objid(#0))` to `name(#0)` for the round-trip check, eliminating the SHA1 hash entirely and the configured-name dependency with it.
