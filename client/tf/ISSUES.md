# TinyFugue C++ Client Review Issues

This file was rebuilt from the current `client/tf` code after reviewing the
import in commit `191a98a17473187009b9c886773957730641bceb`. Updated after
code review of commits `6ceb3ecf..HEAD` (2026-03-17).

## Bugs

- **NAWS subnegotiation missing IAC escaping** (connection.cpp:498-506)
  `send_subneg_naws()` writes width/height bytes raw into the telnet stream.
  RFC 1073 requires 0xFF bytes inside SB..SE to be escaped as 0xFF 0xFF.
  A terminal width of 255 or 511 would produce an unescaped IAC that
  terminates the subnegotiation prematurely.

- **Dead code after SSL/read branches** (connection.cpp:278-283)
  The `if (n <= 0)` block at line 278 is unreachable. Both the SSL and
  non-SSL branches already return on `n <= 0`. Harmless but confusing;
  should be deleted.

- **IAC reserve() undersized in send_line()** (connection.cpp:236)
  `data.reserve(line.size() + 2)` does not account for IAC doubling.
  If the line contains k IAC bytes, the actual size is
  `line.size() + k + 2`. Not a correctness bug (std::string grows
  automatically) but causes unnecessary reallocations.

## Stubbed Or Partially Implemented Interfaces

- **Status bar support is still only a lightweight subset of original TF**
  `status_fields()` and the `/status_*` commands now support dynamic fields
  such as `@world`, `@more`, `@clock`, widths, spacer fields,
  insertion/reset options, a single variable-width field per row, multi-row
  layout, and simple per-field attributes, but this still lacks classic TF's
  variable-driven `status_attr_*` / `status_int_*` formatting model.

- **Status bar attributes limited to bold/underline/reverse**
  (terminal.cpp:740-774) Classic TF allows arbitrary color/attribute
  combinations. Consider extending to support fg/bg color names or
  numeric ANSI codes.

## Telnet Protocol Issues

- **Limited Telnet Option Support**
  The client now handles `TTYPE`, basic `NAWS`, `BINARY`, and UTF-8
  `CHARSET` negotiation, but advanced features like MCCP compression and
  richer charset handling are still missing compared to classic TinyFugue.

- **CHARSET parsing loop off-by-one** (connection.cpp:438)
  `while (start <= sb_buf_.size())` allows `start == size()`. The
  `substr(start, ...)` call is safe in C++ (returns empty string) but
  the condition should be `<` for clarity.

## Portability Issues

- **`localtime_r()` is POSIX-only** (terminal.cpp:718)
  The `@clock` status field uses `localtime_r()` which is unavailable
  on MSVC. Consider `std::localtime()` with a null check, or
  `std::chrono` facilities.

## Opportunities for Improvement

- **TrueColor (24-bit) support**
  The current renderer converts RGB to xterm-256. Modern terminals support
  24-bit color, and the engine should support `ESC[38;2;R;G;Bm` sequences.

- **Shell process read loop doesn't distinguish EAGAIN from errors**
  (main.cpp:448-460) When `read()` returns -1, the loop breaks without
  checking `errno`. EAGAIN is handled implicitly (poll retries next
  iteration) but an actual error leaves the fd open. Consider closing
  on non-EAGAIN errors.

- **Vector overhead in telnet subnegotiation helpers**
  (connection.cpp:470-506) `send_subneg_ttype()` and
  `send_subneg_charset()` use `std::vector<uint8_t>` for small
  fixed-size buffers. Stack arrays would avoid heap allocation.

## Notes

- The codebase now implements far more than the original imported issue list
  claimed: `/def`, hooks, triggers, `/repeat`, `/bind`, many expression
  builtins, `/log`, `/save`, `/saveworld`, `/limit`, `/eval`, `/quote`, and
  more all exist in some form.
- The main gaps are no longer "feature absent" so much as "feature present
  but semantics incomplete, placeholder, or unlike TinyFugue."
