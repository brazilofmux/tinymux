# MUX Networking & Infrastructure — Open Issues

## GANL (Global Adaptive Network Layer)

### ~~1. Session Error Reporting~~ (Fixed)

- Per-session error strings stored in `session_errors_` map, set at failure points in `onConnectionOpen`, cleared on close.

## Core Networking

### 2. Output Throttling Threshold

- **File:** `net.cpp:144`
- **Issue:** The output queue threshold for triggered flushing needs to be tuned. The current limit might cause unnecessary latency or unproductive calls to `process_output`.

## Closed

- **Session Initialization/Cleanup** — Stubs are correct; per-connection setup/teardown is handled in `onConnectionOpen`/`onConnectionClose`, no session-manager-level init needed.
- **Configurable Transport** — `SecureTransportFactory` already selects the platform backend (Schannel/OpenSSL) and gracefully falls back if TLS init fails. No config needed.
- **Listener Error Handling** — Fixed in dea9ecabe. Listener errors now logged via LOG_NET/LERR; engines re-arm listeners internally.
