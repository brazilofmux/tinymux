# Titan iOS Client — Open Issues

Updated: 2026-03-27

## Bugs

### Hydra `GameSession` never sends initial `SetPreferences`

- **Evidence:** `client/ios/Titan/Net/HydraConnection.swift:138-157` opens the bidi stream and only yields periodic ping messages.
- **Missing fields:** `color_format`, `terminal_width`, `terminal_height`, and `terminal_type`.
- **Impact:** The iOS client can connect successfully, but it does not advertise viewport or color capabilities the way the other Hydra clients do. Server defaults may be used instead, which can produce degraded formatting or mismatched negotiation.

### `EventLoopGroup` lifetime is unmanaged across connects

- **Evidence:** `client/ios/Titan/Net/HydraConnection.swift:58-63` allocates a fresh `EventLoopGroup` inside `connect()`, but `client/ios/Titan/Net/HydraConnection.swift:111-113` only closes the channel and never shuts the group down.
- **Impact:** Repeated connect/disconnect cycles can leak threads and related NIO resources in long-lived app sessions.
- **Fix:** Store `EventLoopGroup` as an instance variable and call `shutdown()` on it in `disconnect()`.
