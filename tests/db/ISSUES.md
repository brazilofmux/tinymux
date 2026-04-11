# DB Backend Tests — Open Issues

Updated: 2026-04-10

## Bugs

### ~~Standalone DB test harness no longer builds after backend file moves~~ FIXED

- Updated `db/Makefile` to use `INCDIR=../mux/include` for headers and `ENGDIR=../mux/modules/engine` for `.cpp` sources. `SQLITEDIR` unchanged.

## Opportunities

### ~~Add a path-stability check for test harnesses that compile moved engine code~~ FIXED

- `tests/db/Makefile` now has a `check-paths` target that verifies the required engine, header, and SQLite source paths before `all` or `test` builds run. If a dependency moves again, the harness now fails immediately with an actionable error instead of breaking later during compilation.

## Opportunities (New, 2026-04-10)

### Interface tests never exercise reopen-on-disk persistence semantics

- **File:** `tests/db/test_backend.cpp:43-47`, `tests/db/test_backend.cpp:197-213`
- **Issue:** Every interface test constructs `CSQLiteBackend` against `":memory:"`, so the suite never closes and reopens a real database file. `test_backend_sync_tick()` even comments that sync "should persist it", but there is no reopen step that can detect persistence, WAL, or migration regressions.
- **Impact:** Bugs in on-disk open/close, checkpointing, schema migration, or data durability can land without tripping the abstract-backend harness.
- **Fix:** Add a temp-file-backed factory plus at least one reopen test that writes data, closes, reopens, and revalidates `Get()`, `GetAll()`, and deletion semantics.
