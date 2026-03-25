# Milestones: Strategic Roadmap Execution

**Updated:** 2026-03-25
**Tracks:** `docs/STRATEGIC_ROADMAP.md`

This file is the working checklist.  Each milestone has concrete exit
criteria.  Check items off as they land; update dates when they do.

---

## Phase 1: Parity Closure (Near-term)

Unblock all remaining Tier 2 functions.  Each must produce identical
output to the server for its full input domain.

### M1. SECURE / SQUISH — switch to co_* wrappers

`co_transform` (for SECURE) and `co_compress` (for SQUISH) already
exist in the blob as Ragel-compiled, ANSI-color-aware functions.
Write `co_secure_wrap` and `co_squish_wrap`, reroute `s_tier2_map[]`,
add to `s_allowlist[]`, smoke-test.

- [x] `co_secure_wrap` in softlib.c (calls `co_transform` with SECURE char set)
- [x] SQUISH rerouted to existing `co_compress_wrap` (no new wrapper needed)
- [x] Update `s_tier2_map[]` entries
- [x] Add SECURE, SQUISH to `s_allowlist[]`
- [x] Smoke clean (666 tests) — 2026-03-25
- [x] Remove dead `rv64_secure`, `rv64_squish`

### M2. TRANSLATE — ECALL or co_* wrapper

Server's `translate_string()` handles control characters and preserves
ANSI color.  Evaluate whether `co_transform` covers this or whether a
new `ECALL_TRANSLATE` calling the native `translate_string()` is
cleaner.

- [ ] Determine approach (co_* wrapper vs ECALL)
- [ ] Implement
- [ ] Add TRANSLATE to `s_allowlist[]`
- [ ] Smoke clean
- [ ] Remove dead `rv64_translate`

### M3. STRMATCH / MATCH / GRAB / GRABALL — Unicode case fold

Server uses `mux_strlwr()` (Unicode case folding) before wildcard
matching.  The rv64 versions use ASCII `tolower`.  Two options:
(a) ECALL for `mux_strlwr` and keep wildcard matching in guest code,
or (b) ECALL the entire match operation.

- [ ] Determine approach
- [ ] Implement (4 functions share one wildcard engine)
- [ ] Add STRMATCH, MATCH, GRAB, GRABALL to `s_allowlist[]`
- [ ] Smoke clean
- [ ] Remove dead `rv64_strmatch`, `rv64_match`, `rv64_grab`, `rv64_graball`

### M4. SORT — DUCET collation

Server supports sort types `u`/`U`/`c`/`C` using `mux_collate_sortkey()`
(UCA-based binary sort keys).  `co_sort_wrap` exists but needs to call
the collation key generator for Unicode sort types.  ASCII/numeric sort
types (`a`/`i`/`n`/`d`/`f`) are likely fine as-is.

- [ ] Audit `co_sort_wrap` against server's `fun_sort` for all sort types
- [ ] Add ECALL for `mux_collate_sortkey` if needed
- [ ] Add SORT to `s_allowlist[]`
- [ ] Add targeted sort-type smoke tests (especially `u` and `c`)
- [ ] Smoke clean

### M5. Parity test suite

The smoke suite (666 tests) is a baseline but doesn't specifically
stress Unicode and ANSI color edge cases in the newly unblocked
functions.  Build targeted tests.

- [ ] ANSI color preservation tests (SECURE, SQUISH with embedded color)
- [ ] Unicode case folding tests (STRMATCH with non-ASCII patterns)
- [ ] DUCET collation order tests (SORT with accented characters)
- [ ] Grapheme cluster edge cases (multi-codepoint clusters in ORD)
- [ ] All smoke tests passing with `--enable-jit`

---

## Phase 2a: ARM DBT (Medium-term)

Port `~/slow-32/tools/dbt` (~23K lines) into the TinyMUX framework.

### M6. Adapt ARM translator to TinyMUX DBT harness

The existing ARM DBT has its own harness (`dbt.c`, `block_cache.c`,
`cpu_state.h`).  TinyMUX has its own (`dbt.cpp`, `dbt_state_t`).
Adapt the ARM translator to work within TinyMUX's dispatch loop,
ECALL mechanism, and memory model.

- [ ] Map ARM DBT structures to TinyMUX `dbt_state_t`
- [ ] Integrate ARM `emit_x64.c` with TinyMUX's code cache
- [ ] ECALL dispatch from ARM-translated code
- [ ] Basic execution: simple Tier 2 function runs on ARM host

### M7. ARM intrinsic registration

Register the same intrinsics (co_*, memcpy, slen, scopy, etc.) for
the ARM backend so Tier 2 functions get native host calls.

- [ ] Port `pretranslate_tier2()` intrinsic registration to ARM
- [ ] Validate co_* functions callable from ARM-translated code
- [ ] Math intrinsics (libm) working

### M8. ARM smoke tests

Full parity with x86-64: all smoke tests pass on ARM hardware.

- [ ] Cross-compile or native-build on ARM target
- [ ] 666+ smoke tests passing
- [ ] Tier 2 blob executing correctly
- [ ] JIT-compiled softcode executing correctly

---

## Phase 3: JIT-by-Default (1-2 years)

**Gate:** Phase 1 parity closure complete.  All blocked functions
unblocked and smoke-tested.

### M9. fun_* coverage audit

Catalog every `fun_*` function in the engine.  For each, determine:
is it reachable when JIT is on?  If yes, can it be replaced?  If no,
it's dead code.

- [ ] Generate complete fun_* inventory (from `builtin_functions[]`)
- [ ] Classify: JIT-handled / ECALL-handled / AST-only (FN_NOEVAL) / dead
- [ ] Document in `docs/FUN_COVERAGE.md`

### M10. Remove --enable-jit gate

JIT becomes unconditionally on.  The configure flag and all `#ifdef
TINYMUX_JIT` guards are removed.

- [ ] Remove `--enable-jit` from configure.ac
- [ ] Remove `#ifdef TINYMUX_JIT` conditionals
- [ ] All builds include JIT
- [ ] Smoke clean on both x86-64 and ARM

### M11. Remove dead fun_* code

Functions classified as dead in M9 are removed from the engine.
The JIT compiler retains the semantic knowledge of what they did.

- [ ] Remove dead fun_* implementations
- [ ] Remove dead entries from `builtin_functions[]`
- [ ] Smoke clean
- [ ] Verify no regression in softcode behavior

---

## Phase 4: Concurrency (Long-term / Research)

No milestones yet — requires design documents first.
See `docs/STRATEGIC_ROADMAP.md` Phase 4 for the problem statement.

- [ ] Design document: mutable engine state inventory and ownership
- [ ] Design document: SQLite write coordination strategy
- [ ] Design document: scope of concurrent evaluation (what can run in parallel)
