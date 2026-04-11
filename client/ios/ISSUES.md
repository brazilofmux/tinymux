# Titan iOS Client — Open Issues

Updated: 2026-04-10

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

## Remaining Enhancements

### ~~Hydra viewport sizing still uses a coarse screen-bounds estimate~~ FIXED

- Replaced `UIScreen.main.bounds` estimation with a `GeometryReader` on the
  output pane.  Terminal columns/rows are now computed from the actual view
  dimensions and configured font size.

### ~~Hydra terminal size is not updated after connect~~ FIXED

- `ContentView.outputPane` now sends a fresh `SetPreferences` via
  `HydraConnection.updateTerminalSize()` whenever the output pane geometry
  changes (device rotation, split-screen, etc.).  The update is suppressed
  when the computed dimensions haven't changed.

## Bugs (New, 2026-04-10)

### Hydra reconnect path never fetches missed scroll-back

- **File:** `client/ios/Titan/Net/HydraConnection.swift:224-243`
- **Issue:** `attemptReconnect()` only reopens `GameSession` via `runGameSession(stub:)`. Unlike the Android client, it never calls `GetScrollBack` after reconnect, even though the session RPC surface exposes scrollback length and the server retains buffered output.
- **Impact:** Any game output emitted while the iOS client is disconnected is lost permanently from the local transcript after reconnect. This is a user-visible parity gap versus Android/console, which already repopulate missed lines.
- **Fix:** After a successful reconnect, issue `GetScrollBack` with `ANSI_TRUECOLOR` and append the returned lines before resuming live stream processing.
