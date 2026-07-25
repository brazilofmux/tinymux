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
