# Survey: Pass 13 residual (anti-cool) — 2026-07-26

Disciplined residual pass after Passes 1–12 and the June memsafety/correctness
surveys. **Not** a full A–H re-walk and **not** a voluntary D2/D3 codegen dive.

**Goal:** squeeze remaining High/Medium bugs from slices the map still marks
partial/open or that recent LLP64 work exposed, without competing for the
cool end of the pool (HIR/DBT).

## Scope chosen

| Slice | Why |
|-------|-----|
| **E3** flatfile R/W | Map still `partial`; #806 era validation claimed but not re-logged |
| **ltoa / quota-money** | Follow-on to #1402/#1404/#1408 — boring host types |
| **Bootstrap path builders** | Conf `input_database` → `.sqlite` sibling |
| **Map hygiene** | Recommended rotation issues #1260–#1292 largely closed; record that |

**Explicitly out of scope:** D1–D3 re-scout, new Lua lowering, client J*.

## Methodology

Same lenses as `docs/audit-coverage.md` (bounds, lifetime, perms, failure,
platform). Prefer **config/import/host** reachability over softcode cleverness.
File only when a concrete path exists; prefer expanding an existing issue when
the class matches.

## Results

### Confirmed / filed

| ID | Severity | Summary |
|----|----------|---------|
| **#1411** | Medium | `engine_com` `bMinDB` path: `SIZEOF_PATHNAME` buffer + `strcpy`/`strcat` of `.sqlite` overflows when `input_database` is at conf max length |
| **#1408** (comment) | — | Documented sibling money/quota load-store sites (`pay_quota`, `Pennies`, pcache money) as same domain decision, not separate “widen everything” bugs |

### Reviewed, no new High/Medium

| Area | Outcome |
|------|---------|
| **E3** `db_rw.cpp` object `!` records | #806-style dbref range check still present; lock nest cap; color V4→V5 aborts on buffer pressure rather than truncating. No new defect filed. Status remains **deep** with this re-read. |
| **attrcache** `.sqlite` derivation | Uses `LBUF_SIZE` stack; `indb` conf-capped at `SIZEOF_PATHNAME` ≪ LBUF → no active overflow. Optional refactor into shared helper with #1411. |
| **mux_ltoa** call inventory (~46 sites) | Almost all pass `int`/`dbref`/small counters. Only expression still feeding wide parse into `mux_ltoa(long)` of note is **#1408** `add_quota`. |
| **Map rotation residuals** | #1260, #1280, #1292, #1266–#1270, #1275: **closed** since the map’s “recommended next” table was written. Rotation table needs a refresh more than another D* pass. |

### Empty on purpose

No new softcode-function crawl (Pass 5 still stands). No HIR/DBT re-scout.

## Recommendations

1. Fix **#1411** with a shared path helper (small, self-contained).
2. Resolve **#1408** with an explicit clamp/reject design (not silent widen).
3. Prefer landing free open PRs (#1397 Lua engage, #1405 races when dual-reviewed) over inventing Pass 14.
4. Next residual pass, if any: **E3 export symmetry**, **K2 scenario defaults**, or **F3 dual-path** leftovers — still not D3.

## Sign-off

Pass 13 residual complete: **1 new Medium**, **1 issue family annotation**, **E3 re-read clean**, audit map updated.
