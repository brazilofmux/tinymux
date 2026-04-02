# DB Backend Tests — Open Issues

Updated: 2026-03-27

## Bugs

### ~~Standalone DB test harness no longer builds after backend file moves~~ FIXED

- Updated `db/Makefile` to use `INCDIR=../mux/include` for headers and `ENGDIR=../mux/modules/engine` for `.cpp` sources. `SQLITEDIR` unchanged.

## Opportunities

### Add a path-stability check for test harnesses that compile moved engine code

- **Problem:** The DB tests depend on engine implementation files outside `db/`, but the harness has no guardrail when those sources move.
- **Mitigation idea:** Centralize include/source paths in one shared make fragment or add a simple CI smoke target that verifies `make test` still resolves all backend dependencies.
