# Atlantis Architecture Overview

## Application Structure

Atlantis is a Cocoa/AppKit Objective-C application using the classic
NSApplication → NSWindowController → NSView hierarchy. It does **not**
use NSDocument.

### Entry Point
- `main.m`: Standard `NSApplicationMain()` call
- `RDAtlantisApplication` (NSApplication subclass): Intercepts keyboard
  events for global shortcuts before dispatch to responder chain

### Singleton Controller
`RDAtlantisMainController` is the central hub (singleton via `+controller`):

- **Command registry** — `_rdCommands` dict maps command text → BaseCommand
- **World management** — `_rdConnectedWorlds` dict of active RDAtlantisWorldInstance
- **Plugin registries** — action classes, condition classes, filter classes,
  log types, uploader types, configuration tabs, toolbar options
- **Global events** — hotkeys, toolbar events, aliases, highlights (fallback
  when world-specific not defined)
- **Scripting dispatch** — routes script calls to Perl/Lua engines
- **MCP dispatch** — routes MCP messages to handlers
- **Preferences** — `_rdGlobalWorld` (app-wide defaults), session state

### Data Flow: Server → Display

```
Raw TCP bytes (ALSocket / NetSocket)
    │
    ▼
RDTelnetFilter — IAC parsing, option negotiation, keepalive
    │
    ▼
RDCompressionFilter — MCCP v2 zlib decompression
    │
    ▼
RDMCPFilter — extract MCP #$# packets
    │
    ▼
RDAnsiFilter — ANSI escape codes → NSAttributedString styles
    │
    ▼
RDURLFilter / RDEmailFilter — detect and linkify URLs/emails
    │
    ▼
Event System — pattern matching, gag, highlight, actions
    │
    ▼
Spawn Routing — pattern match → route to appropriate spawn tab
    │
    ▼
RDTextView (Lemuria) — display styled text with scrollback
    │
    ▼
Log System — write to log files (plain, formatted, HTML)
```

### Data Flow: User Input → Server

```
Keyboard input in RDMUSHTextView (spawn input view)
    │
    ▼
RDAtlantisWorldInstance.handleLocalInput:onSpawn:
    │
    ├─ Starts with "/"? → Look up in command registry → execute
    │
    ├─ Alias matching? → expand alias → re-process
    │
    └─ Normal text → sendString: → encode → write to socket
```

## Key Design Patterns

### Filter Chain
Modular filters compose into input/output pipelines. Each filter
inherits from `RDAtlantisFilter` and implements `filterInput:` /
`filterOutput:`. Filters receive a reference to the world instance.

### Plugin Architecture (Objective-C Runtime)
Actions, conditions, commands, log types, and toolbar items all use
dynamic class registration. The runtime inspects classes for required
methods and registers them in dictionaries. New behaviors are added
by subclassing and registering — no core changes needed.

### Event-Driven Automation
Everything automatable is a `BaseEvent` subclass:
- `WorldEvent` / `ComplexEvent` — text triggers
- `HotkeyEvent` — keyboard shortcuts
- `ToolbarUserEvent` — toolbar buttons
- `AliasEvent` — command aliases
- `ScriptEventAction` — scripted handlers

All share the same condition→action pipeline.

### Spawn System (Output Routing)
Each world has a main spawn plus optional sub-spawns. Spawns have
pattern lists (regex/glob). When a line arrives, each spawn's patterns
are checked — matching lines route to that spawn's display. This lets
users split combat, chat, pages, etc. into separate tabs.

### Dual Preference Scopes
World preferences have two layers:
- `_rdWorldPreferences` — world-level defaults
- `_rdCharacterPreferences` — per-character overrides

This allows one world definition to serve multiple characters with
different settings.

## Dependencies

| Framework | Purpose | Replaceable? |
|-----------|---------|-------------|
| Lemuria | Text views, nested views, tabs | Yes — SwiftUI native |
| OgreKit | Oniguruma regex | Yes — Swift Regex / NSRegularExpression |
| Sparkle | Auto-update | Yes — TestFlight / App Store |
| Growl | Notifications | Yes — UserNotifications |
| PSMTabBarControl | Tab bar | Yes — SwiftUI TabView |
| RBSplitView | Split panes | Yes — SwiftUI HSplitView/VSplitView |

## File Formats

| Format | Usage | Location |
|--------|-------|----------|
| .awd | World definitions | ~/Library/Application Support/Atlantis/worlds/ |
| .axworld | World export (XML) | User-shared |
| NSKeyedArchiver | Events, hotkeys, toolbar | Preferences directory |
| AML (XML-like) | Styled text markup | In-memory, status bar |
