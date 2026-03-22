# Commands & Input Processing

## Command Architecture

### BaseCommand
All slash commands inherit from BaseCommand (NSObject):
```objc
- (NSString *)checkOptionsForState:(AtlantisState *)state;  // Validate; return error or nil
- (void)executeForState:(AtlantisState *)state;              // Execute the command
```

Commands receive AtlantisState with full context (world, spawn, variables,
command parameters).

### Registration
Commands registered in `RDAtlantisMainController.init` via `addCommand:forText:`.
Stored in `_rdCommands` dictionary with lowercase keys. Lookup is case-insensitive.

### Dispatch Flow

1. User types in spawn input view
2. `processInputString:onSpawn:` extracts text
3. If starts with "/", extract command name
4. Look up in `_rdCommands` dict
5. Call `checkOptionsForState:` — if error string, display to user
6. If nil (OK), call `executeForState:`

## Built-in Commands

| Command | Class | Purpose |
|---------|-------|---------|
| `/clear` | ClearCommand | Clear scrollback (appends N newlines) |
| `/grab` | GrabCommand | Query MUD object attribute (`grab obj/attr`) |
| `/gname` | GrabNameCommand | Grab object name variant |
| `/qc` | QuickConnectCommand | Temporary connection (`qc host:port`) |
| `/quote` | QuoteCommand | Escape/quote text |
| `/sc` | StackedCommand | Batch multiple commands |
| `/ul` | UlCommand | Upload-related |
| `/wait` | WaitCommand | Delay execution |
| `/logs` | LogsCommand | Log management |

### AliasEvent (special case)
Not a BaseCommand subclass—extends BaseEvent instead. User-defined
aliases participate in the event system with conditions and actions.
Spaces in alias names converted to underscores. Does not support conditions.

## Input Processing Pipeline

### Text Input Flow
```
User types in RDMUSHTextView (spawn input view)
    │
    ▼
handleLocalInput:onSpawn:
    │
    ├─ Check for "/" prefix → command dispatch
    │
    ├─ Check alias events → pattern match and expand
    │
    └─ Send to MUD
        │
        ▼
    sendString: → encode with _rdOutputEncoding → sendData: → socket write
```

### Command History

- Per-world instance: `_rdCommandHistory` (NSMutableArray)
- Navigation via Action_HistoryNext / Action_HistoryPrev
- Hotkey-bindable (up/down arrows typically)

### MUSH-Specific Input

- `Action_InputConvertToMUSH` — convert special codes to MUSH format
- `Action_InputConvertFromMUSH` — convert MUSH codes to readable text
- Grab commands use "SimpleMUUser" as default password (configurable)
