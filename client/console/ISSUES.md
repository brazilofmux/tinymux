# TinyMUX Console Client — Open Issues

Updated: 2026-03-29

## Bugs

### ~~Hydra reconnect resends stale `80x24` terminal geometry~~ FIXED

- Reconnect path now uses cached `termWidth_`/`termHeight_` (set from constructor) instead of hardcoded 80x24.

### Pending overlapped write teardown is still unsafe

- **File:** `client/console/src/connection.cpp:248, 259-260`
- **Issue:** `IoContext` objects for async writes are owned by the eventual IOCP completion path. A manual `disconnect()` can close and erase the connection while overlapped write completions are still in flight, which makes this a completion-lifetime problem, not just a simple leak.
- **Impact:** Depending on timing, the client can leak write contexts or, if "fixed" naively, turn the problem into a use-after-free on a late completion.
- **Fix:** Keep connection/write-operation state alive until all canceled or failed completions are drained, or move write contexts to ref-counted ownership detached from the `Connection` object's lifetime.

### ~~Concurrent `grpc_` access lacks full synchronization~~ FIXED

- Hydra transport state now uses a mutex-protected shared snapshot, so
  reconnect/RPC/disconnect paths do not race on a raw `grpc_` pointer.

### ~~`TerminateThread()` used as fallback in cleanup~~ FIXED

- Console shutdown now cancels the blocking read and waits briefly, but no
  longer force-kills the input thread.

### ~~World file parsing has no field validation~~ FIXED

- Both `world` and `hydra` lines now check the `>>` extraction result. Malformed lines are skipped with a diagnostic to stderr.

## Bugs (New, 2026-04-04)

### Telnet CHARSET negotiation validation gap

- **File:** `client/console/src/connection.cpp:411-431`
- **Issue:** The CHARSET option handler splits offered charsets by a delimiter but doesn't validate: (1) delimiter is printable, (2) charset names are trimmed, (3) list length is bounded. A malicious server sending thousands of charset names causes O(n^2) parsing.
- **Impact:** CPU denial-of-service on charset negotiation.
- **Recommendation:** Cap offered-charsets count (e.g., 50), trim whitespace, use bounded loop.
