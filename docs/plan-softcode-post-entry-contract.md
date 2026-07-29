# Softcode post-entry contract (sibling of Lua #1751)

**Status:** audit complete 2026-07-29; residual harden shipped with this plan  
**Stake issue:** [#1791](https://github.com/brazilofmux/tinymux/issues/1791)  
**Lua twin:** [`plan-lua-post-entry-contract.md`](plan-lua-post-entry-contract.md)

**One-line summary:** Softcode `CALL_FUNC` does **not** soft-decline after partial work. The residual poison path was mid-run **DBT infrastructure** failure → `jit_eval` AST re-run; that is now blocked once any host ECALL has run.

---

## 0. Why this exists

The Lua campaign deleted silent whole-chunk re-run after entry. Its standing
note assumed softcode had the same property only as a **derived** fact:

* depth / invocation watermarks run **before** `dbt_run` (#1002);
* softcode function ECALLs return `-1` (continue) and put errors in guest
  memory rather than `ECALL_DECLINE`.

That left an un-audited gap: what if `dbt_run` itself fails after a host
ECALL already ran (`pemit`, `setq`, nested `lua()`, …)? Softcode
`run_cached_program` used to `return false` → `jit_eval` falls through to
the AST for the **entire** expression — a full re-run, effect-doubling
risk. Lua's arm already committed errors instead; softcode did not.

---

## 1. Non-negotiable contract (same shape as Lua)

```text
PRE-ENTRY (nothing user-visible has run yet)
  ✓ refuse compile / miss cache / watermarks / oversize cargs → AST OK

POST-ENTRY (any host softcode ECALL may have run)
  ✓ complete successfully and harvest
  ✓ same error string the AST would produce from a finished ECALL
  ✓ CPU LIMITED / alarm as AST-shaped diagnostic
  ✗ silent whole-expression AST re-run after host work
```

**Failure is allowed; retry of the whole expression after host work is the poison.**

---

## 2. Audit results (2026-07-29)

| Path | Pre- or post-entry? | Behavior | Verdict |
|------|---------------------|----------|---------|
| Compile refuse / no tier2 / sandbox | Pre | `jit_eval` → `false` → AST | OK |
| Depth / invk watermarks | Pre | before `dbt_run` | OK |
| Oversize `cargs` slots | Pre | before `dbt_run` | OK |
| `ECALL_CALL_FUNC` / `CALL_INDEX` | Post (once entered) | Error strings in guest; return `-1` continue | **Total** — never `ECALL_DECLINE` |
| `ECALL_SETQ` / `SETQ_PACK` | Post | Write-through; return `-1` | Total |
| `ECALL_EXIT` | End | non-negative stops `dbt_run` with success status | OK |
| Mid-run code buffer full | Post (if any ECALL already ran) | `dbt_run` → `-1` → was AST re-run | **Fixed** (#1791) |
| Harvest fail after `rc==0` | Post if host ECALLs | was AST re-run | **Fixed** (#1791) |
| Alarm `-3` | Post | `#-1 CPU LIMITED`, handled, no re-run | OK |
| Nested `lua()` under softcode | Post | Lua path Phase 4; outer softcode ECALL continues | Lua contract holds |

### CALL_FUNC is not the Lua bug class

Softcode never had “decline because type surprised us after the callee
ran.” Builtin and `@function` ECALLs always finish the host call (or write
a `#-1 …` string) and resume the guest. The surprise was justified.

### Residual that *was* real

Any `dbt_run` status that `handle_dbt_run_status` does not treat as
handled (`rc != 0` and not the CPU-limited arm) made softcode
`run_cached_program` return `false`. Common theoretical trigger:
**DBT code buffer full mid-translation of a later block** after earlier
ECALLs already executed (see also #1315 messaging). Rare under normal
load; still a contract hole.

---

## 3. Harden shipped with this plan

1. `eval_ctx::host_ecalls` — incremented in `ecall_invoke_fun`,
   `ecall_invoke_ufun`, `ECALL_SETQ`, `ECALL_SETQ_PACK`.
2. Softcode arm after `dbt_run`: if status is unhandled **and**
   `host_ecalls > 0`, commit `#-1 JIT POST-ENTRY FAIL` (or keep an
   existing `#-1…` diagnostic) and **return true** — no AST re-run.
3. Same rule for harvest failure after a completed run with host work.
4. Pre-entry and “no host ECALL yet” failures still return `false` so
   AST can answer (correct, effect-free).

Lua path unchanged (already Phase 4).

---

## 4. What we are **not** claiming

* Softcode JIT is free of all bugs — only this contract class.
* Code-buffer-full never happens — only that it must not double effects.
* Every mid-run failure has a forceable purity test in CI — forcing
  mid-run `dbt` full from softcode is hostile to the suite; the harden
  is structural.

---

## 5. Residual / follow-ups

| Item | Notes |
|------|--------|
| Grep for new softcode ECALL cases | Any new host side-effect ECALL must `++host_ecalls` |
| Optional soak | Drive code buffer pressure under softcode with effectors; assert single delivery |
| Cross-link | Lua plan § “Standing assumption: the softcode route…” now has this sibling |

---

## 6. Checklist

- [x] CALL_FUNC / CALL_INDEX audit (total via guest error strings)
- [x] Identify softcode post-entry → AST re-run sites
- [x] Harden with `host_ecalls`
- [x] Plan doc + issue #1791
- [ ] CI soak under artificial code-full (optional)
)
