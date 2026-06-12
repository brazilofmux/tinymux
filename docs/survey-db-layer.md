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

### 🔴 db_read does not validate the object dbref — OOB write / crash on malformed flatfile (LIVE-VERIFIED)
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

## Areas still to audit
- [ ] `sqlite_load_game` (`db.cpp:3596`) — does it validate object row dbrefs
      before `db[i] = …` (`db.cpp:3742`)? Same OOB class from a malicious SQLite DB?
- [ ] `getboolexp1` recursion (`db_rw.cpp:207`) — deeply-nested lock expression in
      a malformed DB → unbounded recursion / stack overflow?
- [ ] The `getref`-derived field values (location/contents/exits/owner/parent) —
      validated by `db_check` before use as indices during gameplay?
- [ ] `attrcache.cpp` — attribute load/evict bounds.
- [ ] `db_write` durability (crash-mid-write / atomic replace).
