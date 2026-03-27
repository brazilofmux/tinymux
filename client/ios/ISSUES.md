# Titan iOS Client — Open Issues

Updated: 2026-03-27

## Bugs

### ~~Hydra `GameSession` never sends initial `SetPreferences`~~ FIXED

- `runGameSession()` now yields a `SetPreferences` message (ANSI_TRUECOLOR, 80x24, "Titan-iOS") as the first item on the bidi stream, before the ping task starts. Sent on both initial connect and reconnect since `runGameSession` is called from both paths.

### ~~`EventLoopGroup` lifetime is unmanaged across connects~~ FIXED

- `EventLoopGroup` is now stored as an instance variable, reused across reconnects, and shut down via `syncShutdownGracefully()` in `disconnect()`.

### Hardcoded 80x24 viewport

- **Issue:** `SetPreferences` sends `terminalWidth=80` and `terminalHeight=24`. On iOS, the viewport depends on the text view layout. Actual dimensions should be queried from the UI and updated on resize.
- **Severity:** Medium — functional but not pixel-accurate.
