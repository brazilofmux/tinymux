# Lua JIT bring-up plan (#1278 / #1309)

**Status:** historical (bring-up complete; Phase 4 default-on landed as #1745 / #1325)  
**Living product plan:** [`plan-lua-jit-product.md`](plan-lua-jit-product.md)  
**Owner (era):** Grok (`wip-grok`) on #1309  
**PR (correctness wave):** https://github.com/brazilofmux/tinymux/pull/1321  
**Related:** #1278 (loader), #1316/#1317 (Hatsuhara: `jitstats` Lua counters), #1315 (Win64 tier-2 buffer)

This document records the early bring-up sequence (safety gate off → loader
→ correctness → suite → default-on). Do not treat open phase rows below as
current work unless they still appear in the product plan’s Phase D polish
list. Product defaults and nesting architecture live in the product plan.

## Story (corrected)

1. Lua **interpreter** works; smoke was green only on that path.
2. `lua_bc_load` / `read_int()` treated Lua 5.4 varints as raw 4-byte ints since
   the pipeline landed (2026-03-18). Every compile failed → **`hir_lower_lua`
   never ran**.
3. Fixing the loader revealed empty folds, wrong results, and a softcode-
   reachable hang in `dbt_run` (#1309). Not a regression of the loader — the
   code had never been exercised.

## Phases

| Phase | Goal | Status |
|-------|------|--------|
| **0** Hygiene | Claim #1309, worktree, plan | done |
| **1** Safety gate | `mudconf.lua_jit` default **off** | done (#1310) |
| **2** Loader | `read_int` varint | done (#1310) |
| **3a** Correctness | Trailing RETURN, MMBIN, code→memory, sref ATOI, nest | done (PR #1321) |
| **3b/c** Suite | Full smoke with `lua_jit 1` | **done — 1428/1428** |
| **3d** Opcodes | Deeper HIR audit / HIR_NEG discrimination | absorbed into later correctness; residual optional |
| **3e** Limits | Win64 tier-2 buffer (#1315 Hatsuhara) | re-measured; not a default-on blocker |
| **4** Default-on + engage assert | Flip default; require `lua_run_ok` (#1317) | **done — #1745 / #1325** (after #1326 nest) |

## Phase 3a root causes (fixed)

| Symptom | Cause |
|---------|--------|
| Empty `return "hello"` | Trailing Lua RETURN overwrote real result with `""` |
| Empty `return 42` | Fold ICONST→digit SCONST at return |
| Empty folds | No `hir_build_cfg` → `block_last=-1` → codegen skipped all insns |
| False multi_block | RETURN marked next PC as leader |
| Skipped RETURN after ADD | Extra `pc++` past MMBIN |
| `mux.args` chaos / hang | Map to CARGS; ATOI runtime_ref; **copy `rc.code` into guest memory** |
| Softcode JIT → fun_lua hang | Refuse nested `run_cached_program` (later lifted safely in #1326) |
| Spin at guest PC 0 | Zero code region (missing code copy) → illegal op → exit_with_pc(same) |

## Smoke verification (Unix/macOS, this tree)

```bash
# Production defaults include lua_jit on (--enable-jit builds, after #1745)
make test   # or: cd testcases && ./tools/Smoke

# Explicit Lua JIT emphasis
make test-lua-jit
# equivalent: SMOKE_EXTRA_CONF='lua_jit 1' ./tools/Smoke
```

Results (2026-07-26, pre-default-on era):

| Config | Result |
|--------|--------|
| `lua_jit 0` (then default) | 1428/1428 |
| `lua_jit 1` | 1428/1428 |

Post-#1745, the production default is `lua_jit 1` on JIT builds; re-run
`make test` rather than relying on the table above for current counts.

## Coverage pin (before default-on)

Interpreter-only green is not enough. Need:

1. **Engage assert** — after `lua()`, require `lua_run_ok > 0` via `jitstats()`  
   (Hatsuhara #1317 / #1316 — do not reimplement; merge then wire smoke TC).
2. Optional control: delete `HIR_NEG` codegen → TC059/TC060 fail under `lua_jit 1`.

These pins were prerequisites for Phase 4 and are historical checklist items.

## Claim map (era)

| Ticket / work | Label |
|---------------|--------|
| #1309 bring-up epic | `wip-grok` |
| #1278 loader | `wip-kagura` (landed via #1310) |
| #1316/#1317 jitstats Lua counters | `wip-hatsuhara` |
| #1315 Win64 tier-2 buffer | `wip-hatsuhara` |

## Next (historical checklist — completed via product plan)

1. Land #1321; rebase if needed after #1317.  
2. Smoke TC: `lua_jit 1` + `jitstats()` require `lua_run_ok>0` (needs #1317).  
3. Nest under softcode JIT (#1326) before treating “both on” as product.  
4. Phase 4: `mudconf.lua_jit` default `true` — **done #1745**.  

For current residual polish, see Phase D in
[`plan-lua-jit-product.md`](plan-lua-jit-product.md).
