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

### `/def` accepts invalid regex triggers and substitutions without surfacing an error

- **Files:** `client/console/src/macro.cpp:21-27`, `client/console/src/macro.cpp:157-161`, `client/console/src/command.cpp:433-438`
- **Issue:** `Macro::compile()` swallows `std::regex` compilation failures for regex-backed triggers and leaves `compiled = false`, but `cmd_def()` still prints `Defined: ...` unconditionally. The same silent catch exists for per-line substitution regexes in `check_triggers()`.
- **Impact:** A malformed `/def -t` or `/def -s` rule looks successfully installed to the user, then either never fires or silently skips substitution at runtime. This is the same "silent invalid user config" failure mode that was already fixed for spawn regexes.
- **Fix:** Return a diagnostic from `Macro::compile()` / `parse_def()` when regex compilation fails, and reject the definition instead of storing an inert macro.
