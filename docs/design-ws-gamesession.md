# Design: WebSocket GameSession Transport for HTML5 Client

## Problem

The HTML5 web client uses legacy grpc-web RPCs: unary `SendInput` for input
and server-streaming `Subscribe` for output. grpc-web doesn't support
bidirectional streaming, so the client can't use the `GameSession` RPC that
native clients use. This means GameSession-only features (like
`SetPreferences` mid-stream) require separate backport to the legacy path.

## Approach

Add a WebSocket-based GameSession transport that reuses the existing protobuf
`ClientMessage`/`ServerMessage` types over binary WebSocket frames. The proxy
already has a full WebSocket codec (`websocket.h/cpp`) and the HTML5 client
already hand-rolls protobuf encoding — no new dependencies on either side.

## Protocol

### Subprotocol Negotiation

The existing WebSocket listener (tag 2/5) currently serves legacy telnet
protocol. To distinguish GameSession connections, the client requests the
WebSocket subprotocol `hydra-gamesession` during the HTTP upgrade:

```
GET / HTTP/1.1
Upgrade: websocket
Sec-WebSocket-Protocol: hydra-gamesession
```

The handshake response includes:

```
HTTP/1.1 101 Switching Protocols
Sec-WebSocket-Protocol: hydra-gamesession
```

If the `Sec-WebSocket-Protocol` header is absent or doesn't contain
`hydra-gamesession`, the connection falls through to the existing telnet-
over-WebSocket path (no behavior change).

No request-target parsing is required — the subprotocol header alone
discriminates the two paths.

### Framing

Each WebSocket binary frame contains exactly one serialized protobuf message:

- **Client → Server:** `hydra.ClientMessage` (same proto as gRPC GameSession)
- **Server → Client:** `hydra.ServerMessage` (same proto as gRPC GameSession)

No grpc-web frame headers (flag + length). No envelope. Just raw protobuf
bytes in binary WebSocket frames. The WebSocket frame itself provides
message boundaries.

### Connection Lifecycle

1. Client opens WebSocket to existing WS listener with subprotocol
   `hydra-gamesession`. No auth header — browsers can't set custom
   headers on WebSocket upgrades.
2. Server completes the 101 handshake. Connection is unauthenticated.
   Server holds all output until the first message arrives.
3. Client sends initial `ClientMessage` with `SetPreferences` containing
   `session_id`, color format, terminal size, and terminal type.
4. Server validates session_id. If invalid, server sends a `ServerMessage`
   with `system_notice` containing the error, then closes the WebSocket.
5. On success, server registers a subscriber, replays GMCP cache, and
   begins pushing `ServerMessage` frames.
6. Client sends `ClientMessage` frames (input lines, GMCP, pings,
   preference updates).
7. On WebSocket close frame or disconnect, server unsubscribes and cleans up.

### Authentication

First-message auth: the session ID is carried in `SetPreferences.session_id`
(see Proto Changes below). The server accepts the WebSocket upgrade without
auth, then validates the session on the first `ClientMessage`. This avoids
the browser limitation where `new WebSocket()` cannot set custom headers.

The gRPC GameSession path continues to use metadata headers for auth; it
ignores `SetPreferences.session_id`.

### Phase 1 Message Scope

Phase 1 carries the message types that the existing OutputQueue and gRPC
GameSession writer already produce:

| Direction | Message Type | Source |
|-----------|-------------|--------|
| Server → Client | `game_output` | `OutputQueue::pushOutput()` |
| Server → Client | `gmcp` | `OutputQueue::pushGmcp()` |
| Server → Client | `pong` | Pong sentinel in subscriber queue |
| Client → Server | `input_line` | Forward to active link |
| Client → Server | `gmcp` | Forward to active link |
| Client → Server | `ping` | Queue pong sentinel |
| Client → Server | `preferences` | Update render format, NAWS, ttype |

**Not in phase 1:** `system_notice` and `link_event`. The OutputQueue has
no producer path for these today — the gRPC GameSession writer in
`grpc_server.cpp` doesn't emit them either. Adding them requires new queue
item types and producer call sites across SessionManager. That's a
worthwhile follow-up but out of scope here.

### Control Messages and sendToClient()

The existing telnet/WebSocket path emits text-mode control messages through
`sendToClient()` and related helpers (banners, "/connect" responses,
reconnect notices, "no active link" errors, etc.). These are raw text
frames, not protobuf.

For phase 1, a `WsGameSession` front-door **does not receive these
text-mode control messages**. The connection is authenticated via
first-message auth and goes straight to subscriber output — there is no
login menu, no banner, no slash-command handling on the transport itself.
All session control (connect, switch, disconnect, list) continues to use
the existing grpc-web unary RPCs from the HTML5 client.

This means the WsGameSession transport is a pure game I/O channel: it
carries `ClientMessage`/`ServerMessage` and nothing else. The HTML5 client
uses grpc-web Fetch for control RPCs and WebSocket for the streaming
session — the same split it has today, just with bidi instead of
unary+server-stream.

## Server Changes

### hydra_types.h

Add a new front-door protocol variant:

```cpp
enum class FrontDoorProto {
    Telnet,
    WebSocket,
    WsGameSession,  // NEW — WebSocket carrying protobuf GameSession
    GrpcWeb,
};
```

### websocket.h/cpp

Extend `wsProcessHandshake()` to:
- Parse `Sec-WebSocket-Protocol` header during handshake.
- If it contains `hydra-gamesession`, set a flag on `WsState` and include
  `Sec-WebSocket-Protocol: hydra-gamesession` in the 101 response.

No request-target or `Authorization` header parsing needed — auth is
handled by first-message auth after the handshake completes.

Add to `WsState`:

```cpp
bool isGameSession{false};       // true if hydra-gamesession subprotocol
```

### session_manager.h — FrontDoorState

Add fields for the GameSession subscriber:

```cpp
// WebSocket GameSession state (only used if proto == WsGameSession)
int wsGameSessionSubId{0};
std::shared_ptr<HydraSession::SubscriberQueue> wsGameSessionQueue;
std::shared_ptr<HydraSession::OutputQueue> wsGameSessionOQ;
```

### session_manager.cpp — onFrontDoorData (handshake completion)

After `wsProcessHandshake()` succeeds, check `wsState.isGameSession`:

- If **true**: set `fd.proto = FrontDoorProto::WsGameSession`. Skip
  `showBanner()`. The connection is now unauthenticated and waiting for
  its first `ClientMessage` containing `SetPreferences` with `session_id`.
- If **false**: existing telnet-over-WebSocket path (unchanged).

### session_manager.cpp — First-message auth (WsGameSession)

When the first binary WebSocket frame arrives on a `WsGameSession`
front-door that has no subscriber registered yet:

1. Deserialize as `hydra::ClientMessage`.
2. Expect `preferences` with a non-empty `session_id`.
3. Look up the session via `findByPersistId()`.
4. If invalid: serialize a `ServerMessage` with `system_notice` error,
   send as binary frame, close the WebSocket.
5. If valid: register subscriber on the session's OutputQueue, replay
   GMCP cache, apply color/NAWS/ttype from preferences, store the OQ
   and subscriber queue on the FrontDoorState.

### session_manager.cpp — onFrontDoorData (WsGameSession path)

New handler block for `FrontDoorProto::WsGameSession`:

1. Decode WebSocket frames via existing `wsDecodeFrames()`.
2. For each binary frame, deserialize as `hydra::ClientMessage`.
3. Dispatch on the `oneof` — identical logic to `grpc_server.cpp` lines
   348–424:
   - `input_line`: forward to active link via `safeWrite()`.
   - `ping`: queue pong sentinel into subscriber queue.
   - `preferences`: update `sq->renderFormat`, send NAWS, store ttype.
   - `gmcp`: forward to active link.

### session_manager.cpp — Output delivery

In the existing `onBackDoorData` output fan-out (around line 1356), add a
case for `WsGameSession` front-doors:

```cpp
} else if (fd.proto == FrontDoorProto::WsGameSession) {
    // Drain subscriber queue, serialize as ServerMessage, send as
    // binary WebSocket frames.
}
```

However, the better approach is a **poll-based drain** in `runTimers()` or a
dedicated method called from the main loop. The gRPC path uses a blocking
writer thread; the WebSocket path should be non-blocking since it runs on
the main GANL event loop:

- After every `pushOutput`/`pushGmcp`, the condition variable wakes.
- But we're on the main thread — we can't block on `cv.wait()`.
- Instead, add a `drainWsGameSessions()` method called each main-loop
  iteration that checks all WsGameSession front-doors' subscriber queues
  and sends any pending output.

This is similar to how grpc-web Subscribe streaming already works: output
arrives via `onBackDoorData`, and the grpc-web handler immediately encodes
and writes the frame to the front-door handle.

**Recommended approach:** Follow the grpc-web pattern. In the output fan-out
code that already iterates front-doors (around `session_manager.cpp:1356`),
add a `WsGameSession` case that:

1. Reads `sq->renderFormat` under lock.
2. Pops output/GMCP items from the subscriber queue.
3. Serializes each as a `hydra::ServerMessage`.
4. Wraps in `wsEncodeFrame(serialized, WS_OP_BINARY)`.
5. Calls `safeWrite(handle, frame)`.

This avoids any new threading — output flows through the same main-thread
path as telnet and grpc-web.

### session_manager.cpp — onFrontDoorClose

When a `WsGameSession` front-door closes, remove its subscriber from the
OutputQueue.

## Client Changes

### hydra_connection.js

Replace `_startSubscribe()` + `_sendInput()` with a single WebSocket
connection:

```javascript
_startGameSession() {
    // Use the same host:port as the WS listener
    const wsUrl = this._wsBaseUrl;  // e.g. ws://host:port or wss://host:port
    const ws = new WebSocket(wsUrl, ['hydra-gamesession']);
    ws.binaryType = 'arraybuffer';

    ws.onopen = () => {
        // First message: SetPreferences with session_id for auth
        const prefs = Proto.encode({
            preferences: {
                session_id: this.sessionId,
                color_format: 1,  // ANSI_TRUECOLOR
                terminal_width: Math.floor(window.innerWidth / 8) || 80,
                terminal_height: Math.floor(window.innerHeight / 18) || 24,
                terminal_type: 'Hydra-Web',
            }
        }, ClientMessageFields);
        ws.send(prefs);
    };

    ws.onmessage = (event) => {
        const msg = Proto.decode(
            new Uint8Array(event.data), ServerMessageDecode);
        if (msg.game_output) {
            this.addScrollback(msg.game_output.text);
            this._emit(msg.game_output.text);
        } else if (msg.gmcp) {
            this._handleGmcp(msg.gmcp);
        } else if (msg.pong) {
            this._handlePong(msg.pong);
        }
    };

    ws.onclose = () => {
        if (this.connected) this._attemptReconnect();
    };

    this._ws = ws;
}

sendInput(text) {
    if (!this._ws || this._ws.readyState !== WebSocket.OPEN) return;
    const msg = Proto.encode({
        input_line: text
    }, ClientMessageFields);
    this._ws.send(msg);
}
```

### Proto Changes

Add `session_id` to `SetPreferences`:

```protobuf
message SetPreferences {
    ColorFormat color_format = 1;
    uint32 terminal_width = 2;
    uint32 terminal_height = 3;
    string terminal_type = 4;
    string session_id = 5;           // NEW — WS first-message auth
}
```

This keeps auth bundled with the mandatory first message and avoids
conflicting with the `ClientMessage` oneof (a separate `session_id` field
in the oneof would be mutually exclusive with `preferences`).

The gRPC GameSession path ignores this field — it uses metadata headers.
Existing clients that don't set the field are unaffected (proto3 default
is empty string).

## What Stays The Same

- **All unary RPCs** (Authenticate, GetScrollBack, ListLinks, etc.) continue
  to use the existing grpc-web Fetch path. No change.
- **gRPC server** for native clients — completely untouched.
- **Telnet-over-WebSocket** — unchanged, discriminated by subprotocol.
- **Proto definitions** — one field added to `SetPreferences`, no breaking
  changes.

## Implementation Order

1. **Proto:** Add `session_id` field 5 to `SetPreferences` in
   `hydra.proto`. Regenerate C++ stubs.
2. **websocket.cpp:** Parse `Sec-WebSocket-Protocol` header during
   handshake. Set `WsState::isGameSession` and echo the subprotocol in
   the 101 response. No request-target or Authorization parsing.
3. **hydra_types.h:** Add `WsGameSession` to `FrontDoorProto`.
4. **session_manager.h:** Add `WsGameSession` fields to `FrontDoorState`
   (subscriber ID, subscriber queue, output queue shared_ptr).
5. **session_manager.cpp:** Four integration points:
   a. Handshake completion — set `proto = WsGameSession`, skip banner.
   b. First-message auth — validate `session_id`, register subscriber,
      replay GMCP, apply preferences.
   c. Subsequent `ClientMessage` dispatch — input, ping, prefs, gmcp
      (port of `grpc_server.cpp:348–424` logic).
   d. Output fan-out — add `WsGameSession` case alongside existing
      WebSocket/grpc-web cases, serialize `ServerMessage`, send as
      `wsEncodeFrame(proto, WS_OP_BINARY)`.
   e. `onFrontDoorClose` — unsubscribe cleanup.
6. **hydra_connection.js:** `_startGameSession()`, `ServerMessage` decoder,
   `ClientMessage` encoder, first-message auth with `session_id` in
   `SetPreferences`. Retire `_startSubscribe`/`_sendInput` (keep as
   fallback for old proxies or WebSocket-blocked environments).
7. **Testing:** Connect web client, verify bidi flow, test reconnect,
   test auth rejection, verify telnet-over-WS still works.

## Risk Assessment

- **gRPC server and native clients:** Untouched. The only proto change is
  an additive field on `SetPreferences` which existing clients don't set.
- **Shared WebSocket handshake path:** Moderate risk. `wsProcessHandshake()`
  is shared by telnet-over-WS and the new GameSession path. The
  subprotocol check must not break the existing no-subprotocol case.
  Mitigation: test telnet-over-WS explicitly after the change.
- **Front-door dispatch and fan-out:** Moderate risk. New code paths in
  `session_manager.cpp:373–468` (protocol dispatch) and
  `session_manager.cpp:1262–1394` (output fan-out) touch shared
  infrastructure. The fan-out case follows the grpc-web pattern closely,
  but careful testing is needed.
- **sendToClient() bypass:** By design, WsGameSession does not receive
  text-mode control messages. This is safe in phase 1 because the HTML5
  client uses grpc-web RPCs for all control operations. If future work
  adds slash-command handling on this transport, sendToClient() will need
  to emit `ServerMessage.system_notice` for WsGameSession front-doors.
- **Browser compat:** WebSocket with binary frames and subprotocols works
  in all modern browsers.
- **Fallback:** The existing grpc-web path remains available for
  environments where WebSocket is blocked (corporate proxies, etc.).
