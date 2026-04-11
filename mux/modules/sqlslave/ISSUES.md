# SQLSlave Module (`mux/modules/sqlslave/`) — Open Issues

Created: 2026-04-10.

The SQLSlave module is an optional helper module that proxies SQL
queries to a MySQL backend. It is loaded via `MODULE_PATH` in the
driver's `mux.conf` and exposes `CID_QueryServer`/`IID_IQueryControl`.

The module has not had a pass yet — tracker starts fresh today.

## High — Reference Counting (New, 2026-04-10)

### Non-atomic `CQueryServer::m_cRef` and factory `m_cRef`
- **File:** `mux/modules/sqlslave/sqlslave.cpp:192-207, 435-449`
- **Issue:** `AddRef`/`Release` on both `CQueryServer` and
  `CQueryServerFactory` still use bare `uint32_t m_cRef` with
  `m_cRef++` / `m_cRef--`. The comsys and mail modules already
  converted to `std::atomic<uint32_t>` (see their ISSUES.md FIXED
  entries) to close the decrement/zero-check race for double-delete.
- **Fix:** Change `m_cRef` to `std::atomic<uint32_t>` in both classes,
  mirroring the comsys/mail pattern with `fetch_add(relaxed)` on
  AddRef and `fetch_sub(acq_rel)` on Release.

### Non-atomic `g_cComponents` / `g_cServerLocks`
- **File:** `mux/modules/sqlslave/sqlslave.cpp:49-50`
- **Issue:** Both module-global counters are plain `int32_t` and are
  touched from the constructor, destructor, and `LockServer()` without
  synchronization. Same issue the other modules already fixed.
- **Fix:** Convert to `std::atomic<int32_t>`.

## High — Buffer & Pointer Safety (New, 2026-04-10)

### `ConnectionHelper()` dereferences `m_pServer` without null check
- **File:** `mux/modules/sqlslave/sqlslave.cpp:251`
- **Issue:** `if ('\0' != m_pServer[0])` is the only precondition for
  touching `m_pServer`. A COM caller that passes `nullptr` for
  `pServer` (which `Connect()` stores verbatim without validation)
  crashes the module on the next query. `Connect()` also needs to
  reject `nullptr` for `pDatabase`, `pUser`, and `pPassword`, which
  `mysql_real_connect()` later dereferences through
  `reinterpret_cast<char *>`.

### `Connect()` takes ownership of caller buffers via `delete[]`
- **File:** `mux/modules/sqlslave/sqlslave.cpp:211-227, 160-167`
- **Issue:** The destructor frees `m_pServer`, `m_pDatabase`,
  `m_pUser`, and `m_pPassword` with `delete[]`, yet the parameters to
  `Connect()` arrive as `const UTF8 *` from the caller with no
  documented ownership transfer. Across-module delivery through
  standard marshaling serializes into temporary buffers that the
  transport frees — the module is currently only safe to use in-process
  with a caller that allocates the strings via `new UTF8[]` and
  intentionally hands ownership over.
- **Fix:** Copy the strings into module-owned `std::string`s on entry.
  That removes the hidden ABI contract and makes the out-of-process
  (proxy/stub) path work without a double-free.

## Medium — MySQL Error Propagation (New, 2026-04-10)

### `mysql_real_connect()` failure is silent
- **File:** `mux/modules/sqlslave/sqlslave.cpp:261-270`
- **Issue:** On connection failure, `ConnectionHelper()` leaves
  `m_database` initialized but unconnected and returns normally. The
  next `Query()` call runs `mysql_ping()` which fails, triggering a
  reconnect attempt whose own failure is also silent. The caller has
  no way to distinguish "not yet connected" from "unreachable".
- **Fix:** Log `mysql_error(m_database)` on failure, and surface a
  dedicated `QS_CONNECT_FAILED` result so softcode can differentiate.

### `mysql_next_result()` error result dropped
- **File:** `mux/modules/sqlslave/sqlslave.cpp:388-395`
- **Issue:** The post-query drain loop continues only while
  `mysql_next_result() == 0`. A value `> 0` (error) aborts the loop
  silently and discards whatever error the server returned. Stored
  procedures that fail midway look successful from the MUX side.

### `mysql_options(MYSQL_OPT_RECONNECT)` return value unchecked
- **File:** `mux/modules/sqlslave/sqlslave.cpp:257, 268`
- **Issue:** Both `mysql_options` calls ignore their return value.
  MySQL 8.0+ removed `MYSQL_OPT_RECONNECT` entirely, so the first call
  can fail silently on modern clients while the second call (after
  connect) relies on the option still being supported. Check the
  return and log, or switch to the MySQL 8.0 auto-reconnect pattern.

## Low — Housekeeping

### `ConnectionHelper()` does nothing on detected reconnect
- **File:** `mux/modules/sqlslave/sqlslave.cpp:326-332`
- **Issue:** The `if (lThreadId_before != lThreadId_after)` block is
  empty — the code notices that the connection was reestablished and
  then takes no action. Either delete the dead check or restore
  whatever session-level state (`SET NAMES utf8`, per-session
  variables, prepared statements) a new MySQL session needs.

### `catch (...) { ; }` swallows allocator errors
- **File:** `mux/modules/sqlslave/sqlslave.cpp:80-88, 462-469`
- **Issue:** `new CQueryServerFactory` / `new CQueryServer` are wrapped
  in `try { ... } catch (...) { ; /* Nothing. */ }`. A `bad_alloc` is
  converted into a null pointer check on the next line, which is fine,
  but any non-`bad_alloc` exception (e.g., a constructor invariant
  violation) is silently dropped. Narrow the `catch` to `std::bad_alloc`
  to avoid masking real bugs.
