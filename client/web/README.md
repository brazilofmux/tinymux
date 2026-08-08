# Titan for the browser

The HTML5 edition of the Titan client family (iOS, Android, and this).
Static files only — no build step, no bundler, no external dependencies.
Open `index.html` from any web server and you have a MUD client with
tabs, saved worlds, triggers, timers, spawns, hooks, variables, MCP 2.1,
and a searchable scrollback.  Settings and worlds persist in the
browser's localStorage.

## How a browser reaches a game

Browsers cannot open raw TCP, but they do not need a middlebox either:
**netmux serves WebSocket natively on its ordinary game ports**
(`mux/src/websocket.cpp`, RFC 6455).  A port is shared between telnet
and WebSocket by first-byte protocol detection: a WebSocket client
sends its HTTP `GET` immediately and is upgraded (`/wsclient` and `/`
are both accepted); a classic telnet client waits for the server to
speak and receives the banner after the `proto_detect_window` grace
period (#1074/#2193 — runtime-settable, `0` disables detection for a
telnet-only port).  This works on plain and TLS ports alike, so `wss:`
direct to the game is a two-line config away.

```
browser ── ws(s) ─────────────────> netmux game port  (shared with telnet)
browser ── ws(s) / gRPC-Web ──> hydra ── telnet ──> netmux
```

- **Telnet over WebSocket** (`js/connection.js`) — the simple path,
  and the direct one.  Point the client at the game port (or at a
  Hydra `websocket` listener, or any generic telnet-over-WebSocket
  bridge — nothing in this path cares which) and bytes flow as-is; the
  client's own telnet engine (`js/telnet.js`) handles negotiation,
  `IAC GA` prompts, NAWS, TTYPE, and CHARSET.

- **Hydra GameSession** (`js/hydra_connection.js`) — the optional
  richer protocol, via **Hydra**, the connection proxy in
  [`mux/proxy`](../../mux/proxy): protobuf `ClientMessage`/
  `ServerMessage` over a WebSocket with the `hydra-gamesession`
  subprotocol, falling back to gRPC-Web unary/streaming where
  WebSocket is blocked.  This is what the `/hconnect`, `/hswitch`,
  `/hlinks`, `/hgames`, `/hscroll` commands drive (`/hhelp` lists
  them): multiple game links in one session, server-side scrollback
  fetch, and Hydra-stored credentials with auto-login.  Session resume
  means a dropped browser connection picks up where it left off — the
  proxy holds the game link open across it, which the direct path
  cannot do.

## Deploying

The minimal deployment is netmux plus static files — no extra process:

1. Serve this directory from any static host — nginx, a CDN, an S3
   bucket.  There is nothing to compile.
2. Point the client at the game's host and port.  A page served over
   `https` may only open `wss:` (mixed-content rule), so use a TLS
   game port for any real deployment.

Add Hydra when you want what the proxy provides — session resume,
multi-game links, stored credentials, gRPC-Web for WebSocket-hostile
networks: `cd mux/proxy && make`, copy `hydra.conf.example` to
`hydra.conf`, and enable the listeners you want.  See that file for
TLS certificates, the master key that encrypts stored credentials,
health-check endpoints, and `cors_origin` (gRPC-Web listeners deny
cross-origin requests by default; plain WebSocket is not subject to
CORS).

## Development

Run the tests with:

```
node test_web.js
```

No framework: the harness loads each script into a Node `vm` with a
stub `localStorage`/`WebSocket`, and exercises the telnet engine, MCP
reassembly bounds, trigger/timer/spawn/variable behavior, and the
Hydra transport (session resume, gRPC-Web fallback, reconnect) as plain
assertions.  Keep new logic in the dependency-free `js/*.js` modules so
it stays testable this way.

## Status

Part of the 2.14 development tree.  The released 2.13 server has
neither the WebSocket listener nor Hydra, so a 2.13 game wanting
browser play needs a generic telnet-over-WebSocket bridge and the
simple transport above.
