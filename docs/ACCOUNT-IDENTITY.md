# Account-Level Identity: Multi-Session and Character Association

## Problem

MUD players often have multiple characters on the same game. Administrators
need to:

- Know which characters belong to the same real person.
- Ban a person (not just a character) and have it stick across all alts.
- Audit who is connected and what they're doing across characters.

Players want:

- Seamless switching between characters without full disconnect/reconnect.
- A single login that manages all their characters.

## How RhostMUSH Solves It

RhostMUSH implements an **account membership subsystem** — a hybrid of
hardcoded C functions and softcoded MUSHcode that ties multiple characters
to a master account object.

### Hardcoded Layer

Each connection descriptor carries:

```c
dbref account_owner;       // master account object
char  account_rawpass[100]; // raw password for character switching
```

Five built-in functions expose this to softcode:

| Function | Purpose |
|----------|---------|
| `account_owner(port)` | Query or set the master account for a connection |
| `account_login(player, attr, port, pw)` | Log in a character through the account system |
| `account_su(player, port, attr)` | Switch characters within the same account |
| `account_boot(port, quit)` | Disconnect a connection |
| `account_who(sep)` | List all account-system descriptors |

A global flag `account_subsys_inuse` changes login permission rules so the
account system can bypass normal connect checks.

### Softcode Layer

The actual account logic (creation, character linking, menus, password
management) lives in editable MUSHcode objects, not in C. Character
association is stored in a custom attribute (`_ACCT` by default) on each
player object. The hardcoded layer provides the hooks; softcode provides
the policy.

### Data Flow

```
1. Player connects → account login screen (softcode, via file_object)
2. Softcode validates password against master account's _ACCT attribute
3. account_owner() tags the descriptor with the master account dbref
4. account_login() or account_su() connects/switches characters
5. Admin can query account_who() and account_boot() to manage by account
```

### Strengths

- Account-level banning: target the master account, all alts are affected.
- Character switching without full disconnect.
- Softcode flexibility: game-specific policy without C changes.
- Audit trail: `account_who()` shows all account-tagged connections.

### Weaknesses

- Password stored in cleartext in descriptor memory (volatile, not on disk).
- The `check_connect_ex()` backdoor uses a synthetic "zz" command to bypass
  normal authentication — fragile coupling.
- Account associations are stored in softcode attributes, not a structured
  database — no relational integrity.
- Usability of the `+su` switching flow is questionable in practice.

## How TinyMUX's Architecture Differs

TinyMUX has the **Hydra proxy**, which already provides most of the
account infrastructure at a different layer.

### What Hydra Already Has

Hydra sits between clients and game servers as a connection proxy with:

- **Account database** (SQLite): username, password hash, salt, flags.
- **Session management**: persistent sessions survive client disconnects.
- **Character tracking**: each back-door link stores `gameName` + `character`.
- **Credential storage**: encrypted per-game login secrets with auto-login.
- **gRPC API**: full session/account/link introspection for external tools.
- **Multi-link support**: one session can connect to multiple games.

The account-to-character association is already tracked:

```
HydraSession {
    accountId: 42
    username: "player"
    links: [
        {game: "Farm", character: "Stephen"},
        {game: "Farm", character: "AltChar"},
    ]
}
```

### The Gap

Hydra knows the account identity. The game does not. All back-door
connections come from Hydra (localhost), and the game receives only a
standard `connect <name> <password>` command. There is no mechanism to
communicate account metadata downstream.

This means:

- The game cannot correlate connections to the same Hydra account.
- Game-side `@ban` only affects individual characters, not accounts.
- Admin tools on the game have no visibility into account groupings.
- The game sees all connections as originating from Hydra's IP.

## Design Options

### Option A: GMCP Account Metadata

After the back-door telnet handshake, Hydra sends a GMCP message:

```
IAC SB GMCP "Hydra.Session" {
    "account_id": 42,
    "username": "player",
    "session_id": "a1b2c3"
} IAC SE
```

The game engine receives this via the existing GMCP handler and stores the
account ID on the descriptor. New softcode functions expose it:

```
hydra_account(%#)     → "player"
hydra_account_id(%#)  → 42
hydra_connections(42)  → list of connected dbrefs for account 42
```

**Pros**: Standards-based, no login protocol changes, backward-compatible
(games without Hydra support just ignore the GMCP package).

**Cons**: Requires game-side GMCP handler extension. GMCP arrives after
connect, so there's a brief window where the account is unknown.

### Option B: Login Protocol Extension

Hydra sends an extended connect command:

```
connect Stephen password HYDRA_ACCOUNT=42 HYDRA_USER=player
```

The login parser recognizes the `HYDRA_ACCOUNT` token and tags the
descriptor before completing authentication.

**Pros**: Account is known at login time (no race window). Simple to
implement.

**Cons**: Breaks any game that doesn't understand the extension. Requires
Hydra to be trusted (the game must believe the account claim).

### Option C: Out-of-Band Control Channel

Hydra opens a separate connection (or Unix socket) to the game for
metadata exchange, independent of player connections. Account associations,
bans, and session state flow through this channel.

**Pros**: Clean separation of control and data. Rich bidirectional
communication.

**Cons**: Significant architectural complexity. Requires a new protocol.

### Option D: Softcode-Only (No Engine Changes)

Hydra sends a predefined GMCP or special command after login. A softcode
object on the game catches it and maintains account associations in
attributes, similar to RhostMUSH's approach but with Hydra as the
identity provider instead of the game.

**Pros**: No engine changes needed. Works today.

**Cons**: Fragile. No admin-level enforcement. Can be spoofed if a player
connects directly (bypassing Hydra).

## Recommendation

**Option A (GMCP) is the right path.** It:

- Leverages existing GMCP infrastructure in both Hydra and TinyMUX.
- Is backward-compatible (non-Hydra connections just don't get tagged).
- Keeps the identity authority in Hydra (one source of truth).
- Gives the game enough information for admin tooling without duplicating
  Hydra's account database.

The implementation would be:

1. **Hydra**: After back-door connect succeeds, send `Hydra.Session` GMCP
   with account ID and username.
2. **Engine**: On receiving `Hydra.Session`, store account ID on the
   descriptor. Add a `DESC::hydra_account_id` field.
3. **Softcode**: New functions `hydra_account()`, `hydra_connections()`,
   `hydra_boot_account()` for admin use.
4. **Trust model**: Only accept `Hydra.Session` from connections originating
   from trusted IPs (localhost or configured proxy addresses).

This gives TinyMUX account-level identity without the hardcoded account
subsystem RhostMUSH needed — because Hydra already *is* the account
subsystem.

## Comparison

| Capability | RhostMUSH | TinyMUX + Hydra (current) | TinyMUX + Hydra (Option A) |
|------------|-----------|---------------------------|----------------------------|
| Account database | Softcode attrs | Hydra SQLite | Hydra SQLite |
| Character association | `_ACCT` attr | Hydra session links | Hydra session links + GMCP |
| Game sees account? | Yes (descriptor) | No | Yes (GMCP → descriptor) |
| Character switching | `account_su()` | Hydra `/switch` | Hydra `/switch` |
| Account-level ban | Via softcode | Hydra only | Hydra + game-side |
| Persistent sessions | No | Yes (Hydra) | Yes (Hydra) |
| Trust model | Game is authority | Hydra is authority | Hydra is authority |
| Engine changes needed | 5 functions + flags | None | GMCP handler + 2-3 functions |

## Status

- Research complete.
- No implementation yet.
- Hydra proxy source: `mux/proxy/`
- RhostMUSH reference: `/tmp/rhostmush/Server/src/functions.c` (lines 24554-24839)
