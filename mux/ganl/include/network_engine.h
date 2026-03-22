#ifndef GANL_NETWORK_ENGINE_H
#define GANL_NETWORK_ENGINE_H

#include <network_types.h>
#include <io_buffer.h>
#include <string>
#include <vector>
#include <cerrno>

namespace ganl {

struct ListenerOptions {
    int backlog{-1};           // Use engine default (-1 maps to SOMAXCONN)
    bool reuseAddress{true};
    bool reusePort{false};
    bool dualStack{true};
};

struct SlaveSpawnOptions {
    std::string executable;
    std::vector<std::string> arguments;
    std::vector<std::string> environment;
    int communicationFd{3};
    bool attachToStandardIO{false};
    void* connectionContext{nullptr};
    int* childPidOut{nullptr};
};

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
    virtual ListenerHandle createListener(const std::string& host, uint16_t port, const ListenerOptions& options, ErrorCode& error) {
        return createListener(host, port, error);
    }

    /**
     * Adopt an already-created, bound, and listening socket fd into the engine.
     * Used during @restart to reclaim listener fds that survived exec.
     *
     * @param fd Native descriptor to adopt (ownership is transferred on success)
     * @param error Error code output variable (set on failure)
     * @return Listener handle on success, InvalidListenerHandle on failure
     */
    virtual ListenerHandle adoptListener(int fd, ErrorCode& error) {
        (void)fd;
        error = ENOTSUP;
        return InvalidListenerHandle;
    }

    /**
     * Adopt an already-created connection file descriptor into the engine.
     *
     * @param fd Native descriptor to monitor (ownership is transferred on success)
     * @param connectionContext Context pointer associated with the connection
     * @param error Error code output variable (set on failure)
     * @return Connection handle on success, InvalidConnectionHandle otherwise
     */
    virtual ConnectionHandle adoptConnection(int fd, void* connectionContext, ErrorCode& error) {
        (void)fd;
        (void)connectionContext;
        error = ENOTSUP;
        return InvalidConnectionHandle;
    }

    /**
     * Spawn a child process that communicates over a managed connection (Unix only).
     */
    virtual ConnectionHandle spawnSlave(const SlaveSpawnOptions& options, ErrorCode& error) {
        (void)options;
        error = ENOTSUP;
        return InvalidConnectionHandle;
    }

    /**
     * Initiate an asynchronous outbound TCP connection.
     *
     * Creates a non-blocking socket, resolves the host, initiates connect(),
     * and registers the fd with the engine.  The caller receives a
     * ConnectionHandle immediately.  When the connect completes, processEvents()
     * will emit an IoEvent with type ConnectSuccess or ConnectFail.
     *
     * @param host Hostname or IP address to connect to
     * @param port TCP port
     * @param connectionContext Context pointer for the connection
     * @param error Error code output variable
     * @return Connection handle on success, InvalidConnectionHandle on failure
     */
    virtual ConnectionHandle initiateConnect(const std::string& host, uint16_t port,
                                             void* connectionContext, ErrorCode& error) {
        (void)host; (void)port; (void)connectionContext;
        error = ENOTSUP;
        return InvalidConnectionHandle;
    }

    /**
     * Initiate an asynchronous outbound Unix domain socket connection.
     *
     * @param path Path to the Unix domain socket
     * @param connectionContext Context pointer for the connection
     * @param error Error code output variable
     * @return Connection handle on success, InvalidConnectionHandle on failure
     */
    virtual ConnectionHandle initiateUnixConnect(const std::string& path,
                                                 void* connectionContext, ErrorCode& error) {
        (void)path; (void)connectionContext;
        error = ENOTSUP;
        return InvalidConnectionHandle;
    }

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
     * Detach a connection from the engine without closing the file descriptor.
     * The socket is deregistered from the I/O multiplexer and removed from
     * internal tracking, but shutdown() and close() are NOT called.
     * This is used during @restart to release fds that will survive exec.
     *
     * @param conn Connection handle
     */
    virtual void detachConnection(ConnectionHandle conn) { (void)conn; }

    /**
     * Detach a listener from the engine without closing the file descriptor.
     * The socket is deregistered from the I/O multiplexer and removed from
     * internal tracking, but close() is NOT called.
     * This is used during @restart to release fds that will survive exec.
     *
     * @param listener Listener handle
     */
    virtual void detachListener(ListenerHandle listener) { (void)listener; }

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
