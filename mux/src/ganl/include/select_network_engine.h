#ifndef GANL_SELECT_NETWORK_ENGINE_H
#define GANL_SELECT_NETWORK_ENGINE_H

#include "network_engine.h"
// Unix-like includes
#include <sys/select.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h> // For sockaddr_in
#include <arpa/inet.h> // For inet_ntop
#include <unistd.h> // For close()
#include <fcntl.h> // For fcntl()

#include <map>
#include <vector>
#include <string>
#include <cstdint>
#include <mutex> // For potential thread safety, though select itself isn't thread-safe for modifications

namespace ganl {

// Forward declaration if needed
// class Connection; // Assuming Connection uses this engine

/**
 * SelectNetworkEngine - Network engine implementation using traditional select() API
 *
 * This implementation provides a portable non-blocking I/O mechanism available on
 * many Unix systems and Windows. It uses readiness notification. The caller (Connection)
 * is responsible for performing actual read/write operations when notified.
 * Note: Modifying the interest sets while select() is blocking in another thread is unsafe.
 * This implementation assumes single-threaded usage or external locking for modifications.
 */
class SelectNetworkEngine : public NetworkEngine {
public:
    SelectNetworkEngine();
    ~SelectNetworkEngine() override;

    IoModel getIoModelType() const override;

    // --- NetworkEngine Interface ---
    bool initialize() override;
    void shutdown() override;

    ListenerHandle createListener(const std::string& host, uint16_t port, ErrorCode& error) override;
    bool startListening(ListenerHandle listener, void* listenerContext, ErrorCode& error) override;
    void closeListener(ListenerHandle listener) override;

    bool associateContext(ConnectionHandle conn, void* context, ErrorCode& error) override;
    void closeConnection(ConnectionHandle conn) override;

    // postRead: For select, this typically just ensures read interest is registered.
    // The actual read buffer is managed by the Connection class.
    bool postRead(ConnectionHandle conn, char* buffer, size_t length, ErrorCode& error) override;

    // postWrite: Registers write interest (wantWrite=true).
    // The Connection object is responsible for performing the actual write
    // when notified via a Write event.
    bool postWrite(ConnectionHandle conn, const char* data, size_t length, ErrorCode& error) override;

    // processEvents: Calls select() and generates events based on FD readiness.
    // For Read/Write events, bytesTransferred will be 0 (signifying readiness).
    int processEvents(int timeoutMs, IoEvent* events, int maxEvents) override;

    std::string getRemoteAddress(ConnectionHandle conn) override;
    std::string getErrorString(ErrorCode error) override;

private:
    // Define SocketFD based on platform
    using SocketFD = int;
    const SocketFD INVALID_SOCKET_FD = -1;

    // Internal socket types
    enum class SocketType { Listener, Connection };

    // Information about a socket (listener or connection)
    struct SocketInfo {
        SocketType type;
        void* context{nullptr};       // Connection* or listener context pointer
        bool wantRead{false};         // Interest in read readiness (set by default for connections/listeners)
        bool wantWrite{false};        // Interest in write readiness (set by postWrite)
        // Removed readBuffer and readBufferSize - managed by Connection
    };

    // Listener-specific information
    struct ListenerInfo {
        void* context{nullptr};       // User-provided listener context
    };
    std::mutex mutex_;

    // Data structures for tracking sockets
    std::map<SocketFD, SocketInfo> sockets_;                // All sockets by FD
    std::map<SocketFD, ListenerInfo> listeners_;            // Listener-specific info by FD

    // FD sets for select()
    fd_set masterReadFds_;            // Master set of FDs interested in reading
    fd_set masterWriteFds_;           // Master set of FDs interested in writing
    fd_set masterErrorFds_;           // Master set of FDs to check for errors (usually same as read/write)

    SocketFD maxFd_{-1};              // Highest FD for select() + 1

    bool initialized_{false};         // Whether initialization is complete

    // Platform-specific socket close
    void closeSocket(SocketFD fd);

    // Platform-specific error code retrieval
    ErrorCode getLastError();

    // Helper methods
    bool setNonBlocking(SocketFD fd, ErrorCode& error);
    ConnectionHandle acceptConnection(ListenerHandle listener, ErrorCode& error);
    void updateFdSets(SocketFD fd, const SocketInfo& info); // Add/remove FD from master sets
    void removeFd(SocketFD fd); // Remove FD from maps and master sets
    void updateMaxFd();
};

} // namespace ganl

#endif // GANL_SELECT_NETWORK_ENGINE_H
