# Lua JIT: product enablement plan

**Status:** living plan (2026-07-27)  
**Parent epic:** #1309  
**Critical path:** #1326 (nested execution under softcode JIT)

This plan is the source of truth for multi-agent work on making Lua JIT
real for games, not only for laboratory configs. Older bring-up notes live
in [`plan-lua-jit-bringup.md`](plan-lua-jit-bringup.md) and
[`design-lua-jit.md`](design-lua-jit.md); they describe how we got here.
This document describes how we ship.

---

## 1. The product requirement

A game owner should be able to:

```text
./configure --enable-jit [--enable-nls] …
make install
# conf: defaults
./bin/netmux
```

…and get:

| Surface | Default behaviour |
|---------|-------------------|
| Softcode `[…]` / attributes | Compiled when eligible (`jit_eval_brackets` **on**) |
| `lua()` / Lua attrs | Compiled when eligible (`lua_jit` **on**, once ready) |
| Nested softcode → `lua()` | Lua JIT still **runs**, not silent interpreter fallback |
| Wrong answers | Fail closed (decline → interpreter) or match the interpreter |
| NLS | Orthogonal; optional |

**Non-goal:** forcing games to set `jit_eval_brackets 0` so Lua can run.
That config is a **test laboratory**, not a product mode.

---

## 2. What is broken today (mechanism)

### 2.1 Two knobs, one trap

| Conf | Default | Role |
|------|---------|------|
| `jit_eval_brackets` | **true** | Softcode may JIT-compile `[…]` eval brackets |
| `lua_jit` | **false** | Lua may attempt bytecode → HIR → DBT |

Turning `lua_jit` on under default brackets does **not** make Lua JIT
execute. Almost every real `lua()` call is reached from softcode that is
itself inside `run_cached_program` (compiled attribute, compiled bracket,
or bare `lua(...)` compiled as `AST_FUNCCALL`). Nested entry hits:

```text
run_cached_program: if (s_run_cached_depth > 0) return false;  // #1326
```

Lua then falls back to the **interpreter**. Answers stay correct;
`lua_compile_ok` / `lua_cache_hits` still move; only `lua_run_ok` tells the
truth.

Measured (Win64 and Linux):

| Config | `lua()` under softcode JIT | `lua_run_ok` |
|--------|----------------------------|--------------|
| defaults + `lua_jit 1` | declines nested run | 0 |
| `jit_eval_brackets 0` + `lua_jit 1` + call shape that avoids outer DBT | executes | >0 |

So today you can have:

- full softcode JIT **or**
- observable Lua JIT (by disabling softcode brackets),

but **not both** in a production-shaped process. That is the product bug,
not a test inconvenience.

### 2.2 Why tests lied for a while (#1426)

Result-equality tests pass when the JIT declines: the interpreter is the
oracle and the fallback. Harnesses that only checked compile/cache counters
also stayed green. The correct contract is:

```text
result == interpreter  AND  lua_run_ok advanced
```

That is what `tests/luajit/run.sh` **EXEC** cases do (and they pin
`jit_eval_brackets 0` so the nested guard is not the whole story). Smoke
with only `lua_jit 1` still cannot prove execution under default brackets
until #1326 lands.

### 2.3 What the reentrancy guard is protecting

Not a soft preference. Nested `run_cached_program` would otherwise:

1. **`dbt_reset`** the shared persistent DBT → outer softcode translations gone  
2. **`materialize_program`** into `s_runtime_buffer` → overwrite guest memory the outer program is executing from  

Correct answers on decline are deliberate. The hang risk is real: branch
`feat/1326-nested-lua-vm` lifts the guard with per-depth contexts and can
execute nested Lua, but still idle-hangs under `make test-lua-jit` /
`lua_fn.mux` (see #1326 comments, #1571).

---

## 3. Inventory (2026-07-27)

### Done (do not re-litigate)

| Item | Notes |
|------|--------|
| Loader varint, safety gate `lua_jit` default off | #1310 |
| Bring-up correctness wave | #1321 / #1309 path |
| Fail-closed unimplemented Lua bridges | #1518 |
| EXEC harness + corpus | #1558 / #1426 |
| Float subtype + `HIR_LUA_FTOA` | #1488 / #1562 |
| Tier2 load on Lua compile path | #1561 / #1562 |
| Runtime `^` → FCALL2 `pow` (not string bridge) | #1538 / #1548 era |
| String→number coercion half of #1425 | fixed on master |
| Softcode IEEE/`Ind` vs Lua `pow` docs | #1556 / #1560 |

Close or comment **done** on open issues that are stale after the above:
#1561, #1538, #1425 (both rows), partially #1426 (harness exists; product
path still blocked).

### Open — ordered by critical path

| # | Title | Role in this plan |
|---|--------|-------------------|
| **#1326** | Nested Lua under softcode JIT | **Product gate.** Separate runtime context (memory + DBT) per depth; fix hang |
| **#1571** | Guest loop escapes max_dispatch / alarm | Found while chasing #1326 hang; may be the hang or independent |
| **#1519** | Named Lua-VM bridge ECALLs never executed | Tables/stdlib; compile declines today (safe) |
| **#1315** | Win64 tier-2 buffer pressure | Re-measured: 4 MB buffer, less urgent; still watch Win |
| **#1325** | Default `lua_jit` on | **After** #1326 + engage coverage under default conf |
| **#1309** | Epic | Keep as umbrella; this plan is the product half |
| **#1433** | Intermittent `#t` SIGSEGV (interpreter) | Orthogonal but blocks confidence |

### WIP branch

`feat/1326-nested-lua-vm` — per-depth `jit_runtime` / dual context. Nested
Lua **does** run under default brackets in hand tests; `make test-lua-jit`
hangs. Do not merge until hang is diagnosed. Prefer finishing that branch
over a third design.

---

## 4. Target architecture

```text
                    softcode JIT (depth 0)
                    s_vm[0]: buffer + DBT
                           |
              ECALL fun_lua / TryJIT
                           |
                    Lua JIT (depth 1)
                    s_vm[1]: buffer + DBT   ← independent
                           |
              ECALL back to softcode?
                           |
                    softcode at depth≥1 declines DBT
                    (fold-only / AST) — nesting stays bounded
```

Design already agreed on #1326:

1. Bundle `s_runtime_buffer`, program ids, persistent DBT, ready flags into
   a **per-depth** context (not “Lua owns a second global forever” unless
   depth maps that way).
2. Depth ≥ 2 still refuse (or hard-cap); do not invent unbounded reentrancy.
3. Allocate the second context **lazily** (installations that never call
   `lua()` pay nothing).
4. ECALL handlers already use `ec->memory` — keep it that way; do not re-bind
   them to file-statics.
5. Softcode nested under Lua stays on the outer/main instance rules (decline
   to AST when depth conflict) so we do not need full mutual recursion v1.

Out of scope for v1: true single-DBT reentrancy, superblock sharing across
depths, per-player Lua isolation.

---

## 5. Phased roadmap

### Phase A — Unblock production-shaped execution (#1326)

**Goal:** `lua_jit 1` + default `jit_eval_brackets 1` → `lua_run_ok > 0` for
ordinary `lua()` / `[lua(...)]` / attr forms.

| Work | Notes |
|------|--------|
| Finish `feat/1326-nested-lua-vm` | Diagnose hang (ptrace/gdb on a real host, not only Crostini) |
| Probe #1571 | Confirm whether hang is in-block guest loop vs host deadlock |
| Regression | Outer softcode integrity across nested `lua()` (string pool, `%0` after call, q-regs, two nesteds, `iter`) — already sketched on the issue |
| Gate | `tests/luajit` EXEC with **default** brackets (new mode or drop forced `jit_eval_brackets 0` for a second suite) |
| Gate | `SMOKE_EXTRA_CONF='lua_jit 1' make test` / `make test-lua-jit` completes |

**Exit:** Nested Lua JIT executes; no hang; softcode still JIT by default.

### Phase B — Correctness under real execution

While the guard was up, wrong-answer bugs were invisible. After Phase A they
become production bugs. Keep fail-closed where possible.

| Work | Notes |
|------|--------|
| Audit remaining EXEC gaps | Grow `tests/luajit` EXEC under default conf |
| #1519 bridge | Case-normalize names; then implement or keep decline — tables still AGREE-by-decline |
| Float/table tostring | Residual `HIR_FTOA` on table marshalling if it surfaces |
| #1433 | Interpreter SEGV if still reproducible |

**Exit:** No known silent wrong answer on EXEC corpus; declines are intentional.

### Phase C — Default on (#1325)

| Prerequisite | Why |
|--------------|-----|
| Phase A green | Otherwise default-on is a no-op that burns compile cache |
| Engage assert in smoke under **default** conf | `lua_run_ok` after a known chunk |
| Win64 soak | #1315 sizing OK; still run `lua_jit 1` smoke on hatsuhara |
| Docs / CHANGES | One paragraph: both JITs on; nested uses second context |

**Exit:** `mudconf.lua_jit = true` by default on `--enable-jit` builds.

### Phase D — Product polish (ongoing)

- `jitstats` / `@list` clarity: compile vs run, nested declines  
- Optional conf later: never required for “both work”  
- Opcode coverage growth (closures still out — design-lua-jit)  
- NLS remains orthogonal  

---

## 6. Test strategy (games vs harness)

### Production defaults (what games run)

```text
jit_eval_brackets  1     # default — softcode JIT fully on
lua_jit            1     # after Phase C; 0 until then
```

No special conf. Nested softcode → Lua uses the second runtime context.

### Test laboratory (what AIs must use until Phase A lands)

```text
lua_jit 1
jit_eval_brackets 0      # only so Lua is not nested under softcode DBT
```

Used by `tests/luajit/run.sh` today. **Document as temporary.** After
Phase A, the harness should add (or switch to) a **default-brackets EXEC**
lane so we never again ship a “Lua JIT” that only runs when softcode is
handicapped.

### Two suites, clear names

| Target | Purpose |
|--------|---------|
| `make test` | English, production defaults, interpreter Lua if `lua_jit` still off |
| `make test-lua-jit` | `lua_jit 1`, production brackets once Phase A done; until then keep current extra conf but assert `lua_run_ok` |
| `tests/luajit/run.sh` | Differential EXEC/AGREE/SURVIVE; pin run_ok |

**Never** require games to set `jit_eval_brackets 0`.

### What not to assert in smoke

- Softcode `#-1` strings (ABI)  
- NLS-translated notify under `LANGUAGE=xx` as part of default `make test`  
- That every Lua chunk compiles (many correctly decline)  

### What to assert

- Known EXEC chunks: result + `lua_run_ok`  
- Nested call integrity (outer softcode result unchanged)  
- Process does not hang under `lua_fn` / `lua_jit_fn` with `lua_jit 1`  

---

## 7. Shepherding other agents

### Single rules of engagement

1. **Read this plan** before opening a Lua JIT PR.  
2. **Do not** “fix” Lua JIT by documenting `jit_eval_brackets 0` as the
   supported game mode.  
3. **Do not** merge #1326 while `make test-lua-jit` hangs.  
4. **Do not** flip #1325 default-on until Phase A exit criteria pass.  
5. Prefer **EXEC** (`lua_run_ok`) over compile counters for any claim that
   “the JIT works”.  
6. Fail closed: wrong native path → decline → interpreter, not a clever
   wrong answer.  
7. One concern per PR (runtime split vs bridge case vs opcode).  
8. If you find a hang with zero counter movement, file/link **#1571**.  

### Suggested claim map

| Agent workstream | Owns | Avoid |
|------------------|------|--------|
| **Nesting** | #1326 branch finish, hang bisect, dual-context polish | Default-on, bridge implement |
| **Hang / DBT safety** | #1571, dispatch/alarm in-block | Lua lowering refactors |
| **Corpus** | `tests/luajit` EXEC under default conf after A | Engine architecture |
| **Bridges** | #1519 | Reentrancy |
| **Default-on** | #1325 after gates | Landing mid-hang |
| **Windows** | Soak #1326/#1325 on Win64 | Ignoring dual-context cost |

### Issue hygiene (do now)

| Issue | Action |
|-------|--------|
| #1561, #1538, #1425 | Close as fixed (refs #1562 / earlier FCALL2) if still open |
| #1426 | Retitle or comment: harness fixed; **product path** still blocked on #1326 |
| #1326 | Raise priority (product gate); link this plan; point at WIP branch |
| #1325 | Explicit blocked-on #1326 |
| #1309 | Point epic at this plan for product phase |

### PR checklist (Lua JIT)

```text
[ ] States which phase (A/B/C/D) and issue
[ ] Under default jit_eval_brackets? If not, why temporary
[ ] lua_run_ok measured for any “executes” claim
[ ] make test / tests/luajit / make test-lua-jit as applicable
[ ] No self-merge without dual-review on engine runtime paths
```

---

## 8. Success metrics

| Metric | Today | After Phase A | After Phase C |
|--------|-------|---------------|---------------|
| Default conf + `lua_jit 1`: `lua_run_ok` for simple `lua()` | 0 | >0 | >0 |
| Softcode brackets JIT | on | on | on |
| Games need special conf for both | yes (broken) | no | no |
| EXEC corpus under default brackets | no | yes | yes |
| `lua_jit` default | off | off | **on** |

---

## 9. Explicit non-goals (near term)

- Compiling every Lua opcode (closures, full stdlib)  
- Softcode and Lua sharing one DBT reentrantly  
- Per-player JIT isolation  
- Making `jit_eval_brackets 0` a supported long-term product mode  
- Coupling NLS to JIT work  

---

## 10. Immediate next steps

1. Close stale fixed issues (#1561, #1538, #1425).  
2. Comment on #1326 + #1309 with a link to this plan; bump #1326 priority.  
3. One agent owns hang diagnosis on `feat/1326-nested-lua-vm` (gdb + #1571).  
4. After hang fixed: dual-review, merge #1326, add default-brackets EXEC lane.  
5. Only then #1325 default-on.

Until step 4, treat **every** “Lua JIT works” claim that only used
`jit_eval_brackets 0` as **lab-only**.
