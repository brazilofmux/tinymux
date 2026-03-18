# Win32 Console MU* Client — Implementation Plan

## Architecture

The client mirrors the TitanFugue (ncurses) client's modular architecture but
replaces every Unix-specific subsystem with native Win32 equivalents:

| Module          | TF (ncurses)           | Win32 Console                    |
|-----------------|------------------------|----------------------------------|
| Event loop      | `poll()`               | IOCP `GetQueuedCompletionStatus` |
| Network I/O     | BSD sockets + OpenSSL  | Winsock2 overlapped + Schannel   |
| Terminal output | ncurses                | Win32 Console API + VT sequences |
| Terminal input  | Ragel VT parser        | `ReadConsoleInputW` (structured) |
| Window resize   | `SIGWINCH`             | `WINDOW_BUFFER_SIZE_EVENT`       |

## File Structure

```
client/console/
    console.sln
    console.vcxproj
    src/
        main.cpp          -- Entry point, IOCP event loop
        app.h / app.cpp   -- Top-level state, send/receive logic
        connection.h/cpp  -- IOCP async TCP, Telnet, Schannel TLS
        terminal.h/cpp    -- Console output, input line, status bar
        telnet.h          -- Telnet protocol constants
        command.h/cpp     -- Built-in /commands
        world.h/cpp       -- World definitions and database
```

## Phases

### Phase 1: Skeleton + IOCP + Plain TCP — DONE

- IOCP event loop with dedicated console input reader thread
- Async TCP via ConnectEx/WSARecv with overlapped I/O
- Telnet state machine: NAWS, TTYPE, CHARSET, SGA, ECHO, GMCP
- Win32 Console API terminal with VT sequence support for ANSI color
- UTF-8 console codepage, input line editing with history
- Per-world output scrollback, multiple simultaneous connections
- Commands: /connect, /dc, /fg, /world, /listsockets, /listworlds, /help, /quit

### Phase 2: Schannel TLS

Client-side Schannel implementation (simpler than GANL's server-side):

- `AcquireCredentialsHandle` with `SECPKG_CRED_OUTBOUND` (no client cert)
- `InitializeSecurityContext` handshake loop
- `EncryptMessage` / `DecryptMessage` for data transfer
- Handle `SEC_I_CONTINUE_NEEDED`, `SEC_E_INCOMPLETE_MESSAGE`, `SEC_I_RENEGOTIATE`
- Integrate into Connection: after TCP connect completes, perform TLS handshake
  before starting telnet negotiation
- Reference: `mux/ganl/src/schannel_transport.cpp` (adapt for client mode)

### Phase 3: Unicode 16 + libmux Color Rendering — DONE

All Unicode functionality from libmux.dll — no reimplementation:

- Link against `libmux.lib` for `co_*` functions
- **Grapheme cluster segmentation**: `co_cluster_count()`, `co_cluster_advance()`,
  `co_mid_cluster()`, `co_delete_cluster()` for input line editing
- **Display width**: `co_visual_width()`, `co_console_width()` for column counting
  with fullwidth CJK characters
- **Color rendering**: `co_render_truecolor()` (Win10+ VT), `co_render_ansi256()`
  (fallback), `co_render_ansi16()` (legacy) — feed output through the appropriate
  renderer based on console VT capability
- **NFC normalization**: normalize incoming server text and user input

### Phase 4: GMCP/MSSP + Status Bar + Polish — DONE

- GMCP (telnet option 201): parse JSON payloads, store per-connection state
- MSSP (telnet option 70): key-value pairs from server
- Rich status bar with format fields (world name, idle time, connection count,
  activity indicators)
- Prompt detection and display
- `/log` command (file logging)
- `/recall` (scrollback search)
- Input history per-world

### Phase 5: Triggers + Keybindings

- Basic triggers: `/def -t'pattern' command`, `/gag`, `/hilite`
- Keybind customization: port TF's binding trie for multi-key sequences
- Macro database with pattern matching
- Timer system (`/repeat`)
- World file save/load

## Build

Visual Studio 2026 (PlatformToolset v145), x64 only, C++17.

```
cd client\console
MSBuild console.sln -p:Configuration=Release -p:Platform=x64
```

Output: `client\console\bin\Release\console.exe`

## Design Decisions

1. **No GANL dependency** — GANL is a server library. A thin IOCP+Schannel
   layer is far simpler for a client with 1-5 outbound connections.
2. **Console API + VT hybrid** — `ENABLE_VIRTUAL_TERMINAL_PROCESSING` on
   output lets us feed `co_render_truecolor()` output directly. Input uses
   structured `ReadConsoleInputW` (no VT parsing needed).
3. **Dedicated input thread** — console input is blocking; a reader thread
   posts events to the IOCP via `PostQueuedCompletionStatus`.
4. **libmux.dll shared dependency** (Phase 3+) — all Unicode tables, color
   ops, grapheme segmentation, NFC normalization from the DLL.
