# TinyMUX GANL Integration Plan

## Overview

This document outlines the plan for integrating the GANL (Great American Networking Layer) framework into TinyMUX, replacing the current networking infrastructure in `bsd.cpp`, `netcommon.cpp`, and parts of `game.cpp`. GANL provides a modern, layered, event-driven networking architecture with support for multiple I/O models (IOCP, epoll, kqueue) and secure transport (OpenSSL, SChannel).

## Architectural Comparison

### Current TinyMUX Networking

TinyMUX's current networking is built around the monolithic `DESC` structure:

- **Core data structure:** `struct descriptor_data` (DESC) - A single large structure containing socket, buffers, state, player ID, and all connection information
- **I/O models:** Platform-specific code (Windows IOCP vs. Unix select/poll)
- **Main loop:** `shovechars()` - Handles all connection processing
- **Network handling:** Functions in `bsd.cpp` for socket operations
- **Protocol handling:** Inline Telnet negotiation in `bsd.cpp`
- **SSL handling:** Mixed into the socket code with platform-specific conditionals
- **Notification path:** `raw_notify()` -> `queue_string()` -> `queue_write()` -> `process_output()`
- **Command path:** Socket read -> Telnet processing -> `save_command()` -> `do_command()` -> Game logic

### GANL Architecture

GANL employs a layered approach with clear separation of concerns:

- **Core component:** `ConnectionBase` class (abstract) with derived `ReadinessConnection` and `CompletionConnection` classes
- **I/O abstraction:** `NetworkEngine` interface with platform-specific implementations
- **Buffer management:** `IoBuffer` class for efficient memory handling
- **Protocol handling:** `ProtocolHandler` interface for Telnet negotiation and formatting
- **Secure transport:** `SecureTransport` interface for TLS operations
- **Session management:** `SessionManager` interface for player sessions and state

## Integration Strategy

The integration will follow a phased approach to maintain stability while progressively replacing components:

### Phase 1: Foundation (1-2 months)

1. **Setup GANL in TinyMUX build system**
   - Add GANL to the build system (Makefiles, autoconf)
   - Make GANL compilable in TinyMUX's environment
   - Create compatibility layer for platform definitions

2. **Define integration points**
   - Identify key connection points between MUX game logic and networking
   - Create mapping between GANL types and TinyMUX types
   - Establish state transition mappings

3. **Implement `MuxSessionManager`**
   - Create a concrete implementation of GANL's `SessionManager` interface
   - Map to existing TinyMUX player management
   - Bridge to existing `DESC` operations during transition

### Phase 2: Side-by-Side Implementation (2-3 months)

1. **Implement parallel networking system**
   - Add GANL main loop alongside `shovechars()`
   - Create configuration option to select networking system
   - Enable GANL to use existing TinyMUX SSL certificates

2. **Create notification bridge**
   - Modify `raw_notify()` to route through the appropriate system
   - Create adapter functions for command handling

3. **Implement concrete classes**
   - Create `MuxProtocolHandler` implementing the GANL `ProtocolHandler` interface
   - Implement `OpenSSLTransport` and/or `SChannelTransport` for secure connections
   - Implement platform-specific `NetworkEngine` classes (IOCP, epoll, kqueue)

### Phase 3: Integration and Testing (2-3 months)

1. **Implement the main event loop**
   - Replace `shovechars()` with GANL event processing
   - Handle graceful shutdown and restart
   - Ensure proper signal handling

2. **Feature parity verification**
   - Test SSL functionality
   - Verify Telnet negotiation and option handling
   - Test encoding conversions
   - Verify ANSI color support

3. **Performance testing**
   - Compare old and new systems under load
   - Optimize critical paths
   - Test memory usage and resource handling

### Phase 4: Complete Transition (1-2 months)

1. **Remove legacy code**
   - Remove deprecated networking functions
   - Eliminate dual-path routing

2. **Update documentation**
   - Update configuration documentation
   - Document new architecture for developers

3. **Final system tuning**
   - Fine-tune buffer sizes and timeouts
   - Adjust error handling and logging

## Detailed Component Mapping

### `DESC` Structure → `ConnectionBase` Class

The `DESC` structure contains many aspects that will be distributed across several GANL components:

| DESC Member | GANL Replacement | Notes |
|-------------|------------------|-------|
| `socket` | `ConnectionBase::handle_` | Socket handle |
| `ssl_session` | Handled by `SecureTransport` | SSL context moved to dedicated class |
| `input_buffer` & overlapped I/O | Handled by `NetworkEngine` | I/O model specific details abstracted |
| `connected_at`, `last_time` | `SessionStats` | Tracked in session manager |
| `output_head`, `output_tail` | `IoBuffer` classes | Modern buffer management |
| `raw_input_state`, etc. | `ProtocolHandler` | Telnet state moved to protocol layer |
| `nvt_him_state`, `nvt_us_state` | `ProtocolHandler` | Telnet negotiation in dedicated class |
| `player` | `SessionManager::getPlayerId()` | Player ID mapped via session ID |
| `encoding`, `width`, `height` | `ProtocolState` | Terminal capabilities abstracted |
| `address`, `addr` | `NetworkEngine::getRemoteAddress()` | Address handling abstracted |

### Network Functions → GANL Components

| Current Function | GANL Replacement | Notes |
|------------------|------------------|-------|
| `shovechars()` | `NetworkEngine::processEvents()` | Main event loop |
| `process_output()` | `ConnectionBase` + `processOutgoingData()` | Output handling |
| `shutdownsock()` | `ConnectionBase::close()` | Connection closing |
| `queue_write()` | `ConnectionBase::sendDataToClient()` | Output queuing |
| `welcome_user()` | `SessionManager` hooks | Login sequences |
| `process_input()` | `ProtocolHandler::processInput()` | Input processing |
| `do_command()` | `SessionManager::onDataReceived()` | Command dispatching |
| `raw_notify()` | `SessionManager::sendToSession()` | Notification path |
| `check_idle()` | `SessionManager` timeout handling | Session maintenance |

## SSL Integration

GANL provides a cleaner abstraction for SSL/TLS through the `SecureTransport` interface:

1. **Certificate configuration:**
   - Use existing TinyMUX certificate paths
   - Add configuration options for GANL-specific TLS settings

2. **State machine improvement:**
   - Replace current state enum (`SocketState`) with GANL's more comprehensive state tracking
   - Proper handling of TLS renegotiation

3. **Buffer management:**
   - Use `IoBuffer` for efficient memory management
   - Properly handle partial reads/writes in TLS

## Challenges and Solutions

1. **Backward compatibility**
   - Maintain same interface for game logic during transition
   - Keep same configuration file format but extend with new options

2. **Platform differences**
   - Abstract platform-specific code through `NetworkEngine` implementations
   - Create conditional compilation for Windows/Unix specific optimizations

3. **Performance concerns**
   - Profile key operations during transition
   - Tune buffer sizes based on typical MUD traffic patterns
   - Optimize critical paths in connection handling

4. **State transition complexity**
   - Map existing state models to new state machine
   - Ensure clean handling of in-progress connections during server restart

## Testing Strategy

1. **Unit Tests**
   - Test individual GANL components in isolation
   - Create mocks for TinyMUX dependencies

2. **Integration Tests**
   - Test full connection lifecycle
   - Test SSL handshaking and renegotiation
   - Test telnet option negotiation

3. **Load Testing**
   - Create client simulator to test under load
   - Measure resource utilization under stress

4. **Security Testing**
   - Verify proper certificate validation
   - Test handling of malformed packets
   - Test TLS version negotiation

## Timeline

1. **Phase 1: Foundation** - 1-2 months
2. **Phase 2: Side-by-Side Implementation** - 2-3 months
3. **Phase 3: Integration and Testing** - 2-3 months
4. **Phase 4: Complete Transition** - 1-2 months

**Total estimated time:** 6-10 months

## Resources Required

1. **Development**
   - C++ developers familiar with networking and event-driven programming
   - Testing on multiple platforms (Windows, Linux, BSD)

2. **Testing**
   - Test environment with multiple client types
   - Load testing infrastructure

3. **Documentation**
   - Time to update user and developer documentation
   - Configuration examples for various deployment scenarios

## Conclusion

The integration of GANL into TinyMUX represents a significant architectural improvement, modernizing the networking stack with a cleaner, more maintainable, and more efficient design. The layered approach will make future extensions easier and improve the overall stability of the system. While the transition will require significant effort, the long-term benefits in terms of code quality, feature support, and performance will justify the investment.