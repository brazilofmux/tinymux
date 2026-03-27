# Windows Service (muxsvc) — Open Issues

Updated: 2026-03-27

## High — Functional Correctness

### Service main loop is a placeholder

- **File:** `muxsvc.cpp:68-71`
- **Issue:** `ServiceMain()` just sleeps for 2 seconds in a loop. No actual game server work is performed.
- **Impact:** Service registers and starts but does nothing useful.

### Event creation failures not checked

- **File:** `muxsvc.cpp:57-59`
- **Issue:** `CreateEvent()` return values not checked. If any event creation fails, `WaitForMultipleObjects()` at line 73 will receive null handles and behave unpredictably.
- **Fix:** Check each `CreateEvent()` return and fail gracefully.

### Global event names cause multi-instance conflicts

- **File:** `muxsvc.cpp:57-59`
- **Issue:** Named events "StopEvent", "PauseEvent", "ContinueEvent" are global. If multiple service instances run on the same machine, they share these events.
- **Fix:** Use unnamed events or unique per-instance names.

## Low — Modernization

### Outdated service pattern

- **File:** `muxsvc.cpp:31-43`
- **Issue:** Uses old-style `StartServiceCtrlDispatcher()` with hardcoded service table. No extended status reporting, no pre-shutdown notification support.
