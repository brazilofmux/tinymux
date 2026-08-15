# JIT differential fuzzer

Finds correctness divergences between the softcode **JIT** (tier2/rv64
wrappers + HIR lowering) and the **interpreter** (`ast_eval`), by generating
random nested softcode and evaluating each expression both ways.

It catches *compositional* bugs the per-function audit can't — e.g. `ljust`
not truncating only shows up when `width < content`, which arises naturally
through nested width args.

## Usage

```sh
cd mux && ./configure --enable-jit ...   # REQUIRED: --enable-jit is off by default
make clean install    # from repo root — needs mux/game/bin/{muxscript,engine.so}
testcases/tools/jit_diff/run.sh [count] [batch]   # defaults: 200, 50
SEED=7 testcases/tools/jit_diff/run.sh 400         # vary the corpus
```

The tree **must** be configured with `--enable-jit` (and cleanly rebuilt):
without it the entire JIT path is compiled out and both sides would run the
interpreter. `run.sh` probes for this (via `jitstats()`) and exits 2 rather
than report a meaningless pass.

Exit status: `0` no logic divergence, `1` divergences found, `2` setup error.
This is a developer tool, not part of `make test`.

## How it works

For each generated expression `EXPR`:

- **J (JIT):** `@if strlen(setr(0,EXPR))={...sha1(r(0))...}` — the `@if`
  condition is a pure function tree, so `mux_exec` JIT-compiles it; `r(0)`
  holds the JIT result.
- **I (interp):** `@pemit #1=I~id~[sha1(EXPR)]~` — the eval-bracket in the
  `@pemit` argument makes `mux_exec` bail the JIT, so this is the interpreter
  result.

Results are compared by SHA1. Two hashes per side: raw and `stripansi`'d:

- **LOGIC** — stripped hashes differ → a real semantic divergence.
- **COLOR** — raw differ but stripped match → internal color-encoding only
  (same visible text); reported separately, not a hard failure.

## Corpus shapes

Roughly: 20% custom delimiters/output separators, 15% float arithmetic, 15%
`ifelse()`/`switch()` branches, 14% nested `u()`/`ulocal()` evaluation, the
rest string/list function trees. All default-on; `--brackets`, `--utf8` and
`--longreg` layer additional modes on top.

The float, branch and nested shapes were added after **three consecutive
defects escaped this fuzzer for the same reason** — the corpus was string and
list functions over literal operands:

- **#1143** — float PHI arms collapsed to a string PHI, copying raw double
  bits as a C string.
- **#1157** — a bare `%N` used as an `ifelse()` condition folded to `0`.
- **#1159** — FP slot addresses pinned at 0 in nested compiles, so every
  float temporary aliased guest address 0.

All three are invisible unless the operand is unknown at **compile** time: a
folded float allocates no FP slot, and a folded condition emits no branch.
That is why the corpus builds runtime values out of `v(fz.*)` attributes
(defined in `setup.txt`, replayed by every batch and by the minimizer) rather
than literals or `%q` — `v()` is an ECALL the compiler cannot see through,
while compile-time `%q` tracking can fold `%q` back to a constant. That fold
is precisely what made `ifelse(%q0,...)` look healthy while `ifelse(%0,...)`
was broken.

Nested shapes always wrap the `u()` in an outer call (`strcat`, `add`,
`iter`, …). The wrapper is the point: a bare `u()` compiles through the
one-shot compiler and was always correct, while a `u()` consumed by an
enclosing *compiled* expression routes the inner compile through the shared
heap — the path that was broken. The I side's eval-bracket bails the JIT for
the **outer** expression only; the inner body still evaluates through
`mux_exec` on both sides, and that asymmetry is what exposes the class.

## Soundness (don't break this)

The J and I sides only agree when `EXPR` has no eval-context-sensitive
constructs. The classic trap: a **bracket-less mid-string** function call —
`foo add(2,3) bar` is correctly left literal when bare, but `[...]` evaluates
it. The J side sees it bare and the I side brackets it, so such an expression
is a false positive (this caused the now-closed #773).

The generator therefore **never embeds a function inside literal text**: every
node is a pure-literal leaf or a function-call node, and colored leaves are a
single `ansi(<color>,<words>)` call wrapping the whole list. Keep it that way.

## Known divergences

- **#1171** (OPEN) — `mul()`/`power()` keep more precision than the
  interpreter when an argument is a nested float call: `mul(sqrt(1.5),1)`
  gives `1.224744871391589` under the JIT and `1.224744871391588` under the
  interpreter. The interpreter passes a nested result to its caller as a
  formatted decimal string and re-parses it, and that round trip is lossy;
  the JIT keeps the value in an FP slot. Same class as #829, in a function
  that fix did not cover — float `add`/`sub` route to the `rv64_add` blob
  and already agree.
  **This is a live divergence, so some seeds exit 1.** `SEED=42` over 300
  expressions trips it; `SEED=1` and `SEED=7` are clean. `sqrt`/`power` are
  deliberately NOT removed from the corpus to keep runs green — hiding the
  shape would defeat the point of having added float coverage at all.
- **#772** (fixed) — `ljust`/`rjust`/`center` didn't truncate when
  `width < content`; the rv64 wrappers now pass `bTrunc=1`.
- **COLOR class** (fixed) — the recurring COLOR reports came from three
  real raw-byte divergences, all closed: `co_split_words` excluded a
  word's leading color codes from its range (so item-function rebuilds
  dropped them — a no-op `ldelete(ansi(h,ab cd),99)` lost the
  highlight), the blob's `co_mid_wrap` used code-point `co_mid` instead
  of `co_mid_cluster` (kept trailing color the interpreter excludes),
  and `co_setunion` took LIST1's copy on ties where `handle_sets` takes
  LIST2's (except the identical 1×1 special case, which keeps LIST1's).
  A clean run reports **0 COLOR**, and 0 LOGIC apart from #1171 above;
  treat any COLOR report as a real finding, not noise.

## Minimizer isolation discipline (#784)

The minimizer screens candidates in shared batches (50 per fresh muxscript
process) for speed, but every **accepted** reduction is confirmed standalone
in its own process, and the original expression is confirmed in isolation
before shrinking. An expression that diverges only alongside its fuzz batch
(order/state-dependent, the #778 class) is reported as **STATE-DEPENDENT**
with its original form; `run.sh` preserves the work dir on failure so the
batch (`b*.txt`) can be replayed.
