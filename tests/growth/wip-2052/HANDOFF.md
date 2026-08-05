# WIP handoff — #2052 cursor-based ITER extraction

**Status: incorrect. Do not land.** `parser_fn` TC020 fails.

Everything here is on branch `wip/2052-iter-cursor-incomplete`, commit
`ef65278cf`. `master` is clean and green; nothing in this directory is wired
into any build.

---

## The failure

```
cd testcases && ./tools/Makesmoke parser_fn && ./tools/Smoke
grep TC020 smoke.log
```

```
TC020: parser iter with pct-subst. Failed (X Y Z11 21 31 vs X Y Z31).
```

`testcases/parser_fn.mux:370` compares the interpreted and compiled routes on

```
[iter(X Y Z,%i0)]
[iter(10 20 30,[add(%i0,1)])]
```

The compiled route returns `X Y Z31` — the second loop emits only its **last**
element. The first loop is correct.

## What is isolated

Reverting **`hir_lower.cpp` alone**, keeping the blob and the `s_tier2_map`
registration, turns TC020 green. So the fault is in the lowering, not in
`rv64_split_token`.

## What is NOT reproducible

Every one of these is **correct** on the compiled route:

| expression | result |
|---|---|
| `iter(lnum(5),1)` | `1 1 1 1 1` |
| `iter(lnum(5),##)` | `0 1 2 3 4` |
| `iter(a b c,#@-##)` | `1-a 2-b 3-c` |
| `words(iter(lnum(200),1))` | `200` |
| `iter(a\|b\|c,##,\|)` | `a b c` |
| `iter(lnum(4),##,%b,-)` | `0-1-2-3` |
| `iter(10 20 30,[add(%i0,1)])` | `11 21 31` |
| `[iter(a b c,##)][iter(d e f,##)]` | `a b c d e f` |
| `[setr(1,[iter(X Y Z,%i0)][iter(10 20 30,[add(%i0,1)])])]` | `X Y Z 11 21 31` |

**Two `iter()` loops in one program, `%i0`, and the `setr()` wrapper are all
individually fine when typed at `think`.** It fails only through the smoke
harness — i.e. as an attribute triggered on an object rather than typed.
That difference is the unexplained part and is where I would start.

## Hypothesis tried, and refuted

Loop-carried q-registers must be read where the loop's PHI lives. `INUM` and
`ACC` are `LOAD_Q`'d in the **header** block; my first version loaded
`QREG_ITER_CURSOR` in the **body**. Moving the load to the header and the
store to the latch (alongside `inum_next`) — the exact shape `INUM` uses —
**did not fix it**. So either that is not the mechanism, or not the only one.

That change is in the committed version.

## Hypotheses not yet tested

Unverified. Listed because each is cheap to falsify, not because any is likely.

1. **Shared q-register across two loops in one program.** Before this change
   the cursor was `rc.alloc_output()` — a *distinct buffer per loop*. It is now
   `QREG_ITER_CURSOR`, shared by every ITER in the program. `INUM`/`ACC` are
   also shared and work, so sharing alone is not fatal — but the cursor is the
   only one now read through `HIR_ITOA` into a tier2 call argument, and the
   argument marshalling (`rc.alloc_fargs`, `rv_patch_fargs`) may hold an
   address that the second loop reuses.
2. **`%i0` and the substitution machinery.** TC020 uses `%i0` rather than `##`.
   Worth checking whether `%i0` resolution touches a q-register index near 10-12
   (`QREG_ITER_INUM`/`ACC`/`CURSOR`), which would collide.
3. **Compile-cache key / eval flags.** The smoke path runs inside `@if`, with
   `cargs`/`ncargs` set, so its eval flags differ from a typed `think`. The
   compile cache is keyed on `(expr, nLen, eval)`, so smoke may execute a
   *different compiled variant* than the one my manual tests exercised. This
   would explain "correct when typed, wrong when triggered" better than
   anything else on this list, and it is the one I would test first.
4. **Output-buffer liveness.** Two extra `emit_call` results per iteration
   (`elem`, `next_str`) plus an `HIR_ITOA` change what
   `allocate_output_buffers` (`hir_codegen.cpp`) assigns. If the accumulator's
   buffer is now reused while still live, the symptom — keeping only the last
   element — is what you would expect.

Hypothesis 4 matches the *symptom* most closely; hypothesis 3 matches the
*conditions* most closely.

## What the change contains

- `mux/rv64/src/softlib.c` — `rv64_split_token`, a cursor walk mirroring
  `split_token`'s single-byte-delimiter semantics including the space-run
  collapse. Two modes (element / next offset) rather than an out-parameter, so
  it stays a pure function like every other wrapper. Verified in the blob at
  `0x110b4`.
- `mux/modules/engine/jit_compiler.cpp` — `SPLIT_TOKEN` in `s_tier2_map` and in
  the `tier2_allowed` allowlist.
- `mux/modules/engine/hir_lower.cpp` — ITER carries a byte offset in
  `QREG_ITER_CURSOR` (declared since the beginning, never used until now).

### Do not split these apart

Registering `SPLIT_TOKEN` while `hir_lower` still holds the **original** branch
would make that branch live for the first time. It calls `emit_call` with a
hardcoded `func_idx` of `0` instead of `engine_api_lookup(...)`, and passes the
cursor as an `emit_sconst` whose storage the callee is expected to mutate.
It has never executed. The registration and the lowering move together or not
at all.

## Measured, before the correctness failure was found

A clean **2× at every size, with the exponent unchanged**:

| N | before | after | no JIT |
|---:|---:|---:|---:|
| 200 | 210.45 µs | 111.20 µs | 30.55 µs |
| 500 | 1 274.47 µs | 640.33 µs | 80.22 µs |
| 1000 | 5 102.50 µs | 2 531.14 µs | 153.72 µs |
| 2000 | 20 226.75 µs | 9 939.45 µs | 308.65 µs |

Growth harness `b = 1.99 → 1.95`. **Still quadratic**, because
`acc = strcat(acc, osep, body)` is a second O(n²) term of the same magnitude —
filed as **#2072**. Even correct, this half alone does not reach the bar.

**The bar is the no-JIT column** (#2068), not the previous JIT column.

## Reproducing the measurements

`jitbase.sh <label>` in this directory. It uses `benchmark()`, which loops
`mux_exec` and is **not** JIT-gated, so the same script runs on a
`--enable-jit` tree and a non-JIT one and compares the real dispatch path both
ways. Raw min-of-3 results are checked in beside it:

- `jit_*.txt` — master with the JIT, before this change
- `nojit_*.txt` — a tree configured without `--enable-jit`
- `fixed_*.txt` — this branch

Rebuild after touching the blob:

```
make -C mux/rv64 && make -C mux/rv64 install && make install
```

## Instruments — read before measuring anything

`rvbench`'s `native=` calls `mux_exec`, which **dispatches to the JIT**. It is
not the interpreter, and reading it as one is what made #2052 look like a
defect shared by both routes. `astbench`'s `jit=` times `jit_eval` even when it
bails instantly. Use `astbench`'s `ast=` for the interpreter and `rvbench`'s
`cached=` for the JIT — or `benchmark()` for the real dispatch path.
See `tests/growth/README.md`.
