#ifndef GANL_CONNECTION_H
#define GANL_CONNECTION_H

#include "network_types.h"
#include "io_buffer.h"
#include <memory>
#include <string>
#include <cstring>

namespace ganl {

// Forward declarations for dependencies
class NetworkEngine;
class SecureTransport;
class ProtocolHandler;
class SessionManager;

/**
 * Connection - Central class managing a single client connection
 *
 * This class orchestrates the flow of data between network, TLS, protocol,
 * and session layers. It implements the state machine for a connection lifecycle.
 */
class ConnectionBase : public std::enable_shared_from_this<ConnectionBase> {
public:
    /**
     * Constructor
     *
     * @param handle Network connection handle
     * @param engine Network engine instance
     * @param secureTransport Secure transport instance (optional)
     * @param protocolHandler Protocol handler instance
     * @param sessionManager Session manager instance
     */
    ConnectionBase(ConnectionHandle handle, NetworkEngine& engine,
               SecureTransport* secureTransport,
               ProtocolHandler& protocolHandler,
               SessionManager& sessionManager);

    /**
     * Destructor
     */
    virtual ~ConnectionBase() = 0;

    // Delete copy operations
    ConnectionBase(const ConnectionBase&) = delete;
    ConnectionBase& operator=(const ConnectionBase&) = delete;

    /**
     * Initialize the connection
     *
     * @param useTls Whether to use TLS for this connection
     * @return true on success, false on failure
     */
    bool initialize(bool useTls = true);

    /**
     * Handle a network event for this connection
     *
     * @param event Network event to handle
     */
    void handleNetworkEvent(const IoEvent& event);

    /**
     * Send data to the client
     *
     * @param data Data to send
     */
    void sendDataToClient(const std::string& data);

    /**
     * Close the connection
     *
     * @param reason Reason for disconnection
     */
    void close(DisconnectReason reason);

    /**
     * Get connection handle
     *
     * @return Connection handle
     */
    ConnectionHandle getHandle() const { return handle_; }

    /**
     * Get session ID
     *
     * @return Session ID
     */
    SessionId getSessionId() const { return sessionId_; }

    /**
     * Get connection state
     *
     * @return Current connection state
     */
    ConnectionState getState() const { return state_; }

    /**
     * Get remote address
     *
     * @return Remote address string
     */
    std::string getRemoteAddress() const;

private:
    // Event handlers
    virtual void handleRead(size_t bytesTransferred) = 0;
    virtual void handleWrite(size_t bytesTransferred) = 0;
    void handleClose();
    void handleError(ErrorCode error);

    // State machine transitions
    void startTlsHandshake();
    bool continueTlsHandshake();
    void startTelnetNegotiation();
    bool processApplicationData();
    void transitionToState(ConnectionState newState);

    // Member variables
    SessionId sessionId_{InvalidSessionId};

    // Buffer management
    IoBuffer applicationInput_;  // After telnet processing
    IoBuffer applicationOutput_; // Application data to be sent
    IoBuffer formattedOutput_;   // After telnet/formatting

    // References to shared services
    SecureTransport* secureTransport_;
    ProtocolHandler& protocolHandler_;
    SessionManager& sessionManager_;

    // Private helpers
    void cleanupResources(DisconnectReason reason);

protected:
    // Network operations - needed by derived classes to implement I/O model specific behavior
    bool postRead();
    bool postWrite();

    // These buffer access methods are used by derived classes
    IoBuffer& getEncryptedInputBuffer() { return encryptedInput_; }
    IoBuffer& getDecryptedInputBuffer() { return decryptedInput_; }
    IoBuffer& getEncryptedOutputBuffer() { return encryptedOutput_; }

    // Data processing methods used by derived classes
    bool processSecureData();
    bool processProtocolData();

    // Core properties needed by derived classes
    ConnectionHandle handle_;
    NetworkEngine& networkEngine_;

    // Flags needed by specific I/O models
    bool& pendingReadFlag() { return pendingRead_; }
    bool& pendingWriteFlag() { return pendingWrite_; }

    // State access for derived classes
    bool isClosingOrClosed() const {
        return state_ == ConnectionState::Closing || state_ == ConnectionState::Closed;
    }
    bool isTlsEnabled() const { return useTls_; }

private:
    // Buffer management
    IoBuffer encryptedInput_;    // Raw data from network
    IoBuffer decryptedInput_;    // After TLS decryption
    IoBuffer encryptedOutput_;   // After TLS encryption

    // State management
    ConnectionState state_{ ConnectionState::Initializing };
    DisconnectReason disconnectReason_{ DisconnectReason::Unknown };

    // Cleanup state tracking
    bool resourcesCleanedUp_{ false };  // Track if resources have been cleaned up
    bool socketClosed_{ false };        // Track if socket has been closed

    // Configuration flags
    bool useTls_{ false };
    bool pendingRead_{ false };
    bool pendingWrite_{ false };
};

/**
 * ReadinessConnection - Connection implementation for Readiness I/O Models
 */
class ReadinessConnection : public ConnectionBase
{
public:
    ReadinessConnection(ConnectionHandle handle, NetworkEngine& engine,
        SecureTransport* secureTransport,
        ProtocolHandler& protocolHandler,
        SessionManager& sessionManager);

    ~ReadinessConnection() override;

    // Delete copy operations (optional, base class already deletes them)
    ReadinessConnection(const ReadinessConnection&) = delete;
    ReadinessConnection& operator=(const ReadinessConnection&) = delete;

    // Implement virtual methods specific to readiness
    void handleRead(size_t bytesTransferred) override;
    void handleWrite(size_t bytesTransferred) override;

private:
    // Any members specific only to readiness model connections (if any)
};

/**
 * CompletionConnection - Connection implementation for Completion I/O Models (IOCP)
 */
class CompletionConnection : public ConnectionBase {
public:
    CompletionConnection(ConnectionHandle handle, NetworkEngine& engine,
        SecureTransport* secureTransport,
        ProtocolHandler& protocolHandler,
        SessionManager& sessionManager);

    ~CompletionConnection() override; // Mark as override

    // Delete copy operations (optional, base class already deletes them)
    CompletionConnection(const CompletionConnection&) = delete;
    CompletionConnection& operator=(const CompletionConnection&) = delete;

    // Implement virtual methods specific to completion
    void handleRead(size_t bytesTransferred) override;
    void handleWrite(size_t bytesTransferred) override;

private:
    // Any members specific only to completion model connections (if any)
};

/**
 * Factory for creating Connection objects
 */
class ConnectionFactory {
public:
    /**
     * Create a new Connection
     *
     * @param handle Connection handle
     * @param engine Network engine
     * @param secureTransport Secure transport (optional)
     * @param protocolHandler Protocol handler
     * @param sessionManager Session manager
     * @return Shared pointer to new Connection
     */
    static std::shared_ptr<ConnectionBase> createConnection(
        ConnectionHandle handle,
        NetworkEngine& engine,
        SecureTransport* secureTransport,
        ProtocolHandler& protocolHandler,
        SessionManager& sessionManager);
};

} // namespace ganl

#endif // GANL_CONNECTION_H
