# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

TinyFugue (tf) is a programmable MUD (Multi-User Dungeon) client written in C. This is the ICU branch, which adds Unicode/UTF-8 support via the ICU library. Version 5.0 beta 8, originally by Ken Keys. Licensed under GPLv2.

The source was distributed as `tinyfugue-icu.zip` and extracted in-place.

## Build Commands

```bash
# Prerequisites (Debian/Ubuntu): apt-get install libicu-dev libssl-dev
./configure            # Auto-detect system, generate Makefiles
make all               # Compile without installing
make install           # Compile and install (default prefix: /usr/local or $HOME)
make clean             # Remove object files
make uninstall         # Remove installed files
```

Key configure options:
- `--enable-ssl` / `--disable-ssl` — TLS support (OpenSSL)
- `--disable-widechar` — disable UTF-8/ICU support
- `--enable-inet6` — IPv6 support
- `--disable-termcap` — use hardcoded vt100 codes
- `--enable-core` — enable core dumps and debug symbols (-g), disable optimization
- `--enable-development` — development build
- `--with-inclibpfx=DIR` — add DIR/include and DIR/lib to search paths

There is no test suite.

## Architecture

### Event Loop

`socket.c:main_loop()` is the central event dispatcher. It uses `select()` to multiplex between user terminal input, network sockets (multiple simultaneous MUD connections), and process I/O.

### Core Module Relationships

```
main.c          — Entry point: arg parsing, config file loading, calls main_loop()
  socket.c      — Event loop, network I/O, manages Sock/World connections
    keyboard.c  — Terminal input handling, key bindings
    expand.c    — Macro expansion, TF scripting language interpreter
      macro.c   — Macro/trigger/hook storage, matching, execution
      expr.c    — Expression evaluator (math, strings, function calls)
      command.c — Built-in command dispatch table
    output.c    — Screen rendering, text wrapping, scrollback, status bar
      attr.c    — Text attributes (bold, underline, colors, ANSI codes)
    history.c   — Command history (/recall) and session logging
  variable.c    — Global variable system with typed values
  world.c       — World (MUD server) definitions and management
  tfio.c        — File I/O abstraction (TFILE), output buffering (Screen)
  process.c     — External process execution (/quote, /repeat)
  signals.c     — Signal handling
  tty.c         — Terminal setup/teardown
```

### Key Data Structures

- **`String` / `conString`** (`dstring.h`) — Reference-counted dynamic strings with per-character attributes and timestamps. Most text flows through these.
- **`Value`** (`globals.h`) — Tagged union for all data types (STR, INT, FLOAT, TIME, REGEX, FUNC, etc.). The TF scripting type system.
- **`World`** — MUD server definition (host, port, credentials, associated macros/screen/history).
- **`Sock`** — Active network socket with buffers, linked to a World.
- **`Macro`** — Trigger, hook, or key binding: pattern + body + priority + attributes.
- **`Screen`** — Output buffer with physical/logical lines, filtering, scrollback.

### Bundled Dependencies

- **PCRE 2.08** in `src/pcre-2.08/` — Perl-compatible regular expressions, compiled as part of the build.

### Platform Abstraction

- `src/port.h` — Platform-specific includes
- `src/tfconfig.h` (generated) — Feature detection results from configure
- Platform directories: `unix/`, `os2/`, `macos/`, `win32/`

## Key Files

- `src/cmdlist.h` — Built-in command table (name → handler mappings)
- `src/hooklist.h` — Hook event definitions
- `src/funclist.h` — Built-in function table for expressions
- `src/varlist.h` — Built-in variable definitions
- `src/enumlist.h` — Enumerated type definitions
- `src/vars.mak` — Version number (TFVER) and source file list
- `tf-lib/stdlib.tf` — Standard TF macro library loaded at startup
- `tf-lib/tfrc` — Default user configuration

## Conventions

- Memory management uses custom wrappers (`malloc.h`) with file/line tracking for debugging.
- Strings are reference-counted via the `links` field; use `Stringdup`/`Stringfree`.
- Naming: `sg_` prefix for static globals; `_` suffix sometimes used for internal functions.
- Header files use function-level include guards, not file-level.
- The configure system generates `src/tfconfig.h` and `src/tfdefs.h` from `.in` templates.

## Security Note

`tiny.world` contains plaintext MUD credentials — do not commit or share.
