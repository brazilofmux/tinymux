# Titan for the browser

The HTML5 edition of the Titan client family (iOS, Android, and this).
Static files only — no build step, no bundler, no external dependencies.
Open `index.html` from any web server and you have a MUD client with
tabs, saved worlds, triggers, timers, spawns, hooks, variables, MCP 2.1,
and a searchable scrollback.  Settings and worlds persist in the
browser's localStorage.

## How a browser reaches a game

Browsers cannot open raw TCP, so the client never talks to `netmux`
directly.  Both of its transports go through **Hydra**, the connection
proxy in [`mux/proxy`](../../mux/proxy), deployed alongside the game:

```
browser ── ws(s) / gRPC-Web ──> hydra ── telnet TCP ──> netmux
```

- **Telnet over WebSocket** (`js/connection.js`) — the simple path.
  Point the client at a Hydra `websocket` / `websocket+tls` listener and
  bytes flow as-is; the client's own telnet engine (`js/telnet.js`)
  handles negotiation, `IAC GA` prompts, NAWS, TTYPE, and CHARSET.
  Any other telnet-over-WebSocket bridge works here too; nothing in
  this path is Hydra-specific.

- **Hydra GameSession** (`js/hydra_connection.js`) — the full protocol:
  protobuf `ClientMessage`/`ServerMessage` over a WebSocket with the
  `hydra-gamesession` subprotocol, falling back to gRPC-Web
  unary/streaming where WebSocket is blocked.  This is what the
  `/hconnect`, `/hswitch`, `/hlinks`, `/hgames`, `/hscroll` commands
  drive (`/hhelp` lists them): multiple game links in one session,
  server-side scrollback fetch, and Hydra-stored credentials with
  auto-login.  Session resume means a dropped browser connection picks
  up where it left off — the proxy holds the game link open.

## Deploying

1. Build and run Hydra next to the game: `cd mux/proxy && make`, copy
   `hydra.conf.example` to `hydra.conf`, and enable the listeners you
   want.  See that file for TLS certificates, the master key that
   encrypts stored credentials, and health-check endpoints.
2. Serve this directory from any static host — nginx, a CDN, an S3
   bucket.  There is nothing to compile.
3. Two cross-origin rules to respect:
   - A page served over `https` may only open `wss:`/`https:`
     transports (mixed-content rule), so front the Hydra listeners with
     TLS in any real deployment.
   - gRPC-Web listeners deny cross-origin requests by default; add your
     client's origin with `cors_origin` in `hydra.conf`.

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

Part of the 2.14 development tree.  The released 2.13 server has no
Hydra, so a 2.13 game wanting browser play needs a generic
telnet-over-WebSocket bridge and the simple transport above.
