# Titan Android Client — Open Issues

Updated: 2026-03-29

## Bugs

### ~~Trigger conditions never receive real idle time~~ FIXED

- Trigger evaluation now uses per-tab/transport idle tracking instead of a
  hardcoded `0`.

### ~~Potential race on `inputChannel` during reconnect~~ FIXED

- The Android Hydra client now swaps the active bidi input channel under a
  lock, so reconnect and send paths do not race on different stream channels.

## Newly Confirmed

### ~~Hydra reconnect resends stale `80x24` terminal geometry~~ FIXED

- Reconnect now resends the original computed terminal width/height instead of
  falling back to `80x24`.

### ~~Vendored Android `hydra.proto` lagged the proxy schema~~ FIXED

- Android now includes `GameOutput.end_of_record`, which lets the client
  preserve prompt boundaries instead of treating every `GameOutput.text` chunk
  as a complete display line.
