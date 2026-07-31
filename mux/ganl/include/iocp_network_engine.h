#ifndef GANL_IOCP_NETWORK_ENGINE_H
#define GANL_IOCP_NETWORK_ENGINE_H

#include <network_engine.h>
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
    enum class OpType { Read, Write, Accept, Connect } opType;
    ConnectionHandle connection; // For Read/Write/Connect ops
    WSABUF wsaBuf;
    char* buffer;
    size_t bufferSize;
    IocpNetworkEngine* engine;
    IoBuffer* ioBuffer{nullptr}; // Reference to IoBuffer for memory-managed operations
    void* userContext{nullptr};  // User-provided context for Write operations
    // Owned storage for async WSASend (#796) and WSARecv (#1832).  WSABUF must
    // not point into ConnectionBase buffers that may be destroyed when
    // closeConnection cancels the op before the completion packet drains.
    std::vector<char> ownedBuffer;
    // Generation of the sockets_ map entry at post time (#1832 ABA).  Stale
    // completions whose SOCKET value was reused are discarded when this does
    // not match the live entry.  Accept ops leave this 0 (listener-keyed).
    uint64_t generation{0};

    // --- Accept specific fields ---
    SOCKET acceptSocket;
    char acceptBuffer[2 * (sizeof(SOCKADDR_STORAGE) + 16)];
    ListenerHandle listenerHandle;

    // Constructors for legacy char* buffer operations
    PerIoData(OpType type, ConnectionHandle conn, char* buf, size_t size, IocpNetworkEngine* eng);
    // Constructor for IoBuffer-based Read operations
    PerIoData(OpType type, ConnectionHandle conn, IoBuffer& buffer, IocpNetworkEngine* eng);
    // Constructor for Write operations with user context
    PerIoData(OpType type, ConnectionHandle conn, char* buf, size_t size, void* context, IocpNetworkEngine* eng);
    // Constructor for Accept operations
    PerIoData(ListenerHandle listener, IocpNetworkEngine* eng, int addressFamily);
    // Constructor for ConnectEx operations (#1801)
    explicit PerIoData(ConnectionHandle conn, IocpNetworkEngine* eng);

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
    ListenerHandle createListener(const std::string& host, uint16_t port, const ListenerOptions& options, ErrorCode& error) override;
    bool startListening(ListenerHandle listener, void* listenerContext, ErrorCode& error) override;
    void closeListener(ListenerHandle listener) override;

    bool associateContext(ConnectionHandle conn, void* context, ErrorCode& error) override;
    void closeConnection(ConnectionHandle conn) override;

    ConnectionHandle initiateConnect(const std::string& host, uint16_t port,
                                     void* connectionContext, ErrorCode& error) override;

    bool postRead(ConnectionHandle conn, IoBuffer& buffer, ErrorCode& error) override;
    bool postWrite(ConnectionHandle conn, const char* data, size_t length, void* userContext, ErrorCode& error) override;
    // The non-contextual version is inherited from the base class

    int processEvents(int timeoutMs, IoEvent* events, int maxEvents) override;

    std::string getRemoteAddress(ConnectionHandle conn) override;
    NetworkAddress getRemoteNetworkAddress(ConnectionHandle conn) override;
    std::string getErrorString(ErrorCode error) override;

    // Test hook (#1830): next N postAccept attempts fail before any socket work.
    // Production code never calls this.
    void setTestFailNextPostAccepts(int n) { failNextPostAccepts_ = n; }
    int getPendingAcceptsForTest(ListenerHandle listener);

private:
    // Internal socket type enum
    enum class SocketType { Listener, Connection, OutboundConnecting };

    // Structure to store socket information
    struct SocketInfo {
        SocketType type;
        void* context;    // Connection* or listener context
        bool pendingRead; // Tracks if a read operation is pending
        IoBuffer* activeReadBuffer{nullptr}; // Tracks the active IoBuffer for reads
        std::string remoteAddress; // Stores the remote address string
        // Monotonic identity for this map entry.  SOCKET values are reused
        // after closesocket; completions stamped with an older generation
        // must not touch the replacement connection (#1832).
        uint64_t generation{0};
    };

    // Structure to store listener information
    struct ListenerInfo {
        void* context{nullptr};    // User-provided listener context
        bool isListening{false}; // Whether this listener is active
        int pendingAccepts{0}; // Number of pending AcceptEx operations
        int targetAccepts{2};  // Keep a small pool outstanding (#1830)
        int backlog{SOMAXCONN};       // Backlog used during listen
        ListenerOptions options{}; // Original listener options
        int addressFamily{AF_INET}; // AF_INET / AF_INET6
        // When replenish fails and pendingAccepts drops below target (esp. 0),
        // schedule retries in processEvents with bounded backoff (#1830).
        bool acceptOutage{false};
        ULONGLONG nextAcceptRetryMs{0};
        ErrorCode lastAcceptPostError{0};
    };

    // Helper methods
    IoModel getIoModelType() const override;
    bool initializeWinsock(ErrorCode& error);
    void cleanupWinsock();
    bool createIoCompletionPort(ErrorCode& error);
    SOCKET createSocket(ErrorCode& error);
    bool setSocketOptions(SOCKET socket, int addressFamily, const ListenerOptions& options, ErrorCode& error, bool isListener);
    bool associateWithIocp(HANDLE handle, void* completionKey, ErrorCode& error);
    bool postAccept(ListenerHandle listener, ErrorCode& error);
    // Post AcceptEx until pendingAccepts >= targetAccepts (or fail).
    bool ensureAcceptsPosted(ListenerHandle listener, ErrorCode& error);
    // Retry listeners that are active but short of outstanding accepts.
    int recoverStarvedListeners(IoEvent* events, int maxEvents, int eventCount);
    void noteAcceptReplenishFailure(ListenerHandle listener, ErrorCode error);
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
    uint64_t nextGeneration_{1}; // Allocated under mutex_ for SocketInfo.generation

    // Function pointers for Microsoft-specific extensions
    LPFN_ACCEPTEX lpfnAcceptEx_;
    LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs_;
    LPFN_CONNECTEX lpfnConnectEx_;

    // Test-only: fail next N postAccept calls before real work (#1830).
    int failNextPostAccepts_{0};
};

} // namespace ganl

#endif // GANL_IOCP_NETWORK_ENGINE_H
