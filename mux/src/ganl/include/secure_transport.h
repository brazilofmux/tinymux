#ifndef GANL_SECURE_TRANSPORT_H
#define GANL_SECURE_TRANSPORT_H

#include "network_types.h"
#include "io_buffer.h"
#include <string>

namespace ganl {

/**
 * SecureTransport - Interface for TLS/SSL operations
 */
class SecureTransport {
public:
    virtual ~SecureTransport() = default;

    /**
     * Initialize the secure transport
     *
     * @param config TLS configuration
     * @return true on success, false on failure
     */
    virtual bool initialize(const TlsConfig& config) = 0;

    /**
     * Shut down the secure transport
     */
    virtual void shutdown() = 0;

    /**
     * Create a session context for a connection
     *
     * @param conn Connection handle
     * @param isServer Whether this is a server-side connection
     * @return true on success, false on failure
     */
    virtual bool createSessionContext(ConnectionHandle conn, bool isServer = true) = 0;

    /**
     * Destroy the session context for a connection
     *
     * @param conn Connection handle
     */
    virtual void destroySessionContext(ConnectionHandle conn) = 0;

    /**
     * Process incoming encrypted data
     *
     * @param conn Connection handle
     * @param encrypted_in Buffer containing encrypted data
     * @param decrypted_out Buffer to receive decrypted data
     * @param encrypted_out Buffer to receive encrypted handshake data (if needed)
     * @param consumeInput Whether to consume processed data from the input buffer
     * @return Result code
     */
    virtual TlsResult processIncoming(ConnectionHandle conn, IoBuffer& encrypted_in,
                                     IoBuffer& decrypted_out, IoBuffer& encrypted_out,
                                     bool consumeInput = true) = 0;

    /**
     * Process outgoing plain data
     *
     * @param conn Connection handle
     * @param plain_in Buffer containing plain data
     * @param encrypted_out Buffer to receive encrypted data
     * @param consumeInput Whether to consume processed data from the input buffer
     * @return Result code
     */
    virtual TlsResult processOutgoing(ConnectionHandle conn, IoBuffer& plain_in,
                                     IoBuffer& encrypted_out, bool consumeInput = true) = 0;

    /**
     * Shutdown a TLS session
     *
     * @param conn Connection handle
     * @param encrypted_out Buffer to receive encrypted shutdown data
     * @return Result code
     */
    virtual TlsResult shutdownSession(ConnectionHandle conn, IoBuffer& encrypted_out) = 0;

    /**
     * Check if TLS session is established
     *
     * @param conn Connection handle
     * @return true if established, false otherwise
     */
    virtual bool isEstablished(ConnectionHandle conn) = 0;

    /**
     * Check if TLS layer expects more network read data
     *
     * @param conn Connection handle
     * @return true if more network data is needed, false otherwise
     */
    virtual bool needsNetworkRead(ConnectionHandle conn) = 0;

    /**
     * Check if TLS layer has data to write to the network
     *
     * @param conn Connection handle
     * @return true if there is data to write, false otherwise
     */
    virtual bool needsNetworkWrite(ConnectionHandle conn) = 0;

    /**
     * Get last TLS error string
     *
     * @param conn Connection handle
     * @return Error string
     */
    virtual std::string getLastTlsErrorString(ConnectionHandle conn) = 0;
};

} // namespace ganl

#endif // GANL_SECURE_TRANSPORT_H
