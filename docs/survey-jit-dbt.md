# Survey: JIT / DBT subsystem (softcode → native codegen)

Audit of the TinyMUX JIT compiler and RV64 dynamic binary translator.
Companion to `docs/survey-ganl-networking.md`. Threat model: **player-controlled
softcode reaches native codegen** (AST → HIR → SSA → optimize → RV64 → host DBT)
and the precompiled `softlib.rv64` Tier-2 blob run via the DBT.

## Architecture (from design docs)
- Pipeline: Softcode/Lua → AST → HIR → SSA → optimize → RV64 codegen → host DBT
  (RV64→x64/a64). RV64 is the stable guest ISA; the DBT translates it to the host.
- Backends: `dbt_x64_sysv.cpp` (**Production**, selected via `@DBT_BACKEND@` on
  x86_64), `dbt_x64_win64.cpp` (planned), `dbt_a64_sysv.cpp` (planned/dormant —
  NOT compiled on x86 hosts). Shared: `dbt.cpp`, `dbt_internal.h`, `dbt.h`.
- Tier-2 blob `softlib.rv64`: hand-written RV64 (`mux/rv64/src/softlib.c`) +
  cross-compiled Ragel color_ops; `co_*` intrinsics bypass DBT to host-native.
- Guest memory: a 4MB `guest_memory_t` region; `R12`/`R13` base pointers during
  block execution.

## Status: survey in progress (started this session)

## Findings inventory

### ✅ JIT/interpreter parity — verified solid (no divergence found)
Swept ~70 functions (string/list/float/math) with edge-case inputs comparing
JIT vs the AST interpreter; **zero real divergences**. Consistent with the 1115
passing smoke tests (themselves a parity gate vs recorded output).

**Oracle methodology (important — `asteval()` is NOT a clean oracle):**
`fun_asteval` (`ast.cpp:2702`) re-parses and re-evaluates its argument — but
the argument was *already* evaluated (via the JIT) before asteval received it.
So `[asteval(X)]` = `AST_eval(JIT_eval(X))` — a **double evaluation**. For X
whose output contains eval-significant bytes (backslashes from `escape`, runs of
spaces from `space`/`delete`), the second eval mutates the result, producing
*false* JIT-vs-AST "mismatches" (e.g. `escape(abc)` → `\abc` then re-eval → `abc`;
`space(3)` → 3 spaces then re-eval → 1). Numbers are idempotent under re-eval,
so an asteval comparison is only valid for numeric output.

**Clean oracle (no rebuild needed):** store the expression *literally* in an
attribute (`&C me=<expr>` does not evaluate), then compare
`[<expr>]` (single JIT eval) against `[asteval(get(me/C))]` (`get` returns the
literal code, `asteval` evaluates it once via AST). Both evaluate the same
source exactly once, on different engines. Under this oracle, every former
"mismatch" matched. A `co_delete_cluster` source read (`color_ops.rl:3805`)
independently confirmed `delete()` is a clean copy-skip-copy identical to the
JIT blob `rv64_delete` — the earlier `a c d` vs `a  c d` "bug" was an asteval
artifact.

### ✅ Verified — production x64 path is well-hardened (NOT bugs)
- Instruction fetch in the production translator is bounded against
  `memory_size` at every site (`dbt_x64_sysv.cpp:1279,1316,1394,1414,1478,2010`).
  No OOB read in block translation.
- Tier-2 DELIM_STRING guards: folded delim functions
  (FIRST/REST/LAST/MEMBER/EXTRACT/WORDS/SQUISH/TRIM/DELETE) all carry a fold
  guard (delim-width or `nargs`) AND a runtime guard (`hir_lower.cpp:3277-3376`).
  The other delim functions on the tier-2 allowlist are not folded at all, so
  they need no fold guard — the runtime single-byte-delim guard covers them.
- `while(*p)` aggregators (LADD/LMAX/LMIN/LAND/LOR, `softlib.c`) are correct for
  scalar numeric semantics (empty/trailing words are no-ops).
- **Eval/JIT DoS robustness (live-tested, all safe):** deep nesting is bounded
  by `func_nest_lim`=500 (`conf.cpp:272`) + the LBUF input cap — `add(1,add(1,
  …×2000…))` evaluates (capped) without stack overflow. Wide expressions are
  bounded by the ~100 function-arg cap (`add(1,…×12000)` silently truncates to
  ~100 args, returns 99/100, no crash). HIR overflow is bounded: `hir.emit()`
  checks `n_insns >= HIR_MAX_INSNS (4096)` and returns -1
  (`hir.h:309`); a nested-wide `add(add(1,…×99),…×99)` that would build ~9801
  HIR nodes returns the correct `9801` (graceful fallback), no overflow/crash.
- Player-controlled sizes in the blob are bounded: `LBUF_SIZE` is **32768**,
  consistent across `alloc.h:18` and `mux/rv64/Makefile:12` (the cross-file
  agreement prior work flagged). `rv64_space` clamps `n` to `[0,8000]` →
  ≤8001 bytes into a 32768 LBUF (`softlib.c:1108`); the justify-width helper
  range-checks `w >= LBUF_SIZE` (`:885`); `co_edit` clamps to `LBUF_SIZE-1`
  (`:967-998`); list/output walkers use `end = op + LBUF_SIZE - 1` guards
  (`:1180,1347,1398,1630,1827`). No overflow found on the size-driven paths.

### ✅ FIXED (f2cdde9c8) — was a LIVE player-reachable DoS (SIGFPE crash) — #805
Guarded `INT64_MIN/-1` in `i64Division`/`i64FloorDivision` (timeutil.cpp) + all
four inline header variants (`i64Division`/`i64Remainder`/`i64Mod`/
`i64FloorDivision`) + the two `hir_opt` HIR_DIV/HIR_REM folds. `i64Mod` and the
out-of-line `i64Remainder` were already guarded — incomplete hardening. Verified
live: the exact repro now returns `-9223372036854775808`; mod/remainder return
0; runtime paths (interpreter, attr reads, JIT `u()` stack-arg divisors) handle
div-by-zero and INT_MIN/-1 without crashing. Smoke 1115/1115.
**DBT emit gap (FILED as #811 — earlier "not reachable" call was wrong):** the
DBT DIV/REM emit (`dbt_x64_sysv.cpp:2292,2305,2453,2475`; same in
`dbt_x64_win64.cpp:2304,2317,2465,2487`) is a raw x86 `idiv` with no guard, so
it traps (`#DE`→SIGFPE) on a zero divisor AND on `INT64_MIN/-1`. The earlier
survey entry claimed this was unreachable because `iter(2 0 4,idiv(100,%i0))`
returned `50 #-1 DIVIDE BY ZERO 25` — but that result actually *disproves* the
claim: the compiled RV64 `DIV` (and the a64/interp backends) produce RISC-V's
`-1` on div-by-zero, not the MUX `#-1 DIVIDE BY ZERO` string. Getting the MUX
string means that case was **constant-folded** (`hir_lower.cpp:2783` emits the
error sconst only for a *compile-time-constant* zero divisor) or interpreted —
so the test only ever exercised the constant path and never reached the bare
`idiv` with a runtime divisor. A genuine runtime divisor — `idiv(5,v(attr))`,
`idiv(5,sub(x,x))` — lowers to a bare `rv_DIV` (`hir_codegen.cpp:1395`, no
guard) → the unguarded x86 `idiv`. The interpreter (`dbt_interp.cpp:387`) and
a64 (`dbt_a64_sysv.cpp:1588`, CBZ + non-trapping SDIV) backends both handle it
correctly; only the x86 backends do not — a real DBT parity bug and a
player-reachable crash on x86-64 (the production platform). #805 closed the
softcode-interpreter and constant-fold paths; JIT compilation routes around
both. NOT caught earlier because #805 was verified on this AArch64 host, where
the a64 guards mask it. Reachability of a runtime zero/`INT_MIN` divisor (vs. an
`eval_bailout` to the interpreter) is the one piece left to confirm on an x86-64
build; the static gap and parity divergence are certain. See #811 for the fix
plan (mirror the a64 guards in both x86 backends across all 8 div/rem forms).

### (historical) original report — LIVE player-reachable DoS (SIGFPE server crash)
- **`idiv(-9223372036854775808,-1)` crashed the whole server** (and
  `remainder(-9223372036854775808,-1)`). `INT64_MIN / -1` overflows; on x86 the
  `idiv` instruction traps (`#DE` → SIGFPE). **Reproduced live**: a single
  `think [idiv(-9223372036854775808,-1)]` from a logged-in Wizard on a
  verified-alive netmux killed the process (empty response, process gone, log
  truncated mid-eval, no clean shutdown). Any connected player can do this.
- **Root cause (base MUX helper, NOT JIT-specific):** `i64Division`
  (`mux/lib/timeutil.cpp:388` out-of-line AND `mux/include/timeutil.h:291`
  inline) and `i64Remainder` (`timeutil.h:292` inline) perform raw `x/y` / `x%y`
  with **no `INT64_MIN/-1` guard**. `i64Mod` (`timeutil.cpp`) DOES guard it
  (`if (INT64_MIN==x && -1==y) return 0;`) — classic incomplete hardening:
  mod was fixed, division/remainder were missed.
- **Reachable from multiple layers:** the interpreter `fun_idiv`
  (`funmath.cpp:1075` → `i64Division`) and `remainder` (`:1145` →
  `i64Remainder`); the JIT `try_fold` IDIV (`hir_lower.cpp:206` → `i64Division`);
  and the JIT `hir_opt` HIR_DIV/HIR_REM folds (`hir_opt.cpp:207-216,197-206`)
  which use raw C++ `/` and `%` with only a `!=0` guard, never an INT_MIN/-1
  guard. (My live repro hit the JIT try_fold path at compile time; the
  interpreter path crashes identically at run time.)
- **Fix direction:** add the INT_MIN/-1 guard to `i64Division` and
  `i64Remainder` (mirror `i64Mod`: return `INT64_MIN` for the division — the
  two's-complement-wrap value — and `0` for the remainder), and add the same
  guard to the two hir_opt folds (or route them through the fixed helpers).
  Verify the runtime DBT DIV/REM emit (`dbt_x64_sysv.cpp:2292,2305`) matches
  RISC-V's no-trap semantics so the non-constant path
  (`idiv(%q-holding-INT_MIN,-1)`) is also safe. **[Update: this is the
  still-open hole — filed as #811. #805 fixed only the interpreter + constant
  folds; the x86 DBT emit remains unguarded for runtime divisors.]**

### 🐛 Verified bug — latent, non-production backend
- **a64 LOAD/STORE with `rs1==x0` and nonzero `imm` computes `2*imm`**
  (`dbt_a64_sysv.cpp:1420-1423` LOAD, `1449-1452` STORE). `rc_read(x0)` returns
  `A64_X0` (`:200-202`); the address calc `emit_mov_r64_imm64(X0,imm);
  emit_add_r64(X0, rs1=X0, X0)` self-aliases → `X0+X0`. The STORE comment
  (`:1445`) handles the same X0 hazard for `rs2` but misses `rs1`. x64 backends
  are safe (separate RCX temp). **Dormant backend** — not compiled on x86, so
  latent and not compile/live-testable here. Fix is by-construction.

### 📝 Defense-in-depth (not a confirmed bug; consistent across backends)
- Guest data LOAD/STORE emit unmasked `[base+addr]` in all three backends
  (`dbt_x64_sysv.cpp:~2160`, win64 `~2172`, a64 `~1429`). No bounds mask against
  the 4MB guest region. Relies on JIT codegen correctness (the guest addresses
  are compiler-emitted, not arbitrary player bytes). Masking `addr &=
  (memory_size-1)` would contain any codegen bug to a wrong-value instead of an
  OOB host access — cheap hardening, but a design change with perf implications.

### ✅ Verified-safe — additional production x64 paths (this pass)
- **Code-buffer overflow is guarded.** Emit primitives bounds-check every write
  (`dbt_emit_x64.h:33-44`: `emit_byte`/`emit_bytes` only write when
  `offset (+len) <= capacity`, but `offset` always advances). `translate_block`
  bails `if (e.offset > e.capacity) return nullptr;` **before** `code_used +=
  e.offset` (`dbt_x64_sysv.cpp:2900-2902`, 2921), so an over-large block never
  pushes `code_used` past `CODE_BUF_SIZE` — the next block's
  `capacity = CODE_BUF_SIZE - code_used` (uint32_t) can't underflow into a huge
  value and write past the 1 MB RWX buffer. No OOB.
- **SQLite code cache is keyed + staleness-checked.** `compile_cached`
  (`jit_compiler.cpp:1730`) looks up by `compile_cache_key(expr,nLen,eval)` AND
  `s_blob_version` (`:1717-1719,1765-1767`), and re-checks inlined-dep freshness
  (`deps_are_fresh`) before reuse — a cached program is only run for the exact
  expression + blob version that produced it.

## Preliminary conclusion
The JIT/DBT *memory-safety* surfaces (instruction-fetch bounds, tier-2 DELIM
guards, size-driven blob output bounding, code-buffer overflow, emit-primitive
bounds, code-cache keying) are **well-hardened** — the parallel-agent leads
there were mostly false positives ruled out by reading. **But auditing the
hir_opt arithmetic folding surfaced a live, player-reachable DoS (#805):**
`idiv(-9223372036854775808,-1)` (and `remainder(...)`) crash the server with
SIGFPE via the unguarded `i64Division`/`i64Remainder` (a base MUX helper bug
reachable from both the interpreter and the JIT fold). **Reproduced live.**
Net verified yield: #805 (high-severity DoS), #804 (latent a64 miscompile),
plus a defense-in-depth note (unmasked guest LOAD/STORE).

## Areas still to audit (next passes)
- [ ] SSA construction + linear-scan register allocation correctness (the
      warm-loop register-pressure class — `dbt_internal.h` history shows it was
      a real divergence bug; re-verify the SSA/spill logic, not just the guard).
- [x] Emitter immediate encoding — AUDITED (#830, 0c8d6507f). rel32/disp32
      truncation is a NON-issue: CODE_BUF_SIZE (1 MB) and MEM_SIZE (~5.5 MB) keep
      every code-relative rel32 and guest-memory disp32 far inside range. But the
      OVERFLOW path was unsafe: emit_byte/emit_bytes skip writes past capacity yet
      emit_pos keeps advancing, and emit_patch_rel32 then did an UNGUARDED memcpy
      → rel32 patches written out of bounds past the 1 MB code buffer once
      code_used neared full (75% reached in a short smoke run; the bail is
      post-hoc). Fixed: bounds-guard the patch write (also a64 emit_patch_b26/
      b19). Follow-up (perf, not safety): code_used grows ~monotonically and only
      dbt_reset reclaims it, so the JIT degrades to interpreter once the buffer
      fills — a reset-when-near-full (exec_depth==0) would keep it working.
- [x] HIR optimizer (`hir_opt.cpp`) — AUDITED. Found #828 (faa795243): mux
      `mod()` is floor-mod (i64Mod) but the JIT mapped it to truncate-`%` on its
      runtime paths (blob `rv64_mod` + native MOD→HIR_REM); `mod(-7,3)`→-1 vs 2.
      Fixed (floor-mod blob + drop native lowering). Everything else sound: int
      const-folds mirror native int64 incl. INT64_MIN/-1 guards; float folds are
      plain IEEE matching native/libm; peephole strength-reduction is
      algebraically correct and correctly does NOT invert float comparisons
      (NaN); GVN/CSE only numbers pure deterministic ops, normalizes only truly
      commutative ops (FADD/FMUL incl.), excludes FCALL1/FSQRT (func_idx not in
      key), and the dominator-scope restore is correct.
- [x] Follow-up from the hir_opt pass: float `add()`/`sub()` AddDoubles parity —
      VERIFIED REAL and broader than flagged (#829, 29abb9a9a). Both the tier2
      blob (rv64_add/rv64_sub) and the native float FADD/FSUB chain did a raw
      sequential sum, so runtime float add/sub diverged not just on cancellation
      (add(1e20,1,-1e20)=0 vs 1) but on ORDINARY decimals (add(0.1,0.2)=
      0.30000000000000004 vs 0.3 — no NearestPretty). Fixed: blob calls the
      rv64_add_doubles host intrinsic (stack vals[], not the strtod-clobbered
      DSCRATCH); native float ADD/SUB lowering removed.
- [ ] `hir_lower_lua.cpp` — the Lua lowering path (separate from softcode).
- [ ] `reconstruct_from_cache` — does it validate record sizes/offsets before
      use (corrupted/truncated SQLite blob robustness)?
