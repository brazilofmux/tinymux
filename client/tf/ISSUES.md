# TitanFugue C++ Client — Open Issues

Updated: 2026-03-17 (commit 3916f46f7)

## Bugs

(None currently known.)

## Stubbed Or Partially Implemented Interfaces

- **`@read` depth counter only incremented by future `/read` command**
  The `status_read_depth` backing field is wired into the status bar
  but no command currently increments it. Classic TF increments it
  when a macro calls `tfread()` (nested keyboard read). TitanFugue
  would need a `/read` command or equivalent to make `@read` useful.

- **Format variable evaluation does not cache compiled expressions**
  `status_int_*` and `status_var_*` variables are evaluated via
  `expand_subs()` (supporting `%{var}` and `$[expr]` substitutions),
  but the expression is re-parsed on every status bar redraw. Classic
  TF caches the compiled program. Unlikely to matter in practice
  since redraws are infrequent.

- **Classic TF attribute codes: `E`, `W`, `I` meta-attrs not implemented**
  Both word-token format (`bold,red,bg_blue`) and classic TF single-char
  format (`BuCred`, `rCbgblue`) are supported. The `E` (error), `W`
  (warning), and `I` (info) meta-attribute codes are not implemented as
  they reference TF's configurable error/warning/info style variables.

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
