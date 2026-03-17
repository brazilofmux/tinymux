# TinyFugue C++ Client Review Issues

This file was rebuilt from the current `client/tf` code after reviewing the
import in commit `191a98a17473187009b9c886773957730641bceb`. The original
imported issue list was stale and no longer matched the implementation.

## Stubbed Or Partially Implemented Interfaces

- **Status bar support is still only a lightweight subset of original TF**  
  `status_fields()` and the `/status_*` commands now support dynamic fields such
  as `@world`, `@more`, `@clock`, widths, and spacer fields, but this still
  lacks classic TF's richer option parsing, attributes, and multi-row layout.  
  Comparison: `/home/sdennis/tinyfugue/src/output.c`

## Unicode and Internationalization

- **Scripting functions are not Unicode-aware**  
  Functions like `strlen()`, `substr()`, `strchr()`, `tolower()`, and `toupper()`
  operate on raw bytes. `strlen()` returns byte count instead of character or
  grapheme count, and `substr()` can split multi-byte UTF-8 sequences, creating
  invalid strings. These should be updated to use the `./utf` and
  `./ragel/color_ops.h` (libmux) machinery already available in the project.  
  Refs: `client/tf/src/script_parse.cpp:446` (Built-in functions)

- **`std::regex` used instead of PCRE2**  
  Triggers and `regmatch()` use `std::regex`, which has poor UTF-8 support and
  is generally slower than PCRE2. TinyMUX already includes PCRE2, which should
  be utilized here.  
  Refs: `client/tf/src/script_parse.cpp:525`, `client/tf/src/main.cpp:255`

## Telnet Protocol Issues

- **Hardcoded Terminal Type (TTYPE)**  
  The terminal type is hardcoded to `xterm-256color` during negotiation,
  ignoring the user's `$TERM` environment variable or TF settings.  
  Refs: `client/tf/src/connection.cpp:337`

- **Limited Telnet Option Support**  
  Advanced features like MCCP (compression), CHARSET negotiation, and
  proper subnegotiation of window size (NAWS is implemented, but basic)
   are missing compared to classic TinyFugue.

## Performance and Responsiveness

- **Synchronous shell execution blocks the UI**  
  `/sh`, `/sys`, and `/quote !` use `popen()`, which is synchronous. The
  entire client will hang and stop responding to network data or user input
  until the external command completes.  
  Refs: `client/tf/src/command.cpp:557`, `client/tf/src/command.cpp:613`

- **Select() is used instead of Epoll/Poll**  
  While acceptable for a few connections, `select()` is less efficient than
  `poll()` or `epoll()` for modern high-concurrency or long-running clients.
  (Minor, given the typical connection count).

- **No support for local variables**  
  `/let` always modifies global variables in `app.vars`, even when used inside
  a macro. Classic TinyFugue's `/let` creates a local variable that shadows
  globals and is destroyed when the macro exits. The current implementation
  lacks a scope stack.  
  Refs: `client/tf/src/command.cpp:468`, `client/tf/src/script.h:182`

## Opportunities for Improvement

- **TrueColor (24-bit) support**  
  The current renderer converts RGB to xterm-256. Modern terminals support
  24-bit color, and the engine should support `ESC[38;2;R;G;Bm` sequences.

## Notes

- The codebase now implements far more than the original imported issue list
  claimed: `/def`, hooks, triggers, `/repeat`, `/bind`, many expression
  builtins, `/log`, `/save`, `/saveworld`, `/limit`, `/eval`, `/quote`, and
  more all exist in some form.
- The main gaps are no longer "feature absent" so much as "feature present but
  semantics incomplete, placeholder, or unlike TinyFugue."
