# Survey: Database layer (object store, flatfile I/O, SQLite backend, attrcache)

Audit of the TinyMUX database subsystem. Companion to
`docs/survey-ganl-networking.md` and `docs/survey-jit-dbt.md`.

**Threat model:** operational robustness + load-time memory safety. The flatfile
`.db` format is the portable/shareable DB format (admins import DBs from other
servers, backups, shared areas), and DBs can be corrupted (disk error,
truncation, version skew). A malformed/malicious DB reaching the load path
should fail cleanly, not corrupt memory or crash.

## Files
- `mux/modules/engine/db.cpp` (3774) — object store, `db_grow`, `s_*` accessors,
  `sqlite_load_game`.
- `mux/modules/engine/db_rw.cpp` (1174) — flatfile `db_read` / `db_write`,
  `get_list`, `getboolexp1`.
- `mux/lib/dbutil.cpp` — flatfile primitives (`getref`, `getstring_noalloc`,
  `putstring`, `getboolexp`).
- `mux/modules/engine/sqlitedb.cpp` (2606) — SQLite backend.
- `mux/modules/engine/attrcache.cpp` (1006) — attribute cache.

## Load paths
- `LoadGame` (`engine_com.cpp:2719`) → `sqlite_load_game()` (`db.cpp:3596`) for
  the SQLite DB, OR `load_game(pagefile)` → `db_read` (`db_rw.cpp:501`) for the
  flatfile. `db_read` is also reached from `engine.cpp:1925` (dbconvert/standalone).

## Findings inventory

### ✅ FIXED (f3c96dc82) — was OOB write / crash on malformed flatfile (#806, LIVE-VERIFIED)
`db_read`'s `!` case now validates `i < 0 || i > DB_LOAD_MAX_DBREF` (268M) and
returns -1 (clean rollback) before `db_grow`/`s_*`. Re-ran the bite: `!-5` and
`!999999999` now abort the load cleanly (exit 1, no SIGSEGV/SIGABRT); valid
flatfile still imports; smoke 1115/1115.

**Reproduced live** via the standard flatfile import (`dbconvert -l -i <flat>`,
i.e. `db_load`): taking the stock `netmux.db` and changing one object header
`!2` →
- `!999999999` (huge): `db.cpp(2864): Assertion failed` (the `mux_assert(newdb)`
  in `db_grow` — the ~1e9-entry `MEMALLOC` fails) → **SIGABRT** (exit 134). DoS.
- `!-5` (negative): OOB write at `db[-5]` → **SIGSEGV** (exit 139). Memory
  corruption (a different heap layout could corrupt silently instead of
  crashing).

`db_read` `!`-entry handling (`db_rw.cpp:711-756`):
```cpp
case '!':   // MUX entry
    i = getref(f);          // unbounded int from the file (mux_atol)
    db_grow(i + 1);         // no-op when i+1 <= db_top (so negative i never grows)
    ... s_Name(i, buff);    // db[i].name = ...   (unchecked)
    s_Location(i, getref(f));// db[i].location = ... (unchecked)
    s_Zone(i, ...); s_Contents(i,...); s_Exits(i,...); s_Link(i,...); s_Next(i,...)
```
- `getref` (`dbutil.cpp:27`) returns any `mux_atol` result — no range check.
- `s_Name`/`s_Location` (`db.cpp:3368`, etc.) do `db[i].field = …` / `db[i].name`
  with **no bounds check**. `SIZE_HACK = 1` (`db.cpp:2770`) so only `db[-1]` is
  valid; `i <= -2` indexes before the allocation.
- `db_grow(i+1)` returns early for `i+1 <= db_top`, so a **negative** `i` never
  grows the array → `s_Name(-5,…)` writes `db[-5]` (OOB). A **huge** `i` makes
  `db_grow` `MEMALLOC` a giant array → `mux_assert(newdb)` aborts (DoS). `i =
  INT_MAX` makes `i+1` overflow to `INT_MIN` (UB) → treated as negative → OOB.

**Impact:** loading a corrupted or malicious flatfile `.db` (a normal admin
operation — flatfiles are the portable format) can corrupt memory (OOB write) or
crash (assert). The OOB happens *during* `db_read`, before `db_check` can run.
**Incomplete-hardening root cause:** the *SQLite* load path (`sqlite_load_game`,
`db.cpp:3690-3748`) **does** validate — `if (i < 0 || i >= expected_top) return
-1;` at `:3694` AND again at `:3735` before `db[i].location = …`. The flatfile
`db_read` path never got that check. Same pattern as the i64Mod-vs-i64Division
gap (#805): one path hardened, the sibling missed.

**Fix direction:** validate `i` in the `!` case before `db_grow`/`s_*` — reject
the load (return -1, the existing error path at `db_rw.cpp` already rolls back
the SQLite import) for `i < 0` or `i` above a sane ceiling. The dbref also feeds
`+`/other entry types via `getref`; audit those for the same index use.

### ✅ FIXED (67604c756) — getboolexp1 unbounded recursion + assert error paths (#807)
The v1/v2 flatfile inline-lock parser `getboolexp1` (reached when `read_key` is
set) recursed with no depth bound, and every error path was `mux_assert(0)`.
**Live bug-catch:** a lock of `(`×500000 → stack overflow / SIGSEGV; a truncated
lock → SIGABRT — both on the old build; cleanly rejected (exit 1) on the fixed
build. Root cause: the runtime `@lock` parser caps nesting at `lock_nest_lim`
(20), but this import path was unguarded (incomplete hardening, sibling of
#806). Fix: thread a `depth` arg, bound at `BOOLEXP_LOAD_NEST_MAX = 1024`, and
convert the asserts to a `s_boolexp_corrupt` flag → clean `return -1` rollback.
Smoke 1115/1115.

### ✅ FIXED (7e3da01e3) — unvalidated user-attribute number → anum_table OOB / OOM (#808)
The attribute number from a `+A` flatfile record (`getref`) or a SQLite
attr-name row indexes `anum_table` via `anum_set` (bare `anum_table[x]=v` macro)
+ `anum_extend` (dense `(x+1)` alloc) with no validation. **Live bug-catch:**
`+A-10000000` → `anum_table[-10000000]` write → SIGSEGV; `+A-5` (novel name) →
silent heap corruption; `+A999999999` → ~8 GB alloc → OOM kill. The *read* path
`atr_num` validates `anum<0||>top`; the *write* path (`vattr_define_LEN`) didn't
(4th incomplete-hardening case). Fix: `A_USER_MAX` (16M) constant in `attrs.h`;
`vattr_define_LEN` rejects `< A_USER_START || > A_USER_MAX` (central backstop);
the `+A` handler and SQLite attr-name callback skip bad records. Smoke 1115/1115.

## Recurring theme
All four DB / load-path bugs this audit (#806, #807, #808) plus the JIT #805 are
**incomplete hardening**: a validation/guard added to one path (SQLite dbref
check, runtime `@lock` nest limit, `atr_num` read check, `i64Mod`) while the
sibling path (flatfile dbref, `getboolexp1` import recursion, `anum_set` write,
`i64Division`) was left unguarded.

### ✅ FIXED (#810) — object field dbrefs unvalidated on load → DOLIST chain-walk OOB
No `db_check` existed; `db_read` clamped only `zone`, `sqlite_load_game` clamped
nothing. `DOLIST` (`db.h:218`) walks `contents`/`exits` via `Next(thing)` =
`db[thing].next` guarding only NOTHING/self-loops, so a wild `next` from a
malformed DB → `db[wild].next` OOB read. Fix: new `db_validate_refs()`
(`db.cpp`) called once after load (`engine_com.cpp` LoadGame; both warm/cold
paths) clamps `location/contents/exits/next/parent/zone` → real-or-NOTHING,
`link` → +HOME, wild `owner` → GOD. No-op for valid DBs. Verified live: server
loaded from SQLite with `next=999999999` on #0 starts clean + logs the clamp;
smoke 1115/1115.

## DB-layer audit — complete
Four memory-safety bugs found and fixed, all incomplete-hardening, all
live-verified: **#806** (object dbref OOB), **#807** (lock-parser recursion),
**#808** (attr-number OOB), **#810** (field dbref OOB). The remaining surfaces
were verified safe: `attrcache` (hashtable + length-clamped vectors, no fixed
index), attribute storage (keyed by `makekey`, sparse-safe), `getstring_noalloc`
(invariant-bounded), `db_write` durability (SQLite-backed transactions). The
JIT-side a64 **#804** was fixed upstream by the maintainer.
- [ ] The `getref`-derived field values (location/contents/exits/owner/parent) —
      validated by `db_check` before use as indices during gameplay?
- [ ] `attrcache.cpp` — attribute load/evict bounds.
- [ ] `db_write` durability (crash-mid-write / atomic replace).
