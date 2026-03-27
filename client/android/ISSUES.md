# Titan Android Client — Open Issues

Updated: 2026-03-27

## Bugs

### Trigger conditions never receive real idle time

- **Evidence:** `client/android/app/src/main/java/org/tinymux/titan/ui/TitanApp.kt:206-209` constructs `ConditionContext` with `idleSeconds = 0`.
- **Impact:** Any trigger, timer, or automation logic that depends on connection idle time is effectively disabled or misleading on Android.
- **Mitigation:** Wire `idleSeconds` to the active tab's transport state instead of a placeholder literal.

### Potential race on `inputChannel` during reconnect

- **File:** `client/android/app/src/main/java/org/tinymux/titan/net/HydraConnection.kt:50`
- **Issue:** `@Volatile` ensures reference visibility, but `sendLine()` at line 204 may target the old channel if called concurrently with reconnect creating a new one. There is a race window between checking the connected flag and sending.
- **Impact:** Input could be lost or sent to the wrong stream during reconnect.
- **Fix:** Synchronize reconnect channel swap with send path, or use an atomic reference with a wrapper.
