# Test Infrastructure — Open Issues

Updated: 2026-03-27

## High — Test Brittleness

### 84% of tests use SHA1 hash comparison with no semantic assertions

- **Scope:** 217 of 239 test files, ~590 SHA1 references
- **Issue:** Tests compute `sha1(output)` and compare against a hardcoded hex hash. When a test fails, the hash mismatch gives zero clue about what actually changed. Adding or modifying any function behavior requires recalculating all dependent hashes.
- **Risk:** Function behavior could drift without visibility if a hash is copied incorrectly.
- **Opportunity:** Gradually introduce semantic assertions (`strmatch`, value comparisons) alongside or replacing hashes for the highest-risk functions.

## High — Coverage Gaps

### Only 1 command-level test file

- **File:** `dolist_cmd.mux` (84 lines)
- **Gap:** No tests for `@if`, `@switch`, `@for`, `@while`, `@trigger`, `@wait`, `@notify`, or other command variants.
- **Impact:** Command-level behavior relies entirely on integration smoke testing.

### Minimal edge case testing across the board

- **Missing categories:**
  - Empty/null inputs
  - Maximum argument counts and string length limits
  - Numeric overflow and underflow
  - Nested depth limits
  - Invalid argument types and malformed syntax
  - Permission failure paths
  - Unicode: multi-byte characters, combining diacriticals, RTL text (only `accent_fn.mux` and `strdistance_fn.mux` touch this)

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

## Low — Hardcoded References

### Some tests reference `#0` directly

- **File:** `objid_fn.mux:19`
- **Issue:** Assumes `#0` exists with specific properties. Tests will fail on databases with different object numbering.
