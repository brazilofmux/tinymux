# TitanFugue C++ Client — Open Issues

Updated: 2026-03-29

## Bugs

All previously reported bugs have been resolved:

- ~~`substitute()` only replaces last visual line~~ — Fixed: `replace_last_output_line` tracks `last_logical_line_count` and pops all visual lines of the logical line.
- ~~`Ctrl-C` exits immediately~~ — Fixed: SIGINT handler sets flag, event loop prints "Interrupt: /quit to exit" instead of exiting.
- ~~`compile_trigger` treats `simple` as `glob`~~ — Fixed: `simple` and `substr` both call `regex_escape()` for literal matching.

## ~~Newly Confirmed Bugs~~ FIXED

- ~~**Hydra reconnect drops capability negotiation**~~ FIXED — `sendPreferences()` is now called after stream reopen in `attemptReconnect()`, resending color format, terminal size, and terminal type.

- ~~**Initial Hydra viewport is still hardcoded to `80x24`**~~ FIXED — Terminal dimensions are cached in `termWidth_`/`termHeight_` instance variables, updated by `send_naws()` (called on SIGWINCH). Both `openStream()` and `attemptReconnect()` use the cached values. The initial 80x24 default still applies until the first `send_naws()` call, which happens immediately after connect from `main.cpp`.

- ~~**Hydra-enabled builds fail from source**~~ FIXED — `client/tf/CMakeLists.txt` now points protobuf generation at `mux/proxy/hydra.proto` instead of a nonexistent top-level `proxy/hydra.proto`. Verified with `cmake -S client/tf -B /tmp/tf-build-hydra -DHYDRA_GRPC=ON` and `cmake --build /tmp/tf-build-hydra -j2`.

- ~~**Hydra stream output ignored line framing and EOR prompts**~~ FIXED — `HydraConnection` now buffers `GameOutput.text`, emits only complete newline-terminated lines to the UI, preserves partial text, and treats `GameOutput.end_of_record` as a prompt boundary for `check_prompt()`/`current_prompt()`.

- ~~**`/hrestart` only matched when followed by a space**~~ FIXED — `HydraConnection::send_line()` now recognizes bare `/hrestart` and routes it to the existing usage/help path instead of forwarding it to the active game.

## Newly Identified Bugs (2026-04-04)

- ~~**Hydra reconnect path still races on `grpc_` ownership**~~ FIXED — TF Hydra transport now keeps gRPC/channel state in shared snapshots guarded by mutexes, uses a dedicated stream snapshot for the reader thread, and serializes stream writes so disconnect/reconnect no longer race on raw `grpc_` ownership.

- ~~**`/update` builds a shell command from unquoted user input**~~ FIXED — `/update` now runs `git pull` and `cmake --build` via direct `execvp` argument vectors in controlled working directories, removing the `sh -c` injection path for branch names and repo paths.

## Newly Identified Bugs (2026-04-04) — Continued

- ~~**Incomplete gRPC channel error handling**~~ FIXED — TF Hydra connect/reconnect now validates channel and stub creation explicitly and emits targeted diagnostics when transport state is missing during stream open or reconnect.

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

- ~~**`nlog` status/function support is still a stub**~~
  **Fixed:** `nlog()` now returns the active connection's successful per-world log-line count. The counter resets when logging starts or stops and increments only on actual writes.

- ~~**Redundant per-world logging overrides in `Connection`**~~
  **Fixed:** Removed the duplicate `start_log`, `stop_log`, and `log_line` overrides from `Connection`; TF now uses the shared `IConnection` logging implementation directly.

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
