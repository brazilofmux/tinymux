# Transition plan: Lua post-entry decline/rerun → softcode contract

**Status:** active campaign (2026-07-29)  
**Stake issue:** [#1751](https://github.com/brazilofmux/tinymux/issues/1751)  
**Related product enablement (shipped knobs):** [`plan-lua-jit-product.md`](plan-lua-jit-product.md)

**One-line summary:** Stop pretending discarded compiled runs are free (Phase 0), prove purity (0.5), make remaining ECALLs total or compile-ineligible (1–3), then delete the retry machinery (4).

This document is the multi-agent source of truth for **how** we get from today's default-on Lua JIT to the softcode invariant. Detailed exhibits and inventory live on #1751; do not treat enablement docs as “architecture done.”

---

## 0. Where we are vs where we want to go

### Today (post–#1745 / #1749 / #1750)

| Layer | State |
|--------|--------|
| Default `lua_jit` | **On** (product knobs enabled) |
| Nested softcode → Lua | Can execute (#1326) |
| Instruction budget | Read **live** at run time (#1749) — template for “no baked mudconf” |
| Effectors (`notify`/`pemit`/…) | **Compile-ineligible** or refuse so world effects are not doubled (#1750) — *medicine* |
| Pure-ish work | Often compiles; **runtime decline → silent interpreter re-run** still legal |
| Answer harnesses | Can show green while **persistent Lua state** diverges |

**Broken invariant:** a discarded compiled attempt must observably do nothing. That was true for pure arithmetic; it is false once CALL (or anything with side effects on the Lua VM) can run before a later decline.

**Exhibits (proofs, not the product framing):** see #1751 — (1) double mutation of persistent Lua state under decline-then-rerun; (2) lost effects via irreplayable state (e.g. coroutine resume after refuse).

### Target

Same product knobs (`lua_jit` on, brackets on), but:

1. **All soft refusal at lowering** (cache miss / “ineligible” only).
2. **After entry:** finish, raise what the interpreter would raise, or budget/alarm as the **interpreter’s** limit — **never** whole-chunk re-run.
3. **No live values baked** into the program at lower time.
4. Dynamism handled by **total ECALLs** to real VM ops (`luaL_len`, `lua_gettable`, …), not by “maybe decline later.”

#1750’s “compiled path is effect-free” is a **temporary corridor**, not the destination. Destination is closer to softcode: effects may happen on the compiled path under the same rules, but only when the path is total or never entered.

---

## 1. Non-negotiable contract

```text
PRE-ENTRY (nothing user-visible has run yet)
  ✓ refuse lower / miss cache → interpreter OK

POST-ENTRY (guest code or user-visible Lua work may have run)
  ✓ complete successfully
  ✓ same error the interpreter would raise
  ✓ budget/alarm as interpreter-identical limit error
  ✗ silent re-run of the whole chunk
  ✗ “decline because type/metatable surprised us”
```

**Working definition of post-entry:** any path that may have run guest code or done user-visible Lua work. Registry / `mux.*` setup alone is not “free abort”; nested purity is save/restore (#1751 items 3–5), not decline-and-retry.

**Failure is allowed; retry is the poison.**

---

## 2. Transition strategy

### Principle: make the lie impossible before polishing the truth

| Approach | Verdict |
|----------|---------|
| Convert bins first, keep silent re-run | **Wrong** — still hides double-mutation; agents invent green paths |
| Delete re-run day one with no loud fail | **Wrong** — silent wrong or mysterious empty answers |
| **Phase 0: fail loud at every post-entry decline** | **Right** — visible debt; then empty the FAIL sites |
| Snapshot/rollback Lua so re-run can continue | **Non-goal** |

```text
  (A) Medicine (#1750)     effectors off compiled path
           │
           ▼
  (B) Honesty (Phase 0)    post-entry decline → loud error, no re-run
           │
           ▼
  (C) Empty the FAIL sites (Phases 1–3)  total ECALLs / CALL claims / LIMITED
           │
           ▼
  (D) Ratchet (Phase 4)    delete re-run + ECALL_DECLINE from the model
```

**Default stays ON.** Alpha preference: break hard and visible over quiet wrong state.

**Agent impulse:** the best answer at Phase 0 is **FAIL**, not a clever green path. Do not “fix N” by reintroducing silent re-run.

---

## 3. Phased plan

### Phase 0 — Honesty (first work item; merge gate for everything else)

**Goal:** Post-entry decline **stops laundering through the interpreter**.

- Every path that today does `ECALL_DECLINE` → TryJIT fails → re-run interpreter **after entry** becomes instead:
  - Result: e.g. `#-1 LUA JIT POST-ENTRY DECLINE (ECALL_…)` naming the site
  - Log: `JIT/RETRY`-class line (chunk + ECALL)
  - `jitstats()`: counter whose **only correct long-term value is 0**
- **Pre-entry** fallback (no program, lower refuse, cache miss) **unchanged**.

**Deliberate product regression:** fewer successful `lua()` under default-on. Each hit is a defect the old model hid.

**PR shape:** one focused PR that wires fail-loud for all post-entry sites + stats + log; do not bury bin “fixes” in the same PR if they delay honesty.

**Exit:** Re-run after entry is gone (or only reachable via fatal assert). Counter visible in CI / harness.

---

### Phase 0.5 — Purity oracle

**Goal:** Cannot claim “architecture fixed” while state still diverges quietly.

Add a **STATE** class in `tests/luajit` (names flexible):

| Case | Assert |
|------|--------|
| Exhibit 1 (`bump` / `N`) | Persistent state matches across legs; under Phase 0, JIT leg **loud-fails** instead of double-increment |
| Exhibit 2 (coroutine / effect count) | Delivered-effect counts match, or loud fail where old model dropped effects |

**Discipline:** known-fail-must-flip (#1434 family). Red until Phase 0 + relevant bins land.

**Exit:** Oracle green on CI; post-entry decline counter is 0 on the default suite (or only pre-entry paths remain).

---

### Phase 1 — Totalize GET / SET / LEN (~12 declines)

**Goal:** Metatable/type “surprises” cannot decline mid-run.

| Family | Action |
|--------|--------|
| GETFIELD_*, GETGLOBAL, GETI, SETI, SETFIELD, LEN | ECALL → real VM op (`lua_gettable` / `lua_settable` / `luaL_len` / …) |
| Runtime “wrong type / has metatable” declines | **Delete** — VM raises or returns what the interpreter would |

**Exit:** Those handlers never return `ECALL_DECLINE` for policy reasons; suite + purity oracle still green; Phase 0 counter falls for this bin.

**Risk:** Performance. Correct first; profile later.

---

### Phase 2 — CALL boundary (design center, ~13 declines)

**Goal:** User code may run on the compiled path only under an **honest** result story.

| Rule | Meaning |
|------|---------|
| Claims at **lowering** | int-returning / void / string / effectful — pattern already in-tree (`kIntReturning`, effectful list) |
| Typed fast path | Only if claim is proven; result must match claim or **raise** (not decline) |
| No honest claim | Chunk **ineligible at compile time** |
| Effects in-path | When product wants them: real bridge under permissions (softcode shape), not “refuse compile forever” as the end state |
| Result-type surprise after call | **Cannot** decline — marshal via honest Lua coercion or raise |

**Suggested order:** keep #1750 compile-ineligibility until total/in-path story for effectors; then CALL_VOID / CALL_INT under strong claims; CALL_STR / marshalling; user-defined callables as total ECALL or ineligible.

**Exit:** CALL family contributes 0 to Phase 0 counter; exhibit 1 cannot recur for claimed call shapes.

---

### Phase 3 — LIMITED

**Goal:** Budget exhaustion is not a free retry with doubled side effects.

- `ECALL_LUA_LIMITED` → same error text / class as interpreter instruction limit.
- No re-run; no permanent product string that is not what the interpreter would say.

**Exit:** Stress cases match interpreter on both legs; no double-mutation under budget abort.

---

### Phase 4 — Structural ratchet

**Goal:** The poison mechanism cannot return as “one more soft decline.”

1. Delete post-entry re-run in `TryJIT` (and any twin).
2. Remove `ECALL_DECLINE` from the Lua ECALL ABI (or reduce to assert/unreachable).
3. Pre-entry fallback only.
4. Optional: static grep / test that forbids new `return ECALL_DECLINE` in Lua handlers.

**Exit:** Grep-clean; #1751 close criteria met.

---

### Anytime (parallel, small PRs — do not wait on Phases 1–4)

From #1751 items 3–5:

| Item | Work |
|------|------|
| Nested `mux.args` / executor stomp | Save/restore per-run fields around nested entry |
| `lua_checkstack` on GETGLOBAL / GETFIELD_REF | Grow stack or fail like interpreter |
| Bridge claims TY_STRING but push ints | Fix claims or marshalling |

These improve honesty but **do not** replace Phases 0–4.

---

## 4. What to keep vs throw away

| Keep | Throw away (eventually) |
|------|-------------------------|
| HIR → codegen → DBT pipeline | Post-entry “decline = free abort” |
| Harness EXEC/AGREE/NESTED ratchets | Silent interpreter re-run after entry |
| #1749 live budget ECALL | Baking mudconf / live DB into the blob |
| #1750 real bridge **routing** to C | #1750 “no effects ever on compiled path” as *end* state |
| Compile-time ineligibility | Runtime metatable/type declines on GET/LEN/… |
| Softcode as existence proof | Softcode `CALL_FUNC` post-entry audit (sibling issue only) |

---

## 5. Suggested PR sequence

| Step | Theme | Depends on |
|------|--------|------------|
| **T0** | Phase 0 fail-loud + stats + log | — |
| **T0.5** | STATE oracle red then flip | T0 |
| **T1…** | Total LEN, then GET*, then SET* (can split) | T0 |
| **T2…** | CALL claims + marshal (split by family) | T0; prefer T1 for less noise |
| **T3** | LIMITED = interpreter limit | T0 |
| **T4** | Delete re-run + ECALL_DECLINE | T0–T3 emptied bins |
| **Tx** | Items 3–5 anytime | — |

**Do not** merge “total LEN” that still re-runs on CALL decline and call the architecture done.

---

## 6. Success metrics / close criteria (#1751)

- Phase 0 counter is zero in CI under default-on (or only pre-entry paths remain)
- Phase 0.5 oracle green
- Phases 1–3 emptied the bins they own
- Phase 4 merged: no post-entry re-run path in tree
- Exhibits 1–2 cannot recur by construction
- Items 3–5 fixed or filed as tiny follow-ups and done

---

## 7. Risks

| Risk | Mitigation |
|------|------------|
| Default-on “breaks games” in alpha | Loud errors > wrong state; lower eligibility, don’t re-run |
| CALL design stalls | Phase 0 already stops the lie; CALL can stay loud-fail until Phase 2 |
| Performance of total ECALLs | Correct first; optional later typed paths under proof |
| Agent impulse to green-path | Phase 0 FAIL is the bat; oracle red until real |
| Softcode has same bug class | Sibling stake; not this campaign’s merge gate |

---

## 8. Related

| Item | Role |
|------|------|
| #1751 | Stake issue; exhibits and amendment history |
| #1750 / #1748 | Wrong compiled `mux.*`; effector medicine |
| #1749 | Runtime instruction budget (live-value template) |
| #1745 / #1325 | Default-on |
| #1519 era | Call machinery / decline surface growth |
| Softcode JIT | Existence proof for “compile decides; run commits” |

Inventory of ~27 post-entry decline sites (bins) is maintained on #1751 and should be re-counted when implementing Phase 0.
