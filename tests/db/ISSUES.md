# DB Backend Tests — Open Issues

Updated: 2026-03-27

## Bugs

### ~~Standalone DB test harness no longer builds after backend file moves~~ FIXED

- Updated `db/Makefile` to use `INCDIR=../mux/include` for headers and `ENGDIR=../mux/modules/engine` for `.cpp` sources. `SQLITEDIR` unchanged.

## Opportunities

### ~~Add a path-stability check for test harnesses that compile moved engine code~~ FIXED

- `tests/db/Makefile` now has a `check-paths` target that verifies the required engine, header, and SQLite source paths before `all` or `test` builds run. If a dependency moves again, the harness now fails immediately with an actionable error instead of breaking later during compilation.
