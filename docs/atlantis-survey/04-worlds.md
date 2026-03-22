# Worlds & Spawns

## World Data Model

### RDAtlantisWorldPreferences (persistent config)

Two-layer preference system:

- `_rdWorldPreferences` — world-level defaults
- `_rdCharacterPreferences` — per-character overrides

Key properties:
| Preference Key | Purpose |
|----------------|---------|
| `atlantis.world.name` | Display name |
| `atlantis.world.host` | Hostname/IP |
| `atlantis.world.port` | Port number |
| `atlantis.world.autoconnect` | Auto-connect on launch |
| `atlantis.world.autoopen` | Auto-open window |
| `atlantis.grab.password` | Grab command password |
| `atlantis.encoding` | String encoding |
| `atlantis.colors.*` | Color palette |

Multi-character support: `addCharacter:`, `removeCharacter:`,
`renameCharacter:to:`. Each character gets its own preference subtree.

UUID tracking for persistence. Dirty flag for save optimization.

### RDAtlantisWorldInstance (active session)

Created per (world, character) pair. One instance per active connection.

**Network I/O:**

- NSInputStream / NSOutputStream (SSL-capable)
- NSStreamDelegate for async read/write
- Holdover buffer for incomplete ANSI sequences
- Output buffer + timer for write batching

**Connection properties:**

- `_rdCharacter` — connected character name
- `_rdUuid` — session UUID
- `_rdBasePath` — character-specific storage directory
- `isConnected`, `isConnecting`, `shouldReconnect`
- `connectedSince` — NSDate
- `_rdInputEncoding` / `_rdOutputEncoding` — charset

**MCP Support:**

- `_rdMcpPackets` — pending MCP messages
- `_rdMcpTags` — MCP by data tag
- `_rdMcpSessionKey` — authentication key
- `_rdMcpNegotiated`, `_rdMcpDisabled` — state flags
- `_rdMcpPackages` — negotiated MCP packages
- `supportsMcpPackage:` — capability query

**State Management:**

- `_rdBaseStateInfo` — persistent variables
- `_rdTempVariables` — session-local variables
- `_rdCommandHistory` — command history array

## Connection Lifecycle

1. **Initialize:** `initWithWorld:forCharacter:withBasePath:`
   - Load world preferences
   - Create main spawn
   - Instantiate filter chain
   - Set up MCP support

2. **Connect:** `connect` / `connectAndFocus`
   - Verify not already connected
   - Retrieve host, port, encoding from prefs
   - Create NSInputStream/NSOutputStream pair
   - Start stream reading/writing
   - Begin MCP negotiation

3. **Active Session:** `handleBytesOnStream:`
   - Read incoming data
   - Buffer incomplete sequences
   - Run through filter chain
   - Decode to string
   - Route to spawns via pattern matching
   - Fire events (triggers, aliases)

4. **Input Processing:** `handleLocalInput:onSpawn:`
   - Extract text from spawn input view
   - Check for slash commands
   - Execute command or send to MUD
   - Add to command history

5. **Disconnect:** `disconnect` / `disconnectWithMessage:`
   - Close streams
   - Mark disconnected
   - Optionally send goodbye message

6. **Reconnect:** Auto-reconnect if `shouldReconnect` flag set

## Spawn System (Output Routing)

### Concept
A "spawn" is a sub-window/tab within a world connection. The main spawn
(path = "") always receives all output. Additional spawns are created
on-demand to route specific output (combat, tells, channels, etc.) to
separate views.

### RDAtlantisSpawn
Conforms to `RDNestedViewDescriptor` (Lemuria tab/window protocol).

**View hierarchy:**

- `_rdOutputView` — RDTextView (read-only scrollback)
- `_rdInputView` — NSTextView (command input)
- RBSplitView for resizable panes
- `_rdStatusBar` — ImageBackgroundView with SSL indicator, timer, user text

**Display properties:**

- Font, background color, input color, console color (per-spawn)
- Paragraph style, timestamp toggle, link styling
- Prefix display (e.g., "[COMBAT]")

**Key methods:**

- `appendString:` / `appendStringNoTimestamp:` — output display
- `stringFromInput` / `stringFromInputSelection` — get user input
- `stringIntoInput:` / `stringInsertIntoInput:` — macro injection
- `clearScrollback` — wipe history
- `screenWidth` / `screenHeight` — viewport dimensions

### RDSpawnConfigRecord
Pattern-based routing configuration per spawn:

- `_rdPatterns` — array of RDStringPattern (regex/glob)
- `_rdExceptions` — patterns to exclude
- `_rdActiveExceptions` — override list
- `matches:` — test if output matches this spawn
- `_rdMaxLines` — scrollback limit
- `_rdSpawnPath` — e.g., "combat", "social:tells"
- `_rdSpawnPrefix` — display prefix
- `_rdSpawnWeight` — tab ordering

### Spawn Lifecycle

1. World instance creates main spawn (path = "")
2. Additional spawns configured via SpawnConfigRecords
3. When output arrives, each spawn's patterns checked
4. If line matches, spawn receives the output
5. Spawns created lazily on first pattern match
6. Spawns show activity indicator when unfocused + new text

## World Storage

### WorldCollection

- Worlds stored in `~/Library/Application Support/Atlantis/worlds/*.awd`
- `.awd` format = NSKeyedArchiver (binary Cocoa serialization)
- Methods: `loadAllWorlds`, `saveAllWorlds`, `saveDirtyWorlds`
- Lookup: `worldForName:`, `worldForUUID:`
- `doAutoconnects` — trigger auto-connect/auto-open on launch
- `importWorldXML:` — import .axworld XML format

### World File Formats
| Extension | Format | Usage |
|-----------|--------|-------|
|.awd | NSKeyedArchiver binary | Internal storage |
|.axworld | XML | Shareable export format |

## Address Book (UI)

- Outline view of worlds and characters
- Tab-based configuration panels
- Character management (add/remove/rename)
- Quick connect buttons
- Connection status indicators (green/white lights)

## Variables System

Atlantis has a rich variable namespace (from Variables.txt):

| Namespace | Variables |
|-----------|-----------|
| `application.*` | `version`, `spawn` |
| `world.*` | `name`, `character`, `game`, `connected` |
| `event.*` | `cause`, `line`, `statechange`, `detail`, `command`, `spawn`, `highlighted`, `script.lineAML`, `script.command` |
| `regexp.*` | `0` (full match), `1`+ (capture groups) |
| `datetime.*` | `date`, `time`, `year`, `month`, `day`, `hour`, `minute`, `second`, `weekday`, `monthname`, etc. |
| `command.*` | `data`, `fulltext`, any `-parameter` |
| `userconf.*` | User-defined (persistent) |
| `temp.*` | User-defined (session) |
| `worldtemp.*` | User-defined (world session) |
