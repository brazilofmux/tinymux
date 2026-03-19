# ISSUES.md — Open items

No open bugs. Remaining items are future work.

---

## Future Work

### 1. Lua bridge error consistency

`mux.get()` returns `nil` on permission failure (silent).
`mux.set()` throws a Lua error (loud).  These should be consistent.
Recommendation: return `nil, "permission denied"` (two-value return)
for both, matching Lua conventions.

### 2. Lua bridge — new functions need audit

Any new `mux.*` bridge function must be audited against its softcode
equivalent before release.  The audit template is in git history
(commit 7769863d2).  Functions already audited and fixed:

- `mux.notify/pemit` — @pemit checks (253692195)
- `mux.location` — locatable() (253692195)
- `mux.name` — read_rem_name/nearby (253692195)
- `mux.controls` — restrict who param (253692195)
- `mux.get/set/eval/flags/owner/type/isplayer/pennies` — OK as-is
- `mux.isconnected/iswizard` — minor, acceptable
