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

## Phase 3: Slash Commands
Built-in `/` commands for power users.

- [ ] `/connect <host> <port> [ssl]` — quick connect from input bar
- [ ] `/disconnect` — close current connection
- [ ] `/worlds` — open World Manager
- [ ] `/triggers` — open Trigger Manager
- [ ] `/def <name> <pattern> = <body>` — define trigger inline
- [ ] `/undef <name>` — remove trigger
- [ ] `/find <text>` — search scrollback
- [ ] `/log [filename]` — toggle session logging
- [ ] `/clear` — clear scrollback
- [ ] `/help` — list available commands

## Phase 4: Find in Scrollback
Search output history for text.

- [ ] Find toolbar button or `/find` command
- [ ] Highlight matches in scrollback
- [ ] Navigate between matches (up/down)

## Phase 5: File Logging
Write session output to a file on device storage.

- [ ] `/log` command to start/stop logging
- [ ] Log indicator in status bar
- [ ] Configurable log directory

## Phase 6: Keybindings
Customizable key mappings for common actions.

- [ ] Default bindings (volume keys for scroll, etc.)
- [ ] Keybinding editor dialog
- [ ] Persistent keybinding storage

## Phase 7: TLS Certificate Validation
Replace trust-all with proper cert checking.

- [ ] Trust-on-first-use (TOFU) certificate pinning
- [ ] Certificate fingerprint display
- [ ] Option to accept/reject unknown certs
