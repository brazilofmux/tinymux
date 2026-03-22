# Hydra: Problem Statement and Requirements

## Status: Design

Branch: `master`

## The Problem

Every MUD player has experienced it: the game @restarts and your
connection drops. You reconnect, re-authenticate, and hope you didn't
miss anything. Hydra changes where TLS lives: client-facing connections
terminate at Hydra, while Hydra talks to the game over plain telnet (or
another local non-encrypted transport). That means the game no longer
owns the internet-facing TLS session at all.

This is one instance of a general problem: **the process that owns the
network connection is also the process that needs to restart, crash, or
change.** The connection's lifetime is coupled to the wrong thing.

## Scenarios

### 1. Game @restart drops connections

TinyMUX's @restart uses `exec()` to replace the running process.
Non-TLS connections survive via file descriptor inheritance. Hydra
depends on that property: Hydra's back-door connection to TinyMUX is a
plain telnet or local socket connection, so TinyMUX can @restart
without dropping Hydra's connection to the game. The player should not
see a disconnect because the player is connected to Hydra, not directly
to TinyMUX.

**Impact:** Without Hydra, every internet-facing TLS player drops on
@restart. With Hydra in front, TinyMUX can restart while Hydra keeps
the player-facing TLS session alive.

### 2. Client @restart drops connections

A MUD client that restarts (crash, update, user closes and reopens)
loses its TCP connection. The game sees a disconnect. The player must
reconnect and re-authenticate. Any output generated while disconnected
is lost.

### 3. Mobile network changes

A player on a phone moves from WiFi to cellular (or between cell
towers). The TCP connection's source IP changes. TCP doesn't survive
this. The player disconnects and must reconnect.

### 4. No protocol gateway for legacy games

A player wants to use gRPC, WebSocket, or a custom mobile protocol to
connect to a game that only speaks telnet. There is no standard way to
bridge these protocols without modifying the game server itself.

### 5. Tunneling through restricted networks

Players behind corporate firewalls or restricted networks use ad-hoc
solutions (stunnel, SSH tunnels, screen+TitanFugue) to maintain
persistent connections. These are fragile, require technical
sophistication, and aren't available to most players.

### 6. Multi-game, multi-character management

A player connected to three games with five characters across them has
five independent TCP connections with five independent authentication
states. There is no unified session layer.

## Root Cause

All six scenarios share one root cause: **the session persistence point
is tied to the wrong endpoint.** Without Hydra, the client talks
directly to the game, so either endpoint restarting, crashing, or
changing networks kills the connection. No third party is holding the
player session open.

The only architectural solution is a **persistent intermediary** — a
process that:

1. Owns TLS on the internet-facing side, completely decoupled from the
   game server
2. Maintains session state independent of both client and game
3. Reconnects to either side transparently when connections drop
4. Runs independently of the game for extremely long periods, so game
   maintenance and restarts do not affect player-facing TLS sessions

## Requirements

### R1. Persistent TLS termination

Hydra terminates TLS on the internet-facing side. The TLS session
persists as long as Hydra is running, independent of game server
lifecycle. The game does not terminate TLS. Game @restart does not
affect client TLS connections.

### R2. Session persistence across disconnects

A Hydra session survives:

- Game @restart (for TinyMUX's ordinary non-SSL back-door connection,
  the connection should normally survive)
- Game crash, shutdown, or outage (Hydra reconnects to the game when it
  comes back, if reconnect is enabled for that game)
- Client disconnect and reconnect (Hydra reattaches the client to
  existing game sessions)
- Client network change (new TCP connection, same Hydra session)

The player's experience is continuity—at most a brief pause, never a
full disconnect/reconnect/re-authenticate cycle.

This requirement applies to game lifecycle events and client-side
disconnects. It does not require Hydra itself to preserve TLS context
across its own upgrade or replacement.

### R3. Scroll-back buffer

When a client is disconnected, Hydra buffers output from the game. On
reconnect, the client receives the buffered output so nothing is lost.
Buffer size is configurable with sensible defaults.

### R4. Multi-protocol front door

Hydra accepts connections via:

- Raw telnet (with full telnet option negotiation)
- TLS-wrapped telnet
- WebSocket (for browser-based clients)
- gRPC (for programmatic/mobile clients)

All front-door protocols map to the same session model.

### R5. Telnet protocol translation

Hydra maintains independent telnet negotiation state with each client
and each game. It translates between them:

- Character set conversion (UTF-8, Latin-1, CP437, etc.)
- Terminal size forwarding (NAWS)
- Color capability filtering (strip ANSI for clients that don't
  support it, translate between color depths)
- MSSP/GMCP forwarding or synthesis

A client with different capabilities than what the game expects gets
correct translation without either side needing to change.

For TinyMUX specifically, Hydra's back-door connection uses the same
plain telnet-style protocol that TinyMUX already supports for ordinary
non-SSL clients. Hydra relies on TinyMUX's existing ability to
`@restart` without dropping that non-SSL connection.

### R6. Multi-game, multi-character support

A single authenticated Hydra session can maintain simultaneous
connections to multiple games and multiple characters on the same game.
The user can switch between them or view them simultaneously (depending
on client capability).

### R7. Authentication layer

Hydra has its own user account system, independent of game
authentication. A user logs into Hydra once, and Hydra presents stored
credentials to games on the user's behalf. This decouples internet-
facing authentication from per-game authentication.

Hydra does not own the game password lifecycle. Players continue to
change their passwords inside each game using that game's existing
commands. Hydra only stores the user's current login material and
provides a way for the user to update it for future auto-login.

### R8. Game process management

Hydra can start and stop game server processes. For locally-hosted
games, Hydra is the process supervisor—it can launch a game, monitor
it, restart it on crash, and manage its lifecycle. For remote games,
Hydra connects over TCP.

### R9. Graceful self-upgrade

Hydra should be designed to run unchanged for very long periods, with
the expectation that normal game maintenance does not require Hydra to
restart. If Hydra itself is upgraded or replaced, loss of client TLS
context is acceptable. In that case, Hydra should preserve as much
session state as practical so clients can reconnect and resume, but
preserving the TLS connection itself is not a requirement for Hydra
self-upgrade.

### R10. Builds on existing infrastructure

Hydra reuses:

- **libmux.so** — string handling, UTF-8, time, allocation, SHA1
- **GANL** — non-blocking I/O, TLS, telnet protocol handling
- **Poor Man's COM** — for managing game processes and modules

Hydra is a new binary (`hydra`) that links against libmux.so and GANL
but does *not* link against engine.so. It is not a game server—it is
infrastructure.

Hydra is transparent to the game. The game does not need to know Hydra
exists; from the game's point of view, Hydra is just another network
client speaking the game's normal login and command protocol over a
telnet-style connection.

### R11. Minimal resource footprint

Hydra is a small, focused process. It does not interpret softcode,
manage databases, or run game logic. Its job is:

- Accept connections
- Authenticate users
- Route bytes between clients and games
- Translate protocols
- Buffer output

It should be lightweight enough to run on the same host as the game
with negligible overhead, or on a separate host as a dedicated proxy.

## Non-Requirements

- **Game logic.** Hydra does not run softcode, manage objects, or
  implement any MUD game mechanics.
- **Game modifications for Hydra awareness.** TinyMUX and other target
  games do not need protocol changes, special modules, or explicit
  Hydra integration in order to be usable behind Hydra.
- **Cross-host clustering.** Hydra is a single-process proxy, not a
  distributed system. It may connect to remote games, but it is not
  itself distributed.
- **Backward compatibility with existing proxy protocols.** Hydra
  defines its own session protocol. It does not need to implement
  SOCKS, HTTP CONNECT, or HAProxy PROXY protocol (though these could
  be added later).
- **Automatic synchronization of in-game password changes.** If a
  player changes a game password inside the game, Hydra is not required
  to detect that change automatically. The player updates Hydra's stored
  login material separately.
