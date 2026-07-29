# Lua JIT: product enablement plan

**Status:** product path complete (2026-07-29)  
**Parent epic:** #1309 (closed; residual quality work is optional polish)  
**Default-on:** #1745 / #1325 (`mudconf.lua_jit = true` on `--enable-jit` builds)

This plan was the source of truth for multi-agent work on making Lua JIT
real for games, not only for laboratory configs. Older bring-up notes live
in [`plan-lua-jit-bringup.md`](plan-lua-jit-bringup.md) and
[`design-lua-jit.md`](design-lua-jit.md); they describe how we got here.

**What shipped.** Nested execution under softcode JIT (#1326), engage
coverage, and default-on (#1745). Games that `./configure --enable-jit` and
run with conf defaults get both softcode and Lua JIT without special knobs.
Remaining work is optional coverage and polish (see §5 Phase D), not a
product gate.

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
| `lua()` / Lua attrs | Compiled when eligible (`lua_jit` **on**) |
| Nested softcode → `lua()` | Lua JIT still **runs**, not silent interpreter fallback |
| Wrong answers | Fail closed (decline → interpreter) or match the interpreter |
| NLS | Orthogonal; optional |

**Non-goal:** forcing games to set `jit_eval_brackets 0` so Lua can run.
That config is a **test laboratory**, not a product mode.

---

## 2. Historical mechanism (resolved)

This section records *why* the product path was blocked and what fixed it.
It is not a current-state bug list.

### 2.1 Two knobs, one trap (was)

| Conf | Default (now) | Role |
|------|---------------|------|
| `jit_eval_brackets` | **true** | Softcode may JIT-compile `[…]` eval brackets |
| `lua_jit` | **true** (#1745) | Lua may attempt bytecode → HIR → DBT |

Before #1326, turning `lua_jit` on under default brackets did **not** make
Lua JIT execute. Almost every real `lua()` call was reached from softcode
inside `run_cached_program`. Nested entry hit:

```text
run_cached_program: if (s_run_cached_depth > 0) return false;  // pre-#1326
```

Lua fell back to the **interpreter**. Answers stayed correct;
`lua_compile_ok` / `lua_cache_hits` still moved; only `lua_run_ok` told the
truth.

Measured then (Win64 and Linux):

| Config | `lua()` under softcode JIT | `lua_run_ok` |
|--------|----------------------------|--------------|
| defaults + `lua_jit 1` | declined nested run | 0 |
| `jit_eval_brackets 0` + `lua_jit 1` + call shape that avoids outer DBT | executed | >0 |

**Resolved by #1326:** per-depth runtime context (memory + DBT) so nested
Lua can execute while softcode brackets stay on.

### 2.2 Why tests lied for a while (#1426)

Result-equality tests pass when the JIT declines: the interpreter is the
oracle and the fallback. Harnesses that only checked compile/cache counters
also stayed green. The correct contract is:

```text
result == interpreter  AND  lua_run_ok advanced
```

That is what `tests/luajit/run.sh` **EXEC** cases do. Smoke and default-conf
engage asserts now prove execution under production-shaped brackets.

### 2.3 What the reentrancy guard was protecting

Not a soft preference. Nested `run_cached_program` would otherwise:

1. **`dbt_reset`** the shared persistent DBT → outer softcode translations gone  
2. **`materialize_program`** into `s_runtime_buffer` → overwrite guest memory the outer program is executing from  

Correct answers on decline were deliberate. #1326 replaced the blunt guard
with bounded dual contexts rather than unbounded reentrancy.

---

## 3. Inventory (2026-07-29)

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
| Nested Lua under softcode JIT | #1326 |
| Engage / EXEC under default conf | #1324 era + suite |
| Default `lua_jit` on | #1745 / #1325 |

### Post-flip regressions (not optional; the flip's own test upgrade at work)

| Found | Fixed | Notes |
|-------|-------|-------|
| Baked instruction budget | same PR as this line | Lowering emitted `HIR_ICONST` of `mudconf.lua_instruction_limit`, so cached/persisted programs kept the compile-time limit and `@admin` changes changed nothing (#1613's shape). Caught by test-config's runtime-bounds case the moment default-on put the compiled path in its way — on `--enable-jit` trees only, which is why default-configure validation stayed green. Now a per-run `ECALL_LUA_INSN_BUDGET` read; programs carry no config values. |

### Residual (optional polish, not product gates)

| Area | Notes |
|------|--------|
| Opcode / surface coverage | Closures still out (see design-lua-jit); optional TFOR / dynamic `for`; grow EXEC corpus |
| Named Lua-VM bridges | #1519 closed as tech-debt decision; keep decline until intentionally implemented |
| Win64 soak | #1315 sizing OK; re-measure if tier-2 pressure returns |
| Softcode dual-impl quality | Separate investment tracker (was #1735); not a Lua default-on dependency |

---

## 4. Target architecture (shipped shape)

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

Design agreed on #1326 and landed:

1. Bundle `s_runtime_buffer`, program ids, persistent DBT, ready flags into
   a **per-depth** context.
2. Depth ≥ 2 still refuse (or hard-cap); no unbounded reentrancy.
3. Second context allocated **lazily** (installations that never call
   `lua()` pay nothing).
4. ECALL handlers use `ec->memory` — not file-statics.
5. Softcode nested under Lua stays on outer/main instance rules (decline
   to AST when depth conflict) so v1 does not need full mutual recursion.

Out of scope for v1 (still): true single-DBT reentrancy, superblock sharing
across depths, per-player Lua isolation.

---

## 5. Phased roadmap

### Phase A — Unblock production-shaped execution (#1326) — **done**

**Goal:** `lua_jit 1` + default `jit_eval_brackets 1` → `lua_run_ok > 0` for
ordinary `lua()` / `[lua(...)]` / attr forms.

**Exit met:** Nested Lua JIT executes; softcode still JIT by default.

### Phase B — Correctness under real execution — **done for known gates**

Wrong-answer bugs that were invisible behind the nest guard were fixed or
fail-closed as they surfaced. Grow EXEC further under Phase D as needed.

### Phase C — Default on (#1325 / #1745) — **done**

| Prerequisite | Status |
|--------------|--------|
| Phase A green | met (#1326) |
| Engage assert under default conf | met |
| Win64 / soak gates as required | met for flip |
| Docs / CHANGES | product behaviour on master; this plan stamped 2026-07-29 |

**Exit met:** `mudconf.lua_jit = true` by default on `--enable-jit` builds.

### Phase D — Product polish (ongoing, non-blocking)

- `jitstats` / `@list` clarity: compile vs run, nested declines  
- Opcode coverage growth (closures still out — design-lua-jit)  
- Optional call-surface / loop-form pins beyond the core EXEC corpus  
- NLS remains orthogonal  

---

## 6. Test strategy (games vs harness)

### Production defaults (what games run)

```text
jit_eval_brackets  1     # default — softcode JIT fully on
lua_jit            1     # default after #1745
```

No special conf. Nested softcode → Lua uses the second runtime context.

### Test laboratory (historical / differential)

```text
lua_jit 1
jit_eval_brackets 0      # lab-only: Lua not nested under softcode DBT
```

Some `tests/luajit` lanes historically pinned brackets off to isolate
lowering bugs. Prefer **default-brackets EXEC** for any claim that “the
product path works.” Lab conf is fine for differential isolation, not as
the only proof.

### Suites

| Target | Purpose |
|--------|---------|
| `make test` | English, production defaults (both JITs on when `--enable-jit`) |
| `make test-lua-jit` | Explicit Lua JIT emphasis / extra conf where needed; assert `lua_run_ok` |
| `tests/luajit/run.sh` | Differential EXEC/AGREE/SURVIVE; pin run_ok |

**Never** require games to set `jit_eval_brackets 0`.

### What not to assert in smoke

- Softcode `#-1` strings (ABI)  
- NLS-translated notify under `LANGUAGE=xx` as part of default `make test`  
- That every Lua chunk compiles (many correctly decline)  

### What to assert

- Known EXEC chunks: result + `lua_run_ok`  
- Nested call integrity (outer softcode result unchanged)  
- Process does not hang under `lua_fn` / `lua_jit_fn` with defaults or `lua_jit 1`  

---

## 7. Shepherding other agents

### Single rules of engagement

1. **Read this plan** before opening a Lua JIT PR.  
2. **Do not** “fix” Lua JIT by documenting `jit_eval_brackets 0` as the
   supported game mode.  
3. Prefer **EXEC** (`lua_run_ok`) over compile counters for any claim that
   “the JIT works”.  
4. Fail closed: wrong native path → decline → interpreter, not a clever
   wrong answer.  
5. One concern per PR (runtime split vs bridge case vs opcode).  
6. Default-on already shipped (#1745); do not re-gate polish work on
   re-flipping the conf default.  

### Suggested claim map (post product path)

| Agent workstream | Owns | Avoid |
|------------------|------|--------|
| **Corpus** | Grow `tests/luajit` EXEC under default conf | Re-architecting dual context without a hang |
| **Opcodes / bridges** | New lowering or intentional bridge implement | Silent wrong answers |
| **Windows** | Soak regressions on Win64 | Ignoring dual-context cost |
| **Docs** | Keep this plan + audit map current when defaults change | Rewriting bring-up/design as if they were living product truth |

### PR checklist (Lua JIT)

```text
[ ] States which phase (D polish vs regression fix) and issue if any
[ ] Under default jit_eval_brackets? If not, why temporary
[ ] lua_run_ok measured for any “executes” claim
[ ] make test / tests/luajit / make test-lua-jit as applicable
[ ] No self-merge without dual-review on engine runtime paths
```

---

## 8. Success metrics

| Metric | Pre-#1326 | After Phase A | After Phase C (now) |
|--------|-----------|---------------|---------------------|
| Default conf + `lua_jit`: `lua_run_ok` for simple `lua()` | 0 | >0 | >0 |
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

## 10. Immediate next steps (post #1745)

1. Treat product enablement as **done**; do not re-open default-off work.  
2. Optional: grow EXEC coverage and opcode surfaces under Phase D.  
3. Keep [`audit-coverage.md`](audit-coverage.md) D4 notes aligned with
   defaults when behaviour changes.  
4. Leave [`plan-lua-jit-bringup.md`](plan-lua-jit-bringup.md) and
   [`design-lua-jit.md`](design-lua-jit.md) as historical path docs.

Until a new product gate is filed, treat **every** “Lua JIT works” claim that
only used `jit_eval_brackets 0` as **lab-only** evidence.
