# TinyMUX Console Client — Open Issues

Updated: 2026-04-10

## Bugs

### ~~Hydra reconnect resends stale `80x24` terminal geometry~~ FIXED

- Reconnect path now uses cached `termWidth_`/`termHeight_` (set from constructor) instead of hardcoded 80x24.

### ~~Pending overlapped write teardown is still unsafe~~ FIXED

- **File:** `client/console/src/app.h`, `connection.h`, `connection.cpp`, `command.cpp`
- `app.connections` is now `unordered_map<string, shared_ptr<IConnection>>` and `Connection` inherits `std::enable_shared_from_this<Connection>`. Each heap-allocated write `IoContext` carries a `std::shared_ptr<Connection> owner` populated via `shared_from_this()` in `send_raw()`, so pending writes keep the `Connection` alive until their completion fires and deletes the ctx — even if `app.connections` already erased the map entry. Read and connect overlapped slots are embedded in `Connection`, so they now also pin the object via `pending_read_self_` / `pending_connect_self_` self-references that are set before `WSARecv` / `ConnectEx` and cleared on the matching completion (or on the synchronous error path). `on_completion()` takes a local `keepalive = shared_from_this()` at entry, which prevents `*this` from being destroyed mid-method when a write ctx owning the last reference is deleted. `memset` on the full `IoContext` struct was removed in favour of value-initialization (the `owner` field is a non-trivial `shared_ptr` now). Verification is deferred to a Windows build — the console client requires `<windows.h>`, `<winsock2.h>`, `<mswsock.h>`, etc. and does not compile on this Linux host.

### ~~Concurrent `grpc_` access lacks full synchronization~~ FIXED

- Hydra transport state now uses a mutex-protected shared snapshot, so
  reconnect/RPC/disconnect paths do not race on a raw `grpc_` pointer.

### ~~`TerminateThread()` used as fallback in cleanup~~ FIXED

- Console shutdown now cancels the blocking read and waits briefly, but no
  longer force-kills the input thread.

### ~~World file parsing has no field validation~~ FIXED

- Both `world` and `hydra` lines now check the `>>` extraction result. Malformed lines are skipped with a diagnostic to stderr.

## Bugs (New, 2026-04-04)

### ~~Telnet CHARSET negotiation validation gap~~ FIXED

- The CHARSET option handler now validates that the delimiter is printable, trims ASCII whitespace around offered names, and caps parsing to 50 offered charsets before replying. Local syntax-only verification is blocked in this environment because `client/console` requires Windows headers.

## Bugs (New, 2026-04-10)

### ~~`/def` accepts invalid regex triggers and substitutions without surfacing an error~~ FIXED

- `Macro::compile()` now reports `std::regex_error` details back to the caller instead of silently leaving a dead trigger behind. `parse_def()` validates both trigger regexes and substitution regexes up front, so `cmd_def()` now rejects malformed `/def -t...` and `/def -s...` rules with an explicit error instead of storing a broken macro and printing `Defined: ...`. Reverified with `g++ -std=c++17 -fsyntax-only -I client/console/src -I mux/include -I ragel client/console/src/macro.cpp`.
