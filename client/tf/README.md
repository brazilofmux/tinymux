# TitanFugue

**TitanFugue** is the terminal MU\* client shipped with TinyMUX — a modernized
fork of Ken Keys' [TinyFugue](https://tinyfugue.sourceforge.net/), the classic
programmable MUD/MUSH client. It started from the TinyFugue **5.0 beta 8**
"ICU" branch and was substantially extended for TinyMUX 2.14.

It keeps TinyFugue's programmability — macros, triggers, hooks, worlds, and the
`tf` scripting language — while adding:

- **Full Unicode / UTF-8**, grapheme-aware, via ICU.
- **Modern networking with TLS/SSL** (OpenSSL).
- **24-bit truecolor** rendering.
- IPv6.

TitanFugue is one of a family of **"Titan"** MU\* clients developed under
[`../`](..) (the repo's `client/` tree):

| Path | Client |
|------|--------|
| `client/tf`       | TitanFugue — terminal/ncurses client (this directory) |
| `client/console`  | Win32 console client |
| `client/win32gui` | Win32 GUI client |
| `client/web`      | Web client |
| `client/android`  | Android ("Titan") app |
| `client/ios`      | iOS / macOS ("Titan") app |

## Lineage and license

Derived from TinyFugue 5.0b8 (ICU branch), originally by Ken Keys.
**Licensed under the GNU GPL v2**, inherited from upstream TinyFugue.

## Building

Prerequisites (Debian/Ubuntu): `apt-get install libicu-dev libssl-dev`

```bash
./configure        # auto-detect, generate Makefiles
make all           # compile
make install       # compile + install (default prefix /usr/local or $HOME)
```

Useful `configure` options: `--enable-ssl` / `--disable-ssl`,
`--disable-widechar` (disable UTF-8/ICU), `--enable-inet6` (IPv6),
`--disable-termcap` (hardcoded vt100). See `CLAUDE.md` for the full
architecture overview and developer notes.

## Issue tracking

TitanFugue bugs and enhancements live in the main TinyMUX GitHub issue tracker,
titled `TitanFugue: …`. They were migrated there from this client's former
in-tree `client/tf/ISSUES.md`; the complete FIXED / FALSE-ALARM history is
preserved in git.
