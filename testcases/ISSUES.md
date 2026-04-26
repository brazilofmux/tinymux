# Test Infrastructure — Open Issues

Updated: 2026-04-25

## Open — Cross-Platform Investigation Needed (2026-04-25)

### `isjson({"a":1})` returns 0 on macOS arm64; reportedly 1 on x86-64 Ubuntu

- **Symptom:** On `aarch64-apple-darwin25.4.0` (PR #705 build, no
  `--enable-jit`), Smoke reports two failures:
  - `TC001: isjson valid. Failed (obj=0 array=0 str=1 num=1 true=1 false=1 null=1).`
  - `TC003: isjson type check. Failed (obj=0 obj_as_array=0 array=0 str=1 num=1 num_as_str=0 true=1 false=1 null=1).`
- The only failing assertions are `q0` in each case: `isjson({"a":1})` and
  `isjson({"a":1},object)` return 0 instead of 1. All other q-values match
  expectations (including the deliberate `array=0` from `[1,2,3]` which MUX
  evaluates rather than passes literally).
- The `isjson_fn.mux` smoke is reportedly green on x86-64 Ubuntu with
  `--enable-jit` (Build.sh's default).

- **Mac instrumentation (just before `JsonValidator jv;` in
  `mux/modules/engine/funcweb.cpp::fun_isjson`):**
  ```cpp
  {
      FILE *f = fopen("isjson_probe.log", "a");
      if (f) { fprintf(f, "fargs[0]=<%s>\n",
          reinterpret_cast<const char *>(fargs[0])); fclose(f); }
  }
  ```
  Mac result, running `cd testcases && ./tools/Smoke`:
  ```
  fargs[0]=<"a":1>     ← from isjson({"a":1})   — 5 chars, NO braces
  fargs[0]=<1,2,3>     ← from isjson([1,2,3])
  fargs[0]=<"hello">   ← from isjson("hello")   — 7 chars (correct)
  fargs[0]=<42>        ← from isjson(42)
  fargs[0]=<"a":1>     ← from isjson({"a":1},object)
  ```
  So on Mac, the `{` and `}` are stripped before reaching the function,
  and `JsonValidator` correctly rejects `"a":1` as not-valid-JSON.

- **Probable site of divergence:** `EV_STRIP_CURLY` handling. Either
  `parse_to` in `mux/modules/engine/eval.cpp` (the legacy path) or
  `AST_BRACEGROUP` in `mux/modules/engine/ast.cpp:2336` (the AST path)
  or both. Both are platform-agnostic C++ — yet behavior diverges.

- **Hypotheses (in priority order):**
  1. `--enable-jit` swaps the eval path. JIT/AST may preserve `{}` while
     the no-JIT interpreter strips them. Mac currently can't build with
     `--enable-jit` (DBT AArch64-Apple backend missing — `docs/DBT-PORTABILITY.md`).
  2. A `#ifdef` somewhere in eval changes behavior by host. Grep for
     `__APPLE__`, `__aarch64__`, etc. in `mux/modules/engine/{eval,ast}.cpp`.
  3. The test never actually passed on x86-64 Ubuntu either (recollection
     bug). In which case the test or the function is the bug, not the
     platform.

- **Ask of the x86-64 Ubuntu side:** apply the same one-shot probe above,
  rebuild with the project's default `Build.sh` flags (which include
  `--enable-jit`), run `./testcases/tools/Smoke`, and report what
  `testcases/isjson_probe.log` contains for the `isjson({"a":1})` call.
  - If it reads `fargs[0]=<{"a":1}>` (braces preserved), hypothesis 1 or 2
    is confirmed. Next step: bisect which path keeps them — try also with
    `Build.sh --enable-realitylvls --enable-wodrealms` (no JIT) on Linux
    and see if it now matches Mac.
  - If it reads `fargs[0]=<"a":1>` (braces stripped, same as Mac), the
    test/function relationship is broken everywhere and the smoke
    expectation is the actual bug to fix.

- **Cross-link:** macOS arm64 build PR is
  https://github.com/brazilofmux/tinymux/pull/705 — that PR is unrelated
  to the isjson behavior (it's purely build/linker portability), but it's
  what made the failure visible on Mac for the first time.

- **Also for the x86-64 side:** PR #705 reworks linker flags through new
  configure substitutions (`LIBMUX_SONAME_FLAG`, `ENGINE_SONAME_FLAG`,
  `LD_NOUNDEFINED`, `LD_RPATH_ORIGIN`, `LD_HARDENING`). On Linux the
  substituted values are byte-identical to the original hardcoded ones,
  but worth confirming end-to-end: build `Build.sh` (default `--enable-jit`)
  and `./testcases/tools/Smoke` on x86-64 Ubuntu against the
  `macos-arm64-support` branch, watch for any regression vs. master. Pass
  results back; Mac side will iterate.

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
