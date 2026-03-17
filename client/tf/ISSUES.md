# Known Issues and Gaps

## Bugs

- ~~**Partial UTF-8 from network**: Fixed — incomplete trailing UTF-8 sequences are held back in the line accumulator.~~
- ~~**NAWS not sent on resize**: Fixed — SIGWINCH sends NAWS update to all connections with actual terminal dimensions. Initial connect uses real size instead of hardcoded 80x24.~~
- **Output wrap is snapshot**: Lines are wrapped at the column width when printed. Terminal resize does not re-wrap existing scrollback.

## Input

- No word-movement (Ctrl-Left/Right)
- No kill ring / yank (Ctrl-W, Ctrl-Y)
- No tab completion (world names, commands)

## Connection

- No auto-reconnect on disconnect
- No connect timeout feedback (hangs silently on firewalled ports)
- No MCCP (MUD Client Compression Protocol / zlib)

## Output

- No text search in scrollback
- No timestamps on lines
- No logging to file (`/log`)

## World Management

- No `/addworld` at runtime — worlds can only be loaded from file
- No activity notification per background world (status bar shows `+N bg` but not which world has new text)

## Session

- No `/repeat` or timers (needed for idler scripts)
- No per-world input history
- No password masking on login

## Scripting

None of TF's scripting language is implemented:

- **Macros/triggers**: No `/def`, patterns, priorities, attributes, `-t` triggers, `-h` hooks
- **Control flow**: No `/if`, `/while`, `/test`, `/let`, `/set` (as scripting keyword), `/result`, `/return`
- **Expression evaluator**: No `$[...]` expressions, no `$()` command substitution
- **Substitution syntax**: No `%{var}`, `%{1}` positional args, `%{*}`, `%{?}`
- **Built-in functions**: 0 of 96 (e.g., `strlen`, `substr`, `regmatch`, `world_info`, `send`, `echo`)
- **Built-in variables**: 0 of 100+ typed variables (e.g., `visual`, `wrap`, `more`, `login`)
- **Hooks**: 0 of 33 event hooks (e.g., CONNECT, DISCONNECT, LOGIN, SEND, PROMPT, RESIZE)
- **Key bindings**: Hardcoded — no `/bind`, `/unbind`, `/dokey`
- **Commands**: 9 of 61 implemented (`/connect`, `/dc`, `/fg`, `/quit`, `/set`, `/load`, `/recall`, `/world`, `/help`)
