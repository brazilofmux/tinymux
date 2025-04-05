#ifndef GANL_NETWORK_ENGINE_H
#define GANL_NETWORK_ENGINE_H

#include "network_types.h"
#include "io_buffer.h"
#include <string>

namespace ganl {

class NetworkEngine {
public:
    virtual ~NetworkEngine() = default;

    /**
     * @brief Get the I/O model used by this engine.
     * @return The IoModel enum value (Readiness or Completion).
     */
    virtual IoModel getIoModelType() const = 0;

    /**
     * Initialize the network engine
     *
     * @return true on success, false on failure
     */
    virtual bool initialize() = 0;

    /**
     * Shut down the network engine
     */
    virtual void shutdown() = 0;

    /**
     * Create a listener for accepting connections
     *
     * @param host Host address to bind to (empty for any)
     * @param port Port to listen on
     * @param error Error code output variable
     * @return Listener handle on success, InvalidListenerHandle on failure
     */
    virtual ListenerHandle createListener(const std::string& host, uint16_t port, ErrorCode& error) = 0;

    /**
     * Start listening for connections
     *
     * @param listener Listener handle
     * @param listenerContext Context pointer for the listener
     * @param error Error code output variable
     * @return true on success, false on failure
     */
    virtual bool startListening(ListenerHandle listener, void* listenerContext, ErrorCode& error) = 0;

    /**
     * Close a listener
     *
     * @param listener Listener handle
     */
    virtual void closeListener(ListenerHandle listener) = 0;

    /**
     * Associate a context pointer with a connection
     *
     * @param conn Connection handle
     * @param context Context pointer (usually a Connection object)
     * @param error Error code output variable
     * @return true on success, false on failure
     */
    virtual bool associateContext(ConnectionHandle conn, void* context, ErrorCode& error) = 0;

    /**
     * Close a connection
     *
     * @param conn Connection handle
     */
    virtual void closeConnection(ConnectionHandle conn) = 0;

    /**
     * Post an asynchronous read operation using an IoBuffer
     *
     * @param conn Connection handle
     * @param buffer Reference to an IoBuffer to read into
     * @param error Error code output variable
     * @return true if operation posted, false on immediate error
     */
    virtual bool postRead(ConnectionHandle conn, IoBuffer& buffer, ErrorCode& error) = 0;

    /**
     * Post an asynchronous write operation with application context
     *
     * @param conn Connection handle
     * @param data Data to write
     * @param length Length of data in bytes
     * @param userContext Application-specific context pointer
     * @param error Error code output variable
     * @return true if operation posted, false on immediate error
     *
     * Note: For readiness-based I/O models (select, epoll, kqueue), this function
     * registers write interest for the connection. The connection's handleWrite
     * callback will be called when the socket is ready for writing. The network
     * engine will automatically unregister write interest after the handleWrite
     * callback if the connection has no more data to write.
     *
     * The userContext pointer will be included in the resulting IoEvent when the write
     * operation completes, allowing applications to correlate write completions with
     * their initiating requests.
     */
    virtual bool postWrite(ConnectionHandle conn, const char* data, size_t length,
                         void* userContext, ErrorCode& error) = 0;

    /**
     * Post an asynchronous write operation (no context)
     *
     * @param conn Connection handle
     * @param data Data to write
     * @param length Length of data in bytes
     * @param error Error code output variable
     * @return true if operation posted, false on immediate error
     *
     * This is a backward compatibility method that calls the contextual version
     * with a nullptr context.
     */
    virtual bool postWrite(ConnectionHandle conn, const char* data, size_t length, ErrorCode& error) {
        return postWrite(conn, data, length, nullptr, error);
    }

    /**
     * Process pending network events
     *
     * @param timeoutMs Timeout in milliseconds (-1 for indefinite)
     * @param events Array to fill with events
     * @param maxEvents Maximum number of events to retrieve
     * @return Number of events retrieved, 0 on timeout, -1 on error
     */
    virtual int processEvents(int timeoutMs, IoEvent* events, int maxEvents) = 0;

    /**
     * Get remote address for a connection
     *
     * @param conn Connection handle
     * @return Remote address string (IP:port)
     */
    virtual std::string getRemoteAddress(ConnectionHandle conn) = 0;

    /**
     * Get remote network address for a connection
     *
     * @param conn Connection handle
     * @return NetworkAddress object containing the raw socket address
     */
    virtual NetworkAddress getRemoteNetworkAddress(ConnectionHandle conn) = 0;

    /**
     * Get string representation of an error code
     *
     * @param error Error code
     * @return Error string
     */
    virtual std::string getErrorString(ErrorCode error) = 0;
};

} // namespace ganl

#endif // GANL_NETWORK_ENGINE_H
