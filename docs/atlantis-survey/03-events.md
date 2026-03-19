# Event System

The heart of Atlantis automation. A trigger-condition-action pipeline
using Objective-C runtime plugin architecture.

## Architecture

```
External Trigger (text, timer, keypress, UI)
    │
    ▼
Event Object (BaseEvent subclass)
    │
    ▼
Condition Pipeline (AND/OR evaluation)
    │  ├─ Condition 1: isTrueForState: → YES/NO
    │  ├─ Condition 2: isTrueForState: → YES/NO
    │  └─ ... (AND: all must pass, OR: any passes)
    │
    ▼ (if conditions pass)
Action Execution Loop
    ├─ Action 1: executeForState: → NO (continue)
    ├─ Action 2: executeForState: → NO (continue)
    └─ Action 3: executeForState: → YES (stop)
```

## Event Types

```
BaseEvent (abstract)
  ├─ WorldEvent (world-scoped trigger)
  │   └─ ComplexEvent (advanced matching)
  ├─ HotkeyEvent (keyboard shortcut)
  ├─ ToolbarUserEvent (toolbar button)
  ├─ AliasEvent (command alias)
  ├─ DelayedEvent (fire at specific time)
  └─ RDMenuEvent (menu item)
```

## State Object (AtlantisState)

Context passed through the entire condition→action pipeline:
- `_rdEventLine` — the text/line that triggered the event
- `_rdEventWorld` — current world connection
- `_rdEventSpawn` — active spawn/view
- `_rdEventExtraData` — key-value dict for inter-step communication
- `_rdScriptSafeData` — sandboxed data for script evaluation

Conditions write data into state (e.g., regex captures), actions read it.

## Event Type Enum (AtlantisEventType)

```
AtlantisTypeUI      — UI/menu events
AtlantisTypeEvent   — text/timer/trigger events
AtlantisTypeAlias   — alias expansion
```

Used to filter which actions/conditions are valid for a given event type.

## Protocols

### EventDataProtocol (event container)
- `eventName`, `eventDescription` — display strings
- `eventIsEnabled` / `eventSetEnabled:` — enable/disable
- `eventConditions`, `eventConditionsAnded` — condition list + AND/OR
- `eventActions` — action list
- `eventAddCondition:`, `eventRemoveCondition:`, `eventMoveCondition:toPosition:`
- `eventAddAction:`, `eventRemoveAction:`, `eventMoveAction:toPosition:`
- `eventExtraData:` / `eventSetExtraData:forName:` — custom data
- NSCoding for serialization

### EventActionProtocol
- `+actionName`, `+actionDescription` — class-level metadata
- `+validForType:(AtlantisEventType)` — type filtering
- `-executeForState:(AtlantisState *)` — execute; return YES to stop chain
- `-actionConfigurationView` — optional NIB-based config UI

### EventConditionProtocol
- `+conditionName`, `+conditionDescription` — class-level metadata
- `+validForType:(AtlantisEventType)` — type filtering
- `-isTrueForState:(AtlantisState *)` — evaluate; return YES if true
- `-conditionConfigurationView` — optional NIB-based config UI

## Complete Action List

### Connection/Network
| Action | Purpose |
|--------|---------|
| Action_WorldSend | Send text to active world |
| Action_WorldDisconnect | Disconnect from world |
| Action_WorldReconnect | Reconnect to world |
| Action_GlobalDisconnect | Disconnect all worlds |
| Action_GlobalReconnect | Reconnect all worlds |
| Action_StatusSend | Send status bar command |

### UI Navigation
| Action | Purpose |
|--------|---------|
| Action_NextWorld | Switch to next world |
| Action_PrevWorld | Switch to previous world |
| Action_NextSpawn | Focus next spawn |
| Action_SpawnFocus | Focus specific spawn |
| Action_SpawnClose | Close spawn |
| Action_CloseFocused | Close focused window |
| Action_ToggleSpawnList | Show/hide spawn list |
| Action_WindowClose | Close window |

### Input/Output
| Action | Purpose |
|--------|---------|
| Action_InputClear | Clear input field |
| Action_InputCopy | Copy input to clipboard |
| Action_InputToHistory | Add input to history |
| Action_InputConvertToMUSH | Convert codes to MUSH format |
| Action_InputConvertFromMUSH | Convert MUSH codes to plain |
| Action_HistoryNext | Next in command history |
| Action_HistoryPrev | Previous in command history |
| Action_LastCommand | Repeat last command |
| Action_OutputCopy | Copy output to clipboard |
| Action_OutputCopyInput | Copy input portion of line |

### Text Formatting
| Action | Purpose |
|--------|---------|
| Action_TextHighlight | Apply FG/BG colors to line |
| Action_TextSpawn | Route text to specific spawn |
| Action_TextOmitLog | Don't log this line |
| Action_TextOmitScreen | Don't display this line (gag) |
| Action_TextOmitActivity | Don't count as activity |
| Action_EatLinefeeds | Remove linefeeds |
| Action_Substitute | Find/replace (regex capable) |
| Action_LineClass | Assign CSS-like class to line |

### Audio/Visual Feedback
| Action | Purpose |
|--------|---------|
| Action_Beep | System beep |
| Action_TextSpeak | Text-to-speech (NSSpeechSynthesizer) |
| Action_SoundPlay | Play audio file |
| Action_DockBounce | Bounce dock icon |
| Action_Growl | Send Growl notification |

### Variables
| Action | Purpose |
|--------|---------|
| Action_SetTempVar | Set temp variable in state |

### Logging/Files
| Action | Purpose |
|--------|---------|
| Action_LogpanelOpen | Open logging panel |
| Action_LogpanelClose | Close logging panel |
| Action_OpenLog | Open log for viewing |
| Action_CloseLogs | Close all logs |

### Utility
| Action | Purpose |
|--------|---------|
| Action_PerlEval | Execute Perl/Lua script |
| Action_ShrinkURL | Shorten URL via service |
| Action_ToggleDrag | Toggle drag mode |
| Action_OpenTextEditor | Open external editor |

## Complete Condition List

### Text/Pattern
| Condition | Purpose |
|-----------|---------|
| Condition_StringMatch | Text matches regex/glob/literal |
| Condition_VariableMatch | Variable matches pattern |
| Condition_LineClass | Line has specific class |

### Connection State
| Condition | Purpose |
|-----------|---------|
| Condition_WorldConnected | Any world connected |
| Condition_WorldIsConnected | Specific world connected |
| Condition_WorldIsDisconnected | Specific world disconnected |
| Condition_WorldOpened | World window opened |
| Condition_WorldClosed | World window closed |
| Condition_CharConnected | Character is connected |
| Condition_WorldHasLogs | World has active logs |

### Activity/Idle
| Condition | Purpose |
|-----------|---------|
| Condition_Timer | Interval timer (N seconds between fires) |
| Condition_ComputerIdle | System idle for N seconds |
| Condition_WorldIdle | World idle for N seconds |
| Condition_SpawnActivity | Spawn has unread activity |

### View/Focus
| Condition | Purpose |
|-----------|---------|
| Condition_ViewActive | Specific view is focused |
| Condition_ViewName | View name matches pattern |
| Condition_AtlantisFocus | App window has focus |
| Condition_AtlantisVisible | App window is visible |

### World Info
| Condition | Purpose |
|-----------|---------|
| Condition_HasWorld | Any world defined |
| Condition_HasRealWorld | Named world exists |
| Condition_WorldIsMUSH | World is MUSH-type |

### Composite
| Condition | Purpose |
|-----------|---------|
| Condition_Negate | NOT (wraps one child) |
| Condition_CollectedConditions | Group with AND/OR |

## Registration & Discovery

Actions and conditions are dynamically registered at startup:
```objc
// ActionClasses singleton
- (void)registerActionClass:(Class)actionClass;
- (NSArray *)actionsForType:(AtlantisEventType)type;
- (BaseAction *)instanceOfClassAtIndex:(unsigned)index;

// ConditionClasses singleton
- (void)registerConditionClass:(Class)conditionClass;
- (NSArray *)conditionsForType:(AtlantisEventType)type;
```

Runtime inspects each class for required methods before accepting registration.
Classes sorted alphabetically by name for UI display.

## Serialization

Events persist via NSCoding (NSKeyedArchiver):
```
"event.actions"         → NSMutableArray of BaseAction
"event.conditions"      → NSMutableArray of BaseCondition
"event.enabled"         → BOOL
"event.conditions.anded"→ BOOL
"event.name"            → NSString
"event.desc"            → NSString
```

Each action/condition serializes its own configuration (pattern text,
color choices, etc.) under namespaced keys.
