# Scripting System

## Overview

Atlantis supports extensible scripting via pluggable language engines.
Historically Perl (CamelBones) and Lua (LuaCore), both currently disabled
in the open-source build due to framework link issues.

The scripting API is valuable reference regardless — it documents what
operations Atlantis considers scriptable.

## Architecture

### ScriptingEngine (abstract base)
```objc
- (void)executeFunction:withState:
- (BOOL)engineNeedsReinit
- (NSString *)scriptEngineName
- (NSString *)scriptEngineVersion
- (NSString *)scriptEngineCopyright
```

### ScriptingDispatch (central registry)
- Dictionary of engines keyed by language name
- `executeScript:withState:inLanguage:` — route to engine
- `refreshEngines:state` — reinitialize engines on reload

### PerlScriptingEngine / LuaScriptingEngine
Concrete implementations wrapping their respective interpreters.

## ScriptBridge API

High-level Atlantis operations exposed to scripts:

### Spawn/Window Operations
- `sendText:toWorld:` — send text to MUD
- `sendStatus:toSpawn:inWorld:` — update status bar
- `focusSpawn:` — focus a spawn
- `selectedStringInSpawn:` — get selected text
- `appendHTML:toSpawn:` — append HTML to output
- `appendAML:toSpawn:` — append AML to output

### Input Control
- `getTextFromInput` — read input field
- `sendTextToInput:` — replace input text
- Per-spawn variants of the above

### Variables
- `setVariable:forKey:inWorld:` — set user variable
- `getPreference:inWorld:` — read preference

### Media
- `speakText:` — text-to-speech
- `playSoundFile:` — play audio
- `systemBeep` — beep
- `growlText:withTitle:` — notification

### Upload
- `uploadTextfile:toWorld:` — upload text file
- `uploadCodefile:toWorld:` — upload code file

### Automation Registration
Scripts can register handlers dynamically:
- Line patterns (regex triggers)
- Event types (connect, disconnect, etc.)
- Aliases
- Hotkeys (with PTKeyCombo)
- Timers (with interval, countdown, per-world tracking)

### ScriptEventAction
Concrete action class (part of event system) for script invocation:
- `_rdFunction` — function name to call
- `_rdLanguage` — scripting language
- `_rdWorld` — world scope
- `_rdPattern` — trigger pattern (for line events)
- Timer properties: `_rdSeconds`, `_rdCountdown`, `_rdLastFired`
- Hotkey properties: `PTKeyCombo *_rdKeys`, global/window scope

## Script Entry Points

| File | Language | Purpose |
|------|----------|---------|
| Atlantis.pm | Perl | Main Perl module |
| Atlantis.lua | Lua | Main Lua module |
| Spawn.pm | Perl | Spawn manipulation |
| World.pm | Perl | World manipulation |

## Relevance for SwiftUI Port

The scripting system itself won't be ported (Perl/Lua on iOS is impractical).
However, the ScriptBridge API defines the complete set of programmable
operations, which maps to what our slash commands and trigger actions
should be able to do.
