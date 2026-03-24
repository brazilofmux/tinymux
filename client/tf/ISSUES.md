# TitanFugue C++ Client — Open Issues

Updated: 2026-03-24

## Bugs

All previously reported bugs have been resolved:

- ~~`substitute()` only replaces last visual line~~ — Fixed: `replace_last_output_line` tracks `last_logical_line_count` and pops all visual lines of the logical line.
- ~~`Ctrl-C` exits immediately~~ — Fixed: SIGINT handler sets flag, event loop prints "Interrupt: /quit to exit" instead of exiting.
- ~~`compile_trigger` treats `simple` as `glob`~~ — Fixed: `simple` and `substr` both call `regex_escape()` for literal matching.

## Stubbed Or Partially Implemented Interfaces

- **Lack of multi-key binding support**
  `KeyBindings` and `InputLexer` only support single-event bindings. Classic
  TF supports multi-key sequences such as `Meta-a` (Esc a) or `^X^F`.

- **Nested keyboard read (`tfread()`) is missing**
  The `tfread()` scripting function only supports reading from file handles.
  Classic TF's `tfread()` can also read from the keyboard, allowing macros
  to pause for user input.

- **`@read` depth counter only incremented by future `/read` command**
  The `status_read_depth` backing field is wired into the status bar
  but no command currently increments it. TitanFugue would need a
  `/read` command or nested `tfread()` to make `@read` useful.

- **Format variable evaluation does not cache compiled expressions**
  `status_int_*` and `status_var_*` variables are re-parsed on every
  status bar redraw. Unlikely to matter in practice since redraws are
  infrequent.

- **Classic TF attribute codes: `E`, `W`, `I` meta-attrs not implemented**
  The `E` (error), `W` (warning), and `I` (info) meta-attribute codes
  are not implemented as they reference TF's configurable style variables.

## Previously Reported — Now Resolved

- ~~`SIGTSTP` (Ctrl-Z) support~~ — Implemented: proper shutdown/raise/reinit cycle with handler reinstall on resume.
- ~~Shell process read loop EAGAIN~~ — Fixed: non-blocking read now distinguishes EAGAIN from real errors, closes fd on non-EAGAIN failures.

## Charset Support

Supported charsets (bidirectional, negotiated via telnet CHARSET option 42):

| Charset | Aliases | Use case |
|---------|---------|----------|
| UTF-8 | utf8 | Modern MUDs |
| US-ASCII | ascii | 7-bit only |
| ISO-8859-1 | latin1 | Western European MUDs |
| CP437 | ibm437 | FANSI art, DOS-era MUDs |
| Windows-1252 | cp1252, win1252 | Smart quotes, web text |
| KOI8-R | | Russian Cyrillic MUDs |

## Notes

- Status bar is at parity with classic TF 5.0.
- TrueColor (24-bit) rendering via ncurses with CIE97 perceptual fallback.
- MCCP v2 (telnet option 86) zlib decompression.
- Full telnet: ECHO, SGA, TTYPE, NAWS, BINARY, CHARSET negotiation.
