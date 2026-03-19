# ISSUES.md

No open bugs.

---

## Notes

### Lua bridge — new functions need audit

Any new `mux.*` bridge function must be audited against its softcode
equivalent before release.  The audit template is in git history
(commit 7769863d2).  Functions already audited and fixed:

- `mux.notify/pemit` — @pemit checks (253692195)
- `mux.location` — locatable() (253692195)
- `mux.name` — read_rem_name/nearby (253692195)
- `mux.controls` — restrict who param (253692195)
- `mux.get/set/eval/flags/owner/type/isplayer/pennies` — OK as-is
- `mux.isconnected/iswizard` — minor, acceptable
