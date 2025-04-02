#ifndef GANL_EPOLL_NETWORK_ENGINE_H
#define GANL_EPOLL_NETWORK_ENGINE_H

#include "network_engine.h"
#include <map>
#include <vector>
#include <sys/epoll.h>
#include <cstdint> // For uint32_t
#include <mutex>

namespace ganl {

class EpollNetworkEngine : public NetworkEngine {
public:
    EpollNetworkEngine();
    ~EpollNetworkEngine() override;

    IoModel getIoModelType() const override {
        return IoModel::Readiness;
    }

    // --- NetworkEngine Interface ---
    bool initialize() override;
    void shutdown() override;

    ListenerHandle createListener(const std::string& host, uint16_t port, ErrorCode& error) override;
    bool startListening(ListenerHandle listener, void* listenerContext, ErrorCode& error) override;
    void closeListener(ListenerHandle listener) override;

    bool associateContext(ConnectionHandle conn, void* context, ErrorCode& error) override;
    void closeConnection(ConnectionHandle conn) override;

    // postRead is mostly a no-op for epoll since connections are always registered for read interest
    bool postRead(ConnectionHandle conn, char* buffer, size_t length, ErrorCode& error) override;
    // postWrite enables EPOLLOUT interest to indicate the connection wants to write
    // Write interest is automatically disabled in processEvents after handleWrite is called
    bool postWrite(ConnectionHandle conn, const char* data, size_t length, ErrorCode& error) override;

    int processEvents(int timeoutMs, IoEvent* events, int maxEvents) override;

    std::string getRemoteAddress(ConnectionHandle conn) override;
    std::string getErrorString(ErrorCode error) override;

private:
    // Internal state to track socket type and registered events
    enum class SocketType { Listener, Connection };
    struct SocketInfo {
        SocketType type;
        void* context{nullptr}; // Connection* or listenerContext
        uint32_t events{0};     // Currently registered epoll events (EPOLLIN, EPOLLOUT, etc.)
    };

    struct ListenerInfo {
        void* context{nullptr}; // User-provided listener context
    };
    std::mutex mutex_;

    int epollFd_{-1};
    // Use FD as key for internal maps
    std::map<int, SocketInfo> sockets_;       // Tracks all sockets (listeners and connections)
    std::map<int, ListenerInfo> listeners_;   // Specific info for listeners (redundant context?)
    std::vector<epoll_event> epollEvents_;    // Buffer for epoll_wait results

    // Helper methods for socket operations
    bool setNonBlocking(int fd, ErrorCode& error);
    ConnectionHandle acceptConnection(ListenerHandle listener, ErrorCode& error);
    bool modifyEpollFlags(int fd, uint32_t newEvents, ErrorCode& error);
};

} // namespace ganl

#endif // GANL_EPOLL_NETWORK_ENGINE_H
