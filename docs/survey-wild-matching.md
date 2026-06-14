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

## Unicode case folding — non-capturing path FIXED, capturing path deferred

**#836 (d492e670a): quick_wild data-side case folding.** The pattern is
pre-lowered with Unicode-aware `mux_strlwr`, but the data side folded only with
`mux_tolower_ascii` (identity for non-ASCII), so case-insensitive matching worked
when the uppercase was in the pattern but not the data: `strmatch(café,CAFÉ)=1`
yet `strmatch(CAFÉ,café)=0`. `quick_wild_impl` (behind `strmatch`/`quick_wild`/
`wild_match` → name/attr/channel/help/lock matching) now folds each data
character on the fly with `mux_tolower()` via a new `wild_lit_eq()` helper. Key
subtlety: `mux_tolower` returns an **XOR mask** when `bXor` is set (Latin-1 folds
like É→é are XOR transforms, `89^A9=0x20`) — the folded byte is
`original ^ mask`, not `d->p` directly. Verified with a 21-case `muxscript -e`
matrix + `strmatch_fn.mux` TC004; smoke 1254/1254.

**#837 (LANDED): capturing path (wild1).** `wild1()` and `wild()`'s
literal-prefix fast-match now fold the data side per character via the same
`wild_lit_eq()` helper, so `$`-command / `^`-listen wildcard captures match
case-insensitively for non-ASCII too. Captures stay original-case: only the
literal comparisons fold; the `*`/`?` capture spans copy from the unmodified
data. `EQUAL`/`NOTEQUAL` are gone (no remaining byte-wise comparisons).

Verified with a dedicated unit harness, **`wild_test.cpp`** (committed; compile
command in its header) — it links the real `wild.eo` against libmux, supplies
the `mudstate`/`mudconf` globals + `pool_init`, and calls `wild()`/`quick_wild()`
directly, asserting both match results and `%0..%9` capture contents. 20 cases
(ASCII + UTF-8, captures + spans) all pass; the harness was first run against the
byte-wise `wild1` to confirm it caught the bug (`wild("café *","CAFÉ münchen")`
→ no-match) before the fix flipped it green. This is the only coverage of the
capture path — muxscript's REPL drives neither `$`-commands nor `^`-listens, and
the smoke suite has no capture tests.

**Still open (pre-existing, separate from case folding):** `wild1`'s post-`*`
trailing-`?` (`numextra`) handling is byte-wise (1 byte per `?`), unlike its
char-aware standalone `?` — a `*?literal` pattern can split a multibyte character
into a capture. Not addressed here (it's a `?`/capture-boundary bug, not a fold
bug); a candidate for a follow-up using the same harness.

## Test technique

`muxscript -e 'think <expr>'` (from `mux/game`, `LD_LIBRARY_PATH=mux/lib`)
evaluates one softcode expression — ideal for `strmatch`/interpreter-side
verification. `-e` runs its arg as a command, so wrap in `think`. NOTE: muxscript
does **not** match user `$`-commands / `^`-listens (the minimal script driver
doesn't wire `match_mine`/listen plumbing), so `wild()` captures are not
reachable through it.
