# Titan for iOS — Design Plan

SwiftUI client for iOS (and macOS via multiplatform). Informed by:
- The Titan Android client (feature-complete through 12 phases)
- The Atlantis source survey (`docs/atlantis-survey/`)
- The Console, TF, and Web client implementations

## Naming & Identity
- **App name**: Titan
- **Bundle ID**: org.tinymux.titan
- **Targets**: iOS 17+, macOS 14+ (multiplatform SwiftUI)

## Architecture

### Swift Package Layout
```
client/ios/
├── Titan.xcodeproj
├── Titan/
│   ├── TitanApp.swift              — @main entry, WindowGroup
│   ├── Model/
│   │   ├── World.swift             — World data model + Keychain persistence
│   │   ├── Trigger.swift           — Trigger data model + persistence
│   │   ├── Hook.swift              — Hook data model
│   │   ├── AppSettings.swift       — UserDefaults-backed settings
│   │   ├── TriggerEngine.swift     — Regex matching pipeline
│   │   └── TimerEngine.swift       — Repeating command timers
│   ├── Net/
│   │   ├── MudConnection.swift     — NWConnection-based TCP + TLS
│   │   ├── TelnetParser.swift      — Telnet IAC state machine
│   │   ├── AnsiParser.swift        — ANSI → AttributedString
│   │   └── TofuCertStore.swift     — Keychain TOFU pinning
│   ├── View/
│   │   ├── ContentView.swift       — Main layout (toolbar, tabs, output, input)
│   │   ├── OutputView.swift        — Scrollable styled text display
│   │   ├── InputBar.swift          — Text field + send button
│   │   ├── TabBar.swift            — World tabs with activity dots
│   │   ├── FindBar.swift           — Search UI with match navigation
│   │   ├── WorldManagerView.swift  — List/add/edit/delete worlds
│   │   ├── TriggerManagerView.swift— Trigger list + editor
│   │   ├── SettingsView.swift      — Preferences screen
│   │   ├── ConnectSheet.swift      — Quick connect sheet
│   │   └── CertVerifyAlert.swift   — TOFU certificate dialog
│   └── Service/
│       └── SessionLogger.swift     — File logging
└── TitanTests/
```

### Key Technology Choices

| Concern | Choice | Why |
|---------|--------|-----|
| Networking | `NWConnection` (Network.framework) | Native Apple, TCP+TLS built-in, async |
| TLS | Network.framework SecureTransport | Native, cert access for TOFU |
| UI | SwiftUI | Multiplatform, modern, declarative |
| Text rendering | `AttributedString` + `Text` | Native SwiftUI styled text |
| Persistence | `UserDefaults` + Keychain | Keychain for credentials, UD for prefs |
| Concurrency | Swift async/await + actors | Natural fit for networking |
| Regex | Swift `Regex` | Built-in, type-safe |

### Architectural Patterns

**MVVM with ObservableObject:**
- `WorldTab` — ObservableObject per connection tab
- `AppState` — @Observable singleton holding tabs, active tab, settings
- Views observe state and re-render

**Actor-based networking:**
- `MudConnection` as an actor — safe concurrent access
- Telnet/ANSI parsers are value types (structs)

**Protocol-oriented models:**
- `Persistable` protocol for JSON encode/decode
- `Filterable` protocol for the text processing pipeline

## Phase Plan

Mirrors Android phases but adapted to iOS/SwiftUI idioms.

### Phase 1: Project Skeleton + Connection
Minimum viable: connect to a MUD, see output, type input.

- [ ] Xcode project with SwiftUI multiplatform target
- [ ] `MudConnection` — NWConnection TCP, async read loop
- [ ] `TelnetParser` — IAC state machine (port from Android)
- [ ] `AnsiParser` — ANSI → AttributedString (port from Android)
- [ ] `ContentView` — output scroll + input bar
- [ ] Basic connect dialog (host/port/SSL)

### Phase 2: Multi-Tab + World Manager
- [ ] `WorldTab` model — per-connection state
- [ ] Tab bar with activity indicators
- [ ] `World` model with Keychain persistence
- [ ] World Manager view (list, add, edit, delete, connect)
- [ ] Auto-login commands per world
- [ ] TOFU certificate pinning via `sec_protocol_options`

### Phase 3: Triggers + Commands
- [ ] `Trigger` model + `TriggerEngine`
- [ ] Gag, highlight, command execution
- [ ] Trigger Manager view
- [ ] Slash command dispatcher
- [ ] All commands: /connect, /dc, /worlds, /triggers, /def, /undef,
      /find, /log, /clear, /help, /repeat, /killtimer, /timers,
      /hook, /unhook, /hooks

### Phase 4: Find, Logging, Timers, Hooks
- [ ] Find bar with match count + navigation
- [ ] `SessionLogger` — write to app documents
- [ ] `TimerEngine` — Task-based repeating timers
- [ ] `Hook` model + fire on CONNECT/DISCONNECT/ACTIVITY

### Phase 5: Settings + Polish
- [ ] Settings view (font size, scrollback, defaults)
- [ ] URL detection + clickable links
- [ ] Text selection in output
- [ ] Tab close buttons
- [ ] Keyboard shortcuts (Cmd+F find, arrow history, etc.)
- [ ] Keep screen on (UIApplication.shared.isIdleTimerDisabled)

### Phase 6: Background + Notifications
- [ ] Background URLSession or BGTaskScheduler for connection keep-alive
- [ ] Local notifications for activity on background tabs
- [ ] App badge for unread activity count

## Atlantis-Inspired Features (Future)

Features from the Atlantis survey worth bringing to Titan eventually:

### Spawn System (output routing)
Atlantis's most distinctive feature. Pattern-matched sub-tabs that
route specific output (combat, tells, channels) to separate views.
Could be very powerful on iPad with split view.

### Composite Conditions
AND/OR/NOT grouping for trigger conditions. More expressive than
our current simple matching.

### Variable Namespace
Structured state accessible from triggers and commands:
`world.name`, `event.line`, `regexp.1`, `datetime.time`, etc.

### Rich Action Types
Beyond gag/hilite/command — text-to-speech, sound, notifications,
substitute, line classification, spawn routing.

### MCP Protocol
Remote text editing from the MUD server. Would need an editor view.

## Platform-Specific Considerations

### iOS vs Android Differences
| Feature | Android | iOS |
|---------|---------|-----|
| Background connections | Foreground Service | BGTaskScheduler / limited |
| Credential storage | EncryptedSharedPreferences | Keychain (native, better) |
| TLS | Java SSL + TOFU | Network.framework + TOFU |
| Notifications | NotificationChannel | UNUserNotificationCenter |
| Text rendering | AnnotatedString (Compose) | AttributedString (SwiftUI) |
| Keyboard handling | onPreviewKeyEvent | .onKeyPress (iOS 17+) |
| Keep screen on | FLAG_KEEP_SCREEN_ON | isIdleTimerDisabled |

### iPad Optimizations
- Split view: world list + output side by side
- Keyboard shortcuts via `.keyboardShortcut()` modifier
- Pointer/trackpad support for text selection
- Stage Manager compatibility

### macOS (via Catalyst/multiplatform)
- Menu bar integration
- Multiple windows
- Native keyboard shortcuts (Cmd+C, Cmd+F, etc.)
- Dock badge for activity

## Development Workflow

Since we develop blind (no Mac):
1. Write Swift/SwiftUI code in `client/ios/`
2. Push to brazil branch
3. Rachel (or any Mac user) opens in Xcode, builds, tests
4. Feedback loop via screenshots and descriptions
5. Iterate

Swift on Windows can syntax-check pure Swift (not SwiftUI), useful
for Model/ and Net/ code.
