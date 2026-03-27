# Debian Packaging — Open Issues

Updated: 2026-03-27

## ~~High — Missing Build Dependencies~~ FIXED

### ~~`debian/control` does not declare all required build dependencies~~ FIXED

- Added `g++`, `ragel`, `pkg-config`, `libpcre2-dev`, `libssl-dev` to `Build-Depends`.

## Medium — Outdated Metadata

### ~~Documentation references TinyMUX 2.12~~ FIXED

- Updated all references in `README.Debian` from 2.12 to 2.14.

### ~~Standards-Version is from 2017~~ FIXED

- Updated from `4.0.1` to `4.6.2`.

## Medium — Packaging Quality

### ~~Hardcoded `--enable-stubslave` in rules~~ INTENTIONAL

- `--enable-stubslave` is correct for release/distribution packages per CLAUDE.md. Omitting it is only for local smoke testing.

### Game data files installed world-readable, no postinst permissions

- **File:** `install:6-9, 22-28`
- **Issue:** Config files (`netmux.conf`, `alias.conf`) and database files installed without restricted permissions. No `postinst` script sets proper ownership or modes.
- **Risk:** Game credentials or configuration could be readable by other users on the system.

## Low — Convert Tool Build

### `convert/configure.ac` has weak yacc/lex checking

- **File:** `../mux/convert/configure.ac:13-14`
- **Issue:** Checks for `AC_PROG_YACC` and `AC_PROG_LEX` but doesn't error if they're missing. Build will fail with an unclear message.
