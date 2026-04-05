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
