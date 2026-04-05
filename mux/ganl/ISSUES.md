# GANL (Global Adaptive Network Layer) — Open Issues

Updated: 2026-04-04

## High — Implementation Gaps

### Missing waiting logic in `connection.cpp`
- **File:** `mux/ganl/src/connection.cpp:558`
- **Issue:** `// TODO: Implement waiting logic: We should wait for this write to complete`. 
- **Impact:** Potential data loss or out-of-order delivery if multiple writes are queued and the underlying transport (e.g., OpenSSL) requires blocking or specific event handling before proceeding.

### Missing password callback for encrypted keys
- **File:** `mux/ganl/src/openssl_transport.cpp:75`
- **Issue:** `// TODO: Add password callback support if keys are encrypted`.
- **Impact:** Private keys with passphrases cannot be loaded, making them unusable for SSL/TLS connections without pre-decrypting the keys on disk.

## Medium — Protocol Handling Gaps

### Missing ANSI/MXP processing
- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:597`
- **Issue:** `// TODO: Add ANSI/MXP processing based on context.state.supportsANSI etc.`
- **Impact:** Features like MXP (Mud eXtension Protocol) and advanced ANSI features (beyond basic colors) may not be fully supported in the new GANL layer.

### Incomplete NEW-ENVIRON parsing
- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:1114, 1120`
- **Issue:** Detailed parsing of `VAR/USERVAR/VALUE` sequences and response with `IAC SB NEW-ENVIRON IS ...` is missing.
- **Impact:** Terminal environment negotiation (useful for determining client capabilities) is incomplete.

## High — Memory Safety (New, 2026-04-04)

### ~~Unbounded input buffer growth in telnet protocol handler~~ FIXED

- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:498, 505, 542, 557`
- `TelnetProtocolHandler` now caps `inputBuffer` at 8 KB and `subnegotiationBuffer` at 4 KB. Overflow sets `lastError` and returns `false` from `processInput()`, which closes the connection as a protocol error.

### Dangling pointer risk in select_network_engine.cpp

- **File:** `mux/ganl/src/select_network_engine.cpp:480, 711-716, 773`
- **Issue:** `sockIt->second.activeReadBuffer = &buffer` stores a raw pointer to an `IoBuffer&` passed from `postRead()`. If the buffer's lifetime ends before the event loop processes the read, the pointer dangles.
- **Impact:** Use-after-free, crash, memory corruption.
- **Recommendation:** Use `shared_ptr` or validate pointer validity before each use.

## Medium — Protocol Safety (New, 2026-04-04)

### ~~No NAWS dimension validation~~ FIXED

- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:1029-1034`
- NAWS width/height are now validated to `1..1000`; out-of-range values are reset to the handler defaults of `80x24`.

### ~~Unbounded TTYPE response length~~ FIXED

- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:1062-1069`
- Outbound TTYPE responses now clamp `clientTtype` to 256 bytes before appending it to the subnegotiation reply.

### No negotiation timeout mechanism

- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:341-374`
- **Issue:** Negotiation timeout is set to 10 seconds but `onNegotiationTimeout()` must be explicitly called — no timer mechanism exists. Slow clients stay in `TelnetNegotiating` state indefinitely.
- **Impact:** Resource leak from connections that never complete negotiation.
- **Recommendation:** Integrate timeout with event loop or add background timer.

## Low — Technical Debt

### Missing configuration for StartTLS
- **File:** `mux/ganl/include/telnet_protocol_handler.h:57`
- **Issue:** `canOfferStartTls()` returns hardcoded `true` with a TODO to make it configurable.
- **Impact:** Administrators cannot disable StartTLS via configuration if desired.
