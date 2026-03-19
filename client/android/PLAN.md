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

## Phase 8: Timers — DONE
Repeating commands on an interval, useful for keepalive and auto-actions.

- [x] `data/TimerEngine.kt` — coroutine-based timer with name/command/interval/shots
- [x] `/repeat <name> <seconds> <command>` — create a named repeating timer
- [x] `/killtimer <name>`, `/cancel <name>` — cancel a timer
- [x] `/timers`, `/listtimers` — list active timers with interval and shots
- [x] Timer fires send command to active tab's connection
- [x] All timers auto-cancel on disconnect

## Phase 9: Hooks — DONE
Fire commands on connection events, not just text patterns.

- [x] `data/Hook.kt` — Hook data class + HookRepository with persistence
- [x] Hook events: CONNECT, DISCONNECT, ACTIVITY
- [x] `/hook <name> <event> = <command>` — define a hook
- [x] `/unhook <name>` — remove a hook
- [x] `/hooks` — list all hooks
- [x] CONNECT hooks fire after connection established (send to server)
- [x] DISCONNECT hooks fire on connection lost (logged to output)
- [x] ACTIVITY hooks fire once when background tab first receives text

## Phase 10: UX Polish — DONE
Quality-of-life improvements for daily use.

- [x] URL detection — clickable links in output (blue, underlined)
- [x] URLs open in browser via `LocalUriHandler`
- [x] Word wrap — explicit `softWrap = true` on output text
- [x] Text selection — output wrapped in `SelectionContainer`
- [x] Tab close buttons — X on each tab (except System) for quick close
- [ ] Tab reordering via long-press drag (future)
- [ ] Landscape layout improvements (future)

## Phase 11: Foreground Service
Keep connections alive when the app is backgrounded.

- [ ] Android foreground service with persistent notification
- [ ] Reconnect logic on network change
- [ ] Activity indicator in notification for background tabs
- [ ] Permissions already declared in AndroidManifest.xml

## Phase 12: Settings Screen
User-configurable preferences.

- [ ] Font size slider
- [ ] Scrollback limit
- [ ] Theme (dark/light/OLED black)
- [ ] Log directory picker
- [ ] Default port / SSL toggle
- [ ] Export/import worlds and triggers
