# TitanFugue C++ Client — Open Issues

Updated: 2026-03-24

## Bugs

All previously reported bugs have been resolved:

- ~~`substitute()` only replaces last visual line~~ — Fixed: `replace_last_output_line` tracks `last_logical_line_count` and pops all visual lines of the logical line.
- ~~`Ctrl-C` exits immediately~~ — Fixed: SIGINT handler sets flag, event loop prints "Interrupt: /quit to exit" instead of exiting.
- ~~`compile_trigger` treats `simple` as `glob`~~ — Fixed: `simple` and `substr` both call `regex_escape()` for literal matching.

## Newly Confirmed Bugs

- **Hydra reconnect drops capability negotiation**
  `client/tf/src/hydra_connection.cpp:135-143` sends `SetPreferences` on the initial stream open, but `client/tf/src/hydra_connection.cpp:764-767` recreates the `GameSession` stream during reconnect without resending preferences. The server can therefore lose terminal size, terminal type, and preferred color format after reconnect.

- **Initial Hydra viewport is still hardcoded to `80x24`**
  `client/tf/src/hydra_connection.cpp:140-141` still sends `terminal_width=80` and `terminal_height=24` on first connect. A later resize path exists, but the initial capability advertisement is still wrong until a resize occurs.

## Stubbed Or Partially Implemented Interfaces

- ~~**Lack of multi-key binding support**~~
  **Fixed:** `KeyBindings` uses a trie (`SeqTrieNode`) for multi-key
  sequence matching. `parse_key_sequence()` handles `^X^F`, `Meta-a`,
  `Esc a`, and mixed formats. Timeout-based disambiguation in event loop.

- **Nested keyboard read (`tfread()`) is missing**
  The `tfread()` scripting function only supports reading from file handles.
  Classic TF's `tfread()` can also read from the keyboard, allowing macros
  to pause for user input.

- **`read()` scripting function is still a stub**
  `client/tf/src/script_parse.cpp:890-892` always returns an empty string and explicitly labels the implementation as a stub. Scripts that expect interactive input currently fail silently.

- **`@read` depth counter only incremented by future `/read` command**
  The `status_read_depth` backing field is wired into the status bar
  but no command currently increments it. TitanFugue would need a
  `/read` command or nested `tfread()` to make `@read` useful.

- **Format variable evaluation does not cache compiled expressions**
  `status_int_*` and `status_var_*` variables are re-parsed on every
  status bar redraw. Unlikely to matter in practice since redraws are
  infrequent.

- **`nlog` status/function support is still a stub**
  `client/tf/src/script_parse.cpp:1107-1109` hardcodes `nlog()` to `0`, so classic TF scripts that use log-line counts for status or automation cannot behave correctly.

- ~~**Classic TF attribute codes: `E`, `W`, `I` meta-attrs not implemented**~~
  **Fixed:** `E`/`W`/`I` expand from `error_attr`/`warning_attr`/`info_attr`
  variables. Defaults: bold red, bold yellow, cyan. Word-token equivalents:
  `error`, `warning`, `info`. Users can override via `/set error_attr=...`.

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

## Notes on Hydra Reconnect

Both newly confirmed bugs above share a common fix pattern: cache the actual terminal dimensions and color format in the `HydraConnection` object, then use them in both the initial connect and reconnect paths.

## Notes

- Status bar is at parity with classic TF 5.0.
- TrueColor (24-bit) rendering via ncurses with CIE97 perceptual fallback.
- MCCP v2 (telnet option 86) zlib decompression.
- Full telnet: ECHO, SGA, TTYPE, NAWS, BINARY, CHARSET negotiation.
