# JIT differential fuzzer

Finds correctness divergences between the softcode **JIT** (tier2/rv64 wrappers
+ HIR lowering) and the **interpreter** (`ast_eval`), by generating random
nested softcode and evaluating each expression both ways.

It catches *compositional* bugs the per-function audit can't — e.g. `ljust`
not truncating only shows up when `width < content`, which arises naturally
through nested width args.

## Usage

```sh
make install          # from repo root — needs mux/game/bin/{muxscript,engine.so}
testcases/tools/jit_diff/run.sh [count] [batch]   # defaults: 200, 50
SEED=7 testcases/tools/jit_diff/run.sh 400         # vary the corpus
```

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

- **#772** — `ljust`/`rjust`/`center` don't truncate when `width < content`
  (the rv64 wrappers pass `bTrunc=0`). Until that lands, every run reports
  these as LOGIC. Once fixed, LOGIC should be empty (COLOR may remain — the
  nested-color encoding difference is unfiled and benign-looking).
