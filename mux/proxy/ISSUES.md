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

### GMCP Frame Manual Building
- **Issue:** Telnet IAC SB GMCP frames are manually constructed in `grpc_server.cpp` and `session_manager.cpp`.
- **Impact:** Duplicate logic, potential for incorrect IAC escaping.
- **Opportunity:** Centralize in a protocol helper function.

### Restored Detached Sessions Cannot Persist New Scrollback
- **Issue:** Eagerly restored sessions have no `scrollbackKey` (requires player login). New game output received before login only lives in memory.
- **Impact:** A second Hydra crash loses output generated between restarts.
- **Opportunity:** Pause restored links until player authenticates, or persist an encrypted session key.

### Naming Inconsistency
- **Issue:** Sessions are identified by `persistId` (string), `HydraSessionId` (uint64), and SQLite `id` (TEXT) in different contexts.
- **Impact:** Developer confusion.

## Feature Gaps

### gRPC-Web Bidi Streaming
- **Issue:** grpc-web Subscribe uses chunked HTTP/1.1 for output, but input still goes through separate unary RPCs.
- **Opportunity:** True bidi would need WebSocket-based gRPC (Connect protocol) or Envoy.

### Terminal Capability Reporting
- **Status:** Partially resolved. `SetPreferences` carries width/height/type on `GameSession` streams. Legacy `Subscribe` does not carry terminal size.
- **Opportunity:** Forward NAWS from Subscribe callers if they provide it.

### GMCP Synthesis
- **Issue:** Hydra only forwards GMCP if both sides support it.
- **Opportunity:** Synthesize `Core.KeepAlive` or provide a `Hydra.*` GMCP package.

## Opportunities for Improvement

### Persistence of OutputQueue
- **Opportunity:** In-memory gRPC output queue is lost on Hydra restart. Scrollback is persisted but unsent live messages are not.

### Master Key Management
- **Opportunity:** Integrate with system keystores (Linux Secret Service, macOS Keychain, Windows DPAPI) instead of file-based master key.

### Rate Limiting Cleanup
- **Status:** Per-IP connection limits and login lockout are enforced. `maxScrollbackMemoryMb` is parsed but not enforced (needs per-session memory tracking).
