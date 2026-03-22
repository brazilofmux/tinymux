# Hydra: High-Level Functional Specification

## Status: Design

Branch: `master`

Predecessor: `docs/hydra-problem-statement.md`

## Overview

Hydra is a persistent connection proxy for MUD games. It sits between
clients and game servers, owning TLS on the internet side, maintaining
session state across disconnects on both sides, and translating
protocols and encodings between heterogeneous clients and games.

```
                    Internet                          Local / LAN
                    ════════                          ══════════

  ┌──────────┐                                       ┌──────────┐
  │ Telnet   │──SSL──┐                          ┌────│ Game A   │
  │ Client   │       │                          │    │ (local)  │
  └──────────┘       │                          │    └──────────┘
                     │    ┌──────────────┐      │
  ┌──────────┐       ├───►│              │──────┤    ┌──────────┐
  │ WebSocket│──WSS──┤    │    Hydra     │──────├────│ Game B   │
  │ Client   │       │    │              │      │    │ (local)  │
  └──────────┘       │    └──────────────┘      │    └──────────┘
                     │      │  ▲                │
  ┌──────────┐       │      │  │ Process Mgmt   │    ┌──────────┐
  │ gRPC     │──TLS──┘      │  │                └────│ Game C   │
  │ Client   │              │  │                     │ (remote) │
  └──────────┘              ▼  │                     └──────────┘
                     ┌──────────────┐
                     │   SQLite DB  │
                     │  (accounts,  │
                     │   sessions)  │
                     └──────────────┘
```

## Process Architecture

Hydra is a single long-running process: the `hydra` binary.

**Linked libraries:**

- **libmux.so** — core utilities (string, UTF-8, time, SHA1, alloc)
- **GANL** (statically linked or as.so)—non-blocking I/O engine,
  TLS termination, telnet protocol handling

**Not linked:**

- engine.so—Hydra has no game logic
- No game modules (comsys, mail, lua)

**Build integration:** Hydra builds from the repo root via `make` like
everything else. It lives in `mux/hydra/` alongside `mux/src/` (the
game driver) and `mux/modules/` (game modules).

## Core Concepts

### Sessions

A **session** is the central abstraction. It represents an
authenticated user's presence on Hydra, independent of any specific
network connection.

```
Session
├── Identity (Hydra user account)
├── Front-door connections (0..N)
│   ├── Connection 1: TLS telnet from 203.0.113.5
│   └── Connection 2: WebSocket from 198.51.100.7
├── Back-door connections (0..N)
│   ├── Link 1: Game "Shangrila" character "Amberyl"
│   └── Link 2: Game "Aurelia" character "Keldar"
├── Active link (which back-door connection gets input)
├── Scroll-back buffer (ring buffer, configurable size)
└── Telnet state (per front-door and per back-door)
```

A session exists from login until explicit logout or idle timeout. It
survives any number of front-door disconnects/reconnects and back-door
disconnects/reconnects.

### Links

A **link** is a connection from Hydra to a game server on behalf of a
session. Each link represents one character on one game.

Links have their own lifecycle:

- **Active** — connected to the game, exchanging data
- **Reconnecting** — game connection lost, Hydra retrying
- **Suspended** — user explicitly backgrounded this link
- **Dead** — game unreachable after retry exhaustion

### Front-Door Connections

A front-door connection is a client's network connection to Hydra.
Multiple front-door connections can attach to the same session (e.g.,
a player connected from both a desktop client and a phone).

Each front-door connection has its own:

- TLS state
- Telnet negotiation state (charset, terminal size, color capability)
- Protocol type (telnet, WebSocket, gRPC)

### Back-Door Connections

A back-door connection is Hydra's connection to a game server. It
speaks whatever protocol the game expects (usually telnet, possibly
over a Unix domain socket for local games).

Each back-door connection has its own:

- Telnet negotiation state (reflecting Hydra's negotiation with the
  game, not the client's)
- Character set and encoding
- Authentication state (logged in as which character)

## Functional Components

### 1. Front Door

Accepts client connections. Reuses GANL for non-blocking I/O, TLS
termination, and telnet protocol handling.

**Listeners:**

| Protocol       | Default Port | TLS | Notes                          |
|----------------|--------------|-----|--------------------------------|
| Telnet         | 4201         | No  | Plain telnet (upgrade via STARTTLS) |
| Telnet+TLS     | 4202         | Yes | TLS-wrapped telnet             |
| WebSocket      | 4203         | Yes | WSS, for browser clients       |
| gRPC           | 4204         | Yes | Protobuf-based, for mobile/API |

Ports are configurable. Hydra can listen on as many ports as needed.

**Connection flow:**

1. Client connects to a front-door port
2. TLS handshake (if applicable)
3. Telnet negotiation (charset, NAWS, TTYPE, etc.)
4. Hydra presents a login prompt (its own, not a game's)
5. User authenticates (username + password, or token)
6. Session created or resumed
7. If session has active links, output begins flowing
8. If no links, Hydra presents a menu of configured games

### 2. Session Manager

Manages session lifecycle. This is the heart of Hydra.

**Responsibilities:**

- Create sessions on login
- Resume sessions on reconnect (match by authenticated identity)
- Attach/detach front-door connections to/from sessions
- Attach/detach back-door links to/from sessions
- Manage scroll-back buffer (ring buffer per session)
- Route input from front-door to active link
- Route output from all links to all attached front-door connections
- Handle session idle timeout and cleanup

**Session table storage:** SQLite. The session table survives Hydra
restarts (for the graceful self-upgrade case). Active session state
is in-memory; persistent state (accounts, link configurations, last-
known session state) is in SQLite.

**Session commands:** Hydra intercepts certain input as its own
commands rather than forwarding to the game:

| Command              | Action                                         |
|----------------------|------------------------------------------------|
| `/games`             | List configured games                          |
| `/connect <game>`    | Open a new link to a game                      |
| `/switch <link>`     | Change active link                             |
| `/links`             | List active links and their status             |
| `/detach`            | Detach front-door connection (session persists)|
| `/scroll [n]`        | Show last N lines of scroll-back               |
| `/quit`              | Destroy session and disconnect                 |
| `/passwd`            | Change Hydra password                          |
| `/who`               | Show connected sessions (admin)                |

Commands are prefixed with `/` to distinguish them from game input.
The prefix is configurable.

### 3. Telnet Bridge

The most technically interesting component. Hydra maintains
**independent** telnet negotiation state on each side of every
connection and translates between them.

**Per front-door connection:**

```
Client capabilities (negotiated):
├── Charset: UTF-8
├── Terminal size: 120×40
├── Color: 256-color (xterm-256color)
├── GMCP: supported
└── EOR: supported
```

**Per back-door connection:**

```
Game expectations (negotiated):
├── Charset: Latin-1
├── Terminal size: 80×24 (forwarded from client, or default)
├── Color: ANSI 16-color
├── GMCP: not supported
└── EOR: supported
```

**Translation rules:**

| Aspect      | Direction      | Translation                              |
|-------------|----------------|------------------------------------------|
| Charset     | Game—Client    | Recode from game charset to client charset |
| Charset     | Client—Game    | Recode from client charset to game charset |
| Terminal    | Client—Game    | Forward NAWS (or clamp to game's max)    |
| Color       | Game—Client    | Pass through (client is richer or equal) |
| Color       | Client—Game    | Downgrade (256—16, true—256, strip if needed) |
| GMCP        | Game—Client    | Forward if client supports, else drop    |
| GMCP        | Client—Game    | Forward if game supports, else Hydra handles |
| MSSP        | Game—Client    | Forward (Hydra may augment)              |

**Charset conversion** uses libmux's existing UTF-8 and code page
conversion infrastructure. Hydra maintains conversion tables for all
supported encodings and performs byte-level recoding on the data
stream.

**Color downgrading** uses GANL's existing color_ops (the Ragel-based
PUA color parser). When a game sends 256-color or true-color ANSI and
the client only supports 16-color, Hydra maps to the nearest
available color.

### 4. Back Door

Connects to game servers. Each game is a configured destination with
connection parameters.

**Game configuration (in SQLite or config file):**

```
game "Shangrila" {
    host        localhost
    port        2860
    type        local           # local process or remote TCP
    binary      /home/sdennis/tinymux/mux/game/bin/netmux
    workdir     /home/sdennis/tinymux/mux/game
    autostart   yes             # Hydra starts it if not running
    reconnect   yes             # auto-reconnect on game restart
    retry       5s, 10s, 30s, 60s   # backoff schedule
    charset     UTF-8
}

game "SomeLegacyMUD" {
    host        legacy.example.com
    port        4000
    type        remote
    autostart   no
    reconnect   yes
    retry       10s, 30s, 60s
    charset     Latin-1
}
```

**Connection flow (back-door):**

1. User issues `/connect Shangrila`
2. If game is local and not running and autostart=yes, Hydra starts it
3. Hydra opens TCP (or Unix socket) to game
4. Telnet negotiation with the game (Hydra is the "client" here)
5. Hydra presents stored credentials (or prompts user for them)
6. Link enters Active state
7. Data flows: game output—telnet bridge—front door(s)

**Reconnect behavior:**

When a back-door connection drops (game @restart, crash, network
error):

1. Link enters Reconnecting state
2. Hydra sends a status line to front-door: `[Shangrila: reconnecting...]`
3. Hydra retries per the backoff schedule
4. On success, re-authenticates, link returns to Active
5. On retry exhaustion, link enters Dead state, user is notified

### 5. Process Manager

For locally-hosted games, Hydra can act as a process supervisor.

**Capabilities:**

- Start a game process (fork/exec)
- Monitor it (waitpid, health checks)
- Restart on crash (with backoff)
- Graceful shutdown (send @shutdown command before SIGTERM)
- Log management (capture stdout/stderr)

**Integration with Poor Man's COM:** Hydra can optionally load modules
via the COM interface system. This is how Hydra could be extended with
custom authenticators, protocol handlers, or monitoring integrations.
The initial implementation does not require COM for the proxy itself —
it's a future extension point.

### 6. Account Manager

Hydra maintains its own user accounts in SQLite.

**Schema (conceptual):**

```sql
CREATE TABLE accounts (
    id          INTEGER PRIMARY KEY,
    username    TEXT UNIQUE NOT NULL,
    password    TEXT NOT NULL,       -- bcrypt or argon2
    created     TEXT NOT NULL,
    last_login  TEXT,
    flags       INTEGER DEFAULT 0   -- admin, suspended, etc.
);

CREATE TABLE game_credentials (
    id          INTEGER PRIMARY KEY,
    account_id  INTEGER REFERENCES accounts(id),
    game_name   TEXT NOT NULL,
    character   TEXT NOT NULL,
    credential  TEXT,               -- encrypted game password
    auto_login  INTEGER DEFAULT 1,
    UNIQUE(account_id, game_name, character)
);

CREATE TABLE sessions (
    id          TEXT PRIMARY KEY,    -- session token
    account_id  INTEGER REFERENCES accounts(id),
    created     TEXT NOT NULL,
    last_active TEXT NOT NULL,
    state       TEXT                -- serialized session state
);
```

### 7. Scroll-Back Buffer

Per-session ring buffer that captures game output while no front-door
connection is attached (or always, for `/scroll` support).

**Parameters:**

- Default size: 10,000 lines (configurable per-account or globally)
- Storage: in-memory (fast), with optional SQLite persistence for
  crash recovery
- Replay: on front-door reconnect, Hydra sends buffered output to the
  new connection, respecting the new connection's charset and color
  capabilities

**Flow:**

```
Game output arrives on back-door
        │
        ▼
  Scroll-back buffer ◄── always appended
        │
        ▼
  Any front-door connections attached?
        │
   ┌────┴────┐
   Yes       No
   │         │
   ▼         ▼
 Forward   Buffer only
 to all    (replay on
 front-    reconnect)
 doors
```

## Data Flow

### Normal operation (client connected, game connected)

```
Client                    Hydra                         Game
  │                        │                            │
  │── encrypted bytes ────►│                            │
  │                        │── TLS decrypt ──►          │
  │                        │── telnet parse ──►         │
  │                        │── charset convert ──►      │
  │                        │── telnet re-encode ──►     │
  │                        │────── game bytes ─────────►│
  │                        │                            │
  │                        │◄───── game bytes ──────────│
  │                        │◄── telnet parse ──         │
  │                        │◄── charset convert ──      │
  │                        │◄── color remap ──          │
  │                        │◄── telnet re-encode ──     │
  │                        │◄── scroll-back append ──   │
  │                        │◄── TLS encrypt ──          │
  │◄── encrypted bytes ────│                            │
```

### Client disconnected, game still sending

```
                          Hydra                         Game
                            │                            │
                            │◄───── game bytes ──────────│
                            │── parse, convert ──►       │
                            │── scroll-back append ──►   │
                            │   (no front-door to send)  │
                            │                            │

          ... client reconnects ...

Client                    Hydra
  │                        │
  │── TLS handshake ──────►│
  │── authenticate ───────►│
  │                        │── resume session
  │                        │── replay scroll-back ──►
  │◄── buffered output ────│
  │                        │── live output resumes
```

### Game @restart

```
Client                    Hydra                         Game
  │                         │                            │
  │  (connected, idle)      │◄── connection closed ──────│ @restart
  │                         │                            │
  │◄── "[reconnecting]" ────│                            │
  │                         │                            │
  │                         │   ... retry backoff ...    │
  │                         │                            │ (game back)
  │                         │────── TCP connect ────────►│
  │                         │────── telnet negot. ──────►│
  │                         │────── auto-login ─────────►│
  │                         │                            │
  │◄── "[reconnected]" ─────│                            │
  │                         │◄───── game output ────────►│
  │◄── game output ─────────│                            │
```

## Graceful Self-Upgrade

Hydra itself can be upgraded without dropping client connections,
using the same fd-inheritance pattern as TinyMUX @restart:

1. Admin issues upgrade command
2. Hydra serializes session state to SQLite
3. Hydra detaches all client fds from GANL (but does not close them)
4. Hydra records fd-to-session mappings in a restart database
5. Hydra `exec()`s the new binary
6. New Hydra reads restart database
7. New Hydra re-adopts fds into GANL
8. New Hydra resumes sessions from SQLite
9. Client connections continue uninterrupted—TLS state is preserved
   because the kernel preserved the fds and the TLS library state is
   re-initialized from the existing TCP stream

**Note:** TLS session resumption after exec() is the hardest part.
Options include serializing OpenSSL session state (fragile, version-
dependent) or performing a transparent TLS renegotiation. This needs
further investigation and may be deferred to a later phase.

## Configuration

Hydra reads a configuration file (default: `hydra.conf`) with:

```
# Network
listen telnet       4201
listen telnet+tls   4202 cert=hydra.pem key=hydra.key
listen websocket    4203 cert=hydra.pem key=hydra.key
listen grpc         4204 cert=hydra.pem key=hydra.key

# Accounts
database            hydra.sqlite
password_hash       argon2

# Session defaults
scrollback_lines    10000
idle_timeout        24h
reconnect_timeout   5m

# Logging
log_file            hydra.log
log_level           info

# Games (see game blocks above)
game "Shangrila" { ... }
game "SomeLegacyMUD" { ... }
```

## Build Layout

```
mux/
├── hydra/
│   ├── Makefile
│   ├── hydra_main.cpp          # Entry point, config parsing
│   ├── session_manager.cpp     # Session lifecycle
│   ├── session_manager.h
│   ├── telnet_bridge.cpp       # Protocol translation
│   ├── telnet_bridge.h
│   ├── back_door.cpp           # Game connections
│   ├── back_door.h
│   ├── account_manager.cpp     # User accounts (SQLite)
│   ├── account_manager.h
│   ├── process_manager.cpp     # Game process supervision
│   ├── process_manager.h
│   ├── scrollback.cpp          # Ring buffer
│   ├── scrollback.h
│   └── hydra.conf.example      # Example configuration
├── ganl/                       # Shared with netmux
├── include/                    # Shared headers (libmux, modules)
├── src/                        # Game driver (netmux)
└── modules/                    # Game modules
```

## Phasing

### Phase 1: Minimal viable proxy

- Single front-door protocol (TLS telnet)
- Single back-door protocol (plain telnet)
- Session manager with login/resume
- Telnet bridge with charset conversion
- Scroll-back buffer
- SQLite account storage
- Connect to one game

### Phase 2: Multi-game and process management

- Multiple game configurations
- `/connect`, `/switch`, `/links` commands
- Process manager (start/stop/restart games)
- Game credential storage
- Reconnect with backoff

### Phase 3: Protocol expansion

- WebSocket front-door
- gRPC front-door
- GMCP forwarding
- Color depth translation

### Phase 4: Graceful self-upgrade

- fd-inheritance across exec()
- Session state serialization/restore
- TLS continuity (or transparent renegotiation)

## Open Questions

1. **TLS survival across exec().** OpenSSL session state is deeply
   tied to the process. Can we serialize it? Should we use a different
   approach (kernel TLS, shared-memory TLS context)? Or accept that
   Hydra self-upgrade drops TLS and clients must reconnect?

2. **Back-door protocol enhancement.** Should Hydra speak enhanced
   telnet to TinyMUX games (carrying session metadata in GMCP/MSDP)
   to avoid the login-screen dance? Or is auto-typing credentials
   sufficient?

3. **Session token mechanism.** For client reconnection—cookie-based
   token? TLS client certificate? Time-limited HMAC token?

4. **Multi-front-door output policy.** When two front-door connections
   are attached to the same session, do both get output? Does one
   become read-only? Is this user-configurable?

5. **Scroll-back and charset.** The scroll-back buffer stores output.
   In what encoding? If we store raw game bytes, we need the original
   charset to replay correctly. If we normalize to UTF-8, replay is
   simpler but we've lost the original bytes.
