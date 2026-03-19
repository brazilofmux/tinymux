# Atlantis-Inspired Features — Cross-Client Design

Features from the Atlantis source survey (`docs/atlantis-survey/`) that
would benefit the Titan client family. Prioritized by impact and
feasibility across platforms.

## 1. Spawn System (Output Routing)

**Priority: HIGH — Atlantis's most distinctive feature, no client has it.**

### Concept
A "spawn" is a sub-tab within a world connection. Each spawn has a list
of patterns (regex/glob). When a line arrives from the server, each
spawn's patterns are checked — matching lines route to that spawn's
display. The main spawn always receives everything.

### Use Cases
- Route combat output to a "Combat" tab
- Route pages/tells to a "Pages" tab
- Route channel chat to per-channel tabs ("Public", "Staff", etc.)
- Route system messages to a "System" tab
- Keep the main tab clean for RP/scene text

### Data Model
```
SpawnConfig:
    name: String          — display name ("Combat", "Pages")
    path: String          — identifier ("combat", "pages")
    patterns: [String]    — regex patterns to match
    exceptions: [String]  — patterns to exclude
    prefix: String        — optional prefix ("[COMBAT] ")
    maxLines: Int         — scrollback limit for this spawn
    weight: Int           — tab ordering
```

### Matching Pipeline
```
Server line arrives
    │
    ▼
Main spawn always receives the line
    │
    ▼
For each spawn config (by weight order):
    If line matches any pattern AND no exception:
        Route line to that spawn's display
```

Lines can match multiple spawns (a line can appear in both main and
a spawn tab). This is intentional — the main tab is the complete log.

### Per-Client Implementation

**Console (C++):**
- Spawns as additional output buffers, switchable via `/fg spawn:combat`
- `/spawn add <name> <pattern>` to define
- `/spawn remove <name>` to delete
- Status bar shows spawn activity indicators

**TF (C++):**
- Natural fit — TF already has world switching
- Spawns as sub-worlds within a connection
- `/spawn` command family
- ncurses split view for active spawn

**Web (JavaScript):**
- Spawns as additional tabs within the world tab group
- Nested tab bar: [Main] [Combat] [Pages] [Public]
- Or sidebar panel with spawn list
- Pattern config in World Manager

**Win32GUI (C++):**
- Spawns as child tabs within the world's pane
- Or split view with spawn list sidebar
- Best visual presentation potential

**Android (Kotlin):**
- Spawns as swipeable sub-tabs below the world tab bar
- Or bottom sheet with spawn selector
- Edit spawn patterns in World Manager

**iOS (Swift):**
- Spawns as segmented control or tab bar within world view
- iPad: NavigationSplitView with spawn list + output
- Edit spawn patterns in World Manager

### Implementation Order
1. Data model + persistence (SpawnConfig in world settings)
2. Pattern matching in the line processing pipeline
3. UI for spawn display (platform-specific)
4. UI for spawn configuration
5. Spawn activity indicators

---

## 2. MCP Protocol (MUD Client Protocol)

**Priority: HIGH — used by MOOs and some MUSHes.**

### Concept
Structured server↔client communication beyond raw text. Primary use
case: remote text editing (server sends a document, client opens an
editor, user edits, client sends it back).

### Message Format
```
#$#mcp version: 2.1 to: 2.1
#$#mcp-negotiate-can 12345 package: dns-org-mud-moo-simpleedit min-version: 1.0 max-version: 1.0
```

### Core Packages
| Package | Purpose |
|---------|---------|
| mcp | Version negotiation |
| mcp-negotiate | Package capability exchange |
| dns-org-mud-moo-simpleedit | Remote text editing |

### Per-Client Implementation
- All clients: detect `#$#` prefix, parse MCP messages, handle negotiation
- Remote editing: open a text editor view/dialog with the content,
  send back on save
- Console/TF: open $EDITOR or built-in editor
- Web: textarea in a modal dialog
- GUI clients: editor pane or sheet

### Implementation Order
1. MCP message parser (shared logic, similar across clients)
2. Negotiation handler
3. Simple edit handler with editor UI
4. Wire into telnet/filter pipeline

---

## 3. Composite Conditions

**Priority: MEDIUM — makes triggers much more expressive.**

### Concept
Current triggers: pattern matches → action fires. No way to combine
conditions.

Atlantis model: conditions are composable with AND/OR/NOT:
- "Line matches /combat/ AND world is connected AND idle < 60s"
- "Line matches /tells you/ OR line matches /pages you/"
- "NOT line matches /spambot/"

### Data Model Extension
```
Trigger (extended):
    conditions: [Condition]
    conditionsAnded: Bool     — true=AND, false=OR

Condition types:
    StringMatch(pattern)      — line matches regex
    WorldConnected            — connection is active
    WorldIdle(seconds)        — idle for N seconds
    Negate(condition)         — NOT wrapper
    Group(conditions, anded)  — nested AND/OR
```

### Per-Client Implementation
- Extend existing trigger models with optional conditions list
- Default behavior (no conditions): same as today (pattern match only)
- UI: condition list in trigger editor with add/remove
- Backward compatible — triggers without conditions work as before

### Implementation Order
1. Condition protocol/interface in each client's trigger model
2. StringMatch condition (replaces inline pattern — same behavior)
3. Negate and Group conditions
4. WorldConnected, WorldIdle conditions
5. UI for condition editing

---

## 4. Substitution Action

**Priority: MEDIUM — rewrite ugly server output before display.**

### Concept
A trigger action that does regex find/replace on the display text
before it reaches the output. Different from gag (which hides the
whole line) — substitute rewrites part of it.

### Examples
- Replace `[OOC] PlayerName says "..."` with just `[OOC] Player: ...`
- Strip redundant timestamps from server output
- Reformat combat messages for readability

### Data Model
```
Trigger action type: SUBSTITUTE
    find: String (regex)
    replace: String (with $1, $2 capture references)
```

### Implementation
Straightforward in all clients — apply regex replace to the display
string before passing to AnsiParser/output. Fits naturally into the
existing trigger pipeline between pattern match and display.

---

## 5. Variable Namespace

**Priority: MEDIUM — enables smarter triggers and status bars.**

### Concept
Structured key-value state accessible from triggers, commands, and
status bar formatting:

| Namespace | Variables |
|-----------|-----------|
| `world.*` | `name`, `character`, `host`, `port`, `connected` (seconds) |
| `event.*` | `line`, `cause` (line/timer/key) |
| `regexp.*` | `0` (full match), `1`+ (capture groups) |
| `datetime.*` | `date`, `time`, `hour`, `minute`, `weekday` |
| `temp.*` | User-defined, session-scoped |

### Use Cases
- Status bar: `$world.name [$world.connected]s - $datetime.time`
- Trigger action: `say I see $regexp.1 at $datetime.time`
- Conditional: fire only if `$world.connected > 300`

### Implementation
- Variable store as a dictionary per world instance
- Built-in namespaces populated automatically
- `$var.name` expansion in trigger bodies, command arguments, status bar
- `/set var.name value` and `/unset var.name` for user variables

---

## 6. Line Classification

**Priority: LOW-MEDIUM — useful for advanced trigger workflows.**

### Concept
Assign CSS-like class names to lines. Other triggers can then match
on class instead of text content.

### Example
1. Trigger 1: pattern `/attacks you/` → assign class `combat`
2. Trigger 2: condition `class == combat` → route to Combat spawn
3. Trigger 3: condition `class == combat` → play sound

### Implementation
- Add `lineClass` field to the line metadata
- Classification action type in triggers
- Condition type that checks line class
- Useful mainly when combined with spawns and composite conditions

---

## 7. Text-to-Speech

**Priority: LOW — accessibility win.**

### Concept
Trigger action that speaks matched text aloud. Useful for:
- Alert on pages/tells when not looking at screen
- Accessibility for visually impaired users

### Per-Platform APIs
| Platform | API |
|----------|-----|
| Android | `TextToSpeech` |
| iOS | `AVSpeechSynthesizer` |
| Windows (Console/Win32GUI) | `ISpVoice` (SAPI) |
| Web | `SpeechSynthesis` API |
| Linux (TF) | `espeak` or `festival` |

---

## Implementation Roadmap

### Phase A: Spawn System
Start with the client that benefits most and is easiest to verify:

1. **Android** — can test immediately, swipeable sub-tabs
2. **iOS** — parallel implementation, similar architecture
3. **Web** — nested tabs, straightforward DOM manipulation
4. **Console** — command-line spawn switching
5. **TF** — integrate with existing world system
6. **Win32GUI** — design into the architecture from the start

### Phase B: MCP Protocol
Shared parsing logic, per-client editor UI:

1. **Web** — easiest (textarea in modal)
2. **Console/TF** — $EDITOR integration
3. **Android/iOS** — editor sheet
4. **Win32GUI** — editor pane

### Phase C: Trigger Enhancements
Composite conditions, substitution, variables:

1. All clients in parallel — extend existing trigger models
2. Start with substitution (simplest, most visible benefit)
3. Then composite conditions
4. Then variable namespace

### Phase D: Polish
Line classification, text-to-speech, advanced spawn features.
