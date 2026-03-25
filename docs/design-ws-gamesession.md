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
GET /hydra/gamesession HTTP/1.1
Upgrade: websocket
Sec-WebSocket-Protocol: hydra-gamesession
Authorization: <session_id>
```

The handshake response includes:

```
HTTP/1.1 101 Switching Protocols
Sec-WebSocket-Protocol: hydra-gamesession
```

If the `Sec-WebSocket-Protocol` header is absent or doesn't contain
`hydra-gamesession`, the connection falls through to the existing telnet-
over-WebSocket path (no behavior change).

### Framing

Each WebSocket binary frame contains exactly one serialized protobuf message:

- **Client → Server:** `hydra.ClientMessage` (same proto as gRPC GameSession)
- **Server → Client:** `hydra.ServerMessage` (same proto as gRPC GameSession)

No grpc-web frame headers (flag + length). No envelope. Just raw protobuf
bytes in binary WebSocket frames. The WebSocket frame itself provides
message boundaries.

### Connection Lifecycle

1. Client opens WebSocket to existing WS listener with subprotocol
   `hydra-gamesession` and `Authorization` header containing session_id.
2. Server validates session_id during handshake, registers as subscriber.
3. Client sends initial `ClientMessage` with `SetPreferences` (color format,
   terminal size, terminal type).
4. Server begins pushing `ServerMessage` frames (game output, GMCP, pongs,
   link events, system notices).
5. Client sends `ClientMessage` frames (input lines, GMCP, pings,
   preference updates).
6. On WebSocket close frame or disconnect, server unsubscribes and cleans up.

### Authentication

Session ID is extracted from the `Authorization` header in the HTTP upgrade
request — same as the gRPC metadata approach. No per-message session_id
field needed. If the header is missing or the session is not found, the
server rejects the upgrade with HTTP 401.

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
- Parse and store the `Authorization` header value.

Add to `WsState`:

```cpp
bool isGameSession{false};       // true if hydra-gamesession subprotocol
std::string authSessionId;       // from Authorization header
```

### session_manager.h — FrontDoorState

Add fields for the GameSession subscriber:

```cpp
// WebSocket GameSession state (only used if proto == WsGameSession)
int wsGameSessionSubId{0};
std::shared_ptr<HydraSession::SubscriberQueue> wsGameSessionQueue;
std::shared_ptr<HydraSession::OutputQueue> wsGameSessionOQ;
```

### session_manager.cpp — onAcceptWebSocket

After handshake completes, check `wsState.isGameSession`:

- If **true**: set `fd.proto = FrontDoorProto::WsGameSession`, look up the
  session via `wsState.authSessionId`, register a subscriber on its
  OutputQueue, replay GMCP cache, and begin the output pump. Skip `showBanner()`.
- If **false**: existing telnet-over-WebSocket path (unchanged).

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
    const wsUrl = this._baseUrl.replace(/^http/, 'ws')
                  + '/hydra/gamesession';  // or just same host:port
    const ws = new WebSocket(wsUrl, ['hydra-gamesession']);
    ws.binaryType = 'arraybuffer';

    ws.onopen = () => {
        // Send initial SetPreferences
        const prefs = Proto.encode({
            preferences: {
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
        // link_event, system_notice as needed
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

**Authentication:** The WebSocket API doesn't allow custom headers in
browser JavaScript. The `Authorization` header can't be set via
`new WebSocket(url, protocols)`. Two options:

1. **Query parameter:** `ws://host:port/hydra/gamesession?sid=<session_id>`.
   Server extracts from the URL during handshake. Simple, but session ID
   appears in server logs and browser history.

2. **First-message auth:** Client sends an initial `ClientMessage` with a
   new `authenticate` field (or reuse the `session_id` field already in the
   proto's per-message pattern). Server validates before registering the
   subscriber. Cleaner — no URL leakage.

**Recommendation:** First-message auth. Add a `session_id` string field to
`ClientMessage` (or the existing `SetPreferences` which is already the first
message sent). Server holds output until the first message arrives and
validates the session.

Revised lifecycle:

1. Client opens WebSocket (no auth header needed).
2. Client sends `ClientMessage` with `session_id` + `SetPreferences`.
3. Server validates session_id, registers subscriber, begins output.
4. If invalid, server sends a `ServerMessage` with `system_notice`
   containing the error, then closes the WebSocket.

### Proto Changes

Add `session_id` to `ClientMessage`:

```protobuf
message ClientMessage {
    oneof payload {
        string input_line = 1;
        GmcpMessage gmcp = 2;
        PingMessage ping = 3;
        SetPreferences preferences = 4;
        string session_id = 5;        // NEW — first-message auth
    }
}
```

Or, since `SetPreferences` is always the first message, add `session_id`
to `SetPreferences`:

```protobuf
message SetPreferences {
    ColorFormat color_format = 1;
    uint32 terminal_width = 2;
    uint32 terminal_height = 3;
    string terminal_type = 4;
    string session_id = 5;           // NEW — for WS auth
}
```

The second option is cleaner — keeps the auth bundled with the mandatory
first message. The gRPC path ignores this field (it uses metadata).

## What Stays The Same

- **All unary RPCs** (Authenticate, GetScrollBack, ListLinks, etc.) continue
  to use the existing grpc-web Fetch path. No change.
- **gRPC server** for native clients — completely untouched.
- **Telnet-over-WebSocket** — unchanged, discriminated by subprotocol.
- **Proto definitions** — one field added to `SetPreferences`, no breaking
  changes.

## Implementation Order

1. **Proto:** Add `session_id` to `SetPreferences`.
2. **websocket.cpp:** Parse `Sec-WebSocket-Protocol` and `Authorization`
   (or just the subprotocol) during handshake.
3. **hydra_types.h:** Add `WsGameSession` to `FrontDoorProto`.
4. **session_manager.h:** Add `WsGameSession` fields to `FrontDoorState`.
5. **session_manager.cpp:** Handshake discrimination, subscriber
   registration, ClientMessage dispatch, output fan-out.
6. **hydra_connection.js:** `_startGameSession()`, `ServerMessage` decoder,
   retire `_startSubscribe`/`_sendInput` (keep as fallback for old proxies).
7. **Testing:** Connect web client, verify bidi flow, test reconnect,
   verify telnet-over-WS still works.

## Risk Assessment

- **Low risk:** No changes to gRPC server, telnet path, or native clients.
- **Moderate complexity:** The ClientMessage dispatch in
  `session_manager.cpp` is new code, but it's a direct port of the logic
  already in `grpc_server.cpp:348–424`.
- **Browser compat:** WebSocket with binary frames and subprotocols works
  in all modern browsers.
- **Fallback:** The existing grpc-web path can remain as a fallback for
  environments where WebSocket is blocked (corporate proxies, etc.).
