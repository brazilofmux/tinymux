# DB Backend Tests — Open Issues

Updated: 2026-04-10

## Bugs

### ~~Standalone DB test harness no longer builds after backend file moves~~ FIXED

- Updated `db/Makefile` to use `INCDIR=../mux/include` for headers and `ENGDIR=../mux/modules/engine` for `.cpp` sources. `SQLITEDIR` unchanged.

## Opportunities

### ~~Add a path-stability check for test harnesses that compile moved engine code~~ FIXED

- `tests/db/Makefile` now has a `check-paths` target that verifies the required engine, header, and SQLite source paths before `all` or `test` builds run. If a dependency moves again, the harness now fails immediately with an actionable error instead of breaking later during compilation.

## Opportunities (New, 2026-04-10)

### ~~Interface tests never exercise reopen-on-disk persistence semantics~~ FIXED

- `test_backend.cpp` now has a temp-file-backed factory path plus `test_backend_persist_reopen()`, which writes data to an on-disk SQLite database, closes it, reopens it, revalidates `Get()` and `GetAll()`, deletes one attribute, then reopens again to confirm the deletion persisted. Reverified with `make -C tests/db test`.
