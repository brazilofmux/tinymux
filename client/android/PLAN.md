# Android Client — Feature Roadmap

Bring the Android (Titan) client up to feature parity with the Console,
TF, and Web clients.

## Phase 1: World Manager — DONE
Save/load/edit/delete server profiles so users don't re-enter host/port
each session.

- `data/World.kt` — World data class + WorldRepository (SharedPreferences/JSON)
- World Manager dialog — list, add, edit, delete, quick-connect
- Connect dialog — optional "Save as" field
- "Worlds" toolbar button

## Phase 2: Trigger System — DONE
Pattern-match incoming text with highlight, gag, and command actions.

- `data/Trigger.kt` — Trigger data class + TriggerRepository
- `data/TriggerEngine.kt` — regex matching, gag/hilite/command actions
- Trigger Manager dialog — list, add, edit, delete, enable/disable toggle
- Edit Trigger dialog — name, regex pattern (with live validation),
  action command, priority, shot count, gag switch, hilite switch
- Wired into incoming line pipeline via `processServerLine()`
- `AnsiParser.stripAnsi()` for matching against plain text

## Phase 3: Slash Commands — DONE
Built-in `/` commands for power users.

- [x] `/connect <host> [port] [ssl]` — quick connect from input bar
- [x] `/disconnect`, `/dc` — close current connection
- [x] `/worlds` — open World Manager
- [x] `/triggers`, `/trig` — open Trigger Manager
- [x] `/def <name> <pattern> = <body>` — define trigger inline
- [x] `/undef <name>` — remove trigger
- [x] `/find <text>` — search scrollback (basic, scrolls to last match)
- [x] `/log [filename]` — toggle session logging (implemented in Phase 5)
- [x] `/clear` — clear scrollback
- [x] `/help` — list available commands

## Phase 4: Find in Scrollback — DONE
Search output history for text.

- [x] "Find" toolbar button toggles find bar
- [x] `/find [text]` command opens find bar (optionally pre-filled)
- [x] Match count display (e.g. "3/17")
- [x] Navigate between matches (up/down arrow buttons)
- [x] Auto-scroll to current match
- [x] Close button to dismiss find bar

## Phase 5: File Logging — DONE
Write session output to a file on device storage.

- [x] `data/SessionLogger.kt` — file writer with start/stop/writeLine
- [x] `/log [filename]` command to toggle logging
- [x] `[log]` indicator in status bar when active
- [x] Logs to app-private `files/logs/` directory (no permissions needed)
- [x] Auto-named files: `{worldname}_{timestamp}.log`
- [x] ANSI codes stripped from log output

## Phase 6: Keybindings — DONE (defaults)
Hardware keyboard support with sensible defaults.

- [x] Up/Down arrows — command history navigation
- [x] Ctrl+F — toggle find bar
- [x] Ctrl+L — clear scrollback
- [x] Escape — close find bar
- [x] Page Up/Down — scroll output by 20 lines
- [ ] Customizable keybinding editor (future)
- [ ] Persistent keybinding storage (future)

## Phase 7: TLS Certificate Validation — DONE
Replace trust-all with TOFU (trust-on-first-use) pinning.

- [x] `net/TofuCertStore.kt` — SHA-256 fingerprint storage in SharedPreferences
- [x] `MudConnection` — post-handshake TOFU verification via suspend callback
- [x] `CertVerifyDialog` — shows subject, issuer, SHA-256 fingerprint
- [x] First connection: "Unknown Certificate" dialog with Trust/Reject
- [x] Changed cert: "Certificate Changed!" warning with previous fingerprint
- [x] Accepted certs auto-saved; subsequent connections proceed silently
