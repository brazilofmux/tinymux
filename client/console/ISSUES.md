# TinyMUX Console Client — Open Issues

Updated: 2026-03-27

## Bugs

### Hydra reconnect resends stale `80x24` terminal geometry

- **Evidence:** `client/console/src/hydra_connection.cpp:309-317` rebuilds `SetPreferences` after reconnect, but hardcodes `terminal_width=80` and `terminal_height=24`.
- **Impact:** Reconnected sessions can momentarily or permanently advertise the wrong viewport to Hydra unless another code path updates the dimensions later.
- **Regression note:** This conflicts with the higher-level client tracker claim that native Hydra clients now consistently report actual terminal dimensions.

### Write context memory leak on disconnect

- **File:** `client/console/src/connection.cpp:248, 259-260`
- **Issue:** `new IoContext()` allocated for async writes. On normal completion or immediate failure, the context is properly freed. However, if `disconnect()` is called while writes are pending on IOCP, those contexts are never deleted.
- **Fix:** Maintain a list of pending `IoContext` pointers and delete them all in `disconnect()`.

### Concurrent `grpc_` access lacks full synchronization

- **File:** `client/console/src/hydra_connection.cpp`
- **Issue:** `grpc_` is accessed from `readerLoop` (line 231-232) and main thread (`disconnect`, `send_line`). Stream access is under lock, but the `grpc_` pointer itself and stream recreation during reconnect lack synchronization.
- **Risk:** Race condition if `disconnect()` called while `readerLoop` reads `grpc_`.

### `TerminateThread()` used as fallback in cleanup

- **File:** `client/console/src/main.cpp:240-248`
- **Issue:** `TerminateThread()` used after timeout waiting for input thread to exit. This can leave locks held and corrupt heap state.
- **Fix:** Use cooperative shutdown signaling or accept a longer timeout.

### ~~World file parsing has no field validation~~ FIXED

- Both `world` and `hydra` lines now check the `>>` extraction result. Malformed lines are skipped with a diagnostic to stderr.
