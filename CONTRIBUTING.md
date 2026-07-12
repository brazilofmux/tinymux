# Contributing to TinyMUX

TinyMUX 2.14 is in **alpha**. No games should run it in production yet; the
goal for the next several months is to raise quality — correctness, test
coverage, portability — before entering beta. Contributions that move that
needle are very welcome, whether you are a person or an AI agent working with a
human in the loop.

This document is the contribution *workflow and standards*. For repository
layout, build commands, and coding style, see [`AGENTS.md`](AGENTS.md); for the
generated-file map, see [`docs/generated-files.md`](docs/generated-files.md).

## Principles

- **Every PR is reviewed on its merits, regardless of who wrote it.** A clean
  track record earns goodwill, not a lighter review — good etiquette is exactly
  the cover a bad-faith contribution would use. Reviews read the diff.
- **Derive claims from *this* tree, never from another implementation.** Build
  it, run it, probe it. Expected values, error strings, and behavior come from
  what the current code actually does — not from a spec, another MUSH server,
  ICU, Python, or a source-reading guess.
- **Narrow beats broad.** A small, self-contained, reviewable change lands fast.
  A sweeping one stalls. When in doubt, slice it.

## Before you start

Build the server per the "Build, Test, and Development Commands" section of
[`AGENTS.md`](AGENTS.md), then confirm a clean smoke baseline:

```sh
cd testcases && ./tools/Makesmoke && ./tools/Smoke   # results in smoke.log
```

The smoke tests exercise `engine.so` and the loadable modules, so build with the
full `make install` (not a netmux-only build) before running them. Add
`--enable-jit` at configure time to also cover the JIT/DBT path.

A green run is: **every test succeeds, `Dispatched: N / N expected` (dispatch
equals the suite count), 0 failures, 0 crashes.** See "Reading smoke results"
below — the *counts* depend on build config, but those three properties do not.

## The contribution rhythm

This is the flow that has worked well in practice:

1. **Discuss first.** For anything beyond a trivial fix, open an issue (or
   comment on the relevant tracking issue) describing the change before writing
   code. Cheap to redirect a plan; expensive to redirect a finished PR.
2. **Proof-of-shape, optionally.** For work where the *target* is a judgment
   call, it's welcome to build the change locally, report the branch, the diff
   size, and your validation — but hold the push until the target is agreed.
   Zero rework if the answer is "go"; nothing wasted if it's "different target."
3. **One issue per slice; one PR per issue.** Keep the tracking issue as a map,
   not a checklist that grows without bound. A big effort becomes a series of
   small, individually-reviewable PRs.
4. **Scope your commit message honestly.** Use `Partially addresses #NNN` when a
   slice advances a larger issue; reserve `Closes #NNN` for actually completing
   it.

## Contributing tests

The smoke corpus (`testcases/*.mux`) is the front line of the pre-beta hardening,
and the easiest place to start.

- **Pick a real gap.** Many `*_fn.mux` files have happy-path coverage only.
  Boundary values, error strings, empty/null inputs, and grapheme-cluster edges
  are the productive seams. `israt(1e10)` vs `isnum(1e10)`, `right()` on an
  emoji+modifier cluster, `fmod(x,0)` — each pins a real behavioral fact.
- **Derive every expected value by probing this tree**, e.g.
  `muxscript -e 'think [yourexpr]'` or a scratch `.mux` script. Do not import an
  expected value from another implementation. Sanity-check Unicode results
  against the tables actually built here.
- **Two evaluation lanes.** A build with `--enable-jit` compiles hot softcode
  through the JIT/DBT; without it, everything runs the AST interpreter. These
  can diverge (that's a bug — several were found this way). To exercise the
  compiled path, feed a *runtime* value (`setr()`), because all-constant
  arguments constant-fold and never reach it. An assertion that pins behavior on
  both lanes at once is the strongest kind.
- **Placement.** `testcases/` is for softcode-visible behavior. `tests/`
  (`libmux`, `color_ops`, `db`, `netaddr`) is for lower-level C/C++ behavior
  (nullptr handling, LBUF limits, byte-level UTF-8). Put a case where its
  subject lives.
- **Harness conventions.** Fold a new assertion into an existing test case when
  it belongs to the same semantic block; add a new `TC00N` when it's a distinct
  block (e.g. an error path alongside a positive-formatting case). **Exactly one
  test case per file fires `@trig me/tr.done`** — the terminal one. If you add a
  new terminal case, move the trigger to it; don't leave two.

## Reading smoke results

- **Counts are deterministic per build config, not random.** A `--enable-jit`
  build runs more cases (JIT-specific suites); an interpreter-only build logs
  those as `Skipped (JIT not enabled)`. A stable difference between two
  environments is almost always this, not a flake. Report your build config
  alongside your counts.
- **The signal that matters is dispatch + failures + crashes**, not the raw
  succeeded count: `Dispatched: N / N expected`, `Failed: 0`, `Crashes: 0`. The
  completeness gate names any suite that never dispatched.
- **A fast run with a low count and no failures may be a crash, not a pass.** If
  muxscript dies mid-run the harness reports it as a crash; on macOS a
  `~/Library/Logs/DiagnosticReports/muxscript-*.ips` also appears. Don't read a
  truncated run as success.
- **Report harness limitations separately from real failures**, so a reviewer
  can tell "the suite couldn't test X" from "X is broken."

## Contributing engine or client code

- **Verify beyond the tests.** For a runtime change, confirm it builds, the
  database still loads, and full smoke stays green — and probe the specific
  behavior you changed. A passing test suite is necessary, not sufficient.
- **Mind the tier2 parity copy.** Some helpers in `mux/rv64/src/softlib.c`
  deliberately mirror host functions for JIT parity. If you change a host
  function's observable behavior, grep the softlib for a matching wrapper, fix
  it in the same change, update any test that pins the old value, and regenerate
  the blob. A host-only fix silently diverges the two lanes.
- **Never hand-edit generated files.** Edit the source input and regenerate;
  keep the source change in the same diff. See `docs/generated-files.md`.
- **Keep diffs tight.** Revert incidental regeneration churn (shifted `#line`
  markers, autotools aux-script noise) that isn't part of your change.

## Reporting bugs

- Include a concrete reproduction: the exact input, the observed result, and the
  expected result — ideally a one-line `muxscript -e` probe or a short `.mux`
  script.
- If the behavior differs between the JIT and interpreter lanes, say so and give
  both results — that's a specific, valuable class of report.

## Reporting security issues

Legitimate security reports are welcome and taken seriously. To be actionable, a
report must include a **concrete, code-level exploitation path**: the specific
function and line, the mechanism (what memory is written past what bound, or what
invariant is violated), and a realistic trigger. "A scanner flagged a pattern"
or an unqualified severity label is not, by itself, a finding — automated tools
routinely mislabel length-bounded or heap-managed code as exploitable. A report
that can't be traced to a real defect in the code will be closed with an
explanation. If you found a genuine memory-safety or correctness issue, the
concrete path is what lets us confirm and fix it quickly.

## Etiquette

- Assume good faith; give it in return. Specific, factual review comments in
  both directions.
- Respond to review feedback with the change plus how you verified it. Cite the
  commit you built against.
- It's fine to flag something you noticed in passing (a latent bug, an adjacent
  gap) without turning it into scope creep — note it, and let the maintainer
  decide whether it becomes its own issue.

Thanks for helping make TinyMUX solid enough for beta.
