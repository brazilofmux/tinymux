# Titan iOS Client — Open Issues

Updated: 2026-03-29

## Bugs

### ~~Hydra `GameSession` never sends initial `SetPreferences`~~ FIXED

- `runGameSession()` now yields a `SetPreferences` message (ANSI_TRUECOLOR, 80x24, "Titan-iOS") as the first item on the bidi stream, before the ping task starts. Sent on both initial connect and reconnect since `runGameSession` is called from both paths.

### ~~`EventLoopGroup` lifetime is unmanaged across connects~~ FIXED

- `EventLoopGroup` is now stored as an instance variable, reused across reconnects, and shut down via `syncShutdownGracefully()` in `disconnect()`.

### ~~Hardcoded 80x24 viewport~~ FIXED

- Initial Hydra `SetPreferences` now uses an estimated terminal size derived from
  the current screen bounds and configured font size instead of a fixed `80x24`.

## Newly Confirmed

### ~~Hydra output ignored prompt/end-of-record boundaries~~ FIXED

- The iOS Hydra client now buffers `gameOutput.text` and flushes partial lines
  on `endOfRecord`, instead of assuming every chunk is a complete display line.

### ~~Trigger/timer automation was telnet-only~~ FIXED

- `ConditionContext` and trigger-command execution now use `WorldTab` transport
  state instead of only `tab.connection`, so Hydra tabs participate in idle,
  connected, timer, and trigger-command behavior.
