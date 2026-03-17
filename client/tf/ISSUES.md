# TinyFugue C++ Client Review Issues

This file was rebuilt from the current `client/tf` code after reviewing the
import in commit `191a98a17473187009b9c886773957730641bceb`. The original
imported issue list was stale and no longer matched the implementation.

## Stubbed Or Partially Implemented Interfaces

- **Status bar support is still only a lightweight subset of original TF**  
  `status_fields()` and the `/status_*` commands now support dynamic fields such
  as `@world`, `@more`, `@clock`, widths, spacer fields, insertion/reset
  options, a single variable-width field per row, and multi-row layout, but
  this still lacks classic TF's richer attribute handling.  
  Comparison: `/home/sdennis/tinyfugue/src/output.c`

## Telnet Protocol Issues

- **Limited Telnet Option Support**  
  The client now handles `TTYPE`, basic `NAWS`, `BINARY`, and UTF-8
  `CHARSET` negotiation, but advanced features like MCCP compression and
  richer charset handling are still missing compared to classic TinyFugue.

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
