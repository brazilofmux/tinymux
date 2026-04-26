# Test Infrastructure — Open Issues

Updated: 2026-04-26

## Open — JIT vs no-JIT eval path divergence on `{}` args

### `isjson({"a":1})` returns 1 with `--enable-jit`, 0 without — both platforms

**Cross-platform investigation resolved 2026-04-26.** Not a Mac-specific
bug; the divergence is between the JIT and non-JIT eval paths. macOS
reproduces what x86-64 Linux also does without `--enable-jit`.

- **Probe results** (instrumentation just before `JsonValidator jv;` in
  `mux/modules/engine/funcweb.cpp::fun_isjson`, logging `fargs[0]`):

  | Build | Smoke | `fargs[0]` for `isjson({"a":1})` |
  | --- | --- | --- |
  | Linux x86-64, `--enable-jit` | 925/925 pass | `<{"a":1}>` (braces preserved) |
  | Linux x86-64, no JIT | TC001/TC003 fail | `<"a":1>` (braces stripped) |
  | macOS arm64, no JIT | TC001/TC003 fail | `<"a":1>` (braces stripped) |

  Linux without JIT and macOS produce identical probe output — so the
  Mac failure is not platform-specific. Hypothesis 2 (host-conditional
  `#ifdef`) and Hypothesis 3 (test never passed on Linux) are ruled out.
  Hypothesis 1 is confirmed: `--enable-jit` swaps the eval path, and the
  JIT/AST path preserves `{}` while the legacy non-JIT path strips them.

- **Failing assertions** (both no-JIT builds):
  - `TC001: isjson valid. Failed (obj=0 array=0 str=1 num=1 true=1 false=1 null=1).`
  - `TC003: isjson type check. Failed (obj=0 obj_as_array=0 array=0 str=1 num=1 num_as_str=0 true=1 false=1 null=1).`

  The only failing q-values are the `{"a":1}` cases. `[1,2,3]` is
  evaluated rather than passed literally (deliberate, see below).

- **Site of divergence:** `EV_STRIP_CURLY` handling. The JIT-driven path
  (`AST_BRACEGROUP` in `mux/modules/engine/ast.cpp:2336`) keeps the
  braces when the entire arg is `{…}`; the legacy `parse_to` in
  `mux/modules/engine/eval.cpp` strips them. Both are platform-agnostic
  C++; the divergence is purely which path runs, not which host runs it.

- **PR #705 portability changes:** confirmed clean on x86-64 Linux
  with `--enable-jit` (925/925 smoke pass). The new configure
  substitutions (`LIBMUX_SONAME_FLAG`, `ENGINE_SONAME_FLAG`,
  `LD_NOUNDEFINED`, `LD_RPATH_ORIGIN`, `LD_HARDENING`) produce
  byte-identical link lines to the previous hardcoded GNU-ld flags;
  `readelf -d` on `libmux.so`, `engine.so`, and `netmux` shows the same
  SONAME and `RUNPATH=$ORIGIN:…` as before the PR.

- **Open question — which side is the bug?** Two candidates:
  1. **Non-JIT path is wrong.** A bare `{"a":1}` passed to `isjson` is a
     single brace-grouped arg; stripping the braces before the function
     sees them changes the semantic content (`{"a":1}` → `"a":1`).
     Other functions that take JSON-like args may be silently broken
     under no-JIT in the same way. Fix: make `parse_to`'s
     `EV_STRIP_CURLY` not strip when the brace-group is the entire arg
     value, matching `AST_BRACEGROUP`'s behavior.
  2. **Test was JIT-coupled.** The smoke expectation was written against
     the JIT path's behavior without realising the no-JIT path differed.
     Fix: rewrite the test to avoid bare `{}` in `isjson(...)` calls.

  (1) is the better target — JIT becoming mandatory in 1-2 years means
  the non-JIT path is a deprecation path, but until then it should match
  the JIT path's semantics, not silently miscompile JSON-shaped args.

- **Cross-link:** macOS arm64 build PR is
  https://github.com/brazilofmux/tinymux/pull/705 — landed at
  `bad833cbf` on master 2026-04-26; the failure was first visible on
  Mac because Mac currently can't build with `--enable-jit` (DBT
  AArch64-Apple backend missing — `docs/DBT-PORTABILITY.md`).

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
- Recent progress: `cmogrifier_fn.mux`, `clone_fn.mux`, `isjson_fn.mux`, `moon_fn.mux`, `wrapcolumns_fn.mux`, `printf_fn.mux`, and all of `paginate_fn.mux` now use direct semantic checks instead of SHA1 snapshots, but the broader corpus is still overwhelmingly hash-based.

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
- Recent progress: `abs_fn.mux` and `sign_fn.mux` now cover decimals plus empty/malformed-input coercion, which reduces this gap for the basic numeric smoke tier, but the broader corpus still mostly has one-case files.
- Additional progress: `between_fn.mux`, `bound_fn.mux`, `min_fn.mux`, and
  `max_fn.mux` now include decimal, negative-range, duplicate, and coercion
  cases instead of stopping at a single basic-path assertion block.

## Medium — Infrastructure

### ~~No automatic test discovery~~ FIXED

- `testcases/tools/generate_smoke_suite.py` now discovers top-level `testcases/*.mux` files automatically (excluding harness/setup files), emits generated `&suite.list.1` / `&suite.list.2` overrides, and `testcases/tools/Makesmoke` appends that generated suite file after the checked-in `.mux` corpus before unformatting. Adding a new smoke test no longer requires hand-editing `smoke.mux`. Reverified with `python3 testcases/tools/generate_smoke_suite.py` and `bash -n testcases/tools/Makesmoke`.

### ~~Tests cannot run in isolation or parallel~~ FIXED

- `testcases/tools/Makesmoke` and `testcases/tools/Smoke` now accept explicit workspaces and flatfile paths, so smoke runs no longer have to share the repo-root runtime directories.
- `testcases/tools/SmokeParallel` fans selected tests out across isolated temp workspaces (`./tools/SmokeParallel -j 4 abs_fn route_fn ...`), which closes the remaining parallel multi-test execution and isolation gap.

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
