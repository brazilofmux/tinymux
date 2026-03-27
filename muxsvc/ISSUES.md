# Windows Service (muxsvc) — Open Issues

Updated: 2026-03-27

## High — Functional Correctness

### Service main loop is a placeholder

- **File:** `muxsvc.cpp:68-71`
- **Issue:** `ServiceMain()` just sleeps for 2 seconds in a loop. No actual game server work is performed.
- **Impact:** Service registers and starts but does nothing useful.

### ~~Event creation failures not checked~~ FIXED

- All three `CreateEvent()` calls now check for `NULL` return and report error via `ErrorPrinter()` + `SendStatus(SERVICE_STOPPED)` before returning.

### ~~Global event names cause multi-instance conflicts~~ FIXED

- Changed from named events (`L"StopEvent"`, etc.) to unnamed events (`NULL` name parameter). Each service instance now gets private event handles.

## Low — Modernization

### Outdated service pattern

- **File:** `muxsvc.cpp:31-43`
- **Issue:** Uses old-style `StartServiceCtrlDispatcher()` with hardcoded service table. No extended status reporting, no pre-shutdown notification support.
