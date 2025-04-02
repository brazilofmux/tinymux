#ifndef GANL_IOCP_NETWORK_ENGINE_H
#define GANL_IOCP_NETWORK_ENGINE_H

#include "network_engine.h"
#include <map>
#include <vector>
#include <mutex>
#include <string>
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <ws2tcpip.h>

namespace ganl {

// Forward declarations
class IocpNetworkEngine;

// Structure to track overlapped I/O operations
struct PerIoData {
    OVERLAPPED overlapped;
    enum class OpType { Read, Write, Accept } opType;
    ConnectionHandle connection; // For Read/Write ops
    WSABUF wsaBuf;
    char* buffer;
    size_t bufferSize;
    IocpNetworkEngine* engine;

    // --- Accept specific fields ---
    SOCKET acceptSocket;
    char acceptBuffer[2 * (sizeof(SOCKADDR_IN) + 16)];
    ListenerHandle listenerHandle;

    // Constructors
    PerIoData(OpType type, ConnectionHandle conn, char* buf, size_t size, IocpNetworkEngine* eng);
    PerIoData(ListenerHandle listener, IocpNetworkEngine* eng); // Keep signature the same

    ~PerIoData();
};

class IocpNetworkEngine : public NetworkEngine {
public:
    IocpNetworkEngine();
    ~IocpNetworkEngine() override;

    // NetworkEngine Interface
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
    std::string getErrorString(ErrorCode error) override;

private:
    // Internal socket type enum
    enum class SocketType { Listener, Connection };

    // Structure to store socket information
    struct SocketInfo {
        SocketType type;
        void* context;    // Connection* or listener context
        bool pendingRead; // Tracks if a read operation is pending
        std::string remoteAddress; // Cached remote address (for getRemoteAddress)
    };

    // Structure to store listener information
    struct ListenerInfo {
        void* context;    // User-provided listener context
        bool isListening; // Whether this listener is active
        int pendingAccepts; // Number of pending accept operations
    };

    // Helper methods
    IoModel getIoModelType() const override;
    bool initializeWinsock(ErrorCode& error);
    void cleanupWinsock();
    bool createIoCompletionPort(ErrorCode& error);
    SOCKET createSocket(ErrorCode& error);
    bool setSocketOptions(SOCKET socket, ErrorCode& error);
    bool associateWithIocp(HANDLE handle, void* completionKey, ErrorCode& error);
    bool postAccept(ListenerHandle listener, ErrorCode& error);
    ConnectionHandle handleAcceptCompletion(PerIoData* perIoData, ErrorCode& error);
    void cancelIoOperations(SOCKET socket);
    bool postWSARecv(ConnectionHandle conn, PerIoData* perIoData, ErrorCode& error);
    bool postWSASend(ConnectionHandle conn, PerIoData* perIoData, ErrorCode& error);

    // Member variables
    HANDLE iocp_;   // I/O Completion Port handle
    bool wsaInitialized_; // Flag indicating if WSA initialization was successful
    bool initialized_;    // Flag indicating if engine initialization was successful

    std::mutex mutex_; // Protects access to maps
    std::map<SOCKET, SocketInfo> sockets_; // Map of all socket information
    std::map<SOCKET, ListenerInfo> listeners_; // Map of listener-specific information

    // Function pointers for Microsoft-specific extensions
    LPFN_ACCEPTEX lpfnAcceptEx_;
    LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs_;
};

} // namespace ganl

#endif // GANL_IOCP_NETWORK_ENGINE_H
