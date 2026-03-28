# Hydra Proxy -- Code Review Issues

Review date: 2026-03-27

## TLS Policy (decided 2026-03-27)

MUDs have accepted cleartext telnet connections for 30+ years. Hydra supports
the full ecosystem, but defaults to secure settings. The cleartext code path is
always compiled in but controlled by opt-in configuration:

**Front door (client to Hydra)**:
- TLS required by default.
- `allow_plaintext = yes` in hydra.conf to override (for SSH+TF users, etc.).
- Hydra-specific credential commands (`create`, `/addcred`) always require TLS
  regardless of the `allow_plaintext` setting. Game traffic passthrough is
  allowed on plaintext connections.

**Back door (Hydra to game server)**:
- TLS required by default per game block.
- `tls_required = no` per game block to override (for colocated games on
  localhost/same VPC).
- Operator must consciously acknowledge the cleartext backend risk.

## Critical

### C-1. Cleartext credentials on non-TLS connections -- FIXED
- **Resolution**: Per TLS policy above. `allow_plaintext` config option
  (default: no) gates front-door connections. `create` and `/addcred` are
  rejected on non-TLS connections regardless of `allow_plaintext`.
- **Files changed**: config.h, config.cpp, session_manager.cpp, hydra_main.cpp,
  hydra.conf.example

### C-2. `/addcred` sends game secrets in plaintext -- FIXED
- **Resolution**: See C-1. Back-door TLS enforced via `tls_required` per game
  block (default: yes). Games on localhost set `tls_required no` explicitly.
- **Files changed**: config.h, config.cpp, session_manager.cpp, hydra.conf.example

### C-3. No admin authorization on process management RPCs -- FIXED
- **Resolution**: Added `AccountManager::isAdmin()` query. `/start`, `/stop`,
  `/restart` telnet commands and `StartGame`/`StopGame`/`RestartGame` gRPC RPCs
  now check admin flag before proceeding.
- **Files changed**: account_manager.h, account_manager.cpp, session_manager.cpp,
  grpc_server.cpp

### C-4. CORS allows any origin -- FIXED
- **Resolution**: Replaced wildcard `Access-Control-Allow-Origin: *` with
  configurable `cors_origin` in hydra.conf. Default: deny all. Multiple origins
  supported. `Vary: Origin` header added.
- **Files changed**: config.h, config.cpp, grpc_web.h, grpc_web.cpp,
  session_manager.cpp, hydra.conf.example

## High

### H-1. Unbounded line buffer (DoS) -- FIXED
- **Resolution**: Capped `lineBuf` at 8192 bytes (`MAX_LINE_LENGTH`).
  Connections exceeding the limit are dropped.
- **Files changed**: session_manager.h, session_manager.cpp

### H-2. Unbounded HTTP request buffer (DoS) -- FIXED
- **Resolution**: Capped `httpBuf` at 1MB. Returns HTTP 413 on overflow.
- **Files changed**: session_manager.cpp

### H-3. Unbounded WebSocket fragment buffer (DoS) -- FIXED
- **Resolution**: Added `WS_MAX_PAYLOAD` check for 16-bit extended lengths.
  Fragment reassembly capped at `WS_MAX_PAYLOAD`. Close frame 1009 sent on
  overflow.
- **Files changed**: websocket.cpp

### H-4. Session tokens never expire or rotate -- FIXED
- **Resolution**: Added `session_token_ttl` config (default 24h). Tokens now
  rotate on re-authentication. `dbPersistId` introduced to maintain scroll-back
  foreign keys across rotations.
- **Files changed**: config.h, config.cpp, session_manager.h, session_manager.cpp

### H-5. No TLS on gRPC native listener -- FIXED
- **Resolution**: Added `grpc_tls_cert` and `grpc_tls_key` configuration.
  `GrpcServer` now initializes `SslServerCredentials` if provided.
- **Files changed**: config.h, config.cpp, grpc_server.cpp, hydra.conf.example

### H-6. Non-constant-time password comparison -- FIXED
- **Resolution**: Replaced `std::string::operator!=` with `CRYPTO_memcmp` from
  OpenSSL in `AccountManager::authenticate()`.
- **Files changed**: account_manager.cpp

### H-8. LBUF_SIZE discrepancy (8000 vs 32768) -- FIXED
- **Resolution**: `telnet_bridge.cpp` now defines `LBUF_SIZE=32768` before
  including `color_ops.h` (matching `alloc.h`). All three TelnetBridge methods
  (`ingestGameOutput`, `renderForClient`, `charsetEncodeFromUtf8`) converted
  from fixed stack buffers to heap-allocated `std::vector` sized to input.
- **Files changed**: telnet_bridge.cpp

### H-7. Weak key derivation on POSIX -- FIXED
- **Resolution**: Replaced `crypt_r`-based key derivation with PBKDF2-HMAC-SHA256
  uniformly on all platforms (matching the existing Windows path).
- **Files changed**: account_manager.cpp

## Medium

### M-1. `getpass()` is deprecated -- FIXED
- **Resolution**: Implemented `readPassword()` in `hydra_main.cpp` using
  `tcsetattr` to disable echo on STDIN.
- **Files changed**: hydra_main.cpp

### M-2. Idle/detached session timeouts not enforced -- FIXED
- **Resolution**: Added session reaping in `runTimers()` using
  `sessionIdleTimeout` and `detachedSessionTimeout`. Expired sessions are
  flushed, their links closed, and persisted state deleted.
- **Files changed**: session_manager.cpp

### M-3. `findByPersistId` is O(N) linear scan -- FIXED
- **Resolution**: Added `persistIdIndex_` (`unordered_map<string, HydraSessionId>`)
  for O(1) session lookups.
- **Files changed**: session_manager.h, session_manager.cpp

### M-4. No account creation rate limiting -- FIXED
- **Resolution**: Added IP-based rate limiting (max 2 per hour) for account
  creation via both telnet and gRPC.
- **Files changed**: session_manager.h, session_manager.cpp, grpc_server.cpp

### M-5. `ipTrackers_` map grows without bound -- FIXED
- **Resolution**: Added periodic pruning (every 5 minutes) of `ipTrackers_`
  entries with zero connections and no active lockout.
- **Files changed**: session_manager.h, session_manager.cpp

### M-6. `strerror_r` assumes GNU semantics -- FIXED
- **Resolution**: Added `#if defined(_GNU_SOURCE)` check to handle both GNU and
  XSI variants of `strerror_r`.
- **Files changed**: session_manager.cpp

### M-7. GMCP cache grows without bound -- FIXED
- **Resolution**: Added `MAX_GMCP_CACHE_ENTRIES` (64) limit to `gmcpCache`.
- **Files changed**: session_manager.h, session_manager.cpp

### M-8. Stack buffer overflow risk in telnet_bridge -- FIXED
- **Resolution**: `HydraSession::OutputItem::render` moved from fixed stack
  buffer to heap-allocated `std::vector`. TelnetBridge methods also converted
  to heap buffers (see H-8).
- **Files changed**: session_manager.cpp, telnet_bridge.cpp

### M-9. `std::stoul` without exception handling -- FIXED
- **Resolution**: Wrapped `std::stoul` for Content-Length parsing in try/catch.
  Returns incomplete-request on parse failure.
- **Files changed**: grpc_web.cpp

### M-10. IP-based rate limit bypass via pruning -- FIXED
- **Resolution**: Pruning now removes expired `accountCreateTimes` entries
  first, then only erases the IP tracker if `accountCreateTimes` is also
  empty. Rate-limit window survives disconnection and pruning cycles.
- **Files changed**: session_manager.cpp

## Low

### L-1. `ListenConfig` bools uninitialized -- FIXED
- **Resolution**: Added `{false}` default initializers.
- **Files changed**: config.h

### L-2. Duplicate base64 implementations -- FIXED
- **Resolution**: Consolidated into `base64.h`/`base64.cpp` with three
  overloads: `base64Encode(uint8_t*, size_t)`, `base64Encode(string)`,
  and `base64Decode(string)`. Removed duplicate implementations from
  `websocket.cpp` and `grpc_web.cpp`.
- **Files added**: base64.h, base64.cpp
- **Files changed**: websocket.cpp, grpc_web.cpp, grpc_web.h,
  session_manager.cpp, Makefile.am

### L-3. No log rotation -- FIXED
- **Resolution**: Added SIGHUP signal handler to call `logReopen()`, enabling
  integration with standard `logrotate`.
- **Files changed**: hydra_log.h, hydra_log.cpp, hydra_main.cpp

### L-4. Dead stub files -- FIXED
- **Resolution**: Removed `front_door.cpp`, `front_door.h`, `back_door.cpp`, and
  `back_door.h` as they were superseded by GANL-integrated logic.
- **Files changed**: Makefile.am, front_door.*, back_door.*

### L-5. Hardcoded 100ms poll interval -- FIXED
- **Resolution**: Implemented adaptive polling in `hydra_main.cpp` (10ms when
  active, ramping to 100ms when idle).
- **Files changed**: hydra_main.cpp

### L-6. gRPC insecure listener not restricted -- FIXED
- **Resolution**: `GrpcServer::start()` rejects non-loopback bind addresses
  when TLS is not configured (must be `127.0.0.1`, `[::1]`, or `localhost`).
- **Files changed**: grpc_server.cpp

## Bonus: Bug fix

### B-1. `handleGrpcWebRequest` uses undeclared `handle` variable -- FIXED
- **Resolution**: All four instances of `safeWrite(handle, ...)` in
  `handleGrpcWebRequest()` changed to `safeWrite(fd.handle, ...)`. This was a
  compile error when `GRPC_ENABLED` was defined.
- **Files changed**: session_manager.cpp

### B-2. `setFrontDoorTls` called before front-door entry exists -- FIXED
- **Resolution**: Reordered event loop in `hydra_main.cpp` so `onAccept*` is
  called before `setFrontDoorTls`, ensuring the front-door entry exists in the
  map when TLS state is applied.
- **Files changed**: hydra_main.cpp

## Production Deployment

### P-1. Systemd service file -- FIXED
- **Resolution**: Added `hydra.service` with security hardening
  (NoNewPrivileges, ProtectSystem=strict, PrivateTmp), restart-on-failure,
  environment file for `HYDRA_MASTER_KEY`, and SIGHUP reload support.
- **Files added**: hydra.service

### P-2. Health check endpoint -- FIXED
- **Resolution**: grpc-web listeners serve `GET /healthz` returning HTTP 200
  `ok`. Telnet listeners support TCP health checks (accept = healthy).
  Documented in hydra.conf.example for ALB/NLB configuration.
- **Files changed**: session_manager.cpp, hydra.conf.example

### P-3. Graceful connection drain on SIGTERM -- FIXED
- **Resolution**: On SIGTERM, sessions are notified and flushed, gRPC server
  stops accepting new RPCs, then a 3-second drain loop flushes pending writes
  while rejecting new connections. `TimeoutStopSec=10` in systemd unit.
- **Files changed**: hydra_main.cpp, hydra.service

### P-4. Build hardening flags -- FIXED
- **Resolution**: Added `-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`,
  `-Wformat -Wformat-security` to compiler flags. Full RELRO
  (`-Wl,-z,relro,-z,now`) added to linker flags. Existing `-pie` retained.
- **Files changed**: Makefile.am

### P-5. Metrics/monitoring
- **Status**: Open.
- **Recommendation**: Add Prometheus endpoint or structured JSON logging for
  CloudWatch. Useful metrics: active sessions, front-door count, back-door
  link count, scrollback memory usage, auth failures per minute.

## Deployment issues (discovered 2026-03-28)

### D-1. Write-path fix bypasses GANL transport layer
- **Status**: Reopened (2026-03-28).
- **Problem**: The new `safeWrite()`/`drainWriteBuffer()` path writes with raw
  `::send()` on `ganl::ConnectionHandle`. That bypasses GANL's transport layer,
  so TLS front-doors (`telnet+tls`, `websocket+tls`, `grpc-web+tls`) can send
  plaintext bytes directly on the socket instead of encrypted transport
  records. This is a likely cause of "connects, then falls apart" behavior in
  real deployments.
- **Recommendation**: Keep the event-driven buffering idea, but move the actual
  writes back behind GANL/transport-aware APIs so TLS and future transports
  remain correct.
- **Files changed**: session_manager.h, session_manager.cpp, hydra_main.cpp

### D-2. Proto field name drift: `system_notice` vs `notice`
- **Status**: Fixed (2026-03-28).
- **Problem**: `session_manager.cpp` called `mutable_system_notice()` but
  `hydra.proto` defines the field as `notice` (line 237:
  `SystemNotice notice = 3`).  Compile error when `GRPC_ENABLED` is set.
- **Resolution**: Changed `mutable_system_notice()` to `mutable_notice()`
  in session_manager.cpp (2 occurrences, lines 644 and 661).
- **Note**: Cross-check all client code under `./client/` against
  `hydra.proto` for similar drift.
- **Files changed**: session_manager.cpp

### D-3. NGINX stream listener conflicts with Hydra telnet port
- **Status**: Fixed (configuration change).
- **Problem**: DEPLOY.md configures both NGINX stream and Hydra to use
  port 4201.  NGINX binds `0.0.0.0:4201` first, preventing Hydra from
  binding `127.0.0.1:4201`.  Only the grpc-web listener starts.
- **Resolution**: Hydra telnet listener moved to port 4202 in
  hydra.conf.  NGINX stream upstream updated to proxy to 4202.
  DEPLOY.md should be updated to reflect this.
- **Files affected**: hydra.conf, DEPLOY.md, hydra-stream.nginx.conf

### D-4. `GameOutput.text` receives malformed UTF-8
- **Status**: Open.
- **Problem**: Hydra logs protobuf serialization errors for
  `hydra.GameOutput.text` containing invalid UTF-8. The field is correctly a
  `string`; PUA code points are legal Unicode scalar values and are not the
  issue by themselves. The likely fault is earlier in the pipeline:
  `TelnetBridge::ingestGameOutput()` passes bytes through unchanged whenever
  the game encoding is treated as UTF-8/ASCII, without validating UTF-8 or
  carrying incomplete multibyte sequences across socket reads. That can inject
  malformed UTF-8 into gRPC/grpc-web/WebSocket protobuf messages.
- **Recommendation**: Add temporary diagnostics before `set_text()` to log the
  first invalid byte offset and a short hex dump. Then either validate/sanitize
  claimed UTF-8 input or add a carry-over buffer for split multibyte sequences
  on back-door reads.
- **Files implicated**: telnet_bridge.cpp, session_manager.cpp, grpc_server.cpp,
  hydra.proto
