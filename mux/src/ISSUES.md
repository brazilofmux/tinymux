# MUX Networking & Infrastructure — Open Issues

## GANL (Global Adaptive Network Layer)

### 1. Session Initialization/Cleanup

- **File:** `ganl_adapter.cpp:458-459`
- **Issue:** Investigate if any specific TinyMUX session initialization or cleanup logic is missing from the `initialize()` and `shutdown()` methods of the session manager.

### 2. Session Error Reporting

- **File:** `ganl_adapter.cpp:1152`
- **Issue:** Consider storing the last error per session for better diagnostic reporting via `getLastSessionErrorString`.

### 3. Configurable Transport

- **File:** `ganl_adapter.cpp:1273`
- **Issue:** The transport type is currently hardcoded. It should be made configurable to allow for flexible selection between TLS/SSL and other protocols.

### 4. Listener Error Handling

- **File:** `ganl_adapter.cpp:1839`
- **Issue:** Improve error handling for network listener events to ensure the server gracefully recovers from listener-specific failures.

## Core Networking

### 5. Output Throttling Threshold

- **File:** `net.cpp:144`
- **Issue:** The output queue threshold for triggered flushing needs to be tuned. The current limit might cause unnecessary latency or unproductive calls to `process_output`.
