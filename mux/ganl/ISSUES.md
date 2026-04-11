# GANL (Global Adaptive Network Layer) — Open Issues

Updated: 2026-04-04

## High — Implementation Gaps

### ~~Missing waiting logic in `connection.cpp`~~ FIXED
- **File:** `mux/ganl/src/connection.cpp:558`
- TLS shutdown now defers `networkEngine_.closeConnection()` until pending output drains. `close()` keeps the connection in `Closing`, allows `postWrite()` while draining queued shutdown bytes, and both readiness and IOCP write handlers finalize the socket close only after `encryptedOutput_` is empty.

### ~~Missing password callback for encrypted keys~~ FIXED
- **File:** `mux/ganl/src/openssl_transport.cpp:75`
- `OpenSSLTransport` now installs an `SSL_CTX` password callback backed by `TlsConfig::password`, so PEM private keys protected with passphrases can be loaded without pre-decrypting them on disk.

## Medium — Protocol Handling Gaps

### ~~Missing ANSI/MXP processing~~ FIXED
- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:597`
- `formatOutput()` now respects negotiated capabilities: ANSI-less clients have CSI/OSC escape sequences stripped before transmission, and non-MXP clients have obvious MXP tags suppressed instead of receiving raw markup.

### ~~Incomplete NEW-ENVIRON parsing~~ FIXED
- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:1114, 1120`
- `TelnetProtocolHandler` now sends `SB NEW-ENVIRON SEND` after `WILL NEW-ENVIRON`, parses `VAR/USERVAR/VALUE/ESC` sequences from `IS` and `INFO`, updates width/height plus ANSI/MXP capability hints from environment values, and answers `SEND` with a bounded `SB NEW-ENVIRON IS` reply.

## High — Memory Safety (New, 2026-04-04)

### ~~Unbounded input buffer growth in telnet protocol handler~~ FIXED

- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:498, 505, 542, 557`
- `TelnetProtocolHandler` now caps `inputBuffer` at 8 KB and `subnegotiationBuffer` at 4 KB. Overflow sets `lastError` and returns `false` from `processInput()`, which closes the connection as a protocol error.

### ~~Dangling pointer risk in select_network_engine.cpp~~ FIXED

- **File:** `mux/ganl/src/select_network_engine.cpp:480, 711-716, 773`
- `SelectNetworkEngine` no longer stores a borrowed `IoBuffer*` from `postRead()`. Readiness events now carry `nullptr` for `IoEvent.buffer`, leaving buffer ownership entirely with `Connection`.

## Medium — Protocol Safety (New, 2026-04-04)

### ~~No NAWS dimension validation~~ FIXED

- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:1029-1034`
- NAWS width/height are now validated to `1..1000`; out-of-range values are reset to the handler defaults of `80x24`.

### ~~Unbounded TTYPE response length~~ FIXED

- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:1062-1069`
- Outbound TTYPE responses now clamp `clientTtype` to 256 bytes before appending it to the subnegotiation reply.

### ~~No negotiation timeout mechanism~~ FIXED

- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:341-374`, `mux/ganl/src/connection.cpp`, `mux/ganl/src/*_network_engine.cpp`
- Idle `processEvents()` timeouts now sweep active connections and call `ConnectionBase::checkNegotiationTimeout()`, so stalled telnet handshakes age out even when the client goes silent after the initial negotiation bytes.

## Low — Technical Debt

### ~~Missing configuration for StartTLS~~ FIXED
- **File:** `mux/ganl/include/telnet_protocol_handler.h:57`
- `TelnetProtocolHandler` now stores an `offerStartTls_` policy with a constructor argument and setter, so integrations can explicitly disable STARTTLS offers instead of being locked to a hardcoded `true`.

## High — Concurrency & Lifetime (New, 2026-04-10)

### OpenSSL context pointer dereferenced outside session map lock
- **File:** `mux/ganl/src/openssl_transport.cpp` (approximately line 307, `SSL_read`/`SSL_write` path)
- **Issue:** The transport looks up the per-session `SSL *` by connection id under `sessionsMutex_`, extracts a borrowed pointer, drops the lock, and then dereferences the pointer. A concurrent `destroySessionContext()` call (e.g., triggered by a close readiness event on another thread, or by the engine from inside a write completion) can remove the map entry and free the `SSL *` between the two operations.
- **Fix:** Hold the lock for the whole `SSL_*` call, or promote the map value to `std::shared_ptr<SslSession>` so lookup returns an owning handle.

### `sslObjectsToFree` drained outside the lock that protected it
- **File:** `mux/ganl/src/openssl_transport.cpp:155-157, 218, 269`
- **Issue:** `shutdown()` (and the shutdown paths driven from close-notify) iterate `sessions_` under a lock, copy `SSL *` pointers into a local `std::vector`, unlock, and then call `SSL_free()` on each. `SSL_free` in turn frees the attached `BIO *`. If another thread has just re-added a session with a colliding id (or if the read/write path is still holding the bare pointer from the audit above), the second access fires on freed memory.

### `recv()` / `read()` returning 0 treated like any other "no more data"
- **File:** `mux/ganl/src/connection.cpp:1022-1024` (approximate)
- **Issue:** The readiness loop breaks on `read() == 0` without marking the connection as peer-closed. Downstream code still attempts `postWrite()` and keeps the negotiation-timeout timer running on a half-closed socket. In the IOCP path the symmetric bug is that `CompletionConnection::handleRead()` silently ignores `postRead()` failures after a close, leaking the connection from the event loop.
- **Fix:** Distinguish EOF from `EAGAIN` explicitly and transition into the `Closing` state from both readiness and IOCP paths.

## Medium — Socket Setup & Protocol Parsing (New, 2026-04-10)

### `SO_REUSEADDR` / `SO_REUSEPORT` failures silently ignored
- **File:** `mux/ganl/src/select_network_engine.cpp:204`, `mux/ganl/src/epoll_network_engine.cpp:174`
- **Issue:** `setsockopt` return values for SO_REUSEADDR are discarded. If the call fails (unusual, but possible on some kernels or in seccomp sandboxes), subsequent `bind()` can race with a TIME_WAIT leftover from the previous run, leaving the restart path stuck until the old endpoint times out.

### `IPV6_V6ONLY = 0` setsockopt not validated
- **File:** `mux/ganl/src/select_network_engine.cpp:340`, `mux/ganl/src/epoll_network_engine.cpp:565`
- **Issue:** Dual-stack mode is requested by setting `IPV6_V6ONLY` to 0, then `listen()` runs regardless of whether the setsockopt succeeded. On FreeBSD with `net.inet6.ip6.v6only=1` the listener silently becomes IPv6-only despite the config. Log a warning if the call fails.

### Accept-side FD not immediately set non-blocking
- **File:** `mux/ganl/src/select_network_engine.cpp:875-915`
- **Issue:** `acceptConnection()` calls `accept()` and then acquires the accept-handler lock before calling `setNonBlocking()`. Between the two a signal handler or concurrent operation could observe the fd in its default blocking state. Using `accept4(SOCK_NONBLOCK|SOCK_CLOEXEC)` on Linux closes the window in one syscall.

### Incomplete telnet subnegotiation stalls the state machine
- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:678-720` (approximate)
- **Issue:** If a client sends `IAC SB TTYPE IS foo` but never `IAC SE`, the handler stays in `Subnegotiation_IAC`/`Subnegotiation` indefinitely. The handshake timeout added in the previous pass only fires before negotiation completes, not mid-subnegotiation. Add a per-subnegotiation timeout (or cap the subnegotiation buffer plus deadline) so a half-open client cannot pin state.

### `IAC IAC` escape silently dropped when inputBuffer is full
- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:639-647` (approximate)
- **Issue:** The 8 KiB inputBuffer cap rejects any further `0xFF` byte — but the reject path also drops legitimate literal `0xFF` characters that were correctly escaped as `IAC IAC`. Client-sent binary data that happens to contain 0xFF becomes corrupted instead of triggering a protocol-error close.
- **Fix:** Treat the buffer-full case the same as any other protocol overflow and close the connection.

## Low — Hardening

### No CA validation configured after `SSL_CTX_use_certificate_chain_file`
- **File:** `mux/ganl/src/openssl_transport.cpp:93-113` (approximate)
- **Issue:** The server certificate is loaded but no trust store is configured and no verify flags are set. STARTTLS peers are never validated. When `verifyPeer` is false this is silent — no log line indicates that peer validation was requested-but-bypassed. Log a warning when `verifyPeer` is requested but `SSL_CTX_set_verify()` defaults to `SSL_VERIFY_NONE`.
