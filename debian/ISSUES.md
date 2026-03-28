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

### ~~Game data files installed world-readable, no postinst permissions~~ FIXED

- Added `debian/postinst` that restricts config files (`netmux.conf`, `mux.config`, `muxssl.conf`, `alias.conf`, `compat.conf`) to mode 640 on configure.

## Low — Convert Tool Build

### ~~`convert/configure.ac` has weak yacc/lex checking~~ FIXED

- Added `AC_MSG_ERROR` if yacc/bison or lex/flex are not found. Also fixed `AC_CHECK_LIB` to check for `yywrap` instead of `main`.
