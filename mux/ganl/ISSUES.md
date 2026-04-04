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

### Unbounded input buffer growth in telnet protocol handler

- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:498, 505, 542, 557`
- **Issue:** `inputBuffer` and `subnegotiationBuffer` are `std::vector<char>` with no size limits. `push_back()` calls during normal input and subnegotiation have no bounds checking. A malicious client can exhaust server memory.
- **Impact:** Denial of Service via memory exhaustion.
- **Recommendation:** Add configurable max limits (e.g., 8KB input, 4KB subneg) and close connection on overflow.

### Dangling pointer risk in select_network_engine.cpp

- **File:** `mux/ganl/src/select_network_engine.cpp:480, 711-716, 773`
- **Issue:** `sockIt->second.activeReadBuffer = &buffer` stores a raw pointer to an `IoBuffer&` passed from `postRead()`. If the buffer's lifetime ends before the event loop processes the read, the pointer dangles.
- **Impact:** Use-after-free, crash, memory corruption.
- **Recommendation:** Use `shared_ptr` or validate pointer validity before each use.

## Medium — Protocol Safety (New, 2026-04-04)

### No NAWS dimension validation

- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:1029-1034`
- **Issue:** NAWS values (width/height) are accepted without validation. A client can send 0x0 or 65535x65535, which may cause issues in downstream code that assumes reasonable terminal dimensions.
- **Recommendation:** Validate `0 < width <= 1000` and `0 < height <= 1000`, use defaults for out-of-range.

### Unbounded TTYPE response length

- **File:** `mux/ganl/src/telnet_protocol_handler.cpp:1062-1069`
- **Issue:** `clientTtype` string has no size limit. A client-mode connection with a very long `clientTtype` will generate unbounded TTYPE responses.
- **Recommendation:** Limit terminal type string to 256 bytes.

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
