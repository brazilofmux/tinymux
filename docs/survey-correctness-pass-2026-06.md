# Survey: correctness pass over softcode functions, evaluator, lock & wildcard (June 2026)

A correctness audit (distinct from the memory-safety pass in
[survey-memsafety-pass-2026-06.md](survey-memsafety-pass-2026-06.md)) of the
surface where "correct" is sharply defined and empirically testable: the
softcode function library, the expression evaluator, and lock/wildcard
semantics. Correctness was judged against the in-tree help text
(`mux/game/text/help.txt`), consistency with sibling/inverse functions, the
existing per-function regression tests (which encode intended behavior), and
documented PennMUSH/TinyMUSH norms where MUX follows them.

**Methodology.** One finder per function group / subsystem produced *candidate*
bugs, each as a concrete `input → expected → actual` claim with a `testExpr`.
Every candidate was verified by two opposing lenses: a **spec** lens (is the
claimed `expected` actually the correct answer per the help text / siblings /
existing tests?) and a **code** lens (does the implementation really produce
the claimed `actual`?). Because correctness reasoning is error-prone, every
survivor was then **empirically validated against a live server** (the smoke
harness) before any fix — the live engine is the ground truth.

**Result: 4 confirmed bugs fixed; 1 real divergence deferred pending a design
decision; 1 dual-lens "confirmed" finding refuted by the live server (a false
positive); 2 contested findings rejected.** The empirical gate proved its
worth: the `switch()`/`#$` finding passed *both* code and spec verifiers yet
the running engine already behaves correctly. Fixes are tracked in CHANGES.md
as #850–#853; regression tests are in `testcases/correctness_fn.mux`.

## Confirmed and fixed

### #850 — `xor()` 32-bit truncation (funmath.cpp)

`fun_xor` stored each argument as `int tval = mux_atol(fargs[i])`, narrowing
`mux_atol`'s 64-bit result to 32 bits before the truthiness test. Any non-zero
value whose low 32 bits are zero (e.g. `4294967296`) was treated as false, so
`xor(4294967296)` returned `0` while its list sibling `lxor(4294967296)`
returned `1`. Fixed to `bool tval = isTRUE(mux_atol(...))`, matching
`and()`/`or()`/`lxor()`. Regression: `correctness_fn.mux` TC001.

### #851 — `wrapcolumns()` drops a character on a hard break (funceval.cpp)

On a hard (no-space) line break the wrap loop did `src[brk] = '\0'; src += brk
+ 1;` with `brk == colWidth`, overwriting a *content* character (not a
delimiter) and then skipping it. `wrapcolumns(abcdefghij,4,1)` lost the `e` and
`j`. Because the wrapped segments are emitted as NUL-terminated strings sharing
the input buffer, the in-place approach cannot preserve the break-column
character; the loop was rewritten to copy each segment into a scratch buffer
with its own terminator, consuming the break character only when it is a space.
Regression: `correctness_fn.mux` TC002.

### #852 — `step()` evaluates the attribute as the wrong object (funceval2.cpp)

`fun_step` called `mux_exec(atext, …, executor, caller, enactor, …)`, whereas
every sibling list-munging u-function (`map`, `mix`, `foreach` at funceval2.cpp
855/1022/1044) passes `(thing, executor, enactor)` — i.e. the attribute-owning
object as the executor. So `%!`/`me` and permission checks inside a stepped
`obj/attr` resolved to the caller rather than to the attribute's object. A
behavior change confirmed empirically: with the bug, `step(helper/ATTR,…)`
where `ATTR` is `[name(me)]` returned the caller's name; fixed, it returns the
helper's name. Regression: `correctness_fn.mux` TC004 (a co-located helper
object distinguishes the two identities).

### #853 — `tr()` argument-count contract / NULL deref (functions.cpp)

`tr()` was registered with a one-argument minimum (`{T("TR"), fun_tr, MAX_ARG,
1, 3, …}`) but the body unconditionally reads `fargs[1]`/`fargs[2]` for any
non-empty input. `tr(abc)` therefore reached the body with `fargs[1]`/`[2]` ==
NULL and dereferenced them (a crash — missed by the memory-safety pass because
the defect lives in the registration table). Fixed to `min == 3`, matching the
documented contract and siblings (`STRDELETE` min 3, `ACCENT` min 2); `tr(abc)`
now returns an argument-count error. Regression: `correctness_fn.mux` TC005.

## Deferred — needs a design decision

### `unique()` ignores its documented `<sorttype>` (functions.cpp)

`unique(<list>[,<sorttype>[,<sep>[,<osep>]]])` — help.txt documents
`<sorttype>` as `w=word, i=case-insensitive, n=numeric`, but `fun_unique`
never reads `fargs[1]`; it always compares with a literal `strcmp`. The
divergence is real, but the *fix* is a design decision: the `w/i/n` vocabulary
in the help does not match the `a/i/n/f/d/u/c` sort-type letters used by
`sort()`/`sortkey()` in the same file, and `w` ("word") is not a sort-type
letter anywhere in the codebase or in PennMUSH's helpfile. **Held pending a
look at how PennMUSH / RhostMUSH / TinyMUSH define `unique`'s sorttype** before
implementing (or before correcting the help to match the literal-only
behavior). No code change made.

## Refuted by the live server — NOT a bug

### `switch()`/`case()`/`switchall()`/`caseall()` and `#$` during pattern eval (ast.cpp)

The finding (which passed *both* the code and spec verifiers) claimed that
`ast_noeval_switch`/`ast_noeval_switchall` bind `mudstate.switch_token = <str>`
before the pattern loop, leaking `#$` into pattern evaluation so that
`switch(foo,#$,MATCHED,NOMATCH)` returns `MATCHED`. Run against the live engine
(which dispatches `switch()` through exactly that AST path), the result is
`NOMATCH` — the correct behavior. The verifiers' "verified live" claim was not
actually executed. No change made; `correctness_fn.mux` TC003 guards the
correct behavior against future regression.

## Contested findings — rejected

- **Whitespace-only function argument** (`strlen( )` → `1`): claimed it should
  trim to `0`. Rejected by the spec lens via `strcat_fn.mux` TC002, which
  asserts the current (space-preserving) behavior is intended.
- **`%v<non-letter>` / `%i<non-digit>` substitution fallback** (`eval(A%v1B)`
  → `A1B`): claimed it should be `AB`. Rejected — the "correct = AB" premise
  cites a stale classic evaluator; `parser_fn.mux` TC029 pins the current
  pass-through behavior as intended.

## Subsystems found correct

| Subsystem | Notes |
|-----------|-------|
| functions.cpp (1–5500) | string/list handlers (mid/right/extract/index/pos/member/remove/wordpos/iter/fold/itemize/choose/filter/map …) verified vs help and existing tests; after/before no-match asymmetry confirmed intended. |
| functions.cpp (5400–11000) | itemize/lnum/choose/merge/splice/printf/sort family/setq-setr/result-set accessors; JIT lowering of DELETE/EDIT/TRIM/RIGHT compared to the interpreter. |
| funceval.cpp | squish/strtrunc/between/delextract/soundex/caplist/columns/table verified vs tests (only `wrapcolumns` above). |
| funcweb.cpp | url/html/json/base64 — finder candidate rejected by both verifiers. |
| funmath.cpp | rounding/fp formatting/vector/stat/baseconv — only `xor()` above; platform-sensitive fp formatting explicitly not flagged. |
| boolexp.cpp | lock operator precedence, indirect/carry/owner locks, truth tables. |
| wild.cpp | `*`/`?` semantics, capture-register assignment, case folding, anchoring. |

## Provenance

Generated by the `correctness-sweep` workflow (10 finders + dual-lens
verification, 28 agents), followed by empirical validation of every survivor
on the live smoke server. The full structured result is archived with the run.
