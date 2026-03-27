# Debian Packaging — Open Issues

Updated: 2026-03-27

## High — Missing Build Dependencies

### `debian/control` does not declare all required build dependencies

- **File:** `control:5`
- **Issue:** Missing explicit `Build-Depends` for:
  - `ragel` (needed for `.rl` file compilation)
  - `pkg-config` (used in `configure.ac` for `PKG_CHECK_MODULES`)
  - `libpcre2-dev` (mandatory — `configure.ac:239-241` hard-errors without it)
  - `libssl-dev` (checked at `configure.ac:243-244`)
- **Impact:** Package build fails on a clean system without these pre-installed.

## Medium — Outdated Metadata

### Documentation references TinyMUX 2.12

- **File:** `README.Debian:15, 23`
- **Issue:** Text still mentions TinyMUX 2.12. Should reference 2.14.

### Standards-Version is from 2017

- **File:** `control:6`
- **Issue:** `Standards-Version: 4.0.1` — current Debian policy is 4.7+.

## Medium — Packaging Quality

### Hardcoded `--enable-stubslave` in rules

- **File:** `rules:8-9`
- **Issue:** Configure called with `--enable-stubslave` always. No option to build without it or with other feature flags.

### Game data files installed world-readable, no postinst permissions

- **File:** `install:6-9, 22-28`
- **Issue:** Config files (`netmux.conf`, `alias.conf`) and database files installed without restricted permissions. No `postinst` script sets proper ownership or modes.
- **Risk:** Game credentials or configuration could be readable by other users on the system.

## Low — Convert Tool Build

### `convert/configure.ac` has weak yacc/lex checking

- **File:** `../mux/convert/configure.ac:13-14`
- **Issue:** Checks for `AC_PROG_YACC` and `AC_PROG_LEX` but doesn't error if they're missing. Build will fail with an unclear message.
