# Survey: wildcard matcher (wild.cpp)

Audit of `mux/modules/engine/wild.cpp` — the glob/wildcard matcher reachable with
arbitrary player input (name matching, `@lock`, `lattr`/attribute walks,
`strmatch()`, channel matching, help-topic lookup, `@scan`). Methodology matches
the JIT/DBT campaign: find memory-safety / correctness divergences, verify
empirically, fix, regression-test, file an issue.

## Structure

- `quick_wild_impl(tstr, dstr)` — non-capturing recursive backtracking matcher.
  Behind `quick_wild()`, `wild_match()`, and `strmatch()`.
- `wild1(tstr, dstr, arg)` — capturing version (fills `%0..%9` via `arglist`).
  Behind `wild()`.
- Public entry points pre-lowercase the **pattern** with `mux_strlwr`
  (Unicode-aware) for case-insensitive matching.

## Memory safety — CLEAN

- **Backtracking is bounded.** Both recursive functions check
  `mudstate.wild_invk_ctr >= mudconf.wild_invk_lim` (default 100000) on entry and
  increment it, so adversarial patterns (`*a*a*…`, `*?*?…`) can't cause
  catastrophic backtracking or deep recursion / stack overflow. Callers reset the
  counter at operation boundaries (`wild()`/`wild_match()` reset internally;
  `quick_wild()` relies on the caller — every hot caller resets first). The
  per-operation (not per-match) budget is intentional; not a bug.
- **No under-allocation of capture args.** `wild()` counts `?`/`*` in the pattern
  (over-counting escaped `\?`/`\*`, harmless) and allocates that many LBUFs;
  `wild1` only advances `arg` on real wildcards and bails to `quick_wild_impl`
  once `arg >= numargs`, so it never writes an un-allocated slot. Capture
  arithmetic at the `*`-fill (`(dstr-datapos)-numextra`) stays ≥ 0.
- **Buffers LBUF-bounded.** Capture copies are `mux_strncpy(..., LBUF_SIZE-1)` or
  bounded by data length (< LBUF_SIZE); UTF-8 `?` capture in `wild1` copies ≤ 4
  bytes. Trailing-backslash escape can't walk `tstr` past the NUL (the default
  case returns in both sub-cases when `*tstr == 0`).

## Correctness — ONE divergence found + fixed

**#835 (ff8978f8f): `quick_wild_impl` matched `?` per-byte, not per-character.**
`wild1`'s `?` advances a full UTF-8 character (reads `utf8_FirstByte`, validates
continuation bytes); `quick_wild_impl`'s two `?` sites (the pre-`*` literal loop
and the post-`*` wildcard-skip loop) advanced a single byte. Since the two
matchers are the same match differing only in capture, they must agree — but on
multibyte data they disagreed, and `quick_wild` also disagreed with `strlen()`:

```
strlen(café)=4   strmatch(café,????)=0 (wrong)   strmatch(café,?????)=1 (wrong)
```

Long-standing (present in the mux2.13 snapshots): `wild1`'s `?` was made
UTF-8-aware but `quick_wild_impl`'s was not. Fixed with a shared
`wild_char_len()` helper used at both `?` sites; `*` matching is unaffected
(UTF-8 is self-synchronizing and `*` absorbs any byte/char count difference).
Verified with `muxscript -e` (9 cases) and `strmatch_fn.mux` TC003; smoke
1253/1253.

## Remaining: open rework opportunity (NOT a memory-safety issue)

**Non-ASCII case-insensitivity is a documented limitation.** The pattern side is
lowered with Unicode-aware `mux_strlwr`, but the **data** side folds with
`mux_tolower_ascii` (identity for non-ASCII bytes), and literal comparison is
byte-wise with ASCII-only folding. So `É`/`é` (and the ~24 rare literal
case-transforms that change byte count) don't fold — e.g. `strmatch(CAFÉ,café)`
fails on the `É`. The file header documents this. A fuller character-oriented
rework (Unicode case folding on both sides, decode-once char iteration) would
close it, at the cost of touching a hot, mature primitive — a deliberate,
separate effort, not required for the `?` correctness fix above.
