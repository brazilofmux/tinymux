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

### H-4. Session tokens never expire or rotate
- **Status**: Open.
- **Recommendation**: Add token rotation, TTL-based expiry, and consider IP
  binding for sensitive operations.

### H-5. No TLS on gRPC native listener
- **Status**: Open.
- **Recommendation**: Add TLS support, or document that gRPC native must only
  bind to loopback/VPN.

### H-6. Non-constant-time password comparison -- FIXED
- **Resolution**: Replaced `std::string::operator!=` with `CRYPTO_memcmp` from
  OpenSSL in `AccountManager::authenticate()`.
- **Files changed**: account_manager.cpp

### H-7. Weak key derivation on POSIX -- FIXED
- **Resolution**: Replaced `crypt_r`-based key derivation with PBKDF2-HMAC-SHA256
  uniformly on all platforms (matching the existing Windows path).
- **Files changed**: account_manager.cpp
- **Note**: Existing databases with keys derived via the old `crypt_r` method
  will need password resets to re-derive scroll-back keys.

## Medium

### M-1. `getpass()` is deprecated
- **Status**: Open.
- **Recommendation**: Use custom terminal echo-disable approach.

### M-2. Idle/detached session timeouts not enforced -- FIXED
- **Resolution**: Added session reaping in `runTimers()` using
  `sessionIdleTimeout` and `detachedSessionTimeout`. Expired sessions are
  flushed, their links closed, and persisted state deleted.
- **Files changed**: session_manager.cpp

### M-3. `findByPersistId` is O(N) linear scan
- **Status**: Open.
- **Recommendation**: Add `std::unordered_map<std::string, HydraSessionId>`.

### M-4. No account creation rate limiting
- **Status**: Open.
- **Recommendation**: Rate-limit per IP.

### M-5. `ipTrackers_` map grows without bound -- FIXED
- **Resolution**: Added periodic pruning (every 5 minutes) of `ipTrackers_`
  entries with zero connections and no active lockout.
- **Files changed**: session_manager.h, session_manager.cpp

### M-6. `strerror_r` assumes GNU semantics
- **Status**: Open.
- **Recommendation**: Guard with `#ifdef _GNU_SOURCE` or use `strerror_l()`.

### M-7. GMCP cache grows without bound
- **Status**: Open.
- **Recommendation**: Limit cached GMCP packages.

### M-8. Stack buffer overflow risk in telnet_bridge
- **Status**: Open.
- **Recommendation**: Validate buffer sizes or use heap allocation.

### M-9. `std::stoul` without exception handling -- FIXED
- **Resolution**: Wrapped `std::stoul` for Content-Length parsing in try/catch.
  Returns incomplete-request on parse failure.
- **Files changed**: grpc_web.cpp

## Low

### L-1. `ListenConfig` bools uninitialized -- FIXED
- **Resolution**: Added `{false}` default initializers.
- **Files changed**: config.h

### L-2. Duplicate base64 implementations
- **Status**: Open.
- **Recommendation**: Consolidate into shared utility.

### L-3. No log rotation
- **Status**: Open.
- **Recommendation**: Integrate with logrotate via SIGHUP or implement internal
  rotation.

### L-4. Dead stub files
- **Status**: Open.
- **Recommendation**: Remove or complete the planned refactor.

### L-5. Hardcoded 100ms poll interval
- **Status**: Open.
- **Recommendation**: Make configurable or adaptive.

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

## Production Deployment Gaps

- No systemd service file for process supervision
- No health check endpoint for AWS ALB/NLB integration
- No metrics/monitoring (Prometheus, CloudWatch)
- No graceful connection drain on SIGTERM
- Missing build hardening flags (`-fstack-protector-strong`, `-D_FORTIFY_SOURCE=2`)
