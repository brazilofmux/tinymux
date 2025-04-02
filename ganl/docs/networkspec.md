# MUX Networking Layer Modernization: Architectural Specification (v1.4)

**Version:** 1.4 (Enhanced with detailed documentation for LLM consumption)

**Changelog from v1.3:**
-   Added detailed comments to `IoBuffer` methods to clarify their purpose
-   Enhanced `ConnectionBase` hierarchy documentation with detailed implementation notes
-   Expanded and reorganized core components documentation with bullet points for clarity
-   Added descriptions for all key abstractions and types to improve understanding
-   Included comprehensive migration plan with detailed phases
-   Added concrete timeline with estimated durations for each phase
-   Enhanced task assignments with detailed responsibilities
-   Added code examples with comments for key interface methods
-   Improved overall document structure for better LLM comprehension

## 1. Project Overview

This document specifies the architecture for modernizing the MUD networking layer within the `ganl` (Great American Networking Layer) namespace.
It replaces the traditional, monolithic `DESC` structure and global state management found in `bsd.cpp`/`netcommon.cpp` with a layered, event-driven design.
The architecture supports modern I/O mechanisms (IOCP, epoll, kqueue) and secure transport (OpenSSL, SChannel), facilitating platform agnosticism, testability, and maintainability.

### 1.1 Goals

-   Replace the monolithic `DESC` structure with a layered approach.
-   Create a platform-agnostic networking API using modern C++.
-   Support efficient, non-blocking, event-driven I/O models (IOCP, epoll, kqueue).
-   Implement robust SSL/TLS handling (OpenSSL, SChannel) with correct state and buffer management.
-   Handle Telnet protocol negotiation (options, subnegotiation) correctly.
-   Support multiple character encodings (UTF-8, Latin-1, CP437, ASCII) with conversion.
-   Support ANSI color, MXP, and potentially other MUD-specific formatting.
-   Establish clear separation and well-defined interfaces between Network I/O, Secure Transport, Protocol Handling, Session Management, and Application Logic.
-   Enable easier unit testing, integration testing, and performance benchmarking.
-   Facilitate parallel development by humans and LLMs.
-   Maintain backward compatibility during a phased transition.

## 2. Architectural Overview

The architecture is composed of distinct layers, orchestrated by a `ConnectionBase`-derived object for each client connection.
The specific behavior, especially concerning I/O posting and handling, depends on the `IoModel` (Readiness or Completion) reported by the `NetworkEngine`.

```
+-----------------------------------------------------------------+
|                     MUD Game Application Layer                  |
|                     (Command Interpreter, Game Logic)           |
+----------------------------------^------------------------------+
                                   | processCommand
+----------------------------------|------------------------------+
|                       Command Processor                         |
|                       (Parses/Validates Commands)               |
+----------------------------------^------------------------------+
                                   | handleCommand
+----------------------------------|------------------------------+
|                       Session Manager                           |
|  (Manages Sessions, Authentication, Player State, Stats)        |
+----------------------------------^------------------------------+
                                   | onDataReceived / sendToSession
+----------------------------------|------------------------------+      +-----------------------+
|                       Protocol Handler                          |----->|   ConnectionBase      |
|  (Telnet, Negotiation, Encoding, Formatting)                    |<-----|   (State, Buffers,    |
+----------------------------------^------------------------------+      |    Orchestration)     |
                                   | processInput / formatOutput   |      +----------^----------+
+----------------------------------|------------------------------+                 | handleNetworkEvent
|                    Secure Transport Layer (Optional)            |<----------------+ handleRead/Write
|                    (SSL/TLS Abstraction)                        |                 | (via derived class)
+----------------------------------^------------------------------+                 |
                                   | processIncoming / processOutgoing              |
+----------------------------------|------------------------------+                 |
|                    Network I/O Engine                           |-----------------+
|                    (IOCP, epoll, kqueue - reports IoModel)      | processEvents, postRead/Write
+-----------------------------------------------------------------+
```

### 2.1 Core Components & Data Flow

1.  **Network I/O Engine:** Platform-specific event loop (IOCP, epoll, kqueue).
    * Notifies the `ConnectionBase` object (via its `context` pointer) of I/O readiness/completion by queuing `IoEvent`s.
    * Reads raw bytes into the `ConnectionBase`'s `encryptedInput_` buffer.
    * Writes raw bytes from the `ConnectionBase`'s `encryptedOutput_` buffer.
    * Reports its I/O model type (Readiness or Completion) via `getIoModelType()`.

2.  **`ConnectionBase` (Abstract):** Represents the core logic for a single client connection. **Crucially replaces the old `DESC` struct.**
    * Owns the `ConnectionHandle`.
    * Owns I/O buffers (`encryptedInput_`, `decryptedInput_`, `applicationInput_`, `applicationOutput_`, `formattedOutput_`, `encryptedOutput_`) using the `IoBuffer` abstraction.
    * Holds connection-specific state (`ConnectionState`).
    * Orchestrates data flow between layers upon receiving `IoEvent`s from the `NetworkEngine`.
    * Drives the optional `SecureTransport` layer for encryption/decryption.
    * Passes decrypted data (`decryptedInput_`) to the `ProtocolHandler`.
    * Receives application data from the `SessionManager` (via `sendDataToClient`), processes it via `ProtocolHandler`, passes it to `SecureTransport` (if enabled), and posts write requests to `NetworkEngine`.
    * Defines the core state machine but delegates specific I/O handling (`handleRead`, `handleWrite`) to derived classes.
    * Controls buffer consumption with `consumeInput` flags for various processing steps.

3.  **`ReadinessConnection` / `CompletionConnection` (Concrete):**
    * Derived from `ConnectionBase`.
    * Implement the `handleRead` and `handleWrite` virtual methods specific to the underlying I/O model.
    * `ReadinessConnection`: For epoll/kqueue-based systems where I/O operations report readiness.
    * `CompletionConnection`: For IOCP-based systems where I/O operations report completion.

4.  **`ConnectionFactory`:**
    * Responsible for creating the correct `ConnectionBase` derived class (`ReadinessConnection` or `CompletionConnection`) based on the provided `NetworkEngine` type or configuration.
    * Uses `NetworkEngine::getIoModelType()` to determine which class to instantiate.

5.  **Secure Transport Layer:**
    * Handles SSL/TLS operations (OpenSSL/SChannel).
    * *Optional* per connection (determined during `ConnectionBase::initialize`).
    * Operates on the `ConnectionBase` buffers.
    * Manages TLS state machine per connection.
    * Provides `processIncoming` and `processOutgoing` with `consumeInput` flag.

6.  **Protocol Handler:**
    * Parses Telnet commands from `decryptedInput_`.
    * Handles character encoding translation.
    * Assembles complete lines/commands into `applicationInput_`.
    * Formats outgoing application text (from `applicationOutput_`) into `formattedOutput_`.
    * Manages Telnet protocol state.
    * Reports negotiation status via `getNegotiationStatus()`.
    * Provides `processInput` and `formatOutput` with `consumeInput` flag.

7.  **Session Manager:**
    * Manages logical player sessions.
    * Handles authentication.
    * Maps `SessionId` to player state and `ConnectionHandle`.
    * Receives complete commands/lines from `applicationInput_` (via `ConnectionBase` triggering `processApplicationData`) and passes them to the `CommandProcessor`.
    * Receives messages destined for players and calls `ConnectionBase::sendDataToClient`.
    * Provides session statistics via `getSessionStats()`.
    * Uses `int` for player IDs.

8.  **Command Processor:**
    * Receives validated command lines from the `SessionManager`.
    * Parses the commands.
    * Invokes the appropriate MUD game logic/functions.

9.  **MUD Game Application Layer:**
    * The existing MUD core logic.
    * Communicates with players via the Session Manager.

### 2.2 Key Abstractions & Types

*   **`ganl::ConnectionHandle`:** Platform-agnostic identifier for a network connection (socket). Typically `uintptr_t`.
*   **`ganl::SessionId`:** Platform-agnostic identifier for a logical player session. Typically `uint64_t`. `InvalidSessionId` (0) indicates no session.
*   **`ganl::IoBuffer`:** A class for managing byte buffers, handling dynamic resizing, read/write positions, locking, and efficient data manipulation. Core functionality for all I/O operations.
*   **Handles:**
    *   `ganl::ConnectionHandle` (`uintptr_t`) - Represents a network socket
    *   `ganl::ListenerHandle` (`uintptr_t`) - Represents a listening socket
    *   `ganl::SessionId` (`uint64_t`) - Identifies a player session
*   **Invalid Handle Constants:**
    *   `ganl::InvalidConnectionHandle` (0) - Used to indicate invalid connection
    *   `ganl::InvalidListenerHandle` (0) - Used to indicate invalid listener
    *   `ganl::InvalidSessionId` (0) - Used to indicate invalid session
*   **Core Types:**
    *   `ganl::ErrorCode` (`int`) - System-specific error codes
*   **Enums:**
    *   `ganl::IoEventType`: None, Accept, ConnectSuccess, ConnectFail, Read, Write, Close, Error - Network event types
    *   `ganl::TlsResult`: Success, WantRead, WantWrite, Error, Closed - TLS operation results
    *   `ganl::SessionState`: Connecting, Handshaking, Authenticating, Connected, Closing, Closed - Session lifecycle states
    *   `ganl::ConnectionState`: Initializing, TlsHandshaking, TelnetNegotiating, Running, Closing, Closed - Connection lifecycle states
    *   `ganl::DisconnectReason`: Unknown, UserQuit, Timeout, NetworkError, ProtocolError, TlsError, ServerShutdown, AdminKick, GameFull, LoginFailed - Why connections terminate
    *   `ganl::EncodingType`: ASCII, Latin1, UTF8, CP437, CP1252 - Character encodings supported
    *   `ganl::IoModel`: Unknown, Readiness, Completion - I/O notification model types
    *   `ganl::NegotiationStatus`: InProgress, Completed, Failed - Telnet negotiation status (Used by `ProtocolHandler`)
*   **Structs:**
    *   `ganl::IoEvent`: { connection, listener, type, bytesTransferred, error, context } - Network event data
    *   `ganl::TlsConfig`: { certificateFile, keyFile, password, verifyPeer } - TLS configuration parameters
    *   `ganl::ProtocolState`: { telnetBinary, telnetEcho, telnetSGA, telnetEOR, encoding, supportsANSI, supportsMXP, width, height } - Protocol negotiation state
    *   `ganl::SessionStats`: { bytesReceived, bytesSent, commandsProcessed, connectedAt, lastActivity } - Session performance metrics

## 3. Component Specifications

*(Namespace `ganl` implied)*

### 3.0 `IoBuffer` Abstraction (Helper Class)

*   **Purpose:** Manage byte streams safely and efficiently between layers.
*   **Header:** `ganl/io_buffer.h`
*   **Interface:**
    ```cpp
    class IoBuffer {
    public:
        explicit IoBuffer(size_t initialCapacity = 4096);
        ~IoBuffer();
        IoBuffer(const IoBuffer&) = delete;
        IoBuffer& operator=(const IoBuffer&) = delete;
        IoBuffer(IoBuffer&&) noexcept;
        IoBuffer& operator=(IoBuffer&&) noexcept;

        // Buffer access
        char* writePtr();               // Get pointer for writing
        const char* readPtr() const;    // Get pointer for reading
        size_t readableBytes() const;   // Available bytes to read
        size_t writableBytes() const;   // Available space to write
        size_t capacity() const;        // Total buffer capacity
        bool empty() const;             // Check if buffer is empty

        // Buffer operations
        void commitWrite(size_t bytesWritten);  // Update write position
        void consumeRead(size_t bytesRead);     // Update read position
        void ensureWritable(size_t required);   // Expand buffer if needed
        void compact();                         // Move unread data to start
        void clear();                           // Reset buffer positions
        void append(const void* data, size_t len); // Add data to buffer
        std::string consumeReadAllAsString();   // Get and consume all data

        // Reuse control
        void lockForReuse(bool lock);
        bool isLockedForReuse() const;
    private: // ... implementation details ...
    };

    // Helper functions
    namespace utils {
        // Dump buffer contents as hex for debugging
        std::string dumpIoBufferHexString(const IoBuffer& buffer, size_t maxBytes = 64);
        void dumpIoBufferHex(const IoBuffer& buffer, size_t maxBytes = 64);
    }
    ```

### 3.1 Connection Layer (Orchestrator)

*   **Purpose:** Manages state and data flow for a single connection using `ConnectionBase` and derived classes (`ReadinessConnection`, `CompletionConnection`).
*   **Header:** `ganl/connection.h`

#### 3.1.1 `ConnectionBase` (Abstract Base Class)

*   **Inheritance:** `public std::enable_shared_from_this<ConnectionBase>`
*   **Responsibilities:** Owns handle, buffers, state; orchestrates layers; drives state machine; provides common methods. Must now check `ProtocolHandler::getNegotiationStatus` before transitioning from `TelnetNegotiating` to `Running`. Must correctly pass `consumeInput` flags to `SecureTransport` and `ProtocolHandler`.
*   **Interface:**
    ```cpp
    class ConnectionBase : public std::enable_shared_from_this<ConnectionBase> {
    public:
        ConnectionBase(ConnectionHandle handle, NetworkEngine& engine,
                       SecureTransport* secureTransport, ProtocolHandler& protocolHandler,
                       SessionManager& sessionManager);
        virtual ~ConnectionBase() = 0; // Abstract
        ConnectionBase(const ConnectionBase&) = delete;
        ConnectionBase& operator=(const ConnectionBase&) = delete;

        bool initialize(bool useTls = true);
        void handleNetworkEvent(const IoEvent& event);
        void sendDataToClient(const std::string& data);
        void close(DisconnectReason reason);
        ConnectionHandle getHandle() const;
        SessionId getSessionId() const;
        ConnectionState getState() const;
        std::string getRemoteAddress() const;

    protected: // For derived classes
        virtual void handleRead(size_t bytesTransferred) = 0; // Implemented by derived
        virtual void handleWrite(size_t bytesTransferred) = 0;// Implemented by derived
        bool postRead();
        bool postWrite();
        IoBuffer& getEncryptedInputBuffer();
        IoBuffer& getDecryptedInputBuffer();
        IoBuffer& getEncryptedOutputBuffer();
        bool processSecureData();
        bool processProtocolData();
        bool isClosingOrClosed() const;
        bool isTlsEnabled() const;
        bool& pendingReadFlag();
        bool& pendingWriteFlag();
        ConnectionHandle handle_;
        NetworkEngine& networkEngine_;

    private: // Internal logic & members
        // Event handlers, state transitions, buffer members, service refs...
        // (As detailed in v1.2, matches connection.h structure)
        void handleClose();
        void handleError(ErrorCode error);
        void startTlsHandshake();
        bool continueTlsHandshake();
        void startTelnetNegotiation();
        bool processApplicationData();
        void transitionToState(ConnectionState newState);
        void cleanupResources(DisconnectReason reason);
        // ... Buffers (encryptedInput_, decryptedInput_, etc.) ...
        // ... State (sessionId_, state_, useTls_, etc.) ...
        // ... Service pointers/refs (secureTransport_, etc.) ...
    };
    ```

#### 3.1.2 `ReadinessConnection` / `CompletionConnection` (Concrete Classes)

*   **Inheritance:** `public ConnectionBase`
*   **Purpose:** Implement I/O model specific `handleRead`/`handleWrite`.
*   **Header:** `ganl/connection.h`
*   **Interface:**
    ```cpp
    class ReadinessConnection : public ConnectionBase {
    public:
        ReadinessConnection(ConnectionHandle handle, NetworkEngine& engine,
                           SecureTransport* secureTransport, ProtocolHandler& protocolHandler,
                           SessionManager& sessionManager);
        ~ReadinessConnection() override;

    protected:
        // Implement for readiness-based I/O (epoll, kqueue, select)
        void handleRead(size_t bytesTransferred) override;
        void handleWrite(size_t bytesTransferred) override;
    };

    class CompletionConnection : public ConnectionBase {
    public:
        CompletionConnection(ConnectionHandle handle, NetworkEngine& engine,
                            SecureTransport* secureTransport, ProtocolHandler& protocolHandler,
                            SessionManager& sessionManager);
        ~CompletionConnection() override;

    protected:
        // Implement for completion-based I/O (IOCP)
        void handleRead(size_t bytesTransferred) override;
        void handleWrite(size_t bytesTransferred) override;
    };
    ```

#### 3.1.3 `ReadinessConnection` (Concrete Class)

*   **Inheritance:** `public ConnectionBase`
*   **Purpose:** Implements connection logic for readiness-based I/O models (epoll, kqueue, select).
*   **Implementation:** Overrides `handleRead` and `handleWrite`.
    *   `handleRead(bytesTransferred)`: Called when a socket is ready for reading. Commits `bytesTransferred` to `encryptedInput_`, calls `processSecureData` / `processProtocolData`. If more data may be available, continues reading until EWOULDBLOCK or buffer full.
    *   `handleWrite(bytesTransferred)`: Called when a socket is ready for writing. Consumes `bytesTransferred` from `encryptedOutput_`. If more data exists in `encryptedOutput_`, continues writing until buffer empty or EWOULDBLOCK, then registers write interest as needed.
*   **Constructor:** Forwards arguments to `ConnectionBase`.

#### 3.1.4 `CompletionConnection` (Concrete Class)

*   **Inheritance:** `public ConnectionBase`
*   **Purpose:** Implements connection logic for completion-based I/O models (IOCP).
*   **Implementation:** Overrides `handleRead` and `handleWrite`.
    *   `handleRead(bytesTransferred)`: Called when an overlapped `WSARecv`/`ReadFile` completes. Commits `bytesTransferred` to `encryptedInput_`, calls `processSecureData` / `processProtocolData`. Posts a *new* overlapped `postRead()` if not closing.
    *   `handleWrite(bytesTransferred)`: Called when an overlapped `WSASend`/`WriteFile` completes. Consumes `bytesTransferred` from `encryptedOutput_`. If more data exists in `encryptedOutput_`, posts a *new* overlapped `postWrite()`.
*   **Constructor:** Forwards arguments to `ConnectionBase`.

#### 3.1.5 `ConnectionFactory`

*   **Purpose:** Creates `std::shared_ptr<ConnectionBase>`, selecting derived type based on `NetworkEngine::getIoModelType()`.
*   **Header:** `ganl/connection.h`
*   **Interface:**
    ```cpp
    class ConnectionFactory {
    public:
        static std::shared_ptr<ConnectionBase> createConnection(
            ConnectionHandle handle, NetworkEngine& engine,
            SecureTransport* secureTransport, ProtocolHandler& protocolHandler,
            SessionManager& sessionManager);
    };
    ```

### 3.2 Network I/O Engine (`NetworkEngine`)

*   **Purpose:** Platform-specific event loop and async I/O.
*   **Header:** `ganl/network_engine.h`
*   **Interface:** *(Updated to match `network_engine.h`)*
    ```cpp
    class NetworkEngine {
    public:
        virtual ~NetworkEngine() = default;

        // NEW: Identify underlying I/O strategy
        virtual IoModel getIoModelType() const = 0;

        virtual bool initialize() = 0;
        virtual void shutdown() = 0;

        // Listener Management
        virtual ListenerHandle createListener(const std::string& host, uint16_t port, ErrorCode& error) = 0;
        virtual bool startListening(ListenerHandle listener, void* listenerContext, ErrorCode& error) = 0;
        virtual void closeListener(ListenerHandle listener) = 0;

        // Connection Management
        // Associates ConnectionBase* (passed as void*) with handle for event dispatch
        virtual bool associateContext(ConnectionHandle conn, void* context, ErrorCode& error) = 0;
        virtual void closeConnection(ConnectionHandle conn) = 0;

        // I/O Operations (Asynchronous)
        virtual bool postRead(ConnectionHandle conn, char* buffer, size_t length, ErrorCode& error) = 0;
        // Note: For Readiness models, postWrite registers interest. handleWrite is called on readiness.
        // Engine manages unregistering write interest automatically after handleWrite if connection
        // indicates no more data to write (mechanism TBD, e.g., return value from handleWrite or flag).
        virtual bool postWrite(ConnectionHandle conn, const char* data, size_t length, ErrorCode& error) = 0;

        // Event Processing
        virtual int processEvents(int timeoutMs, IoEvent* events, int maxEvents) = 0;

        // Utility
        virtual std::string getRemoteAddress(ConnectionHandle conn) = 0;
        virtual std::string getErrorString(ErrorCode error) = 0;
    };
    ```

### 3.3 Secure Transport Layer (`SecureTransport`)

*   **Purpose:** Abstract TLS operations (OpenSSL/SChannel). Optional.
*   **Header:** `ganl/secure_transport.h`
*   **Interface:** *(Updated to match `secure_transport.h`)*
    ```cpp
    class SecureTransport {
    public:
        virtual ~SecureTransport() = default;

        virtual bool initialize(const TlsConfig& config) = 0;
        virtual void shutdown() = 0;

        // Per-Connection Lifecycle
        virtual bool createSessionContext(ConnectionHandle conn, bool isServer = true) = 0;
        virtual void destroySessionContext(ConnectionHandle conn) = 0;

        // TLS Processing (Operates on ConnectionBase buffers)
        // NEW: consumeInput flag allows ConnectionBase to control buffer consumption
        virtual TlsResult processIncoming(ConnectionHandle conn, IoBuffer& encrypted_in,
                                         IoBuffer& decrypted_out, IoBuffer& encrypted_out,
                                         bool consumeInput = true) = 0;
        virtual TlsResult processOutgoing(ConnectionHandle conn, IoBuffer& plain_in,
                                         IoBuffer& encrypted_out,
                                         bool consumeInput = true) = 0;
        virtual TlsResult shutdownSession(ConnectionHandle conn, IoBuffer& encrypted_out) = 0;

        // State & Utility
        virtual bool isEstablished(ConnectionHandle conn) = 0;
        virtual bool needsNetworkRead(ConnectionHandle conn) = 0;
        virtual bool needsNetworkWrite(ConnectionHandle conn) = 0;
        virtual std::string getLastTlsErrorString(ConnectionHandle conn) = 0;
    };
    ```

### 3.4 Protocol Handler (`ProtocolHandler`)

*   **Purpose:** Handle Telnet negotiation, encoding, formatting, line assembly.
*   **Header:** `ganl/protocol_handler.h`
*   **Interface:** *(Updated to match `protocol_handler.h`)*
    ```cpp
    class ProtocolHandler {
    public:
        virtual ~ProtocolHandler() = default;

        // Per-Connection Lifecycle
        virtual bool createProtocolContext(ConnectionHandle conn) = 0;
        virtual void destroyProtocolContext(ConnectionHandle conn) = 0;

        // Telnet & Data Processing (Called by ConnectionBase)
        virtual void startNegotiation(ConnectionHandle conn, IoBuffer& telnet_responses_out) = 0;
        // NEW: consumeInput flag allows ConnectionBase control
        virtual bool processInput(ConnectionHandle conn, IoBuffer& decrypted_in,
                                 IoBuffer& app_data_out, IoBuffer& telnet_responses_out,
                                 bool consumeInput = true) = 0;
        virtual bool formatOutput(ConnectionHandle conn, IoBuffer& app_data_in,
                                 IoBuffer& formatted_out,
                                 bool consumeInput = true) = 0;

        // NEW: Check status of initial Telnet negotiation phase
        virtual NegotiationStatus getNegotiationStatus(ConnectionHandle conn) = 0;

        // Configuration & State
        virtual bool setEncoding(ConnectionHandle conn, EncodingType encoding) = 0;
        virtual EncodingType getEncoding(ConnectionHandle conn) = 0;
        virtual ProtocolState getProtocolState(ConnectionHandle conn) = 0;
        virtual void updateWidth(ConnectionHandle conn, uint16_t width) = 0;
        virtual void updateHeight(ConnectionHandle conn, uint16_t height) = 0;

        // Utility
        virtual std::string getLastProtocolErrorString(ConnectionHandle conn) = 0;
    };
    ```

### 3.5 Session Manager (`SessionManager`)

*   **Purpose:** Manage user sessions, authentication, state, stats, access control.
*   **Header:** `ganl/session_manager.h`
*   **Interface:** *(Updated to match `session_manager.h`)*
    ```cpp
    class SessionManager {
    public:
        virtual ~SessionManager() = default;

        virtual bool initialize() = 0;
        virtual void shutdown() = 0;

        // Session Lifecycle (Triggered by ConnectionBase)
        virtual SessionId onConnectionOpen(ConnectionHandle conn, const std::string& remoteAddress) = 0;
        virtual void onDataReceived(SessionId sessionId, const std::string& commandLine) = 0;
        virtual void onConnectionClose(SessionId sessionId, DisconnectReason reason) = 0;

        // Application/Admin Actions
        virtual bool sendToSession(SessionId sessionId, const std::string& message) = 0;
        virtual bool broadcastMessage(const std::string& message, SessionId except = InvalidSessionId) = 0;
        virtual bool disconnectSession(SessionId sessionId, DisconnectReason reason) = 0;

        // Authentication
        virtual bool authenticateSession(SessionId sessionId, ConnectionHandle conn,
                                        const std::string& username, const std::string& password) = 0;
        // Player ID is int
        virtual void onAuthenticationSuccess(SessionId sessionId, int playerId) = 0;
        // Player ID is int
        virtual int getPlayerId(SessionId sessionId) = 0;

        // State & Info
        virtual SessionState getSessionState(SessionId sessionId) = 0;
        // NEW: Get session statistics
        virtual SessionStats getSessionStats(SessionId sessionId) = 0;
        virtual ConnectionHandle getConnectionHandle(SessionId sessionId) = 0;

        // Access Control (Integrates with existing MUX logic)
        virtual bool isAddressAllowed(const std::string& address) = 0;
        virtual bool isAddressRegistered(const std::string& address) = 0;
        virtual bool isAddressForbidden(const std::string& address) = 0;
        virtual bool isAddressSuspect(const std::string& address) = 0;

        // Utility
        virtual std::string getLastSessionErrorString(SessionId sessionId) = 0;
    };
    ```

### 3.6 Command Processor (`CommandProcessor`)

*   **Purpose:** Bridge to MUD command interpreter.
*   **Header:** `ganl/command_processor.h` *(Assumed, not provided, interface kept from v1.2)*
*   **Interface:**
    ```cpp
    class CommandProcessor {
    public:
        virtual ~CommandProcessor() = default;
        virtual bool initialize() = 0;
        virtual void shutdown() = 0;
        virtual void processCommand(SessionId sessionId, int /*dbref/playerId*/, const std::string& commandLine) = 0;
        virtual void processUnauthenticatedCommand(SessionId sessionId, ConnectionHandle conn, const std::string& commandLine) = 0;
    };
    ```
    *Note: `playerId` type should match `SessionManager::getPlayerId()` return type (`int`)*.

## 4. Implementation Strategy

*(Adjusted for the ConnectionBase hierarchy)*

1.  **Phase 1: Core Framework & Abstractions**
    *   Implement/Refine base types (`network_types.h`).
    *   Implement/Test `IoBuffer`.
    *   Implement base interfaces (`NetworkEngine`, `SecureTransport`, `ProtocolHandler`, `SessionManager`, `CommandProcessor`).
    *   Implement `ConnectionBase`, `ConnectionFactory`, derived `Connection` classes.
    *   Implement platform detection & basic factory for `NetworkEngine`.
    *   Implement `NetworkEngine::getIoModelType()`.
    *   Build test harness & mocks.
2.  **Phase 2: Basic I/O Implementation**
    *   Implement `NetworkEngine` for one platform (e.g., epoll).
    *   Implement `ReadinessConnection` (`handleRead`/`handleWrite`).
    *   Refine `ConnectionFactory` to create `ReadinessConnection`.
    *   Implement `ProtocolHandler`, including `startNegotiation`, `processInput`/`formatOutput` (handling `consumeInput`), and `getNegotiationStatus`.
    *   Implement basic `SessionManager` (session creation/tracking).
    *   Implement basic `CommandProcessor` (echo command).
    *   Test: Simple echo server functionality using the readiness path.
3.  **Phase 3: Alternate I/O & Core Features**
    *   Implement `NetworkEngine` backends (IOCP, epoll, etc.).
    *   Implement `CompletionConnection` (`handleRead`/`handleWrite`).
    *   Refine `ConnectionFactory` to create `CompletionConnection`.
    *   Implement `SecureTransport` (e.g., OpenSSL or SChannel). Integrate TLS handshake (`startTlsHandshake`, `continueTlsHandshake`, `processSecureData`) into `ConnectionBase`. Update `initialize` and state transitions, handling `consumeInput`.
    *   Implement `ProtocolHandler` Telnet negotiation basics.
    *   Implement `SessionManager` authentication and state transitions. Integrate site access checks.
    *   Implement `SessionManager` authentication and state transitions, including `getSessionStats` and mapping `playerId` as `int`.
    *   Integrate site access checks.
    *   Implement `CommandProcessor` hooks for login/create commands.
    *   Test basic connectivity, TLS, Telnet negotiation, login.
    *   Test: Secure connection.
4.  **Phase 4: Advanced Protocol & Game Integration**
5.  **Phase 5: Integration & Migration**
6.  **Phase 6: Optimization & Cleanup**

## 5. Testing Strategy

*   **Unit Tests:** `IoBuffer`, `ProtocolHandler` state/parsing (including `getNegotiationStatus`), `SessionManager` logic (including stats).
*   **`ConnectionBase` Logic Tests:** Test state transitions (especially Telnet->Running using mock `getNegotiationStatus`), buffer flow logic (testing `consumeInput=true/false` paths).
*   **I/O Model Tests:** Test `Readiness/CompletionConnection`, mock `NetworkEngine`.
*   **Integration Tests:** Test data flow, TLS, Telnet flows.
*   **Platform Tests:** IOCP, epoll, kqueue behavior.
*   **Performance Benchmarks**.

## 6. Migration Plan

The transition from the old networking code to the GANL architecture will happen in progressive phases:

1. **Parallel Implementation:**
   - Develop the GANL stack independently from the existing MUD networking code
   - Create comprehensive tests to validate functionality
   - Build tools like echo_server and stress_client for testing

2. **Adapter Layer:**
   - Create adapter interfaces between old and new systems
   - The `CommandProcessor` will initially forward commands to the old command parser
   - Redirect existing `raw_notify` calls to the new `SessionManager`
   - Implement monitoring to compare performance and reliability between systems

3. **Progressive Replacement:**
   - Begin with new connections using GANL while existing connections remain on old system
   - Add feature flags to control which system handles new connections
   - Monitor for issues in production environment

4. **Feature Parity Verification:**
   - Ensure all protocol features work identically in both systems
   - Validate telnet negotiation, encoding, formatting behave as expected
   - Verify all session management features work properly

5. **Complete Migration:**
   - Switch all new connections to GANL
   - Allow existing connections to naturally close on old system
   - Remove deprecated code when safe

## 7. Task Assignments

*   **Core Framework (Human Lead / LLM):**
    * Types in `network_types.h`
    * `IoBuffer` implementation
    * Base interface definitions
    * `ConnectionBase` and derived classes
    * `ConnectionFactory` implementation

*   **Windows I/O & TLS (LLM/Dev):**
    * `IOCPNetworkEngine` implementation
    * `CompletionConnection` implementation
    * `SChannelTransport` implementation
    * Windows-specific utilities

*   **Unix I/O & TLS (LLM/Dev):**
    * `Epoll/KqueueNetworkEngine` implementations
    * `ReadinessConnection` implementation
    * `OpenSSLTransport` implementation
    * Unix-specific utilities

*   **Protocol Layer (LLM/Dev):**
    * `ProtocolHandler` implementation
    * Telnet protocol support
    * Character encoding handling
    * Text formatting (ANSI, MXP)

*   **Session & Command Layer (Human Dev):**
    * `SessionManager` implementation
    * `CommandProcessor` implementation
    * Authentication system
    * MUD core integration

*   **Testing (All/Dedicated):**
    * Unit tests for each component
    * Integration tests for data flow
    * Performance benchmarks
    * Platform-specific testing

## 8. Timeline and Milestones

### Phase 1: Core Framework & Abstractions (1-2 months)
- Define all types and interfaces
- Implement `IoBuffer`
- Implement `ConnectionBase` hierarchy
- Create basic `NetworkEngine` factory
- Develop test framework

### Phase 2: Basic I/O Implementation (1-2 months)
- Implement epoll-based `NetworkEngine`
- Implement `ReadinessConnection`
- Create basic `ProtocolHandler`
- Develop echo server sample application
- Implement initial testing

### Phase 3: Alternate I/O & Core Features (2-3 months)
- Implement IOCP and kqueue engines
- Implement `CompletionConnection`
- Add SSL/TLS support
- Implement Telnet negotiation
- Expand test suite

### Phase 4: Advanced Protocol & Game Integration (2-3 months)
- Implement full `SessionManager`
- Develop `CommandProcessor` integration
- Add encoding support
- Add ANSI/MXP formatting
- Complete integration tests

### Phase 5: Integration & Migration (1-2 months)
- Create adapters to existing MUD code
- Perform parallel testing
- Monitor performance metrics
- Begin phased rollout

### Phase 6: Optimization & Cleanup (1 month)
- Performance tuning
- Memory usage optimization
- Code cleanup
- Documentation updates
- Final release
