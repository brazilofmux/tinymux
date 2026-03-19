# Atlantis Source Code Survey

Reference documentation for the [OpenAtlantis/Atlantis](https://github.com/OpenAtlantis/Atlantis)
macOS MUD client, surveyed as groundwork for a SwiftUI iOS/macOS client.

Atlantis is an Objective-C Cocoa application by Rachel Blackman (Sparks),
open-sourced under the [OpenAtlantis](https://github.com/OpenAtlantis) organization.
Version 0.9.9.8, last updated 2022-10-28. Built with Xcode 14, targeting macOS 10.12+.

## Documents

1. [Architecture Overview](01-architecture.md) — high-level structure, data flow, design patterns
2. [Networking & Protocol](02-networking.md) — sockets, telnet, ANSI, MCCP, charset negotiation
3. [Event System](03-events.md) — triggers, conditions, actions, the automation pipeline
4. [Worlds & Spawns](04-worlds.md) — world model, connections, spawn routing, address book
5. [Commands & Input](05-commands.md) — slash commands, input processing, command history
6. [Filter Pipeline](06-filters.md) — filter chain architecture, each filter's role
7. [MCP Protocol](07-mcp.md) — MUD Client Protocol handlers
8. [Scripting](08-scripting.md) — Perl/Lua bridge, ScriptBridge API
9. [UI Systems](09-ui.md) — hotkeys, toolbar, preferences, logs, uploads
10. [Lemuria Framework](10-lemuria.md) — text views, nested view manager, tab system

## Repository Stats

- **Atlantis**: 1,099 files, 211 Objective-C source files (.m)
- **Lemuria**: ~80 files, the windowing/text framework Atlantis builds on
- **Frameworks**: OgreKit (regex), Sparkle (auto-update), Growl (notifications)

## Source Directory Map

| Directory | Files | Purpose |
|-----------|-------|---------|
| Core/ | 4 | App delegate, main controller |
| Command System/ | 20 | Slash commands |
| Event System/ | 184 | Triggers, actions, conditions |
| Event Configuration Kit/ | 9 | Event editor UI |
| Filter System/ | 14 | Data processing pipeline |
| MCP System/ | 16 | MUD Client Protocol |
| Worlds/ | 16 | World model, spawns, address book |
| Support/ | 84 | Networking, sockets, utilities |
| Scripting/ | 12 | Perl/Lua scripting bridge |
| Hotkey System/ | 6 | Keyboard bindings |
| Log System/ | 12 | Session logging |
| Preferences/ | 31 | Settings UI |
| Toolbar System/ | 23 | Customizable toolbar |
| Upload System/ | 8 | File upload to MUD |
| Utility Classes/ | 34 | Reusable components |
| Extensions/ | 13 | Cocoa category extensions |
| Misc/ | 12 | Input views, about box, progress |
