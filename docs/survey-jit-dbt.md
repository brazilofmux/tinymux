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
- Player-controlled sizes in the blob are bounded: `LBUF_SIZE` is **32768**,
  consistent across `alloc.h:18` and `mux/rv64/Makefile:12` (the cross-file
  agreement prior work flagged). `rv64_space` clamps `n` to `[0,8000]` →
  ≤8001 bytes into a 32768 LBUF (`softlib.c:1108`); the justify-width helper
  range-checks `w >= LBUF_SIZE` (`:885`); `co_edit` clamps to `LBUF_SIZE-1`
  (`:967-998`); list/output walkers use `end = op + LBUF_SIZE - 1` guards
  (`:1180,1347,1398,1630,1827`). No overflow found on the size-driven paths.

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

## Preliminary conclusion (production x64 path)
The production x86-64 JIT/DBT path is **well-hardened** on every memory-safety
surface checked: instruction-fetch bounds, tier-2 DELIM guards, size-driven blob
output bounding, code-buffer overflow, emit-primitive bounds, and code-cache
keying. The parallel-agent leads were mostly false positives ruled out by
reading. Net yield so far: one latent **a64** miscompile (non-production
backend) + one defense-in-depth note (unmasked guest LOAD/STORE).

## Areas still to audit (next passes)
- [ ] SSA construction + linear-scan register allocation correctness (the
      warm-loop register-pressure class — `dbt_internal.h` history shows it was
      a real divergence bug; re-verify the SSA/spill logic, not just the guard).
- [ ] Emitter immediate encoding (x64 rel32/disp32 truncation on large
      offsets, side-exit `jcc_patch` rel32 range, block chaining backpatch).
- [ ] HIR optimizer (`hir_opt.cpp`) — constant folding / strength reduction
      correctness on player-controlled constants (div-by-zero, INT_MIN/-1).
- [ ] `hir_lower_lua.cpp` — the Lua lowering path (separate from softcode).
- [ ] `reconstruct_from_cache` — does it validate record sizes/offsets before
      use (corrupted/truncated SQLite blob robustness)?
