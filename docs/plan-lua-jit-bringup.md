# Lua JIT bring-up plan (#1278 / #1309)

**Owner:** Grok (`wip-grok`) on #1309  
**Related:** #1278 (loader diagnosis, `wip-kagura`), draft PR #1306 (loader one-liner)

## Story (corrected)

1. Lua **interpreter** works; smoke is green on that path only.
2. `lua_bc_load` / `read_int()` treated Lua 5.4 varints as raw 4-byte ints since
   the pipeline landed (2026-03-18). Every compile failed → **`hir_lower_lua`
   never ran**.
3. Fixing the loader reveals wrong results, empty string returns, and a softcode-
   reachable **infinite loop** in `dbt_run` (#1309). That is not a regression of
   the loader fix — the code was never exercised.

## Phases

| Phase | Goal | Status |
|-------|------|--------|
| **0** Hygiene | Claim #1309, worktree, link issues | done |
| **1** Safety gate | `mudconf.lua_jit` default **off**; loader fix may land green | in progress |
| **2** Loader | `read_int` varint (Kagura #1306 / cherry-pick) | cherry-picked on branch |
| **3a** Hang / empty folds | Trailing RETURN, MMBIN pc++, mux.args, nest | mostly fixed; residual mux.args DBT loop |
| **3b** Strings | Empty string returns | after 3a |
| **3c** Suite | 23× `#-1 NO ATTRIBUTE SPECIFIED` under full `lua_fn.mux` | after 3a |
| **3d** Opcodes | HIR/DBT audit; HIR_NEG discrimination | parallel once green |
| **3e** Limits | Instruction budget, cancel if hang class remains | with 3a |
| **4** Default-on | Flip `lua_jit` default; force-JIT smoke / engage assert | last |

## Phase 1 details (this PR)

- Conf: `lua_jit` (cf_bool, GOD/WIZARD), default `false`.
- `CLuaMod::TryJIT` returns false when off → interpreter only.
- `@list lua` reports JIT enabled/disabled.
- Branch includes the loader cherry-pick so enabling `lua_jit 1` is one conf flip
  for developers; production stays safe.

## Phase 3a investigation notes

Repro (with `lua_jit 1`):

```
&A2 me=return "hello"
&A1 me=return mux.args[1]+mux.args[2]
think R1=[lua(me/A2)]      → empty (wrong)
think R2=[lua(me/A1,2,3)]  → hang 100% CPU in dbt_run
```

Hypotheses to test (ordered):

1. **Nested DBT** — softcode JIT ECALL → `fun_lua` → Lua `run_cached_program`
   re-enters `s_persistent_dbt` while outer softcode holds translations.
2. **Shared runtime buffer / materialize** — string-returning chunk leaves
   `s_runtime_buffer_program_id` or pool state that poisons the next program.
3. **HIR RETURN / string path** — `OP_LUA_RETURN1` with `TY_STRING` may leave
   `out_addr` / `folded_result` wrong (`needs_jit = ecalls||native_ops` may
   fold pure string chunks; empty harvest would match 12× empty failures).
4. **State stuck in compiled key** — `jit_eligible` sticky after first compile;
   broken code keeps re-running.

Success for 3a: ordered A2→A1 returns; no core pin; softcode wall-clock still
not the only defense (prefer structural fix over relying on alarm).

## Coverage pin (required before default-on)

Something must fail if the JIT path is **not** taken when `lua_jit 1`:

- Control from #1278: delete `HIR_NEG` codegen → TC059/TC060 must **fail**.
- Or `@list lua` / counter of `compile_ok` / `run_ok` asserted in a smoke case.

Interpreter-only green is not coverage of `hir_lower_lua`.

## Claim map

| Ticket / work | Label |
|---------------|--------|
| #1278 loader diagnosis + PR #1306 | `wip-kagura` (do not steal) |
| #1309 bring-up (gate, hang, correctness) | `wip-grok` |
| Windows verify after Unix green | `wip-hatsuhara` (later) |

## Do not

- Merge loader alone without gate or full #1309 fixes.
- Claim #1278 / push to Kagura’s draft branch.
- Enable `lua_jit` by default until `lua_fn.mux` is green with force-on.
