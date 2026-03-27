# DB Backend Tests — Open Issues

Updated: 2026-03-27

## Bugs

### Standalone DB test harness no longer builds after backend file moves

- **Evidence:** `make test` in `db/` currently fails with `No rule to make target '../mux/src/sqlitedb.h', needed by 'test_sqlitedb.o'.`
- **Cause:** `db/Makefile:5-23` still points `SRCDIR` at `../mux/src` and `SQLITEDIR` at `../mux/sqlite`, but the relevant files now live in `mux/include/` and `mux/modules/engine/`.
- **Current locations:** `mux/include/sqlitedb.h`, `mux/include/sqlite_backend.h`, `mux/include/storage_backend.h`, `mux/modules/engine/sqlitedb.cpp`, and `mux/modules/engine/sqlite_backend.cpp`.
- **Impact:** The only standalone backend regression suite in the repository is effectively disabled, so SQLite backend breakage can slip through unnoticed.

## Opportunities

### Add a path-stability check for test harnesses that compile moved engine code

- **Problem:** The DB tests depend on engine implementation files outside `db/`, but the harness has no guardrail when those sources move.
- **Mitigation idea:** Centralize include/source paths in one shared make fragment or add a simple CI smoke target that verifies `make test` still resolves all backend dependencies.
