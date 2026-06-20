# Survey: JIT / eval performance (profiling pass)

Performance (not correctness) pass over the softcode eval path: the JIT/DBT
kernel, the layers into it, and the SQLite bytecode cache. Companion to
`docs/survey-jit-dbt.md` (which is the *correctness/security* audit).

## Method
- **Clean measurement tool: `rvbench(<expr>,<iters>)`** — a wizard function that
  benchmarks three paths on the same expression: native AST (`mux_exec`),
  compile-every-time, and the production path (`compile_cached` LRU +
  `run_cached_program` block-cache reuse). It calls the compiler directly, so it
  bypasses the eval gate and gives clean per-call microsecond numbers.
- Driven offline via `muxscript -g . -c bench.conf --readonly` against a smoke DB
  (`testcases/tools/benchsetup.sh` builds an isolated `bench.d` with player #1 as
  wizard). `jitstats()` / `jitstats(reset)` expose the engine counters.
- `perf record` requires lowering `kernel.perf_event_paranoid` (was 4; needs <=2)
  via sudo — restore to 4 afterward.

## ⚠️ Methodology gotchas (cost real time to discover)
1. **`think [expr]` does NOT exercise the JIT.** `jit_can_handle`
   (`ast.cpp:2678`) bails on any `AST_EVALBRACKET` node — a *deliberate* parity
   guard ("eval brackets have subtle re-evaluation behavior"). So bracket-wrapped
   softcode runs on the AST interpreter, not the JIT. Benchmark with **bare**
   expressions (`think add(...)`, not `think [add(...)]`), or via `rvbench`
   (which takes the bare expr directly). Symptom of the trap: `jitstats` shows
   `eval_attempts=0` no matter how many evals you run.
2. **The smoke DB contaminates `perf` profiles.** `smoke.flat` runs
   `rvbench`/`benchmark` testcases at @startup (millions of iterations), which
   dominate a `perf record` of muxscript. Self-time samples are still indicative;
   call-graph attribution to `run_cached_program` etc. is drowned out. Trust
   `rvbench` numbers over `perf` call-graph for the production path.
3. The JIT needs the Tier-2 blob (`bin/softlib.rv64`) to load
   (`tier2_lazy_init`, `jit_compiler.cpp:834`) or `jit_eval` bails before
   `eval_attempts++`. The on-disk blob is loaded by magic/version only (no build
   stamp), so a stale blob still loads.

## Measured landscape (rvbench, cached µs/call)
| expression | needs_jit | native | cached | JIT speedup |
|---|---|---|---|---|
| `add(100,200)` (folds) | no | 0.96 | 0.03 | ~32× |
| `strcat/mid/ucstr` (folds) | no | 0.33–0.39 | 0.08 | ~4× |
| `r(0)` (1 ecall, setup floor) | yes | 0.72 | 0.41 | 1.8× |
| `ladd(1 2 3 4 5)` (tier2) | yes | 3.22 | 1.58 | 2× |
| `iter(lnum(100),mul(%i0,%i0))` | yes | 250 | 250 | **1.0× (none)** |

Takeaways:
- **Constant-folded exprs** are essentially free under JIT (early-return memcpy
  in `run_cached_program`, `jit_compiler.cpp:2291`).
- **Per-call setup floor ≈ 0.4µs** for any JIT-executed program: the
  unconditional `materialize_program` + `tier2_reset_writable` + full SUBST/CARGS
  population at the top of `run_cached_program` (runs even on the program-reuse
  `dbt_rerun` fast path — `materialize_program`'s blob memcpy is redundant when
  `program_id` matches).
- **`iter()` / list iteration gets zero JIT benefit** and is the dominant heavy
  cost in real softcode. The loop *is* compiled (`disp=2` for 100 elements — not
  re-dispatching), but each element pays string↔number marshalling:
  list-walking (`co_find_delim`/`co_extract`), number parse, and result
  formatting. The JIT does the same conversions the interpreter does → no win.

## Top cost: number→string formatting (`NearestPretty`)
`perf` self-time was ~10% in `dtoa_r` via `NearestPretty(double)` for the
`iter(...,mul(...))` workload. **`NearestPretty` (`lib/mathutil.cpp`) calls
`mux_dtoa` nine times** (an `R ± k·ulp` shortest-decimal search). For
integer-valued results (the common case for `mul`/`add`/`sub` over integer
lists) this is pure waste — a whole number is already its own shortest decimal.

### ✅ FIXED — integer fast-path in `NearestPretty`
Added an early return for `fabs(R) < 2^50` and `R` integer-valued (cast
round-trip). Provably equivalent: for |R| < 2^50, `ulp(R) < 2^-2`, so every
search neighbor `R ± k·ulp` (k=1..4) is non-integral and `dtoa`-renders with
strictly more digits than integral R — the search already returns R. |R| ≥ 2^50
(where a neighbor can be integral) and NaN/Inf fall through to the exact path.
Shared by the AST interpreter, HIR, and the Tier-2 blob `mul` (calls host
`NearestPretty` as an intrinsic, `jit_compiler.cpp:744`), so all paths benefit.

Result (rvbench, before → after):
- `iter(lnum(100),mul(%i0,%i0))`: 250 → **163 µs/call** (−35%), native and cached.
- `add(mul(3,3),div(100,r(0)))`: cached 1.37 → **0.72 µs** (−47%).
Parity verified (`add(0.1,0.2)=0.3`, `fdiv(10,3)`, `add(1e20,1,-1e20)=1`,
`mul(123456,789012)`); smoke **1264/1264**, 0 crashes.

## Not a bottleneck — the SQLite code-cache layer (the original worry)
Already well-engineered, no work needed: writes are **deferred + batched**
(50-entry queue or 250ms timer via `cache_queue_code_cache_put` →
`cache_flush_writes`, never blocking eval), one `BEGIN/COMMIT` per flush
(`attrcache.cpp`), `journal_mode=WAL`, `synchronous=NORMAL`,
`wal_autocheckpoint=0`, prepared statements reused (`m_stmtCodeCacheGet/Put`),
blobs small (~2–11 KB, not the legacy 4 MB), staleness checked in-memory
(`deps_are_fresh`, no DB query).

## ✅ FIXED — the per-call setup floor (run_cached_program)
The ~0.4µs floor for any JIT-executed program turned out to be dominated by the
per-call **CARGS/SUBST population**, not `materialize_program`. Compiling the
population out dropped `r(0)` cached from 0.40 → 0.15µs (62% of the floor).
Cause: `run_cached_program` populated all ~45 SUBST slots + CARGS every call
(`mux_sprintf`×4, `Name`/`Location`/`Moniker` lookups, the 36-entry %q loop) even
for programs referencing none of them (`r(0)` is an ECALL — functions get cargs
via the host pointer array and read `mudstate.global_regs` directly, not the
guest slots).

Fix (4abad84db): `emit_sref` (the single choke point for runtime subst/carg
refs) records each referenced guest address; `compile_expression` classifies
them into a per-program `subst_mask` + `cargs_used`; `run_cached_program`
populates only those. SQLite-reconstructed programs default to populate-all
(conservative). Also skips the `materialize_program` blob memcpy on a
consecutive same-program re-run (correct — code/str are read-only, fargs are
re-patched each run — but impact below noise: blobs are sub-KB).
Result: `r(0)` 0.40 → **0.17µs** (−58%); `add(mul(3,3),div(100,r(0)))` 0.72 →
**0.51µs** (−29%). Parity verified vs the AST oracle; smoke 1264/1264.

**Note on `materialize_program`:** the original agent report flagged it as the
redundant-work hot spot, but a clean same-session A/B showed *no* measurable
gain from skipping it — blobs are too small for the memcpy to matter. The real
floor money was in SUBST/CARGS population. (Lesson: verify the lead, don't trust
the agent's ranking.)

## ✅ FIXED — list-walking (`co_find_delim` ASCII fast path, b16367b5f)
`co_find_delim` is the shared delimiter scanner under `first`/`rest`/`extract`/
`words`/`iter` and the per-element list walk. It ran a color/UTF-8-aware Ragel
DFA one byte per state transition. **Internal color is PUA UTF-8** (BMP
U+F500-F7FF → `0xEF ...`, SMP U+F0000-F05FF → `0xF3 ...`; rendered to ANSI only
at the network layer), so every color byte and every multi-byte UTF-8 byte is
≥ 0x80. An ASCII delimiter (0x01..0x7F) can never appear inside a color token or
a multi-byte char → the first matching byte is always a standalone visible
delimiter → `memchr` is exact. Added a `memchr` fast path for ASCII targets
(`lib/color_ops.rl`, regenerated `color_ops.c`); target 0 or ≥ 0x80 still uses
the DFA. Measured (rvbench A/B): `iter(lnum(100),##)` 139 → **106µs** (−24%);
`iter(lnum(100),mul(%i0,%i0))` 157 → **133µs** (−15%). Parity vs AST oracle incl.
color-bearing lists; smoke 1264/1264. Note: `words`/`extract` of *constant*
lists const-fold (no runtime walk), so the win shows on runtime lists (iter).

## Remaining opportunities (not yet done)
- **`tier2_reset_writable` per-call BSS memset** — the other floor component;
  could be gated on whether the program calls a tier-2 function that uses BSS.
- **Bracketed `[...]` softcode bypasses the JIT entirely** (the deliberate
  `AST_EVALBRACKET` guard). Large real-world surface; matching the subtle
  re-eval semantics is a substantial project, not a quick win.
- **Persist `subst_mask`/`cargs_used` in the SQLite cache** (schema bump) so
  post-restart programs loaded from disk also get the floor savings.
