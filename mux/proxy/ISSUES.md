# Hydra Proxy — Open Issues & Opportunities

## Bugs & Technical Debt

### gRPC Subscribers Steal Messages From Each Other
- **Issue:** `HydraSession::OutputQueue` and `gmcpQueue` are global per-session queues, but every `Subscribe`, `SubscribeGmcp`, and `GameSession` consumer pops from the same shared queue.
- **Evidence:** `proxy/grpc_server.cpp:384-405`, `proxy/grpc_server.cpp:461-471`, `proxy/grpc_server.cpp:507-523`, `proxy/session_manager.h:60-79`
- **Impact:** The first subscriber that drains the queue consumes the message for everyone else. Two live gRPC clients on the same Hydra session will randomly miss text and GMCP updates instead of each receiving a full copy.
- **Opportunity:** Replace the shared pop-once queue with per-subscriber cursors or per-subscriber queues. This also gives a place to store subscriber-specific rendering preferences.

### `GameSession` Stream Is Not Actually Negotiating `ColorFormat`
- **Issue:** The spec comments say clients specify `ColorFormat` when opening `GameSession`, but the RPC signature is `stream ClientMessage` with no initial request envelope, and the server only reads metadata for `authorization`.
- **Evidence:** `proxy/hydra.proto:197`, `proxy/hydra.proto:367`, `proxy/grpc_server.cpp:286-318`
- **Impact:** The advertised `GameSessionRequest.color_format` is dead API surface. The server cannot honor per-stream render preferences, and client/server expectations are already diverging.

### gRPC `GameSession` ignoring `ColorFormat`
- **Issue:** The `hydra.proto` defines `ColorFormat` and a `GameSessionRequest` message, but the `GameSession` RPC is defined as `rpc GameSession(stream ClientMessage) returns (stream ServerMessage)`. There is no way for the client to pass the initial `ColorFormat` when opening the stream.
- **Root Cause:** The `.proto` definition and the `grpc_server.cpp` implementation are out of sync.
- **Impact:** All gRPC clients receive output rendered in TrueColor, regardless of their actual capabilities.

### `OutputQueue` Pre-rendering Limitation
- **Issue:** `SessionManager::onBackDoorData` pre-renders game output to TrueColor ANSI before pushing it to the `HydraSession::OutputQueue`.
- **Root Cause:** The `OutputQueue` stores `OutputItem` which contains already-rendered `text`.
- **Impact:** This prevents different gRPC subscribers to the same session from receiving different color formats (e.g., one client wanting TrueColor and another wanting PLAIN text). The queue should store the internal PUA-encoded UTF-8 and render per-subscriber.

### Restored Detached Sessions Cannot Persist New Scrollback Until A User Logs In Again
- **Issue:** Startup restore intentionally brings back detached sessions without a `scrollbackKey`, and `flushSession()` skips DB flushes when that key is empty.
- **Evidence:** `proxy/session_manager.cpp:1463-1472`, `proxy/session_manager.cpp:2148-2165`, `docs/hydra-problem-statement.md`
- **Impact:** If Hydra restarts while sessions are detached, back-door links reconnect and keep receiving output, but any new scrollback generated before the next successful login only lives in memory. A second Hydra crash loses exactly the data R3 says should survive restarts.
- **Opportunity:** Persist an encrypted session-wrapped key at login time or pause restored detached links until the player re-authenticates and rehydrates the scrollback key.

### Saved Session Timestamps Are Clobbered On Interactive Resume
- **Issue:** `resumeSavedSession()` restores `persistId` and scrollback from SQLite but resets `created` and `lastActivity` to `time(nullptr)` instead of the saved values.
- **Evidence:** `proxy/session_manager.cpp:1475-1493`
- **Impact:** Session metadata exposed through `GetSession` becomes inaccurate after a restart-and-resume path, which will also distort idle-time policy decisions and operator visibility.

### Unsafe Direct `send()` Calls
- **Issue:** `SessionManager` and `grpc_server.cpp` frequently use raw `send()` on `ganl::ConnectionHandle` (cast to `int`).
- **Root Cause:** Bypassing the GANL `NetworkEngine`'s abstraction layer.
- **Impact:**
  - **Blocking I/O:** `send()` can block if the socket buffer is full, hanging the gRPC reader/writer threads or the main GANL thread.
  - **Thread Safety:** Multiple threads (main GANL thread, gRPC reader thread, gRPC writer thread) may attempt to `send()` to the same FD simultaneously without locking.
  - **Partial Writes:** `send()` may not write the full buffer; GANL's write queue handles this, but raw `send()` calls here do not.

### GMCP Frame Manual Building
- **Issue:** Telnet IAC SB GMCP frames are manually constructed in multiple places (`grpc_server.cpp`, `SessionManager`).
- **Impact:** Duplicate logic and potential for incorrect telnet escaping (IAC IAC). This should be centralized in a protocol handler.

### Dead Links Are Dropped From Persistence Without Rewriting `activeLink`
- **Issue:** `flushSession()` omits dead links from `links_json` but still writes the old `activeLink` index.
- **Evidence:** `proxy/session_manager.cpp:1443-1453`
- **Impact:** The saved `activeLink` index can point past the compacted link array on reload. `restoreSessionLinks()` clamps it later, so this does not crash, but it does silently switch the user to link 1 after restart instead of preserving intent.
- **Opportunity:** Serialize a stable link identifier or remap `activeLink` while compacting.

### Naming Inconsistency
- **Issue:** The codebase uses `persistId`, `session_id`, and `id` (numeric) to refer to sessions in different contexts.
- **Impact:** Developer confusion and potential for bugs when mapping between SQLite, gRPC, and internal memory.

### Config Parser Ignores TLS Material For Front-Door Listeners
- **Issue:** `parseListenLine()` accepts `cert=` and `key=` for TLS listeners, but `hydra_main.cpp` never uses those fields when creating listeners.
- **Evidence:** `proxy/config.cpp:46-81`, `proxy/hydra_main.cpp:154-188`
- **Impact:** TLS listener configuration looks supported in `hydra.conf` and docs, but the main process currently just opens a plain listener. That is a functional gap with security consequences, not cosmetic debt.

## Feature Gaps

### gRPC-Web Bidi Streaming
- **Issue:** `grpc_server.cpp` has a custom `Subscribe` method for gRPC-Web that uses chunked encoding, but it doesn't implement the full gRPC-Web specification for bi-directional streaming.
- **Opportunity:** Implement a proper gRPC-Web proxy or use a standard one (like Envoy) if full browser support is needed.

### Terminal Capability Reporting (gRPC)
- **Issue:** There is no mechanism in the gRPC `GameSession` to report terminal size (NAWS) or terminal type (TTYPE).
- **Impact:** Hydra uses default values (80x24) when negotiating with the back-door game server, even if the gRPC client has a different window size.

### GMCP Synthesis
- **Issue:** Hydra is intended to be a smart proxy, but it currently only forwards GMCP if both sides support it.
- **Opportunity:** Hydra should synthesize common GMCP messages (like `Core.KeepAlive`) or provide its own `Hydra.*` GMCP package for proxy-specific features.

## Opportunities for Improvement

### Persistence of `OutputQueue`
- **Opportunity:** If Hydra restarts, the in-memory `OutputQueue` for gRPC is lost. While scrollback is persisted in SQLite, the live queue of unsent messages is not.

### Master Key Management
- **Issue:** The design specifies a master key for credential encryption, but the current implementation (check `crypto.cpp`) may be using a hardcoded or simple file-based approach.
- **Opportunity:** Integrate with system keystores (Linux Secret Service, macOS Keychain, Windows DPAPI).
