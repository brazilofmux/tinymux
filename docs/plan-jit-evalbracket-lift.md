# Plan: lifting the `AST_EVALBRACKET` JIT guard

Phased, individually-testable decomposition of the "big JIT" work described in
[`survey-jit-perf.md`](survey-jit-perf.md): allow bracketed `[...]` softcode to
be JIT-compiled instead of unconditionally bailing to the classic AST evaluator.

Each phase leaves the tree green (full `make test` + `make test-ganl` +
`make test-netaddr`) and is independently reviewable.

Decisions (2026-07-20): carry the Phase 4 runtime toggle (patience over
early flag removal); Phase 3 lands the conservative resync-after-every-ECALL
cut first, per-builtin flagging comes as a follow-up. No schedule pressure —
phases land one at a time as they prove out. The actual guard flip is
the *last* code change — all correctness machinery is built and verified behind
the still-active guard first, so nothing observable changes until the final
phase.

## The goal in one line

`mux/modules/engine/ast.cpp:2684-2686`, inside the `jit_can_handle()` walk:

```cpp
if (node->type == AST_EVALBRACKET) {
    return false;                    // <-- disqualifies the whole program
}
```

Any eval-bracket anywhere in the AST disqualifies the entire expression from the
JIT. Lifting it lets scoped, bracketed softcode (the common shape of real
attributes) compile.

## Why the naive revert failed

A previous straight revert passed a broad parity sweep but broke the smoke suite
on q-register **scope** semantics:

- `letq_fn.mux` TC002 — `[setq(b,OUTER)][letq(b,INNER,%qb)][%qb]` expected
  `INNEROUTER`, produced `INNERINNER` (`testcases/letq_fn.mux:32-38`).
- `overflow_inject_fn.mux` TC010 — nested `localize()` expected `CBA`, produced
  `.CCC.` (`testcases/overflow_inject_fn.mux:191-203`).

The lowerer needs no change — `hir_lower.cpp:3464-3477` already lowers
`AST_EVALBRACKET` as an FMAND (evaluate) context. The failure is entirely in how
the JIT models q-register scope save/restore.

## Root cause (the slot/`global_regs` asymmetry)

The JIT keeps a **parallel copy** of every q-register in guest RISC-V memory, at
fixed SUBST slots, separate from the interpreter's `mudstate.global_regs[]`:

- Slot layout: `SUBST_QREG0 = 4`, one 256-byte slot per register at
  `SUBST_BASE + (SUBST_QREG0+i)*SUBST_SLOT` (`include/dbt_compile.h:254`).
- JIT `%q` reads go **straight to the slot** via `emit_sref`
  (`hir_lower.cpp:3749-3754`) — this is the fast path the JIT exists for.
- Slots are populated from `global_regs` **only at program entry**
  (`jit_compiler.cpp:2273-2281` eager, `2452-2463` lazy/masked).

Coherence of the two copies is maintained on the **write** side but not the
**restore** side:

| Operation | Touches slot? | Touches `global_regs`? | Where |
|-----------|:-:|:-:|-------|
| JIT `setq`/`setr` (`ECALL_SETQ`/`ECALL_SETQ_PACK`) | ✅ | ✅ | `jit_compiler.cpp:3510-3592` |
| interp `fun_setq`/`fun_setr` (fallback) | ❌ | ✅ | `functions.cpp:10678,10718` |
| `_SAVE_QREGS`/`_RESTORE_QREGS` (inlined scope) | ❌ | ✅ | `functions.cpp:14846-14893` |
| `save_global_regs`/`restore_global_regs` | ❌ | ✅ | `eval.cpp:696-751` |
| `localize`/`letq`/`ulocal` | ❌ | ✅ | `funceval.cpp:1680-1775` |

Stale-read sequence (all inside one JIT program):

1. slot[i] = `global_regs`[i] = `OUTER` (entry marshal).
2. `_SAVE_QREGS` saves `OUTER` (global only); slot still `OUTER`.
3. body `setq(i,INNER)` → dual-write: slot[i]=`INNER`, `global_regs`[i]=`INNER`.
4. `_RESTORE_QREGS` resets `global_regs`[i]=`OUTER`, **but slot[i] stays `INNER`**.
5. later `%q<i>` reads slot[i] = `INNER` → **stale** → `INNERINNER` / `.CCC.`.

The write side keeps both copies coherent; the restore side fixes only one. That
asymmetry is the entire bug.

## Scope is smaller than the survey implied

Agent mapping (2026-07-20) established that **command-boundary** save/restore does
*not* need new work, because every JIT program re-marshals slots from
`global_regs` at entry. That covers boolexp/lock eval (`boolexp.cpp:219`),
`@switch` (`ast.cpp:2089`), hooks (`command.cpp:1317`), look/`@verb`
(`look.cpp:497`), top-level command eval (`engine.cpp:484`), **and the queue
reload** (`cque.cpp:199-211`) — all sit outside a single `mux_exec`/JIT program.

Within one JIT program there are exactly **two** divergence sources:

- **D1 — inlined scope save/restore.** `_SAVE_QREGS`/`_RESTORE_QREGS` emitted
  around inlined `localize`/`letq`/`ulocal` bodies (`hir_lower.cpp:2285-2312,
  2396-2421, 2509-2524`). This is the `INNERINNER`/`.CCC.` failure.
- **D2 — reg-mutating ECALL callees.** A non-inlined `u()`/`ulocal()` (or any
  fallback that runs interpreter softcode doing `setq`) ECALLs out
  (`ecall_invoke_fun`, `jit_compiler.cpp:2547-2612`), which mutates
  `global_regs` but never re-marshals the caller's slots on return.

Both are latent today only because brackets currently disqualify the programs
that would expose them. Fixing D1 and D2 is necessary and sufficient.

**All three shapes reproduced deterministically via `jiteval` (Phase 0,
2026-07-20)** — clean cache, expressions passed raw via `v()` indirection:

| Shape | Expression | AST | JIT (forced) |
|-------|-----------|-----|--------------|
| D1-letq | `[setq(b,OUTER)][letq(b,INNER,%qb)][%qb]` | `INNEROUTER` | `INNERINNER` |
| D1-localize | `[setq(0,A)][localize(…nested…)]%q0` | `CBA` | `CCC` |
| D2-ecall-u | preload `%q9=qf`; `[u(me/%q9)][%q0]`, `qf` does `setq(0,MUTATED)` | `MUTATED` | `ENTRY` |

D2 confirmed on the ECALL path (`ecalls=3`, `qreg_resyncs=0`). Two hazards
verified along the way:

- **Static tracking masks the bug in simple probes.** When the whole scope
  body is statically resolvable, the lowerer's compile-time qreg SSA tracking
  produces correct results without touching the runtime slots (e.g. a fresh
  `letq` over tracked values compiles to 2 HIR insns reading the entry slot) —
  so a passing simple case proves nothing about the restore path. Oracle
  expressions must read `%q` *after* a restore whose body wrote a tracked
  register (D1) or route the write through an opaque ECALL (D2).
- **Quoting footgun.** `jiteval`/`asteval` have ordinary-arg evaluation
  (flag 0): a bracketed expression passed inline is evaluated by the *outer*
  evaluator before the function sees it, and both sides compare pre-computed
  literals — vacuously equal. Always store the expression in an attribute and
  pass `v(attr)`. (`astbench` is FN_NOEVAL for this reason; its `result=` is
  AST-side only and is *not* a JIT parity check.)
- `localize(u(<opaque>))` is **accidentally consistent** today: the
  interpreter-side `setq` never writes the slot, and the restore returns
  `global_regs` to the entry value — which is what the slot still holds. Bare
  `u()` (D2) is the exposed case.

## Design decision: resync slots, don't drop the cache

Two options:

- **(a)** Drop the SUBST-slot cache; route every `%q` read/write through
  `global_regs` across the guest boundary.
- **(b)** Re-marshal the slots from `global_regs` at the restore/return points,
  mirroring what `ECALL_SETQ_PACK` already does on the write side.

**Recommend (b).** Option (a) discards the direct `emit_sref` slot read that is
the JIT's reason to exist; (b) keeps the fast read path and adds a bounded number
of re-sync points. The re-marshal primitive already exists inline
(`jit_compiler.cpp:2452-2463`); it just needs extracting and re-invoking.

---

## Phases

### Phase 0 — Fixtures and oracles (no product change)

Stand up everything needed to *observe* the bug before touching it, so later
phases have a red/green signal.

- Add a `jitstats` counter `qreg_resyncs` (struct at `include/dbt_compile.h:53`,
  emitted by `fun_jitstats`, `jit_compiler.cpp:4494`) — zero until Phase 2 wires
  the resync. Lets tests assert the new path actually fires.
- Add `jiteval(<expr>)` — a wizard-only debug function that forces an
  expression through `jit_eval()`, bypassing the `jit_can_handle()` gate
  (returns `#-1 JIT BAILOUT` if the JIT declines). This makes the guarded
  divergences observable in-server *without lifting the guard*: compare
  `jiteval(v(attr))` against `asteval(v(attr))`. (An earlier draft proposed a
  `dbt_test.cpp` unit instead, but that harness links only the RV64
  translator files — it cannot reach the engine's marshal/ECALL machinery
  where the bug lives. `jiteval` tests the real path.)
- Add `testcases/tools/jit_qreg/oracle.sh` — opt-in script driving the three
  divergence shapes below; **expected red (exit 1)** until Phases 2–3 land,
  then it flips green and guards them.
- Record a baseline `jit_diff` parity sweep run
  (`testcases/tools/jit_diff/run.sh 400`, plus `SEED=7`, `SEED=13`) with the
  guard *up* — this is the regression floor the final phase must still meet.

  **Baseline recorded 2026-07-20:** default seed and `SEED=7` — 400 compared,
  0 LOGIC each. `SEED=13` — 1 pre-existing LOGIC divergence, unrelated to
  brackets/q-registers: `after(ansi(r,ij x cd ab x),ansi(r,cd))` returns empty
  from the JIT tier2 wrapper vs ` ab x` from the interpreter (filed as
  **#980**; verified with `jiteval`, plain-text `after()` agrees). The
  guard-lift phases must not add to this count; #980 is tracked separately.

**Green check:** full `make test`; `oracle.sh` reports exactly the three known
divergences (harness self-checks the AST side); baseline sweep clean. No
production behavior change (counter starts at 0, guard untouched; `jiteval` is
a new wizard-only debug fn).

### Phase 1 — Extract the slot-marshal primitive (pure refactor)

Lift the inline "populate SUBST_QREG slots from `global_regs`" loop
(`jit_compiler.cpp:2452-2463`) into a named helper, e.g.
`marshal_qregs_to_slots(ctx, mask)`, and call it from both existing populate
sites (`2273-2281`, `2452-2463`).

**Green check:** behavior-identical; full `make test` + parity sweep unchanged.
Reviewable as a no-op refactor. This is the primitive Phases 2–3 reuse.

### Phase 2 — Fix D1: resync slots on inlined scope restore (guard still up)

Extend the `_RESTORE_QREGS` ECALL path so that, in JIT context, after
`restore_global_regs` it re-marshals the q-slots via the Phase 1 helper (a
dedicated resync ECALL, or a combined restore+resync opcode mirroring
`ECALL_SETQ_PACK`). Wire it into the ULOCAL/letq/localize restore emission in
`hir_lower.cpp` (`2285-2312, 2396-2421, 2509-2524`). Bump `qreg_resyncs`.

This is a **correctness no-op today** for any program that currently bails, but
it activates for scope constructs that *are* already JITtable without brackets
(e.g. `localize(setq(0,X))` — a funccall with no eval-bracket). Resyncing slots
to `global_regs` can only *align* the JIT with the authoritative interpreter
state, never diverge from it.

**Green check:**
- Full `make test` stays green (proves no regression on the currently-reachable
  JIT set).
- `oracle.sh` D1-letq and D1-localize flip red→green (`INNEROUTER`, `CBA`);
  D2-ecall-u stays red until Phase 3.
- `jitstats()` shows `qreg_resyncs>0` across the oracle run.

### Phase 3 — Fix D2: resync slots after reg-mutating ECALL returns (guard still up)

After an ECALL whose callee may mutate `global_regs` (`ecall_invoke_fun`,
`jit_compiler.cpp:2547-2612`), re-marshal the slots. Conservative first cut:
re-marshal after every fun ECALL; optimize later by flagging which builtins touch
registers (`setq`/`setr`/`u`/`ulocal`/`localize`/`letq`/`unsetq`/…). Guard the
cost behind `subst_mask` so programs that never read `%q` pay nothing.

**Green check:** full `make test`; parity sweep still clean; `oracle.sh`
D2-ecall-u flips red→green — **all three shapes now green, exit 0**, and the
script graduates from opt-in to a `make test` step; no measurable regression on
`rvbench(<hot-expr>,100000)` for `%q`-free programs (the mask short-circuits).

### Phase 4 — Lift the guard (behind a toggle first)

Replace the unconditional bail at `ast.cpp:2684-2686` with:

```cpp
if (node->type == AST_EVALBRACKET) {
    if (!node->has_close_bracket) return false;   // unterminated → legacy
    // caller also gates on !(eval & EV_NOFCHECK): NOFCHECK brackets are
    // literal passthrough (ast.cpp:2366), but the lowerer always evaluates.
}
```

Add the `!(eval & EV_NOFCHECK)` gate at the `jit_can_handle` call site
(`ast.cpp:2736`, which already has `eval` in hand). Land the lift behind a
`mudstate` debug flag (default off) so it can be A/B toggled at runtime for
bisecting, mirroring how `sandbox()`/`asteval()` force the AST path
(`jit_compiler.cpp:4395`, `ast.cpp:2764`).

**Green check (toggle ON):**
- `letq_fn.mux` TC002 and `overflow_inject_fn.mux` TC010 now route through the
  JIT and stay green (the two canonical failures).
- `jit_diff` parity sweep (`run.sh 400`, `SEED=7`, `SEED=13`) — **zero `LOGIC`
  divergences**, matching the Phase 0 baseline.
- `jitstats()` shows `eval_handled` up and bracket-driven `eval_bailout` down;
  `qreg_resyncs>0`.
- `jit_parity_fn.mux` / `jit_route_parity_fn.mux` / `jit_fold_parity_fn.mux`
  (all use `asteval({…})` as the AST oracle) stay green.

**Green check (toggle OFF):** identical to pre-Phase-4 — proves the lift is
cleanly gated.

### Phase 5 — Default on, widen coverage, remove scaffolding

- Flip the toggle default on (or remove it) once sweeps are consistently clean.
- Add permanent smoke testcases for scoped brackets that now JIT (extend
  `letq_fn.mux`/`localize` coverage with cases asserted via `astbench` parity).
- Large-N sweeps with multiple `SEED`s; `minimize.py` any survivor before
  filing.
- Update `survey-jit-perf.md` to record the guard as lifted and cross-link this
  plan; note the perf delta from `rvbench` on representative bracketed exprs.

**Green check:** full `make test`; extended sweeps clean; survey updated.

---

## Verification tooling reference

| Tool | What it proves | How |
|------|----------------|-----|
| `make test` (smoke) | end-to-end scope semantics | `letq_fn.mux` TC002, `overflow_inject_fn.mux` TC010, `include_fn.mux`, `unsetq_fn.mux` |
| `jit_diff` sweep | JIT vs AST differential, at scale | `testcases/tools/jit_diff/run.sh [N] [batch]`, `SEED=…`; `LOGIC` tag = real divergence |
| `astbench(expr,iters)` | per-expr result+timing parity | `ast.cpp:2797`; emits `ast=… jit=… ratio=… result=…` |
| `asteval({expr})` | force AST oracle for a route | `ast.cpp:2764`; used by `jit_*_parity_fn.mux` |
| `jitstats()` / `jitstats(reset)` | handled vs bailout, new `qreg_resyncs` | `jit_compiler.cpp:4494`; counters at `dbt_compile.h:53` |
| `rvbench(expr,iters)` | perf (native vs compile-each vs cached) | `jit_compiler.cpp:4668`; **timing only, not parity** |
| `dbt_test` run_code/run_code_dbt | interp-vs-translator, deterministic | `dbt_test.cpp:261/304`; distinguishes DBT bug from GCC miscompile |

Build gate: JIT is compile-time only — configure with `--enable-jit`
(`configure.ac:81`, default off), then `make clean install` from repo root. There
is no runtime enable/disable knob; A/B is per-call via `sandbox()`/`asteval()` or
(added in Phase 4) the debug toggle.

## Out of scope (already covered by entry marshal)

Queue reload (`cque.cpp:199-211`), boolexp/lock (`boolexp.cpp:219`), `@switch`
(`ast.cpp:2089`), hooks (`command.cpp:1317`), look/`@verb` (`look.cpp:497`), and
top-level command eval (`engine.cpp:484`) all sit at command boundaries outside a
single JIT program, so the per-program entry marshal already keeps them coherent.
No new resync points are needed there — only D1 and D2.
