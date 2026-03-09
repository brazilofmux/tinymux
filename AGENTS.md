# Repository Guidelines

## Project Structure & Module Organization
- `mux/` is the main server distribution. Core code is in `mux/src`, runtime assets/config in `mux/game`, and primary docs in `mux/*.md`.
- `parser/` contains standalone parser research tools (`tokenize`, `parse`, `eval`) and a focused corpus.
- `db/` contains SQLite backend unit-style test binaries (`test_sqlitedb`, `test_backend`).
- `testcases/` stores smoke-test `.mux` scripts plus automation in `testcases/tools/`.
- `docs/` and `specs/` hold design notes and historical architecture docs.
- `client/`, `win32/`, and `muxsvc/` are client/service code paths for non-Unix targets.

## Build, Test, and Development Commands
- Main server build (from `mux/src`):
  - `./configure`
  - `make -j$(nproc)`
  - `make install` (required; creates `game/bin` symlinks)
- Deterministic/package build:
  - `DEBIAN_BUILD=1 ./configure && make && make install`
- Parser tools (from `parser/`):
  - `make` to build `tokenize`, `parse`, `eval`
  - `./eval --ast` for AST + output inspection
- DB backend tests (from `db/`):
  - `make test` runs `test_sqlitedb` and `test_backend`
- End-to-end smoke tests (from `testcases/` after server build):
  - `./tools/Makesmoke`
  - `./tools/Smoke` (results in `smoke.log`)

## Coding Style & Naming Conventions
- Languages are C/C++ (C++17 in active test/tooling Makefiles).
- Match existing file style; no repo-wide formatter config is enforced.
- Keep warnings clean under `-Wall -Wextra`.
- Use descriptive, subsystem-aware names (`sqlite_backend.*`, `parse.cpp`).
- Testcase files use lowercase snake case with suffixes like `_fn.mux`.

## Testing Guidelines
- Run targeted tests for changed areas first (`db/`, `parser/`), then run smoke tests for behavioral changes.
- Add/update `.mux` coverage in `testcases/` when changing parser/evaluator behavior.
- Name new smoke cases consistently with existing patterns (for example, `newfeature_fn.mux`).

## Commit & Pull Request Guidelines
- Recent history favors concise, imperative subjects (for example, `Fix ...`, `Add ...`, `Remove ...`).
- Keep commit titles specific to one behavior or subsystem.
- PRs should include:
  - What changed and why
  - Risk/compatibility notes (especially parser/eval behavior)
  - Exact test commands run and key results
  - Linked issue(s) when available
