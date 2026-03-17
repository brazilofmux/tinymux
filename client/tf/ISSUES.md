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

## Telnet Protocol Issues

- **Limited Telnet Option Support**  
  Advanced features like MCCP (compression), CHARSET negotiation, and
  proper subnegotiation of window size (NAWS is implemented, but basic)
   are missing compared to classic TinyFugue.

## Performance and Responsiveness

- **Select() is used instead of Epoll/Poll**  
  While acceptable for a few connections, `select()` is less efficient than
  `poll()` or `epoll()` for modern high-concurrency or long-running clients.
  (Minor, given the typical connection count).

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
