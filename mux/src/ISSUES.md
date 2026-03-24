# MUX Networking & Infrastructure — Open Issues

## GANL (Global Adaptive Network Layer)

### ~~1. Session Error Reporting~~ (Fixed)

- Per-session error strings stored in `session_errors_` map, set at failure points in `onConnectionOpen`, cleared on close.

## Core Networking

### ~~2. Output Throttling Threshold~~ (Fixed)

- Default `output_limit` bumped from 16384 to `2 * LBUF_SIZE` (65536) to match the LBUF_SIZE increase to 32K. Stale TODO removed — with GANL, `process_output` always fully drains the queue.

## Closed

- **Session Initialization/Cleanup** — Stubs are correct; per-connection setup/teardown is handled in `onConnectionOpen`/`onConnectionClose`, no session-manager-level init needed.
- **Configurable Transport** — `SecureTransportFactory` already selects the platform backend (Schannel/OpenSSL) and gracefully falls back if TLS init fails. No config needed.
- **Listener Error Handling** — Fixed in dea9ecabe. Listener errors now logged via LOG_NET/LERR; engines re-arm listeners internally.
