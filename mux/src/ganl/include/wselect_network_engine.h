#pragma once // Use pragma once for Windows convention
#ifndef GANL_WSELECT_NETWORK_ENGINE_H
#define GANL_WSELECT_NETWORK_ENGINE_H

#include "network_engine.h"

// Ensure Winsock headers are included correctly
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h> // Included after winsock2.h
#pragma comment(lib, "Ws2_32.lib")

#include <map>
#include <vector>
#include <string>
#include <cstdint>
#include <memory> // For unique_ptr if needed elsewhere, not strictly here
#include <mutex> // For potential thread safety

// Define FD_SETSIZE if not defined, though winsock2.h should define it
// Note: To increase this value, define FD_SETSIZE before including winsock2.h
// typically in a project-wide setting, not here (where it's too late)
#ifndef FD_SETSIZE
#define FD_SETSIZE 64
#warning "FD_SETSIZE was not defined by winsock2.h, using default 64."
#endif

// IMPORTANT: The WSelectNetworkEngine is limited by FD_SETSIZE (typically 64 on Windows)
// and cannot handle more simultaneous connections than this value.
// For higher scalability on Windows, use IOCPNetworkEngine instead.

namespace ganl {

    /**
     * WSelectNetworkEngine - Network engine implementation using Windows select() API
     *
     * Implements the NetworkEngine interface using Winsock and select().
     * Note: This engine is limited by FD_SETSIZE (typically 64 sockets).
     * For higher scalability on Windows, IOCPNetworkEngine should be used.
     * Assumes single-threaded usage or external locking for modifications.
     */
    class WSelectNetworkEngine : public NetworkEngine {
    public:
        WSelectNetworkEngine();
        ~WSelectNetworkEngine() override;

        IoModel getIoModelType() const override;

        // --- NetworkEngine Interface ---
        bool initialize() override;
        void shutdown() override;

        ListenerHandle createListener(const std::string& host, uint16_t port, ErrorCode& error) override;
        bool startListening(ListenerHandle listener, void* listenerContext, ErrorCode& error) override;
        void closeListener(ListenerHandle listener) override;

        bool associateContext(ConnectionHandle conn, void* context, ErrorCode& error) override;
        void closeConnection(ConnectionHandle conn) override;

        bool postRead(ConnectionHandle conn, char* buffer, size_t length, ErrorCode& error) override;
        bool postWrite(ConnectionHandle conn, const char* data, size_t length, ErrorCode& error) override;

        int processEvents(int timeoutMs, IoEvent* events, int maxEvents) override;

        std::string getRemoteAddress(ConnectionHandle conn) override;
        NetworkAddress getRemoteNetworkAddress(ConnectionHandle conn) override;
        std::string getErrorString(ErrorCode error) override;

    private:
        // Use SOCKET type for Windows
        using SocketFD = SOCKET;
        const SocketFD INVALID_SOCKET_FD = INVALID_SOCKET;

        // Internal socket types
        enum class SocketType { Listener, Connection };

        // Information about a socket
        struct SocketInfo {
            SocketType type;
            void* context{ nullptr };
            bool wantRead{ false };
            bool wantWrite{ false };
            // Removed readBuffer/readBufferSize
        };

        // Listener-specific information
        struct ListenerInfo {
            void* context{ nullptr };
        };

        // Socket tracking maps
        std::map<SocketFD, SocketInfo> sockets_;
        std::map<SocketFD, ListenerInfo> listeners_;

        // Use standard fd_set, acknowledging FD_SETSIZE limit
        fd_set masterReadFds_;
        fd_set masterWriteFds_;
        fd_set masterErrorFds_;

        // Keep track of currently monitored sockets to iterate after select()
        // Using std::vector is simpler than custom SocketSet for standard FD_SETSIZE limit
        std::vector<SocketFD> monitoredSockets_;

        bool initialized_{ false };
        bool wsaInitialized_{ false };

        // Helper methods
        void closeSocket(SocketFD sock);
        ErrorCode getLastError();
        bool setNonBlocking(SocketFD sock, ErrorCode& error);
        ConnectionHandle acceptConnection(ListenerHandle listener, ErrorCode& error);
        void addSocketInternal(SocketFD sock, SocketInfo info); // Add to maps and sets
        void removeSocketInternal(SocketFD sock); // Remove from maps and sets
        ErrorCode translateError(int wsaError); // Map WSA errors to errno style
    };

} // namespace ganl

#endif // GANL_WSELECT_NETWORK_ENGINE_H
