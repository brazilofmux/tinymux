# tests/growth — complexity coverage

`make test-growth` (opt-in; not part of `make test`).

This measures **how cost grows with input size**, and asserts the growth class.
It deliberately does not assert any absolute number.

## Why growth and not thresholds

`tests/perf` compares ns/call against a per-machine baseline. That works, but it
needs a baseline file per box, its tolerances had to be calibrated by hand, and
only one of its three legs is trustworthy enough to gate on (#2046). Absolute
timings are a property of the machine as much as of the code.

An exponent is not. Doubling N costs **2.0× if the algorithm is linear** and
**4.0× if it is quadratic**, on every machine — the hardware cancels out of the
ratio. So this half of performance coverage is portable by construction: the
same expected values are correct on a Mac laptop, an x86-64 Linux box and a
Windows VM, with no calibration step and no baseline to go stale.

The trade is real and worth stating plainly: this cannot see a 10% regression.
It sees the class change. A 10% regression is an annoyance; an O(n) list walk
turning into an O(n²) one is what makes a live game unplayable, and it is
invisible to a threshold test until the list gets long — at which point the
threshold test reports "slower" without saying why.

## Reading the output

Each case reports a fitted exponent `b` — the slope of log₂(time) against
log₂(N), i.e. the *b* in *t ~ N^b* — followed by the raw per-doubling ratios:

| b | per doubling | meaning |
|---|---|---|
| ~0 | ~1.0× | constant |
| ~1 | ~2.0× | linear |
| ~2 | ~4.0× | quadratic |

The verdict comes from `b`, not from the ratios, because one bad point must not
decide it. The first case measured in a muxscript process pays cold
compile-cache and allocator costs the rest do not; with per-ratio bands that
alone produced a 1.42× first step on an otherwise clean 1.97×/2.03× line and
failed the run. There is now a discarded warm-up pass as well, so no case is
penalised for its position in the list — but the least-squares fit is what makes
the result robust rather than merely luckier.

The class bands in `driver.py` are wide and **do not touch**. A genuinely noisy
measurement lands in a gap and is reported `unclassified`, which fails, rather
than being quietly promoted into the neighbouring class.

Nothing is classified unless its smallest-N time clears `FLOOR_US`. Below that
the measurement is dominated by fixed per-call overhead and *everything* reads
as constant — which is precisely how a quadratic loop passes a growth test. A
ratio near 1.0× is usually evidence the measurement is broken, not evidence the
code is fast.

## Instruments

Two routes, each read from the instrument that is honest about it:

- **interp** — `astbench()`'s `ast=` field. `fun_astbench` calls
  `ast_eval_node` directly, so the construct really runs on the interpreter.
- **jit** — `rvbench()`'s `cached=` field: compile cache plus block cache, what
  a warm server executes. `rvbench` returns `#-1 COMPILATION FAILED` when an
  expression will not compile, so a bail cannot be read as a fast result.

Two fields are deliberately **not** used, and both have already misled us:

- `astbench`'s `jit=` field. `fun_astbench` calls `jit_eval` unconditionally.
  When the lowerer has no case for a construct, `jit_eval` returns false
  immediately and the timing loop measures a function that does nothing.
  `citer()` reads a flat 2.7 µs at every N through that field — that is not
  speed, it is absence (`CITER` has no case in `hir_lower.cpp`). `astbench`
  cannot notice, because its `result=` comes from a third `ast_eval_node` call
  and never from the JIT.
- `rvbench`'s `native=` field. It calls `mux_exec`, which dispatches to the JIT
  for any JIT-eligible expression — and `iter()` is eligible. For `iter()` it is
  therefore a *second* measurement of the JIT, differing from `cached=` only by
  `mux_exec`'s per-call dispatch overhead. Reading it as "the interpreter" is
  what made #2052 look like a defect shared by both routes when only one route
  has it.

## Input shape is checked before anything is timed

`PROBES` counts every generated list at every N actually used, and the run
aborts before timing if any is not the size it claims.

This is not defensive padding. `repeat(a ,N)` trims the trailing space inside
its argument and yields **one** element, so an `iter()` over it ran once
regardless of N and looked beautifully flat. And an LBUF is 32768 bytes, so
`lnum(8000)` is ~39 KB of digits and truncates silently — a "list of 8000" that
is nothing of the kind. A growth measurement over input that is not the size it
claims is worse than no measurement, because it looks like a result.

## Controls

`ladd(lnum(N))` is measured on both routes and must read linear on both. If a
change makes *every* case read quadratic, that is the harness's fault and the
controls say so.

The tightest control is in the defect itself: `iter(lnum(N),1)` is measured on
both routes — the same expression over the same list. The interpreter line must
read linear while the JIT line reads quadratic. A harness that could not read
linear would fail on that first line.

## xfail

`CASES` records known-wrong growth classes with an issue number. An xfail does
not silence a case:

- an xfail that still fails prints `xfail` and does not fail the run;
- an xfail that **passes** prints `XPASS` and **does** fail the run, because the
  fix has landed and the table is now lying.

Both arms are worth re-checking after any change to `driver.py` — deliberately
break a case and confirm the run goes red. A check that cannot go red is
decoration.
