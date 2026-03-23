# Hydra Proxy — Open Issues & Opportunities

## Recently Fixed

- ~~gRPC Subscribers Steal Messages~~ — Fixed: per-subscriber queues (d0d5d05)
- ~~GameSession Not Negotiating ColorFormat~~ — Fixed: SetPreferences in ClientMessage oneof (6853a2e)
- ~~GameSessionRequest dead API~~ — Fixed: removed, replaced by SetPreferences (6853a2e)
- ~~OutputQueue Pre-rendering~~ — Fixed: stores PUA, renders per-subscriber (634ce31)
- ~~Saved Session Timestamps Clobbered~~ — Fixed in both restore paths (621a80b, 96b73e2)
- ~~Dead Links Drop activeLink~~ — Fixed: remapped on compaction (621a80b)
- ~~Config Parser Ignores TLS Material~~ — Fixed: TLS listener infra wired into GANL (8237a3a)
- ~~Unsafe Direct send() Calls~~ — Fixed: all writes routed through GANL postWrite via safeWrite() (3dd350f)
- ~~GMCP Frame Manual Building~~ — Fixed: centralized in telnet_utils.h (bc01d39)
- ~~Restored Session Scrollback Gap~~ — Fixed: links deferred until player login provides key (541d842)

## Bugs & Technical Debt

### ~~Naming Inconsistency~~
- **Fixed:** Standardized to `internalId` (uint64 in-memory) and `persistId` (string durable) everywhere. (348d8f5)
- **Status:** Windows agent working on this.

## Bugs & Technical Debt

### Telnet `IAC` Escaping Missing in SB Frames
- **Issue:** `buildGmcpFrame` and `buildNawsFrame` in `telnet_utils.h` do not escape `IAC` (255) bytes in their payloads.
- **Impact:** If a GMCP payload or a NAWS width/height byte happens to be 255, it will be interpreted as the start of a telnet command, breaking the sub-negotiation and potentially the entire stream.
- **Fix:** Payload bytes must be checked and escaped as `IAC IAC` (255, 255).

### $O(N)$ Memory Check in `onBackDoorData`
- **Issue:** Every time a game sends data, Hydra iterates over *all* active sessions to calculate global scrollback memory usage.
- **Impact:** Significant performance bottleneck and lag as the number of sessions grows.
- **Fix:** Replace with a global atomic counter updated by `ScrollBack::append` and `ScrollBack` eviction/load.

### `GetScrollBack` RPC Ignores `color_format`
- **Issue:** `GrpcServer::GetScrollBack` ignores the requested `color_format` in the `ScrollBackRequest`.
- **Impact:** Clients receive raw PUA text or whatever is stored in the ring buffer, even if they requested PLAIN or TrueColor.
- **Fix:** Use `OutputItem::render` or `TelnetBridge::renderForClient` during replay.

### `SetPreferences` Overwrites Unspecified Fields
- **Issue:** Protobuf 3 scalar fields default to 0. When a client sends `SetPreferences` (e.g., for a NAWS resize) but doesn't set `color_format`, the server receives 0 and resets the subscriber to `ANSI_TRUECOLOR`.
- **Impact:** Unexpected color rendering changes when resizing windows or updating other preferences.
- **Fix:** Add `COLOR_NO_CHANGE = 0` to the enum or make fields `optional` (proto3).

### Thread-Unsafe `strerror()` in `safeWrite`
- **Issue:** `SessionManager::safeWrite` uses `strerror(err)` for logging.
- **Impact:** `strerror` is not thread-safe on many platforms as it uses a static buffer.
- **Fix:** Use `strerror_r` or a safe wrapper.

## Design Notes

### Browser Transport Strategy: WebSocket for I/O, gRPC-Web for Management

The grpc-web protocol (HTTP/1.1 POST) has a fundamental asymmetry:

- **Server-streaming works:** Hydra's Subscribe RPC pushes game output as
  chunked HTTP frames.  The browser's `fetch()` + `ReadableStream` receives
  them incrementally.  This is live streaming — output flows as it arrives
  from the game.

- **Client-streaming does not exist in grpc-web:** There is no way for the
  browser to stream input lines to the server over the same connection.
  Each input line requires a separate `SendInput` unary RPC (one HTTP
  POST round-trip per line).

- **Bidi streaming (GameSession) is not available from browsers.**  Native
  gRPC clients (TF, Console, Android) use the bidi `GameSession` RPC for
  both input and output on a single persistent stream.  Browsers cannot.

**What this means in practice:**

| Capability | Native gRPC | Browser grpc-web | Browser WebSocket |
|-----------|-------------|------------------|-------------------|
| Game output streaming | GameSession bidi | Subscribe (chunked) | Telnet over WS |
| Input | GameSession bidi | SendInput (unary) | Telnet over WS |
| SetPreferences | First ClientMessage | Not available | N/A (telnet NAWS) |
| Link/credential/process mgmt | Unary RPCs | Unary RPCs | /commands in text |
| GMCP | Via GameSession | SubscribeGmcp | Telnet subneg |
| Round-trips per input line | 0 (streamed) | 1 (POST each) | 0 (streamed) |

**Recommended browser architecture:**

1. Use **WebSocket** (Hydra telnet front-door) for live game I/O — full
   bidi, no per-line overhead, telnet protocol handles NAWS/charset/GMCP.
2. Use **gRPC-Web** for management RPCs only — ListGames, Connect,
   SwitchLink, credentials, process control, GetScrollBack.
3. Or use WebSocket for everything (the existing web client already works
   this way) and treat gRPC-Web as an optional enhancement.

**Future option:** The [Connect protocol](https://connectrpc.com/) from Buf
supports bidi streaming over WebSocket from browsers.  Adding Connect
support to Hydra would give browsers the full `GameSession` experience.
This would require a Connect-compatible HTTP handler alongside the
existing grpc-web handler.

### ~~Terminal Capability Reporting~~
- **Fixed:** `SetPreferences` on `GameSession` streams + `terminal_width`/`terminal_height` on `SessionRequest` for `Subscribe`. Both forward NAWS to the game. (9033f3d)

### GMCP Synthesis
- **Issue:** Hydra only forwards GMCP if both sides support it.
- **Opportunity:** Synthesize `Core.KeepAlive` or provide a `Hydra.*` GMCP package for proxy-specific features.

## Opportunities for Improvement

### ~~Persistence of OutputQueue~~
- **Mitigated:** The OutputQueue itself is transient by design (it's a delivery buffer, not storage). The crash window is now narrowed: sessions with gRPC subscribers flush scroll-back every 15s instead of 60s, and sessions flush immediately when the last front-door disconnects. Graceful shutdown flushes everything. The remaining gap is at most 15s of output during an ungraceful crash, which is acceptable. (1b3a6fc)

### ~~Master Key Management~~
- **Fixed:** Three key sources: env var (`HYDRA_MASTER_KEY`), file with permission checking, auto-generation on first run. System keystores deferred — not practical for server daemons. (a60cd06)

### ~~Rate Limiting Cleanup~~
- **Fixed:** All resource limits enforced. `maxScrollbackMemoryMb` now tracked per-session via `ScrollBack::memoryBytes()` with global check in `onBackDoorData()`. (706800347)
