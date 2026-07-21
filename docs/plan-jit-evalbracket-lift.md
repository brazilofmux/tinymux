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
| R1-letq-r | `[setq(0,PRE)][letq(0,IN,r(0))][r(0)]` | `INPRE` | `PREPRE` |
| R2-scope-r | `[setq(0,A)][localize(setq(0,B))][r(0)]` | `A` | `B` |
| RW-strcat | `strcat(%q0,setq(0,B),%q0)` with `%q0=A` | `AB` | `BB` **(production route too)** |
| D2-ecall-r | `[setq(0,PRE)][u(me/%q9)][r(0)]` | `MUTATED` | `PRE` |

(R1/R2/RW found during Phase 2 implementation, D2-ecall-r during Phase 3; all
seven covered by `oracle.sh`, green as of Phase 3. RW was reachable in
production without brackets.)

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

- Add a `jitstats` counter `qreg_resyncs` (`jit_stats_t` in
  `include/dbt_compile.h`, emitted by `fun_jitstats`) — zero until Phase 2
  wires the resync. Lets tests assert the new path actually fires.
  *(Landed in #981.)*
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

**LANDED (2026-07-20), scope grew during implementation** — investigation
surfaced three additional q-register defects beyond planned D1, one of them
**live in production** (not latent behind the guard). Four pieces:

1. **Runtime slot resync (the planned work).** `ecall_invoke_fun` detects a
   `_RESTORE_QREGS` return and re-marshals all q-slots via the Phase 1 helper;
   bumps `qreg_resyncs`.
2. **Compile-time tracking save/restore (R1/R2).** The lowerer's `qreg[]` SSA
   tracking serves `r(n)` reads, and inlined scopes never restored it: a
   tracked `r(n)` *after* a scope restore read the inner SSA
   (`[setq(0,A)][localize(setq(0,B))][r(0)]` → `B`), and letq assignments
   never *updated* tracking, so `r(n)` *inside* a letq body read the pre-letq
   SSA (`letq(0,IN,r(0))` → stale). All three inlined scopes
   (ulocal/letq/localize) now snapshot `qreg[]` before the body and restore
   after; letq assignments update tracking like setq does.
3. **`%q` read materialization (RW — production-reachable).** `%q`
   substitution reads lowered to bare `emit_sref` — a *deferred pointer
   deref* observed at consumption time, not a point-in-time load. Any
   read-before-write therefore returned the post-write value:
   `strcat(%q0,setq(0,B),%q0)` → `BB` instead of `AB`, **on the production
   route, bracket-free** — a live wrong-answer bug the parity sweeps' corpus
   never generated. Reads now materialize at their sequence point via a
   single-arg `HIR_STRCAT` (snapshots the slot into an output slot at
   position). Perf: cached JIT on a six-read `%q` expr is still ~3.5× faster
   than native AST (0.16µs vs 0.56µs/call).
4. **Build-stamp invalidation gap.** `JIT_BUILD_STAMP` (`__DATE__ __TIME__`)
   lives in `jit_compiler.cpp`'s TU, so an incremental build touching only
   `hir_lower.cpp`/`hir_codegen.cpp` kept serving persisted blobs compiled by
   the *old* lowering. `jit_compiler.eo` now explicitly depends on both
   sources in the engine Makefile, refreshing the stamp whenever codegen
   changes.

**Green results:** oracle 5/6 (D1-letq `INNEROUTER`, D1-localize `CBA`,
R1 `INPRE`, R2 `A`, RW `AB`; D2 stays red for Phase 3); smoke 1310/1310
including new `jit_parity_fn.mux` TC010 (bracket-free read-write-read on the
production JIT route); `jit_diff` 400×2 seeds, 0 LOGIC; `qreg_resyncs` fires.

### Phase 3 — Fix D2: resync slots after reg-mutating ECALL returns (guard still up)

**LANDED (2026-07-20).** Conservative cut as planned, plus the compile-time
mirror of D2 found during implementation:

- **Runtime:** `ecall_invoke_fun` re-marshals the `%q` slots after *every*
  host ECALL, masked by the program's `subst_mask` (new `eval_ctx.qreg_mask`,
  set at all three run paths) — `%q`-free programs skip at a single bit test.
  Subsumes Phase 2's `_RESTORE_QREGS` special case. A pure callee makes the
  resync a semantic no-op.
- **D2-ecall-r (new shape):** the compile-time `qreg[]` tracking has the same
  exposure — a tracked `r(n)` read *after* an opaque ECALL returned the stale
  SSA (`[setq(0,PRE)][u(me/%q9)][r(0)]` → `PRE` instead of `MUTATED`). The
  lowerer now clobbers tracking after ECALLs whose callee may mutate
  registers: the general opaque-call site (tier2 wrappers excluded — pure),
  `_EDEFAULT_GET` (evaluates the attribute body), and non-local inlined `u()`
  *after the merge block* — the inline body and the runtime `fun_u` fallback
  are alternative paths, so tracked values lowered along the inline path are
  not valid post-merge. (`ulocal` restores registers on both paths; its
  Phase 2 snapshot restore already covers it.) Clobbered reads fall back to
  the `fun_r` ECALL, which reads the authoritative `global_regs`.

**Green results:** oracle **7/7 green, exit 0** — graduated into `make test`
as `test-jit-qreg` (skips cleanly on non-JIT builds); smoke 1310/1310;
`jit_diff` 400×2 seeds 0 LOGIC; perf: `%q`-free ECALL expr unaffected
(cached 0.29µs vs native 0.59µs), a two-ECALL `%q`-reading expr still beats
native (0.61µs vs 0.82µs) despite masked resyncs + materialization.

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

**LANDED (2026-07-20), toggle default OFF.** Results and findings:

- **Toggle:** `jit_eval_brackets` config directive (cf_bool, CA_GOD, default
  off; runtime-settable via `@admin`). Guard admits terminated brackets when
  on; unterminated brackets always bail; `EV_NOFCHECK` gated at the call site
  (that mode is literal passthrough, which the lowerer doesn't model).
- **Harness upgrades (required for a meaningful green check):**
  - `jit_diff` J and I sides now run in **separate processes** — the I side
    always with the toggle off, keeping the eval-bracket bail (= the true
    production interpreter route) as the oracle. An in-process
    `asteval({…})`-forced I side was tried first and manufactured ~113 false
    LOGIC divergences: `fun_asteval` is not flag-faithful (trims a trailing
    space after an empty-yielding bracket that production preserves) —
    filed as **#987**.
  - `JITDIFF_BRACKETS=1` mode: bracket-wrapped corpus (embedded-in-text,
    adjacent, pure) + toggle in the J-side conf + a canary proving the
    toggle took.
  - `SMOKE_EXTRA_CONF` passthrough in the smoke runner for whole-suite
    toggle-on runs.
- **The core result:** `letq_fn.mux` TC002 (`letq scoping` — the historical
  `INNERINNER` failure) and the localize scoping cases **route through the
  JIT and pass** with the toggle on. Bracket sweep (`JITDIFF_BRACKETS=1`,
  400 exprs): **0 LOGIC**. The Phases 0–3 groundwork held.
- **Two pre-existing production JIT bugs witnessed** by the toggle-on smoke
  run (their TCs carry brackets, so they were AST-carried until the toggle;
  both then shown to reproduce **bracket-free on today's default route**):
  - **#988** — maxArgsParsed comma-catenation missing: `sha1(abc,def)`
    computes `sha1("abc")` (JIT drops `,def`; `ecall_invoke_fun` silently
    clamps).
  - **#989** — tier2 `wordpos()` miscounts UTF-8 positions (byte-vs-cluster,
    #980 family). The sweep corpus is ASCII-only — blind here; UTF-8 corpus
    mode added to the Phase 5 list.

**Toggle OFF (default):** oracle 7/7, smoke 1310/1310, standard sweep 0
LOGIC — byte-identical behavior, cleanly gated.

**Correction (post-#990):** the Phase 4 PR claimed the toggle-on smoke was
"green except exactly the two filed bugs" — that read only the tail of the
log. The full failure set was 11 lines / 9 distinct TCs: the three fixed by
#988/#989 (sha1 parser-tests ×2 files + wordpos) plus **six more
pre-existing divergences**, confirmed pre-existing by a stash-bisect and
tracked as **#991**.

**#988 and #989 — FIXED (2026-07-20):**
- #988: the general call lowering now mirrors `ast_eval_node`'s
  maxArgsParsed comma-catenation (constant pieces join at compile time so
  folding still sees an SCONST; runtime pieces STRCAT with `,` literals).
  `sha1(abc,def)` and `sha1(abc,strcat(d,ef))` both compute the packed
  hash on the JIT route.
- #989: `rv64_wordpos` now bounds `charpos` by `co_cluster_count` and
  resolves it to a byte offset via `co_mid_cluster` (mirroring the
  interpreter's grapheme walk); softlib.rv64 blob rebuilt (the RISC-V
  cross-toolchain is on this box). ASCII behavior unchanged.
- Locked by bracket-free `jit_parity_fn.mux` TC011/TC012 on the default
  JIT route; smoke 1312/1312.

**Gate for Phase 5 (default-on):** ~~the six #991 divergences fixed,
toggle-on smoke fully green~~ **DONE (2026-07-20)** — three root causes:

1. **Sequence leading-space trim** (four TC classes at once): the lowerer
   mirrored only `ast_eval_node`'s *trailing* AST_SPACE trim, not the
   leading one — `objeval(*p, ncon(#1))` returned `" 0"` vs the AST's
   `"0"`. Live today bracket-free (any leading-space arg to a NOEVAL
   re-evaluator).
2. **ECALL eval-flag leakage**: the AST dispatches builtins with
   `feval & EV_TRACE` (ast.cpp); `ecall_invoke_fun` passed the full
   program flags, so `fun_eval`'s inner `mux_exec` inherited
   `EV_STRIP_CURLY` and stripped nested braces the AST preserves
   (`eval(if(1,{outer {inner} text}))` → `outer inner text`). Masked to
   `EV_TRACE`, mirroring the AST exactly.
3. **Two more byte-oriented blob wrappers**: `rv64_delete` did raw byte
   arithmetic (split the é in `delete(AéB０C,1,1)`) — now mirrors
   `fun_delete` (negative-range normalization + `co_delete_cluster`);
   `co_repeat_wrap` silently truncated at LBUF — now mirrors
   `fun_repeat` exactly (single-char clipped fill, `#-1 STRING TOO
   LONG` overflow).

**Toggle-on smoke: 1312/1312 — fully green.** Oracle 7/7; standard sweep
0 LOGIC; bracket sweep default seed 0 LOGIC.

Remaining for Phase 5 (default-on): **#993** (one state-dependent LOGIC at
`SEED=7` — batch-only, isolation-clean, precisely the stateful-corpus
target — and one COLOR-encoding divergence, #980 family), the sweep-corpus
modes (UTF-8/`chr()`, stateful registers), soak with the toggle on, then
flip the default and retire `jiteval`.

### Phase 5 — Default on, widen coverage, remove scaffolding

Surface-reduction commitments (so the push doesn't permanently grow the
feature surface):

- **Retire or demote `jiteval`.** Once the guard is lifted, the production
  route reaches everything `jiteval` bypasses the gate for — plain eval vs
  `asteval()` covers parity with no bypass function. Remove it, or demote to
  a debug build (`#ifdef`) if a forced-JIT probe still earns its keep.
- **Teach `jit_diff` stateful register shapes** (setq/setr/letq/localize
  woven between `%q`/`r(n)` reads). The RW production bug was invisible to
  the sweep only because the corpus never generated read-write-read shapes —
  a better corpus pushes the system with zero server surface, and keeps
  paying after we stop looking.

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
