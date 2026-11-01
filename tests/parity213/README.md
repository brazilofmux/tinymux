# 2.13 ↔ 2.14 parser parity jig

MUSH function-call recognition is context sensitive in ways a tokenizer
cannot decide on its own. `add(` is a call and `foo(` is not, and only a
function-table lookup separates them — so the parser knows things the
lexer cannot. The grammar is not well-formed, and there is no tidy rule
to validate against.

That means **the specification is what 2.13 actually does**. This harness
measures it rather than theorising about it.

## What it compares

Up to three engines, all driven identically — a real `netmux` on a scratch
port, over a socket, through `probe.py` — so no difference can be an
artifact of one side running under `muxscript`:

| engine | how |
|---|---|
| 2.14 JIT | this tree, default conf (`jit_eval_brackets` on) |
| 2.14 AST | this tree, `jit_eval_brackets 0` |
| 2.13 | reference tree, if one is available |

The **2.14-internal** comparison always runs and needs nothing extra. The
two routes of the same build disagreeing with each other is a defect
regardless of what 2.13 says.

## Running it

```sh
make install                 # from the repo root
sh tests/parity213/run.sh
```

To include the 2.13 reference leg, point it at a built 2.13 tree:

```sh
MUX213_ROOT=/path/to/2.13 sh tests/parity213/run.sh
```

With no `MUX213_ROOT`, a few conventional locations are tried
(`~/tinymux-213`, `~/mux-2.13`, `/tmp/mux213`). If none is found the 2.13
leg is skipped and the internal comparison still runs.

Exit status: `0` no divergence, `1` divergence, `2` setup error.

## Building a 2.13 reference

```sh
git worktree add ~/tinymux-213 origin/release/2.13
cd ~/tinymux-213/mux/src
./configure --enable-realitylvls --enable-wodrealms
make install
```

Two things differ from 2.14 and will bite otherwise:

- **2.13 builds from `mux/src`**, not the repo root. There is no
  top-level configure.
- **2.13 has no `muxscript`.** There is no headless evaluator, which is
  why this harness talks to a live server. That is also the reason the
  2.14 legs are driven the same way rather than the convenient way.

## Not part of `make test`

`make test-parity213` is opt-in. Divergences currently exist (see #1214),
so wiring this into the default suite would fail every build. It is a
measurement tool, not a pass/fail gate — at least until the shapes it
reports have been adjudicated.

## Reading the output

A divergence is a **finding, not automatically a bug**. Some of 2.13's
behaviour is organic rather than designed. For example:

```
[strcat(x (add(1,2) y)]     2.13 -> strcat(x (add(1,2) y)
```

2.13 returns the entire expression verbatim, `strcat(` included, because
the unbalanced parenthesis stopped it recognising the outer call at all.
That has shipped for years and softcode may depend on it. A
reimplementation that treats the grammar as well-formed will "correct"
cases like this and change behaviour silently.

So the output is evidence for a design conversation, not a defect list.

## The verdict column

`corpus.txt` lines are `NAME|expression[|verdict]`. The verdict records
**which engine is right for that shape**, decided case by case. It is
recognised by value rather than position, because `|` is a legal MUX
delimiter and may appear inside an expression.

| verdict | meaning | harness check |
|---|---|---|
| `2.13` | 2.13's output is correct; 2.14 should match | both 2.14 routes must equal 2.13 |
| `2.14` | current 2.14 behaviour is the desired one | the two 2.14 routes must agree; divergence from 2.13 is accepted |
| `both` | all three should agree | all three equal |
| `neither` | neither engine is satisfactory; wants new behaviour | reported, never satisfied or violated |
| `pin` | deliberately deferred | reported, not a violation |
| *(absent)* | not yet decided | reported as UNADJUDICATED if it diverges |

**Only a VIOLATED verdict fails the harness.** An unadjudicated divergence
is reported so it can be decided, not treated as a regression — the point
is to fill the column in over time, turning the map into a specification.

The criterion for deciding is debuggability, not compatibility. 2.14 is
allowed to be better than 2.13. The ranking that has emerged:

1. **worst** — silently corrupting valid input (plausible-looking wrong
   output, no error)
2. silently changing the meaning of valid input
3. an error on input that is genuinely broken — *this is good*; it tells
   the author something is wrong
4. best — a specific diagnostic naming the actual problem

That is why the `PU_*` unbalanced-paren shapes carry `neither`: 2.13
echoes the whole expression back verbatim and 2.14 quietly drops part of
it. Both leave the author guessing; *"unbalanced parenthesis"* would beat
either.

## Grammar policy

Settled 2026-10-31 (#2322). 2.14 has its own scanner and parser producing
a proper AST, so it defines the grammar rather than reproducing 2.13's
back-out heuristics or adopting PennMUSH's optional closers. Verdicts
follow from these rules:

- **R1 — call position.** A call is a name *immediately* followed by
  `(`, at the head of a region: an eval bracket, a call argument, or top
  level. Accumulated text is never a name; 2.13's
  `#-1 FUNCTION (X ADD) NOT FOUND` is not reproduced.
- **R2 — no space before the paren.** `name (args)` is text. In 2.13 it
  was a call only while `space_compress` was on (see below), so there was
  no single behaviour to match, and text fails visibly.
- **R3 — unknown names.** Inside an eval bracket an unknown name in
  call position is `#-1 FUNCTION (NAME) NOT FOUND` and ends its region
  (#1247). At top level an unknown name is text.
- **R4 — scoping and broken input.** Each eval bracket, brace group and
  call argument list keeps its own paren count; an inner region is opaque
  to the outer one (#1219). A call whose `)` does not arrive before its
  enclosing region ends is
  `#-1 FUNCTION (NAME) MISSING CLOSE PARENTHESIS` (#2323) and is not
  dispatched -- for any name inside a bracket, and for a name that
  resolves at top level, where R3 makes an unknown one text. A stray `)`
  with nothing open is literal, parens outside any call are always text,
  and text is emitted exactly as typed, so prose is unaffected.
- **R5 — no auto-close.** Missing closers are never supplied; a typo
  must not become plausible output.

Under R2, `MX_BP` (`[strcat(a [b (c,d) e] f)]`) changed from `2.13` to
`2.14`: `[b (c,d) e]` is a well-formed region of text, not broken
softcode. It had been this README's example of 2.13 being better.

## space_compress changes what the corpus measures

In 2.13 `space_compress` does two unrelated jobs: ordinary output
whitespace compression, and gating the trailing-space trim on the
candidate function name (`eval.cpp:1434`). Turning it off therefore
changes a *parsing* behaviour, not just formatting:

```
                  space_compress on   space_compress 0
[add (1,2)]       3                   #-1 FUNCTION (ADD ) NOT FOUND
[add  (1,2)]      3                   #-1 FUNCTION (ADD  ) NOT FOUND
[ add (1,2)]      3                   #-1 FUNCTION ( ADD ) NOT FOUND
```

So `name (args)` is not a stable 2.13 behaviour — it exists only when a
formatting option happens to be on, and the error it produces otherwise
carries the space inside the function name. That is why R2 settles it as
text in 2.14, independent of the setting.

The corpus is measured under the default (`space_compress` on). To see
the other side, add the setting to the generated conf:

```sh
# in probe_engine(), or by hand against a scratch netmux:
printf 'space_compress\t0\n' >> t.conf
```

A permanent fourth engine leg was considered and left out: it would
double an already multi-minute run for an axis that only affects the
handful of `SC_*` shapes. If a fix lands that touches the trim, adding
the leg is the right next step.

## Runtime

A full run spins three servers and probes every shape against each, so it
takes a few minutes at ~95 shapes. That is why it is opt-in rather than
part of `make test`.

## Trusting the reader

`run.sh` runs `selftest.py` before it measures anything, and refuses to
report if it fails.

The reason is that a desynchronised probe does not produce obviously
broken output — it produces *plausible* rows attributed to the wrong
shape. The original failure (#1233) looked like this in a real run:

```
PN_EMPTY   2.13='<NO-OUTPUT>'
PN_ONLY    2.13='ZZENDPN_EMPTYZZ | (a)'     <- previous shape's sentinel
```

which, once the verdict column existed, presented as a specification
violation that did not exist. On one branch it accounted for five false
`VIOLATED` verdicts out of seven.

`selftest.py` drives the two framing hazards deterministically against a
scripted stand-in server, so the guarantee does not depend on catching an
intermittent fault in the wild:

- **late sentinel** — a case's sentinel arrives after its window closes.
  It must be recognised as stale, its own row recovered rather than left
  `<NO-OUTPUT>`, and the *following* case left uncontaminated.
- **batched delivery** — two cases arrive in one TCP segment. Neither may
  be dropped.

Both fail against the pre-#1233 reader with exactly the symptom above, so
the test pins the bug rather than merely exercising the code. Run it alone
with `python3 tests/parity213/selftest.py` (a couple of seconds), or set
`PARITY_SKIP_SELFTEST=1` to skip the preflight on a fast re-run.

## Extending the corpus

`corpus.txt` is `NAME|expression`, one per line, `#` for comments. Keep
each expression on a single line and avoid the marker text (`ZZ`).

The corpus deliberately probes **position** (where in the text a call
sits), **arming** (what precedes it), and **nesting** — not grammar
productions. It also includes the player-facing prose shapes (`say`,
`pose`, `@pemit`, attribute round-trips), because the reason the rule
exists is that ordinary speech containing `add(1)` must not evaluate
while `[add(1)]` must:

```
say Hey, I tried add(1).                     -> literal
say Hey, I tried add(1) and I got [add(1)].  -> ... and I got 1.
```

Those shapes currently agree across all three engines. Keeping them in
the corpus means a future change that breaks player-visible behaviour
shows up next to the softcode-visible ones rather than being discovered
by a player.
