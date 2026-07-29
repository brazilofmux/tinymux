# Transition plan: Lua post-entry decline/rerun → softcode contract

**Status:** campaign complete (2026-07-29); residual quality work continues  
**Stake issue:** [#1751](https://github.com/brazilofmux/tinymux/issues/1751) (closed)  
**Related product enablement:** [`plan-lua-jit-product.md`](plan-lua-jit-product.md)

**One-line summary:** Post-entry re-run is gone; remaining work is typed fidelity, corpus growth, and pre-entry eligibility — not “one more soft decline.”

This document was the multi-agent source of truth for the campaign. It now records **what shipped**, the **standing contract**, and **residual design goals** so later work does not re-open the poison mechanism.

---

## 0. Where we are vs where we wanted to go

### Shipped (Phases 0–4 + follow-ups)

| Layer | State |
|--------|--------|
| Default `lua_jit` | **On** (#1745 / #1325) |
| Nested softcode → Lua | Executes (#1326) |
| Instruction budget | Live at run time (#1749); LIMITED = interpreter text (Phase 3) |
| Post-entry re-run | **Deleted** (Phase 4) — fail loud or complete; never silent re-run |
| GET/SET/LEN family | Total ECALLs or plain-proven typed paths (Phase 1) |
| CALL boundary | Claims at lowering; marshal at softcode boundary; ineligible for type-erased Lua consumers (#1763 / #1764) |
| Effectors (`notify`/`pemit`/`set`/`eval`) | **Same bridge on both routes** — effect-free corridor retired after Phase 4 (exactly-once without re-run) |
| Lua truthiness | VALUE / BOOL / NIL tags; only nil/false falsy |
| Known-absent plain GETI | Closed key set → nil at lowering (no GETI_INT post-entry miss) |
| Harness ratchets | `POST_ENTRY_LOUD_BUDGET=0`, `AGREE_DECLINE_BUDGET` may only fall |

### Target (unchanged contract)

Same product knobs (`lua_jit` on, brackets on), with:

1. **All soft refusal at lowering** (cache miss / “ineligible” only).
2. **After entry:** finish, raise what the interpreter would raise, or budget/alarm as the **interpreter’s** limit — **never** whole-chunk re-run.
3. **No live values baked** into the program at lower time.
4. Dynamism handled by **total ECALLs** to real VM ops, or compile-time proof — not “maybe decline later.”

Exhibits 1–2 (double mutation; lost effect under re-run) **cannot recur by construction**: re-run is gone.

---

## 1. Non-negotiable contract (standing)

```text
PRE-ENTRY (nothing user-visible has run yet)
  ✓ refuse lower / miss cache → interpreter OK

POST-ENTRY (guest code or user-visible Lua work may have run)
  ✓ complete successfully
  ✓ same error the interpreter would raise
  ✓ budget/alarm as interpreter-identical limit error
  ✗ silent re-run of the whole chunk
  ✗ “decline because type/metatable surprised us” after entry
```

**Failure is allowed; retry is the poison.**

`ECALL_DECLINE` remains only as a residual safety net that commits a loud error (no re-run). New handlers must not rely on it for policy — prefer total ops or lowering ineligibility.

---

## 2. What the phases meant (historical map)

| Phase | Goal | Outcome |
|-------|------|---------|
| 0 | Post-entry decline → loud, no re-run | Shipped; `POST_ENTRY_LOUD` ratchet |
| 0.5 | STATE purity oracle | Green; exhibits 1–2 covered |
| 1 | Total GET/SET/LEN | Shipped; plain_proven for typed reads |
| 2 | CALL claims / marshal / ineligible | Shipped; #1763 marshal + #1764 consumer refuse |
| 3 | LIMITED = interpreter limit | Shipped |
| 4 | Delete re-run | Shipped |
| Follow-up | Truth tags; closed-key nil; effectors on path | Shipped / in flight as residual PRs |

Do **not** reintroduce silent re-run as a “fix.” Do **not** revive effect-free corridor as the end state — it was medicine under re-run.

---

## 3. Residual design goals (quality, not campaign reboot)

These are the open quality / clarity targets after #1751 close:

| Goal | Shape | Notes |
|------|--------|------|
| **Typed CALL results** | Keep Lua values on the VM stack (handle-style) until softcode boundary | Subsumes #1764’s decline-on-consume for `if`/`==`/arith on CALL_STR text |
| **Runtime absent key** | GETI with non-constant key still may hit residual NONINT loud | Closed-set covers constant keys only; dynamic miss needs nil in a typed or handle slot |
| **Anytime items 3–5** | Nested `mux.args`/executor stomp; `lua_checkstack`; int-returning bridge claims | Independent small work; does not wait on typed results |
| **Seam corpus** | Grow softcode ↔ Lua smoke (TC061+) | Answer + state + effects |
| **ECALL_DECLINE ABI** | Optional: assert/unreachable in Lua handlers | Phase 4 left a loud safety net; tightening is polish |
| **Softcode CALL_FUNC sibling** | Same post-entry audit | Out of campaign scope unless filed separately |

### Design principles for residuals

1. **String lies are non-goals** for in-Lua semantics. Marshal only at the softcode boundary (`fun_lua` / chunk return).
2. **Provenance over coercion.** When HIR erases a Lua type, refuse consumers or keep a handle — do not invent softcode truthiness.
3. **Proof enables typed fast paths** (`plain_proven`, `keys_closed`, call claims). Loss of proof → ineligible or total ECALL, not silent 0.
4. **Effects are allowed** on the compiled path under the same permissions as the interpreter, because re-run cannot double them.
5. **Equality compares TYPES, not representations.** Lua's `==` is false across types, and HIR erases exactly the distinctions that decide it: `false` and `0` are one ICONST, `nil` and `""` one empty SCONST, and the numeric path coerces `"5"` to `5`. Every equality opcode must classify both sides (`lua_type_class_of_value` / `_of_const`) before comparing. Order comparisons are *not* the same rule — Lua raises on mismatched types there, so they decline.

6. **New value class ⇒ consumer audit.** Producers are easy; consumers are easy to miss. Emptying a loud bin has repeatedly introduced silent wrongs (#1755, #1756, #1763, #1766 first cut) when a new HIR representation (type-erased string, nil-as-empty-SCONST, …) was not checked at every Lua-semantic use site — including opcodes that look like twins of ones already fixed (EQ vs EQK).


### Standing assumption: the softcode route has no post-entry decline

Compiled Lua effects are exactly-once (#1767) **because** nothing can re-run
a chunk after entry. That holds on the Lua side by construction (Phase 4).
It holds on the *softcode* side — which sits above `[lua(...)]` and was
explicitly out of scope for this campaign — only as a **derived** property
of two unrelated designs:

* the softcode depth / invocation / slot bails are checked **before**
  `dbt_run` (the #1002 watermark design), so they are pre-entry; and
* after Phase 4 exactly one `return ECALL_DECLINE` remains in
  `jit_compiler.cpp`, the defensive arm in `run_cached_program` that nothing
  emits into.

If a softcode ECALL ever gains a post-entry decline, a compiled Lua effect
beneath it would be delivered again by the AST re-run, and **#1767's premise
expires silently**. Nothing in the code says so today; this note is the
record. The durable form is the sibling softcode audit this plan already
suggests — until then, treat "no post-entry decline on the softcode route"
as an invariant to check when touching softcode ECALL error paths.


---

## 4. Harness discipline

| Ratchet | Rule |
|---------|------|
| `POST_ENTRY_LOUD_BUDGET` | May fall, never rise; target **0** |
| `AGREE_DECLINE_BUDGET` | May fall, never rise; decline is correct but not coverage |
| EXEC | Must match **and** `lua_run_ok` advances |
| STATE | Persistent state + effect markers match across legs |

AGREE alone is green when the interpreter answers; EXEC and STATE are what make progress visible.

---

## 5. Success metrics (campaign close — met)

- [x] Phase 0 counter zero under suite (or only pre-entry remains)
- [x] Phase 0.5 oracle green
- [x] Phases 1–3 emptied the bins they own
- [x] Phase 4: no post-entry re-run path
- [x] Exhibits 1–2 cannot recur by construction
- [ ] Items 3–5 fixed or filed (optional residual)

---

## 6. Related

| Item | Role |
|------|------|
| #1751 | Stake issue (closed); exhibits and amendment history |
| #1750 / #1748 | Wrong compiled `mux.*`; effect-free medicine (retired after Phase 4) |
| #1749 | Runtime instruction budget (live-value template) |
| #1745 / #1325 | Default-on |
| #1763 / #1764 | CALL_STR marshal + consumer refuse |
| Softcode JIT | Existence proof for “compile decides; run commits” |
