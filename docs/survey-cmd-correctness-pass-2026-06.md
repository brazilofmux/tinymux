# Survey: command-side verb correctness pass (June 2026)

A correctness audit of the command-side verb handlers (the `do_*` functions
dispatched from `command.cpp`), complementing the function/eval/lock/wild pass
in [survey-correctness-pass-2026-06.md](survey-correctness-pass-2026-06.md).
Correctness was judged against the in-tree help (`help.txt`, `wizhelp.txt`,
`staffhelp.txt`), the command-table `CA_*`/`CS_*`/switch flags, sibling-command
consistency, documented PennMUSH/TinyMUSH command semantics, and the existing
`testcases/*_cmd.mux` tests.

**Methodology.** One finder per handler produced *candidate* bugs (concrete
scenario → expected → actual); each was verified by two opposing lenses (a
**spec** lens against the help/siblings/tests, and a **code** lens tracing the
dispatch path and permission checks). A finding is "confirmed" only where both
lenses independently agreed.

**Validation caveat.** Unlike the softcode pass, command-side findings could
not be broadly validated on a live server: the smoke harness runs read-only,
and these verbs need a non-controlling builder, cross-owner queued state,
connected players in distinct rooms, or `CA_GOD`/`CA_WIZARD` rights the smoke
executor lacks. These fixes therefore rest on the dual-lens verification plus a
line-by-line reading of each site, and on the full smoke suite still passing
(1264/1264, no regression). They are *not* covered by new live regression
tests; that limitation is called out here deliberately.

**Result: 5 confirmed bugs fixed + 1 help-text correction; 6 contested
findings rejected (all genuine non-bugs, one of which surfaced the help fix).**
Tracked in CHANGES.md as #855–#860.

## Confirmed and fixed

### #855 — `@clone/cost` on an exit bypasses the locality check (create.cpp)

`do_clone` gated exit cloning on `Controls(executor, loc)` only inside the
non-`/cost` cost branch. With `CLONE_SET_COST` (the `/cost` switch, `CA_PUBLIC`)
that branch is skipped, so `@clone/cost <exit>=<n>` spliced a cloned exit into
the executor's current location's exit list with no control check — contrary to
the `@clone` help ("you can only clone exits when you own your current
location"). Fixed by hoisting the `TYPE_EXIT` control check ahead of the cost
branches (with `/inventory`, `loc` is the executor, which is always
controlled). *Mutating; verified by code trace + dual lens.*

### #856 — `@mark` refusal cites a non-existent command (walkdb.cpp)

`er_mark_disabled` (shown by `@mark`/`@mark_all`/`@apply_marked` when automatic
DB cleaning is enabled) told the user to run `@unmark_all`, which exists nowhere
in the command table (grep confirms the string appears only in this message) —
following it yields "Huh?". The real command is `@mark_all/clear` (per
`wizhelp.txt`). Message corrected. *Non-mutating; the absent command was
grep-verified.*

### #857 — `@ps <object>` hides another owner's queue from a controller (cque.cpp)

For a non-player target, `do_ps` left `executor_targ = Owner(executor)`, so
`que_want` filtered out every queue entry not owned by the caller. A wizard /
zone controller inspecting an object owned by another player saw an empty
queue. The sibling `@halt` sets `executor_targ = NOTHING` for a non-player
target (any owner); `do_ps` was missing that `else`. Added it. *Non-mutating;
verified against `do_halt` and `que_want`.*

### #858 — `whisper "<quoted name>"` skips the locality gate (speech.cpp)

The quoted-name `lookup_player` branch of `do_pemit_whisper` appended the
target to the recipient list without the `noisy_check_whisper_target`
locality/connected check that both sibling branches apply, so whispering a
quoted far-away player produced a success confirmation *and* a "too far away"
delivery error, and polluted `A_LASTWHISPER`. Gated that branch to match the
siblings. The adjacent quoted-neighbor branch used a `continue` that did not
advance the parse pointer (a potential non-terminating loop); rewritten to a
conditional append. *Mutating (`A_LASTWHISPER`); verified by code trace.*

### #859 — `@flag/remove` is silent on an unknown flag (flags.cpp)

The `FLAG_REMOVE` branch of `do_flag` emitted no feedback when the flag name was
empty/ill-formed or simply not in the flag-name map — every other flag-name
failure path reports an error. Added the missing `else` diagnostics
("Error: Bad flagname given or flag not found."). *Non-mutating; `CA_GOD`.*

### #860 — `report` help documents the wrong bucket size (help.txt)

`report` uses 4-hour buckets (`HOURS_PER_PERIOD == 4`, a deliberate change in
commit `4a845139f`), but the help still said "8 hour segments". The code is
intended, so the help was corrected to 4 hours. *Doc-only.*

## Contested findings — rejected

All six contested findings were rejected by the spec lens as genuine non-bugs:

- **`examine <obj>/<attr>` realm gate** — the `REALM_DO_HIDDEN_FROM_YOU` gate
  operates at object-perception granularity by design; the obj/attr path
  already enforces `NoExamine`/`Examinable`.
- **`@poor` "sets all to amount"** — `@poor` is the canonical wealth *cap*
  (lowers only); the finding's "raise players below the threshold" expectation
  is wrong.
- **`@poor` rejects negative/fractional** — `is_rational` accepting a sign is
  consistent with sibling money handling; `@poor` is `CA_GOD`.
- **`report` 4h vs 8h** — real doc/code mismatch, but the *code* is intended
  (commit `4a845139f`); resolved as the #860 help fix, not a code change.
- **`@drain <obj>=0` no-op** — `@drain <object>` (no count) is the documented
  form and works; the `=<count>` form is undocumented.
- **`@set` flag feedback vs target `QUIET`** — long-standing intended MUX
  behavior (flag feedback honors the *setter*'s QUIET, not the target's).

## Subsystems found correct

`set` (clean — @set/@name/@alias/@lock/@chown/@power/@cpattr/@mvattr/@edit/
@wipe/@trigger/@chzone all verified vs help, command-table flags, and
`trigger_cmd.mux`), `move` (clean — goto/get/drop/enter/leave/teleport
mechanics, locks, and `/quiet` gating), `command` (clean — switch/abbreviation/
`CS_*` arg parsing), and `object` (clean — `@dbck` and the destroy/clone/chown
lifecycle primitives). The `look`, `wiz`, and `predicates` handlers produced
only contested/rejected findings.

## Provenance

Generated by the `cmd-correctness-sweep` workflow (12 finders + dual-lens
verification, 38 agents). The full structured result is archived with the run.
