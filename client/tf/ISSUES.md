# TitanFugue C++ Client — Open Issues

Updated: 2026-03-17 (commit 3916f46f7)

## Bugs

- **`substitute()` only replaces the last visual line, not the logical line**
  When a logical line is wrapped into multiple visual lines in the scrollback
  buffer, calling `substitute()` (or `replace_last_output_line`) only removes
  the very last `pop_back()` entry. Classic TF replaces the entire logical
  line regardless of wrapping.

- **`Ctrl-C` exits immediately instead of showing the interrupt menu**
  Classic TF shows an interactive menu (Continue, Exit, Disable Triggers,
  Kill Processes) on `SIGINT`. TitanFugue currently treats `SIGINT` as an
  immediate shutdown signal.

- **`Macro::compile_trigger` treats `simple` match type as `glob`**
  The `simple` match type (literal match) is not explicitly handled and
  falls through to the `glob_to_regex` compiler.

## Stubbed Or Partially Implemented Interfaces

- **`SIGTSTP` (Ctrl-Z) support is only manual via `/suspend`**
  While `/suspend` is implemented, the client does not automatically
  handle `SIGTSTP` signals. Standard Unix behavior for CLI tools is
  to suspend correctly on `Ctrl-Z`.

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

Not supported (rejected during negotiation): ISO-8859-15 (Latin-9),
CP850, KOI8-U. These differ from their near-neighbors in enough
codepoints to cause silent corruption. Proper tables could be added
to `charset.cpp` if demand arises.

## Opportunities for Improvement

- **Shell process read loop doesn't distinguish EAGAIN from errors**
  (main.cpp) When `read()` returns -1, the loop breaks without
  checking `errno`. EAGAIN is handled implicitly (poll retries next
  iteration) but an actual error leaves the fd open. Consider closing
  on non-EAGAIN errors.

## Notes

- Status bar is at parity with classic TF 5.0: variable-driven fields,
  all 7 internal fields (`@world`, `@more`, `@clock`, `@active`, `@log`,
  `@read`, `@mail`), format variables (`status_int_*`, `status_var_*`),
  attribute variables (`status_attr_int_*`, `status_attr_var_*`),
  right-justification (negative widths), reactive redraw, both word-token
  and classic single-char attribute syntax.
- TrueColor (24-bit) rendering via ncurses `init_extended_color()` with
  CIE97 perceptual-distance fallback to xterm-256 via libmux's
  `co_nearest_xterm256()`.
- MCCP v2 (telnet option 86) zlib decompression.
- Full telnet: ECHO, SGA, TTYPE, NAWS (with IAC escaping), BINARY,
  CHARSET negotiation.
