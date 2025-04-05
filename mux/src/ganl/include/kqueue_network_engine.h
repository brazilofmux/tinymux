#ifndef GANL_KQUEUE_NETWORK_ENGINE_H
#define GANL_KQUEUE_NETWORK_ENGINE_H

#include "network_engine.h"
#include <map>
#include <vector>
#include <mutex> // Added for thread safety on maps
#include <sys/event.h>
#include <cstdint> // For flags

namespace ganl {

class KqueueNetworkEngine : public NetworkEngine {
public:
    KqueueNetworkEngine();
    ~KqueueNetworkEngine() override;

    // --- NetworkEngine Interface ---
    IoModel getIoModelType() const override {
        return IoModel::Readiness;
    }

    bool initialize() override;
    void shutdown() override;

    ListenerHandle createListener(const std::string& host, uint16_t port, ErrorCode& error) override;
    bool startListening(ListenerHandle listener, void* listenerContext, ErrorCode& error) override;
    void closeListener(ListenerHandle listener) override;

    bool associateContext(ConnectionHandle conn, void* context, ErrorCode& error) override;
    void closeConnection(ConnectionHandle conn) override;

    // postRead ensures EVFILT_READ is enabled
    bool postRead(ConnectionHandle conn, char* buffer, size_t length, ErrorCode& error) override;
    // postWrite enables EVFILT_WRITE interest
    bool postWrite(ConnectionHandle conn, const char* data, size_t length, ErrorCode& error) override;

    int processEvents(int timeoutMs, IoEvent* events, int maxEvents) override;

    std::string getRemoteAddress(ConnectionHandle conn) override;
    NetworkAddress getRemoteNetworkAddress(ConnectionHandle conn) override;
    std::string getErrorString(ErrorCode error) override;

private:
    // Internal state to track socket type and user context
    enum class SocketType { Listener, Connection };
    struct SocketInfo {
        SocketType type;
        void* context{nullptr}; // Connection* or listenerContext
        // Kqueue doesn't require storing registered events as explicitly as epoll
        // The state is managed directly via kevent calls.
    };

    int kqueueFd_{-1};
    std::mutex mutex_; // Protect maps
    // Use FD as key for internal maps
    std::map<int, SocketInfo> sockets_;       // Tracks all sockets (listeners and connections)
    std::vector<struct kevent> keventResults_; // Buffer for kevent results

    // Helper methods for socket operations
    bool setNonBlocking(int fd, ErrorCode& error);
    ConnectionHandle acceptConnection(ListenerHandle listener, ErrorCode& error);
    // Helper to register/modify kqueue events
    bool updateKevent(int fd, short filter, u_short flags, ErrorCode& error);
};

} // namespace ganl

#endif // GANL_KQUEUE_NETWORK_ENGINE_H
