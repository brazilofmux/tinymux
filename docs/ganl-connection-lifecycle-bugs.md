# GANL Connection Lifecycle Bugs

## Status: Active Investigation (master + brazil)

## Bug 1: Pure Virtual Call on Shutdown (FIXED)

**Commit:** `45e78970` (master)

**Root cause:** `onConnectionClose` called `process_output` during
`ServerShutdown` reason (from `~ConnectionBase` destructor).
`process_output` → `send_data` → `get_connection` acquired a
shared_ptr to the connection being destroyed. When the shared_ptr
dropped, re-entrant destructor → pure virtual call.

**Fix:** Skip `process_output` when `reason == ServerShutdown`.

## Bug 2: Double Free on QUIT → Reconnect (OPEN)

**Reproduction:** Connect, login, QUIT, reconnect on same port.

**Backtrace:**
```
#16 destroy_desc(d)                  at netcommon.cpp:530
#17 GanlAdapter::free_desc2(d)       at ganl_adapter.cpp:774
#18 onConnectionClose                at ganl_adapter.cpp
#19 cleanupResources(ServerShutdown)
```

**Root cause:** Multiple code paths free the same DESC:

1. `onConnectionClose` (via `cleanupResources` from `~ConnectionBase`)
   calls `free_desc2(d)` at line 774.
2. The R_LOGOUT path at line 691 calls `ResetDescriptorForLogout(d)`
   which resets the DESC for reuse but does NOT remove it from
   `handle_to_conn_`. When the old connection's shared_ptr drops
   (from map overwrite by new connection on recycled fd), the
   destructor fires → `cleanupResources` → `onConnectionClose` →
   `free_desc2` on the already-reused/freed DESC.

**The fundamental issue:** There is no single-owner model for DESC
lifetime. Multiple paths (QUIT handler, `onConnectionClose`,
`~ConnectionBase`, `close_sockets`) can all trigger DESC cleanup.
The R_LOGOUT "reuse DESC" pattern from legacy networking doesn't
work in GANL where TCP connections actually close.

**Possible fixes:**

A. **Single-owner:** Make `onConnectionClose` the ONLY path that
   frees DESCs. All other paths (QUIT, close_sockets) just mark
   the DESC for cleanup and let `onConnectionClose` handle it.

B. **Reference counting:** Add a refcount to DESC. Each path that
   holds a reference increments. Only the last release frees.

C. **Idempotent free:** Mark DESC as freed (null sentinel) and
   check before freeing. Quick fix but doesn't address root cause.

D. **Remove DESC reuse:** The R_LOGOUT path should close the
   connection and free the DESC, not try to reuse it. GANL doesn't
   support "return to login screen" on the same TCP connection.
   This is the cleanest fix for the QUIT case.

## Reproduction

```bash
cd mux/game
LD_LIBRARY_PATH=./bin gdb -batch \
  -ex 'run -c netmux.conf -p netmux.pid -e data' \
  -ex 'thread apply all bt' -ex 'quit' ./bin/netmux
# From another terminal: telnet localhost 2860, login, QUIT, reconnect
```
