# Hydra: High-Level Design

## Status: Design

Branch: `master`

Predecessors: `docs/hydra-problem-statement.md` (requirements),
`docs/hydra-functional-spec.md` (functional spec)

## Scope

This document describes the software architecture for Hydra—the
components, their interfaces, the data structures, and the event flow.
It covers Phase 1 (minimal viable proxy) in detail and identifies
where Phases 2–4 extend the design.

Unless otherwise noted, the detailed design in this document is for
Phase 1 only. Phase 1 is telnet-centric on both sides: a TLS telnet
front door and a telnet-style back door. WebSocket and gRPC front
doors are later extensions that reuse the same session model but are
not fully specified here.

## GANL Prerequisite: Client-Side Connection Support

GANL today is a server-side networking library. It accepts inbound
connections via listeners, manages their lifecycle through
`ConnectionBase`, and drives telnet negotiation as the server.

Hydra needs GANL to also work as a **client**—initiating outbound TCP
connections to game servers, performing telnet negotiation from the
client side, and managing those connections with the same event-driven
model.

The low-level primitives exist: `adoptConnection()` and
`postWrite()`/`postRead()` are proven in the email channel's async
SMTP client. What is missing is the high-level abstraction that
`ConnectionBase` provides on the server side:

- **`OutboundConnection`** — a new peer of `ReadinessConnection` and
  `CompletionConnection` that manages the lifecycle of an outbound TCP
  connection: async connect, telnet negotiation (client-side), data
  exchange, reconnect.

- **Client-side telnet negotiation** — `TelnetProtocolHandler` today
  starts negotiation by sending server options (WILL EOR, WILL
  CHARSET, DO NAWS, DO TTYPE, etc.). For outbound connections, Hydra
  is the client and should respond to the game's server-side options
  rather than initiate its own. This requires a negotiation mode flag
  or a separate `startClientNegotiation()` entry point.

- **Async connect state machine** — `ConnectionBase` assumes the
  socket is already connected (accepted from a listener).
  `OutboundConnection` needs states before `Running`:
  `Resolving → Connecting → TelnetNegotiating → Running`.

- **`NetworkEngine::initiateConnect()`** — a new method that creates a
  socket, calls `connect()` (non-blocking), adopts the fd, and
  returns a handle. This wraps the pattern currently open-coded in the
  email channel.

This work benefits both Hydra and any future GANL client use case
(MUD-to-MUD linking, external service integration, health checks).

### OutboundConnection State Machine

```
                    ┌────────────┐
                    │ Resolving  │  DNS lookup
                    └─────┬──────┘
                          │ address(es) resolved
                          ▼
                    ┌────────────┐
              ┌────│ Connecting │  async connect()
              │     └─────┬──────┘
              │           │ socket writable (connect complete)
     connect  │           ▼
     failed   │     ┌────────────────────┐
              │     │ TelnetNegotiating  │  exchange options
              │     └─────┬──────────────┘
              │           │ negotiation complete or timeout
              │           ▼
              │     ┌────────────┐
              │     │  Running   │  normal data exchange
              │     └─────┬──────┘
              │           │ connection lost
              ▼           ▼
        ┌────────────────────┐
        │   Disconnected     │
        └─────┬──────────────┘
              │ retry policy says try again
              ▼
        ┌────────────┐
        │ Resolving  │  (back to top)
        └────────────┘
```

### Client-Side Telnet Negotiation

When Hydra connects to a game as a client, the game (server) initiates
telnet negotiation. Hydra should respond intelligently:

| Game sends     | Hydra responds | Purpose                           |
|----------------|----------------|-----------------------------------|
| WILL EOR       | DO EOR         | Accept end-of-record marking      |
| WILL ECHO      | DO ECHO        | Let game control echo             |
| WILL SGA       | DO SGA         | Accept suppress-go-ahead          |
| DO NAWS        | WILL NAWS + SB | Send terminal size from client    |
| DO TTYPE       | WILL TTYPE     | Report terminal type              |
| WILL CHARSET   | DO CHARSET     | Accept charset negotiation        |
| WILL GMCP      | DO GMCP        | Accept if front-door supports it  |
| WILL MSSP      | DO MSSP        | Accept server status data         |

Hydra synthesizes NAWS and TTYPE values from the front-door client's
negotiated state. If no front-door connection is attached when the
back-door negotiates, Hydra uses sensible defaults (80×24, "Hydra").

## Process Architecture

```
┌──────────────────────────────────────────────────────────┐
│                      hydra (process)                     │
│                                                          │
│  ┌──────────────┐   ┌──────────────┐   ┌──────────────┐ │
│  │  Front Door  │   │   Session    │   │  Back Door   │ │
│  │              │──►│   Manager    │◄──│              │ │
│  │  GANL server │   │              │   │  GANL client │ │
│  │  listeners   │   │  sessions[]  │   │  outbound    │ │
│  │  TLS term.   │   │  accounts db │   │  connections │ │
│  │  telnet neg. │   │  scrollback  │   │  telnet neg. │ │
│  └──────────────┘   └──────┬───────┘   └──────────────┘ │
│                            │                             │
│                     ┌──────┴───────┐                     │
│                     │   SQLite DB  │                     │
│                     └──────────────┘                     │
│                                                          │
│  Links against: libmux.so, GANL                          │
│  Does NOT link: engine.so                                │
└──────────────────────────────────────────────────────────┘
```

### Event Loop

Hydra runs a single-threaded event loop, the same model as netmux:

```cpp
while (!shutdown_requested) {
    int n = networkEngine->processEvents(poll_timeout_ms, events, MAX_EVENTS);
    for (int i = 0; i < n; i++) {
        dispatchEvent(events[i]);
    }
    sessionManager.runTimers();   // idle timeouts, reconnect backoff
}
```

All I/O is non-blocking. All state mutations happen on the main
thread. No locks needed.

### GANL Instances

Hydra uses a **single** `NetworkEngine` instance for both front-door
and back-door connections. The engine multiplexes all fds (listeners,
inbound connections, outbound connections) in one `processEvents()`
call.

Hydra uses **two** `TelnetProtocolHandler` instances:

- **Front-door handler** — server-mode negotiation (same as netmux),
  one protocol context per front-door connection.
- **Back-door handler** — client-mode negotiation (new), one protocol
  context per back-door link.

The two handlers are independent. A front-door connection might
negotiate UTF-8 with 256-color support while the corresponding
back-door link negotiates Latin-1 with 16-color. The telnet bridge
translates between them.

## Data Structures

### Session

```cpp
struct HydraSession {
    uint64_t            id;             // Unique session ID
    uint32_t            accountId;      // FK to accounts table
    std::string         username;       // Cached for display

    // Front-door connections attached to this session (0..N)
    std::vector<FrontDoorConn*> frontDoors;

    // Back-door links (0..N)
    std::vector<BackDoorLink*>  links;
    BackDoorLink*               activeLink;  // Phase 1: session-wide input target

    // Scroll-back buffer
    ScrollBack          scrollback;

    // Timestamps
    time_t              created;
    time_t              lastActivity;

    // State
    enum { Login, Active, Detached } state;
};
```

**Phase 1 session policy:** A session has exactly one active input
target (`activeLink`). Any attached front-door connection may send
Hydra commands, and any non-command input from any attached front door
is forwarded to the session's active link. Output from all attached
links is broadcast to all attached front doors, with the source game
identified in the scroll-back metadata. This is a deliberate Phase 1
simplification, not an accidental default.

### FrontDoorConn

```cpp
struct FrontDoorConn {
    ganl::ConnectionHandle  handle;
    HydraSession*           session;     // nullptr until authenticated

    // GANL manages TLS and telnet state via the handle.
    // We cache the negotiated capabilities for the telnet bridge:
    ganl::ProtocolState     protoState;  // charset, NAWS, color, GMCP
    ganl::EncodingType      encoding;

    // Input line buffer (assembled by telnet protocol handler)
    std::string             inputLine;

    // Protocol type
    enum { Telnet, WebSocket, Grpc } type;
};
```

### BackDoorLink

```cpp
struct BackDoorLink {
    ganl::ConnectionHandle  handle;      // InvalidConnectionHandle if disconnected
    HydraSession*           session;

    // Game identity
    std::string             gameName;    // FK to game config
    std::string             character;   // Logged-in character name

    // Negotiated state with the game
    ganl::ProtocolState     protoState;
    ganl::EncodingType      encoding;

    // Link lifecycle
    enum { Connecting, Negotiating, AutoLoggingIn,
           Active, Reconnecting, Suspended, Dead } state;

    // Reconnect state
    int                     retryCount;
    time_t                  nextRetry;

    // Per-link scroll-back (optional, for multi-link sessions)
    // Primary scroll-back is on the session.
};
```

### ScrollBack

```cpp
class ScrollBack {
    // Ring buffer of lines, stored as PUA-encoded UTF-8.
    // Color is represented as PUA code points (U+F500–F7FF,
    // U+F0000–F3FFF) — the same internal format TinyMUX uses.
    // On replay, Hydra renders to the front-door's color depth
    // via co_render_ansi{16,256,truecolor}() and re-encodes to
    // the front-door's charset.

    struct Line {
        std::string     text;       // PUA-encoded UTF-8
        std::string     source;     // game name (for multi-link)
        time_t          timestamp;
    };

    std::vector<Line>   buffer;     // fixed capacity, wraps
    size_t              head;       // next write position
    size_t              count;      // lines currently stored
    size_t              capacity;   // max lines (configurable)

    void append(const std::string& text, const std::string& source);
    void replay(FrontDoorConn* conn, size_t maxLines);
};
```

**Internal representation:** Scroll-back stores PUA-encoded UTF-8 —
the same internal color format that TinyMUX uses throughout its
engine. Color information is carried as PUA code points inline in the
UTF-8 string, not as raw ANSI escape sequences. This means:

- One canonical representation (PUA/UTF-8) that is charset-neutral
  and color-depth-neutral
- All of libmux's `co_*` functions work directly on buffered strings:
  `co_visual_width()`, `co_visible_length()`, `co_copy_columns()`,
  `co_strip_color()`, etc.
- CJK double-width characters are correctly measured via
  `ConsoleWidth()` / `co_visual_width()`
- On replay, color is rendered to the reconnecting client's depth
  using `co_render_ansi16()`, `co_render_ansi256()`, or
  `co_render_truecolor()` — the same rendering pipeline TinyMUX uses
- Charset is converted from UTF-8 to the client's encoding using the
  existing code page tables (`cp437_utf8`, `latin1_utf8`, etc.)
- Color downgrading uses `co_nearest_xterm16()` and
  `co_nearest_xterm256()` with CIE97 perceptual distance

**Persistence boundary:** In Phase 1, scroll-back is in-memory only.
Replay is guaranteed for client reconnect while Hydra remains running.
If Hydra itself is replaced, saved session metadata may be restored, but
scroll-back is not guaranteed to survive unless a later phase adds
explicit persistence.

## Front Door Detail

### Listener Setup

```cpp
// During initialization, for each configured listen directive:
ganl::ErrorCode err;
ganl::ListenerHandle lh = networkEngine->createListener(host, port, err);
networkEngine->startListening(lh, &listenerCtx, err);
// listenerCtx records whether this is TLS, WebSocket, etc.
```

### Accept → Authenticate → Attach

```
Accept event
    │
    ▼
Create FrontDoorConn, allocate GANL protocol context
    │
    ▼
TLS handshake (if TLS listener)
    │
    ▼
Telnet negotiation (server-side: WILL EOR, DO NAWS, DO TTYPE, etc.)
    │
    ▼
Send Hydra banner + login prompt
    │
    ▼
Read username + password
    │
    ▼
Authenticate against SQLite accounts table
    │
    ├── Fail → send error, allow retry or disconnect
    │
    └── Success
         │
         ▼
    Existing session for this account?
         │
    ┌────┴────┐
    Yes       No
    │         │
    ▼         ▼
  Attach    Create new
  to it     session
    │         │
    └────┬────┘
         │
         ▼
    Replay scroll-back (if resuming)
         │
         ▼
    If session has active links, begin forwarding
    If no links, show game menu
```

### Session Command Dispatch

When a line arrives from a front-door connection:

```cpp
void onFrontDoorInput(FrontDoorConn* fd, const std::string& line) {
    if (fd->session == nullptr) {
        // Not yet authenticated — handle login state machine
        handleLogin(fd, line);
        return;
    }

    if (line.size() > 0 && line[0] == '/') {
        // Hydra command
        dispatchSessionCommand(fd->session, fd, line);
        return;
    }

    // Forward to active back-door link
    BackDoorLink* link = fd->session->activeLink;
    if (link && link->state == Active) {
        // Convert charset: front-door encoding → back-door encoding
        std::string converted = telnetBridge.convertInput(
            fd->protoState, link->protoState, line);
        sendToGame(link, converted);
    } else {
        sendToFrontDoor(fd, "[No active game link. Use /connect <game>]\r\n");
    }
}
```

## Back Door Detail

### Outbound Connection Flow

```cpp
void connectToGame(HydraSession* session, const GameConfig& game,
                   const std::string& character) {
    BackDoorLink* link = new BackDoorLink();
    link->session = session;
    link->gameName = game.name;
    link->character = character;
    link->state = Connecting;

    // GANL client-side connect (new API)
    ganl::ErrorCode err;
    if (game.transport == GameConfig::Tcp) {
        link->handle = networkEngine->initiateConnect(
            game.host, game.port, link, err);
    } else {
        link->handle = networkEngine->initiateUnixConnect(
            game.socketPath, link, err);
    }

    if (link->handle == ganl::InvalidConnectionHandle) {
        // DNS or socket creation failure — immediate
        link->state = Dead;
        notifyFrontDoors(session, "[Failed to connect to " + game.name + "]\r\n");
        return;
    }

    session->links.push_back(link);
    if (!session->activeLink) {
        session->activeLink = link;
    }
}
```

### Connect Completion

```cpp
void onBackDoorConnected(BackDoorLink* link) {
    link->state = Negotiating;

    // Create GANL protocol context for client-side telnet
    backDoorProtocol->createProtocolContext(link->handle);

    // Client-side: we don't initiate negotiation.
    // We wait for the game's server-side options and respond.
    // Start reading from the game.
    postRead(link->handle);
}
```

### Game Data Flow

```cpp
void onBackDoorData(BackDoorLink* link, const std::string& appData) {
    HydraSession* session = link->session;

    // If link is still in AutoLoggingIn state, keep forwarding all game
    // output to the player. Hydra does not rely on brittle prompt
    // scraping to detect generic login success across all games.
    if (link->state == AutoLoggingIn) {
        // Phase 1 policy: once the login sequence has been sent, the
        // link is treated as active for routing purposes and the player
        // can see and correct any login failure manually.
        link->state = Active;
    }

    // Ingest: charset-decode + ANSI→PUA → PUA-encoded UTF-8
    std::string pua = telnetBridge.ingestGameOutput(
        link->protoState, appData);

    // Append to scroll-back (PUA-encoded UTF-8)
    session->scrollback.append(pua, link->gameName);

    // Render for each attached front-door connection
    for (FrontDoorConn* fd : session->frontDoors) {
        // PUA→ANSI at client's color depth + charset-encode
        std::string out = telnetBridge.renderForClient(
            fd->protoState, pua);
        sendToFrontDoor(fd, out);
    }
}
```

## Telnet Bridge Detail

The telnet bridge is not a separate process or connection. It is a
translation layer inside Hydra that converts data between front-door
and back-door telnet states.

### Internal Color Format: PUA Code Points

Hydra uses the same PUA (Private Use Area) color encoding that TinyMUX
uses internally. Color is not carried as raw ANSI escape sequences
inside Hydra — it is parsed to PUA on ingestion and rendered back to
ANSI on output:

```
Back door (game)                    Hydra internal                  Front door (client)
═════════════════                   ══════════════                  ═══════════════════

ANSI SGR bytes   ──ANSI→PUA──►   PUA-encoded UTF-8   ──PUA→ANSI──►  ANSI SGR bytes
(game encoding)     parser        (canonical form)      renderer     (client encoding)
                                       │
                                       ▼
                                  scroll-back buffer
                                  co_* string ops
```

**PUA encoding (from `color_ops.h`):**
- U+F500–F507: attributes (reset, intense, underline, blink, inverse)
- U+F600–F6FF: 256 foreground indexed colors
- U+F700–F7FF: 256 background indexed colors
- U+F0000–F3FFF: 24-bit true color (FG/BG, split across two code
  point pairs)

This means every piece of text inside Hydra — in the scroll-back, in
the telnet bridge, in string measurement — is in the same format that
all of libmux's `co_*` functions expect. No special-casing needed.

### ANSI → PUA Parser (New Work)

TinyMUX generates PUA internally from softcode. It has extensive PUA →
ANSI renderers (`co_render_ansi16/256/truecolor()`) but does **not**
have a parser that goes the other direction: ingesting ANSI SGR escape
sequences from a byte stream and converting them to PUA code points.

Hydra needs this parser. It belongs in libmux.so (as part of
`color_ops.rl`) so that both Hydra and TinyMUX can use it:

- **`co_parse_ansi()`** — Ragel state machine that scans a byte
  stream, identifies ANSI SGR sequences (`ESC [ ... m`), converts
  them to the corresponding PUA code points, and passes non-ANSI
  bytes through unchanged.
- Handles: 16-color SGR (30–37, 40–47, 90–97, 100–107), 256-color
  (`38;5;N`, `48;5;N`), true color (`38;2;R;G;B`, `48;2;R;G;B`),
  attribute codes (0=reset, 1=bold, 4=underline, 5=blink, 7=inverse).
- Input: raw bytes in any supported charset (ANSI escapes are always
  ASCII, so charset only matters for non-escape content).
- Output: PUA-encoded UTF-8 string.

This is a natural extension of the existing `color_ops.rl` Ragel
machine and follows the same patterns.

### Translation Pipeline

```cpp
class TelnetBridge {
public:
    // Game → Hydra internal (back-door ingestion)
    // 1. Charset-decode game bytes to UTF-8
    // 2. Parse ANSI SGR → PUA code points (co_parse_ansi)
    // Result: PUA-encoded UTF-8, ready for scroll-back and co_* ops
    std::string ingestGameOutput(
        const ganl::ProtocolState& gameState,
        const std::string& gameBytes);

    // Hydra internal → Client (front-door output)
    // 1. Render PUA → ANSI at client's color depth
    //    (co_render_ansi16/256/truecolor, or co_strip_color)
    // 2. Charset-encode UTF-8 to client's encoding
    //    (using cp437_utf8, latin1_utf8, etc.)
    // Result: bytes ready to send to client
    std::string renderForClient(
        const ganl::ProtocolState& clientState,
        const std::string& puaUtf8);

    // Client → Game (front-door input)
    // 1. Charset-decode client bytes to UTF-8
    // 2. Charset-encode UTF-8 to game's encoding
    // Input from clients rarely contains color, but charset
    // conversion is still needed.
    std::string convertInput(
        const ganl::ProtocolState& clientState,
        const ganl::ProtocolState& gameState,
        const std::string& clientLine);

    // Forward NAWS from client to game
    void forwardTerminalSize(
        const FrontDoorConn* fd,
        BackDoorLink* link);

    // Forward GMCP between sides (if both support it)
    void forwardGmcp(
        const std::string& package,
        const std::string& data,
        bool gameToClient);
};
```

### Color Depth Rendering

On output, Hydra renders PUA to the client's color capability using
the existing libmux renderers:

| Client capability | Renderer                     | Downgrade function       |
|-------------------|------------------------------|--------------------------|
| True color (24b)  | `co_render_truecolor()`      | None (full fidelity)     |
| 256-color         | `co_render_ansi256()`        | `co_nearest_xterm256()`  |
| 16-color          | `co_render_ansi16()`         | `co_nearest_xterm16()`   |
| No color          | `co_strip_color()`           | N/A                      |

Color distance uses CIE97 perceptual distance with the existing
`xterm_palette[]` table and K-d tree lookup in libmux.

### Charset Conversion

Uses libmux's existing code page tables and UTF-8 infrastructure:

```
Game (Latin-1 bytes)
    │
    ▼  charset decode using latin1_utf8[]
PUA-encoded UTF-8 (internal)
    │
    ▼  co_render_ansi*() + charset encode
Client (UTF-8 bytes with ANSI SGR)
```

When both sides use UTF-8, charset conversion is a no-op. When the
client uses a narrower encoding (Latin-1, CP437), Unicode characters
outside that encoding's range are approximated using the existing
code page tables — the same approximation logic TinyMUX already uses.

CJK double-width characters are correctly accounted for via
`ConsoleWidth()` and `co_visual_width()` throughout the pipeline.

## Account Management

### SQLite Schema

```sql
-- Schema version tracking
CREATE TABLE schema_version (
    version     INTEGER NOT NULL
);

-- User accounts
CREATE TABLE accounts (
    id          INTEGER PRIMARY KEY,
    username    TEXT UNIQUE NOT NULL COLLATE NOCASE,
    pw_hash     TEXT NOT NULL,          -- argon2id hash
    pw_salt     TEXT NOT NULL,
    created     TEXT NOT NULL DEFAULT (datetime('now')),
    last_login  TEXT,
    flags       INTEGER DEFAULT 0       -- bit 0: admin, bit 1: suspended
);

-- Stored game login credentials
CREATE TABLE game_credentials (
    id          INTEGER PRIMARY KEY,
    account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    game_name   TEXT NOT NULL,
    character   TEXT NOT NULL,
    login_verb  TEXT NOT NULL,          -- e.g. "connect"
    login_name  TEXT NOT NULL,          -- player or character name
    secret_enc  BLOB,                   -- encrypted password or token
    secret_nonce BLOB NOT NULL,         -- per-row AEAD nonce
    secret_key_id TEXT NOT NULL,        -- which local master key wrapped it
    auto_login  INTEGER DEFAULT 1,
    UNIQUE(account_id, game_name, character)
);

-- Persistent session state (for resume after Hydra restart)
CREATE TABLE saved_sessions (
    id          TEXT PRIMARY KEY,        -- opaque token
    account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    created     TEXT NOT NULL,
    last_active TEXT NOT NULL,
    links_json  TEXT                     -- serialized link list
);
```

### Password Hashing

Argon2id with sensible defaults (time=3, mem=64MB, parallelism=1).
libmux provides SHA1; Argon2 is added as a dependency or bundled
(reference implementation is ~1000 lines of C).

### Stored Game Login Material

Hydra stores structured login material, not a fully expanded plaintext
command string. For a typical MUSH/MUD/MUCK game this is enough to
construct and send a normal login command such as:

```
connect <login_name> <password>
```

`secret_enc` contains encrypted login secret material. The encryption
key is local to the Hydra host and is not stored in the SQLite
database. This keeps the database schema independent of one exact game
command format and avoids persisting reusable plaintext command lines.

### Reversible Credential Protection

Hydra must be able to recover stored game passwords in order to perform
auto-login, so one-way password hashing is not applicable to
`game_credentials`. Instead:

- Hydra account passwords are hashed with Argon2id and are never
  recoverable.
- Stored game credentials are encrypted with an AEAD cipher and are
  recoverable only by Hydra with access to the local key material.
- The SQLite database stores ciphertext, nonce, and key identifier, but
  not the plaintext secret and not the master key.

Recommended Phase 1 approach:

- Algorithm: AES-256-GCM or ChaCha20-Poly1305
- One random nonce per stored credential row
- One locally configured master key, referenced by `secret_key_id`
- Additional authenticated data (AAD): `account_id`, `game_name`,
  `character`, and `login_name`

This gives confidentiality for the stored secret and integrity for the
credential record binding.

### Master Key Management

Hydra's credential-encryption master key is not stored in SQLite. It is
loaded from local host configuration at startup, for example:

- a root-owned file such as `/etc/hydra/master.key` with restrictive
  permissions
- a dedicated OS key store or secret service, if available

Phase 1 assumes a local key file is sufficient.

Operational rules:

- Hydra should refuse to enable auto-login if no master key is
  configured.
- Hydra may still start without the key for proxy-only use, but stored
  encrypted game credentials cannot be decrypted in that mode.
- Loss of the master key means stored game passwords are unrecoverable.
  Users must re-enter them.
- The database alone is not enough to recover game passwords.

### Key Rotation

Hydra should support key rotation by key identifier:

1. A new master key is installed and assigned a new `secret_key_id`
2. Newly stored credentials use the new key
3. Existing credentials are re-encrypted lazily on successful decrypt
   or eagerly via an admin maintenance command
4. Old keys remain available until all credentials have been rewrapped

Hydra does not need to reveal stored game passwords to administrators
or users. Operationally, credentials should be writable and replaceable,
not exportable in plaintext.

### First-Run Bootstrap

If Hydra starts with an empty accounts table, it enters bootstrap
mode: the first connection that creates an account automatically gets
admin privileges. Alternatively, `hydra --create-admin <user>` creates
an admin account from the command line before first run.

## Session Resume Protocol

When a client connects and authenticates to Hydra, Hydra checks for
an existing session:

```
Client authenticates as "alice"
    │
    ▼
SELECT * FROM saved_sessions WHERE account_id = ?
    │
    ├── No saved session, no in-memory session
    │   → Create new session, show game menu
    │
    ├── In-memory session exists (detached or has other front-doors)
    │   → Attach this front-door to existing session
    │   → Replay in-memory scroll-back
    │   → Resume live output from active links
    │
    └── Saved session in SQLite (after Hydra restart)
        → Restore session from SQLite
        → Attach this front-door
        → No guaranteed scroll-back replay in Phase 1
        → Reconnect back-door links per saved link list
```

No special client-side protocol is needed. The client logs in
normally. Hydra handles resume server-side based on identity.

## Game Configuration

### Config File Format

```
# hydra.conf

# --- Network ---
listen telnet       0.0.0.0:4201
listen telnet+tls   0.0.0.0:4202 cert=/path/to/cert.pem key=/path/to/key.pem
listen websocket    0.0.0.0:4203 cert=/path/to/cert.pem key=/path/to/key.pem

# --- Database ---
database /var/lib/hydra/hydra.sqlite

# --- Session defaults ---
scrollback_lines    10000
session_idle_timeout 24h
link_reconnect_timeout 5m

# --- Logging ---
log_file /var/log/hydra/hydra.log
log_level info

# --- Games ---
game "Shangrila" {
    host        localhost
    port        2860
    transport   tcp
    type        local
    binary      /opt/tinymux/game/bin/netmux
    workdir     /opt/tinymux/game
    autostart   yes
    reconnect   yes
    retry       5s 10s 30s 60s
    charset     utf-8
}

game "LegacyMUD" {
    host        mud.example.com
    port        4000
    type        remote
    reconnect   yes
    retry       10s 30s 60s
    charset     latin-1
}
```

### Game Config Data Structure

```cpp
struct GameConfig {
    std::string         name;
    enum { Tcp, Unix }  transport;
    std::string         host;           // for Tcp
    uint16_t            port;           // for Tcp
    std::string         socketPath;     // for Unix
    enum { Local, Remote } type;

    // Local game management
    std::string         binary;     // path to game executable
    std::string         workdir;    // working directory
    bool                autostart;  // start on Hydra startup

    // Reconnect policy
    bool                reconnect;
    std::vector<int>    retrySchedule;  // seconds between retries

    // Protocol
    ganl::EncodingType  charset;
};
```

## Local vs. Remote Game Behavior

The design distinguishes two cases for back-door connection loss:

### Local game with @restart (TinyMUX)

TinyMUX preserves non-TLS file descriptors across `exec()`. Hydra's
back-door connection is plain telnet (no TLS), so **the back-door fd
survives the game's @restart**. From Hydra's perspective, nothing
happened—the connection stays up, data continues to flow. The player
sees no interruption.

This is the primary design goal: Hydra's architecture is specifically
set up so the back-door connection is the kind that TinyMUX already
knows how to preserve.

### Remote game or game crash

If the game process actually dies (crash, full shutdown, remote network
failure), the back-door TCP connection closes. Hydra detects this via
a Read event returning 0 bytes or an error. The link enters
`Reconnecting` state and Hydra begins the retry schedule.

During reconnection:

- The front-door session stays alive
- Scroll-back continues to record any status messages
- The player sees `[GameName: reconnecting...]` status updates
- On successful reconnect, Hydra re-sends the normal login command if
  auto-login is enabled, otherwise the player logs in manually
- The player sees `[GameName: reconnected]` and output resumes

## Build Integration

### Directory Layout

```
mux/
├── hydra/
│   ├── Makefile.in             # autoconf-generated
│   ├── hydra_main.cpp          # main(), config parsing, event loop
│   ├── front_door.cpp          # Front-door listener + connection mgmt
│   ├── front_door.h
│   ├── back_door.cpp           # Back-door outbound connections
│   ├── back_door.h
│   ├── session_manager.cpp     # Session lifecycle, command dispatch
│   ├── session_manager.h
│   ├── telnet_bridge.cpp       # Charset/color translation
│   ├── telnet_bridge.h
│   ├── account_manager.cpp     # SQLite account CRUD, password hashing
│   ├── account_manager.h
│   ├── scrollback.cpp          # Ring buffer
│   ├── scrollback.h
│   ├── process_manager.cpp     # fork/exec/waitpid for local games
│   ├── process_manager.h       # (Phase 2)
│   ├── config.cpp              # Config file parser
│   ├── config.h
│   └── hydra.conf.example
├── ganl/                       # Shared — gains OutboundConnection
├── include/                    # Shared headers
├── libmux/                     # Shared — string, UTF-8, time, etc.
└── src/                        # Game driver (netmux)
```

### Makefile Targets

From the repo root:

- `make` — builds libmux.so, netmux, engine.so, modules, **and hydra**
- `make install` — installs all binaries including hydra
- `make hydra` — builds hydra only
- `make test` — runs game smoke tests (hydra has its own test target)
- `make hydra-test` — runs hydra-specific tests

### Link Dependencies

```
hydra: libmux.so ganl/*.o hydra/*.o -lsqlite3 -lssl -lcrypto -lpthread
```

No dependency on engine.so or any game module.

## Phase Boundaries

### Phase 1: Minimal Viable Proxy

Deliverables:

1. GANL `OutboundConnection` + client-side telnet negotiation
2. `hydra` binary with single-threaded event loop
3. Front door: TLS telnet listener (one port)
4. Back door: plain telnet or Unix socket to one configured game
5. Session manager: login, resume, detach
6. Telnet bridge: charset conversion (UTF-8 ↔ Latin-1 ↔ CP437)
7. Scroll-back buffer with replay on reconnect
8. SQLite accounts + structured game login material
9. Config file parser
10. Basic logging

What a Phase 1 session looks like:

```
$ openssl s_client -connect hydra.example.com:4202
  ┌─────────────────────────────────┐
  │  Hydra v0.1                     │
  │  Login: alice                   │
  │  Password: ****                 │
  │                                 │
  │  Resuming session...            │
  │  [Shangrila: connected]         │
  │                                 │
  │  -- Scroll-back (3 lines) --    │
  │  Amberyl says "brb"             │
  │  The sun sets over the plaza.   │
  │  A gentle breeze blows.         │
  │  -- End scroll-back --          │
  │                                 │
  │  > look                         │
  │  Town Square                    │
  │  You see a fountain here.       │
  └─────────────────────────────────┘
```

### Phase 2 Additions

- Multiple game configurations, `/connect`, `/switch`, `/links`
- Process manager (local game start/stop/restart)
- Game credential storage and auto-login
- Reconnect with configurable backoff

### Phase 3 Additions

- WebSocket front-door (GANL already has WebSocket in netmux)
- gRPC front-door (requires protobuf dependency)
- GMCP forwarding between sides
- Color depth translation via `color_ops`

### Phase 4 Additions

- Session state serialization to SQLite on shutdown
- Session restore from SQLite on startup
- Controlled drain (stop accepting, wait for sessions to detach)
