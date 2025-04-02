#include "iocp_network_engine.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <memory>
#include <cassert>
#include <locale>
#include <codecvt>

// Need to link with Ws2_32.lib
#pragma comment(lib, "ws2_32.lib")
// Need to link with Mswsock.lib for AcceptEx and related functions
#pragma comment(lib, "mswsock.lib")

// Define a macro for debug logging
#ifndef NDEBUG // Only compile debug messages if NDEBUG is not defined
#define GANL_IOCP_DEBUG(sock, x) \
    do { std::cerr << "[IOCP:" << sock << "] " << x << std::endl; } while (0)
#else
#define GANL_IOCP_DEBUG(sock, x) do {} while (0)
#endif

namespace ganl {

    // --- PerIoData Implementation ---

    PerIoData::PerIoData(OpType type, ConnectionHandle conn, char* buf, size_t size, IocpNetworkEngine* eng)
        : opType(type), connection(conn), buffer(buf), bufferSize(size), engine(eng), acceptSocket(INVALID_SOCKET) {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));
        wsaBuf.buf = buffer;
        wsaBuf.len = static_cast<ULONG>(bufferSize);
        GANL_IOCP_DEBUG(conn, "PerIoData created for " << (opType == OpType::Read ? "Read" : "Write")
            << " operation, buffer=" << static_cast<void*>(buffer)
            << ", size=" << bufferSize);
    }

    // Constructor for Accept operations
    PerIoData::PerIoData(ListenerHandle listener, IocpNetworkEngine* eng)
        : opType(OpType::Accept),
        connection(InvalidConnectionHandle), // Keep this as Invalid for Accept ops
        buffer(acceptBuffer),
        bufferSize(sizeof(acceptBuffer)),
        engine(eng),
        listenerHandle(listener)
    {
        ZeroMemory(&overlapped, sizeof(OVERLAPPED));

        // Create socket for AcceptEx (error handling is good here)
        ErrorCode error = 0;
        acceptSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (acceptSocket == INVALID_SOCKET) {
            error = WSAGetLastError();
            GANL_IOCP_DEBUG(listener, "Failed to create accept socket: " << error);
        }
        else {
            GANL_IOCP_DEBUG(listener, "Accept socket created: " << acceptSocket);
        }

        wsaBuf.buf = buffer;
        wsaBuf.len = static_cast<ULONG>(bufferSize);
    }

    PerIoData::~PerIoData() {
        // For accept operations, we need to close the socket if it wasn't successfully accepted
        if (opType == OpType::Accept && acceptSocket != INVALID_SOCKET) {
            GANL_IOCP_DEBUG(acceptSocket, "Closing unaccepted socket in PerIoData destructor");
            closesocket(acceptSocket);
            acceptSocket = INVALID_SOCKET;
        }
        GANL_IOCP_DEBUG(connection, "PerIoData destroyed");
    }

    // --- IocpNetworkEngine Implementation ---

    IocpNetworkEngine::IocpNetworkEngine()
        : iocp_(INVALID_HANDLE_VALUE),
        wsaInitialized_(false),
        initialized_(false),
        lpfnAcceptEx_(nullptr),
        lpfnGetAcceptExSockaddrs_(nullptr) {
        GANL_IOCP_DEBUG(0, "Engine Created");
    }

    IocpNetworkEngine::~IocpNetworkEngine() {
        GANL_IOCP_DEBUG(0, "Engine Destroyed");
        shutdown();
    }

    bool IocpNetworkEngine::initialize() {
        GANL_IOCP_DEBUG(0, "Initializing...");

        if (initialized_) {
            GANL_IOCP_DEBUG(0, "Already initialized");
            return true;
        }

        ErrorCode error = 0;

        // Initialize Winsock
        if (!initializeWinsock(error)) {
            GANL_IOCP_DEBUG(0, "Winsock initialization failed: " << getErrorString(error));
            return false;
        }

        // Create I/O Completion Port
        if (!createIoCompletionPort(error)) {
            GANL_IOCP_DEBUG(0, "Failed to create IOCP: " << getErrorString(error));
            cleanupWinsock();
            return false;
        }

        initialized_ = true;
        GANL_IOCP_DEBUG(0, "Initialization successful");
        return true;
    }

    void IocpNetworkEngine::shutdown() {
        GANL_IOCP_DEBUG(0, "Shutdown requested");

        if (!initialized_) {
            GANL_IOCP_DEBUG(0, "Already shut down or not initialized");
            return;
        }

        // First, close all listeners to stop accepting new connections
        std::vector<SOCKET> listenerSockets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& pair : listeners_) {
                listenerSockets.push_back(pair.first);
            }
        }

        for (SOCKET listener : listenerSockets) {
            GANL_IOCP_DEBUG(listener, "Closing listener");
            closeListener(listener);
        }

        // Now close all remaining connections
        std::vector<SOCKET> connectionSockets;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            for (const auto& pair : sockets_) {
                if (pair.second.type == SocketType::Connection) {
                    connectionSockets.push_back(pair.first);
                }
            }
        }

        for (SOCKET conn : connectionSockets) {
            GANL_IOCP_DEBUG(conn, "Closing connection");
            closeConnection(conn);
        }

        // Close IOCP handle
        if (iocp_ != INVALID_HANDLE_VALUE) {
            GANL_IOCP_DEBUG(0, "Closing IOCP handle");
            CloseHandle(iocp_);
            iocp_ = INVALID_HANDLE_VALUE;
        }

        // Clean up Winsock
        cleanupWinsock();

        // Clear data structures
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sockets_.clear();
            listeners_.clear();
        }

        initialized_ = false;
        GANL_IOCP_DEBUG(0, "Shutdown complete");
    }

    ListenerHandle IocpNetworkEngine::createListener(const std::string& host, uint16_t port, ErrorCode& error) {
        GANL_IOCP_DEBUG(0, "Creating listener for " << host << ":" << port);
        error = 0;

        if (!initialized_) {
            GANL_IOCP_DEBUG(0, "Engine not initialized");
            error = ERROR_NOT_READY;
            return InvalidListenerHandle;
        }

        // Create a socket for listening
        SOCKET listenSocket = createSocket(error);
        if (listenSocket == INVALID_SOCKET) {
            GANL_IOCP_DEBUG(0, "Failed to create socket: " << getErrorString(error));
            return InvalidListenerHandle;
        }

        // Set socket options (reuse addr, etc.)
        if (!setSocketOptions(listenSocket, error)) {
            GANL_IOCP_DEBUG(listenSocket, "Failed to set socket options: " << getErrorString(error));
            closesocket(listenSocket);
            return InvalidListenerHandle;
        }

        // Bind the socket to the address and port
        sockaddr_in serverAddr;
        ZeroMemory(&serverAddr, sizeof(serverAddr));
        serverAddr.sin_family = AF_INET;
        serverAddr.sin_port = htons(port);

        if (host.empty() || host == "0.0.0.0") {
            serverAddr.sin_addr.s_addr = INADDR_ANY;
            GANL_IOCP_DEBUG(listenSocket, "Binding to INADDR_ANY:" << port);
        }
        else {
            GANL_IOCP_DEBUG(listenSocket, "Binding to " << host << ":" << port);
            if (inet_pton(AF_INET, host.c_str(), &serverAddr.sin_addr) != 1) {
                error = WSAGetLastError();
                GANL_IOCP_DEBUG(listenSocket, "Failed to convert IP address: " << getErrorString(error));
                closesocket(listenSocket);
                return InvalidListenerHandle;
            }
        }

        if (bind(listenSocket, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR) {
            error = WSAGetLastError();
            GANL_IOCP_DEBUG(listenSocket, "bind() failed: " << getErrorString(error));
            closesocket(listenSocket);
            return InvalidListenerHandle;
        }

        // Associate the socket with IOCP
        if (!associateWithIocp(reinterpret_cast<HANDLE>(listenSocket), reinterpret_cast<void*>(listenSocket), error)) {
            GANL_IOCP_DEBUG(listenSocket, "Failed to associate with IOCP: " << getErrorString(error));
            closesocket(listenSocket);
            return InvalidListenerHandle;
        }

        // Initialize socket info structures
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sockets_[listenSocket] = { SocketType::Listener, nullptr, false, "" };
            listeners_[listenSocket] = { nullptr, false, 0 };
        }

        GANL_IOCP_DEBUG(listenSocket, "Listener created successfully");
        return static_cast<ListenerHandle>(listenSocket);
    }

    bool IocpNetworkEngine::startListening(ListenerHandle listener, void* listenerContext, ErrorCode& error) {
        SOCKET listenSocket = static_cast<SOCKET>(listener);
        GANL_IOCP_DEBUG(listenSocket, "Starting listener. Context=" << listenerContext);
        error = 0;

        if (!initialized_) {
            GANL_IOCP_DEBUG(listenSocket, "Engine not initialized");
            error = ERROR_NOT_READY;
            return false;
        }

        // Check if it's a valid listener
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto sockIt = sockets_.find(listenSocket);
            auto listenerIt = listeners_.find(listenSocket);

            if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Listener ||
                listenerIt == listeners_.end()) {
                GANL_IOCP_DEBUG(listenSocket, "Invalid listener handle");
                error = ERROR_INVALID_HANDLE;
                return false;
            }

            // Update context and mark as listening
            sockIt->second.context = listenerContext;
            listenerIt->second.context = listenerContext;
            listenerIt->second.isListening = false; // Will be set to true after listen() succeeds
        }

        // Start listening
        if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR) {
            error = WSAGetLastError();
            GANL_IOCP_DEBUG(listenSocket, "listen() failed: " << getErrorString(error));
            return false;
        }

        // Get AcceptEx function pointer if not already retrieved
        if (lpfnAcceptEx_ == nullptr) {
            GUID guidAcceptEx = WSAID_ACCEPTEX;
            DWORD dwBytes = 0;

            if (WSAIoctl(listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                &guidAcceptEx, sizeof(guidAcceptEx),
                &lpfnAcceptEx_, sizeof(lpfnAcceptEx_),
                &dwBytes, NULL, NULL) == SOCKET_ERROR) {
                error = WSAGetLastError();
                GANL_IOCP_DEBUG(listenSocket, "Failed to get AcceptEx function pointer: " << getErrorString(error));
                return false;
            }

            GUID guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;

            if (WSAIoctl(listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                &guidGetAcceptExSockaddrs, sizeof(guidGetAcceptExSockaddrs),
                &lpfnGetAcceptExSockaddrs_, sizeof(lpfnGetAcceptExSockaddrs_),
                &dwBytes, NULL, NULL) == SOCKET_ERROR) {
                error = WSAGetLastError();
                GANL_IOCP_DEBUG(listenSocket, "Failed to get GetAcceptExSockaddrs function pointer: " << getErrorString(error));
                return false;
            }
        }

        // Mark as listening and post initial accept
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto listenerIt = listeners_.find(listenSocket);
            if (listenerIt != listeners_.end()) {
                listenerIt->second.isListening = true;
            }
        }

        // Post an initial accept operation
        if (!postAccept(listener, error)) {
            GANL_IOCP_DEBUG(listenSocket, "Failed to post initial accept: " << getErrorString(error));
            return false;
        }

        GANL_IOCP_DEBUG(listenSocket, "Listener started successfully");
        return true;
    }

    void IocpNetworkEngine::closeListener(ListenerHandle listener) {
        SOCKET listenSocket = static_cast<SOCKET>(listener);
        GANL_IOCP_DEBUG(listenSocket, "Closing listener");

        if (!initialized_) {
            GANL_IOCP_DEBUG(listenSocket, "Engine not initialized");
            return;
        }

        // Cancel all pending I/O operations on this socket
        cancelIoOperations(listenSocket);

        // Close the socket
        closesocket(listenSocket);

        // Remove from internal maps
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sockets_.erase(listenSocket);
            listeners_.erase(listenSocket);
        }

        GANL_IOCP_DEBUG(listenSocket, "Listener closed");
    }

    bool IocpNetworkEngine::associateContext(ConnectionHandle conn, void* context, ErrorCode& error) {
        SOCKET socket = static_cast<SOCKET>(conn);
        GANL_IOCP_DEBUG(socket, "Associating context " << context);
        error = 0;

        if (!initialized_) {
            GANL_IOCP_DEBUG(socket, "Engine not initialized");
            error = ERROR_NOT_READY;
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sockets_.find(socket);
            if (it == sockets_.end() || it->second.type != SocketType::Connection) {
                GANL_IOCP_DEBUG(socket, "Invalid connection handle or not a connection type");
                error = ERROR_INVALID_HANDLE;
                return false;
            }

            it->second.context = context;
        }

        GANL_IOCP_DEBUG(socket, "Context associated successfully");
        return true;
    }

    void IocpNetworkEngine::closeConnection(ConnectionHandle conn) {
        SOCKET socket = static_cast<SOCKET>(conn);
        GANL_IOCP_DEBUG(socket, "Closing connection");

        if (!initialized_) {
            GANL_IOCP_DEBUG(socket, "Engine not initialized");
            return;
        }

        // Cancel all pending I/O operations on this socket
        cancelIoOperations(socket);

        // Shutdown the socket first to send FIN
        ::shutdown(socket, SD_BOTH);

        // Close the socket
        closesocket(socket);

        // Remove from internal map
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sockets_.erase(socket);
        }

        GANL_IOCP_DEBUG(socket, "Connection closed");
    }

    bool IocpNetworkEngine::postRead(ConnectionHandle conn, char* buffer, size_t length, ErrorCode& error) {
        SOCKET socket = static_cast<SOCKET>(conn);
        GANL_IOCP_DEBUG(socket, "Posting read for up to " << length << " bytes");
        error = 0;

        if (!initialized_) {
            GANL_IOCP_DEBUG(socket, "Engine not initialized");
            error = ERROR_NOT_READY;
            return false;
        }

        // Check if it's a valid connection
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sockets_.find(socket);
            if (it == sockets_.end() || it->second.type != SocketType::Connection) {
                GANL_IOCP_DEBUG(socket, "Invalid connection handle or not a connection type");
                error = ERROR_INVALID_HANDLE;
                return false;
            }

            // Check if there's already a pending read
            if (it->second.pendingRead) {
                GANL_IOCP_DEBUG(socket, "Read already pending");
                error = ERROR_IO_PENDING;
                return false;
            }

            // Mark as having a pending read
            it->second.pendingRead = true;
        }

        // Create per-I/O data structure
        std::unique_ptr<PerIoData> perIoData = std::make_unique<PerIoData>(
            PerIoData::OpType::Read, conn, buffer, length, this);

        // Post WSARecv
        if (!postWSARecv(conn, perIoData.get(), error)) {
            // Reset pendingRead flag on failure
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sockets_.find(socket);
            if (it != sockets_.end()) {
                it->second.pendingRead = false;
            }
            return false;
        }

        // Release ownership of perIoData (will be cleaned up after operation completes)
        perIoData.release();

        GANL_IOCP_DEBUG(socket, "Read posted successfully");
        return true;
    }

    bool IocpNetworkEngine::postWrite(ConnectionHandle conn, const char* data, size_t length, ErrorCode& error) {
        SOCKET socket = static_cast<SOCKET>(conn);
        GANL_IOCP_DEBUG(socket, "Posting write for " << length << " bytes");
        error = 0;

        if (!initialized_) {
            GANL_IOCP_DEBUG(socket, "Engine not initialized");
            error = ERROR_NOT_READY;
            return false;
        }

        // Check if it's a valid connection
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sockets_.find(socket);
            if (it == sockets_.end() || it->second.type != SocketType::Connection) {
                GANL_IOCP_DEBUG(socket, "Invalid connection handle or not a connection type");
                error = ERROR_INVALID_HANDLE;
                return false;
            }
        }

        // Create per-I/O data structure
        // Note: For write operations, we use const_cast because WSASend takes non-const buffer
        // This is safe because WSASend won't modify the buffer, but the API requires non-const
        std::unique_ptr<PerIoData> perIoData = std::make_unique<PerIoData>(
            PerIoData::OpType::Write, conn, const_cast<char*>(data), length, this);

        // Post WSASend
        if (!postWSASend(conn, perIoData.get(), error)) {
            return false;
        }

        // Release ownership of perIoData (will be cleaned up after operation completes)
        perIoData.release();

        GANL_IOCP_DEBUG(socket, "Write posted successfully");
        return true;
    }

    int IocpNetworkEngine::processEvents(int timeoutMs, IoEvent* events, int maxEvents) {
        if (!initialized_ || iocp_ == INVALID_HANDLE_VALUE) {
            GANL_IOCP_DEBUG(0, "Engine not initialized or IOCP handle invalid");
            return -1;
        }

        if (maxEvents <= 0) {
            return 0;
        }

        int eventCount = 0;
        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        LPOVERLAPPED overlapped = nullptr;

        // Wait for completion or timeout
        BOOL result = GetQueuedCompletionStatus(
            iocp_,
            &bytesTransferred,
            &completionKey,
            &overlapped,
            timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs) // Cast timeoutMs to DWORD
        );

        DWORD lastError = GetLastError(); // Get error immediately after GQCS

        // Check for timeout or critical IOCP error
        if (overlapped == nullptr) {
            if (lastError == WAIT_TIMEOUT) {
                // GANL_IOCP_DEBUG(0, "GetQueuedCompletionStatus timed out."); // Can be noisy
                return 0; // Timeout, no event
            }
            else {
                // Critical error with the IOCP itself
                GANL_IOCP_DEBUG(0, "GetQueuedCompletionStatus failed critically (overlapped is null): " << getErrorString(lastError));
                // Consider signaling a fatal error or shutting down.
                return -1;
            }
        }

        // --- Process Completions ---
        // We enter the loop because we got at least one completion (overlapped != nullptr)
        do {
            // If we've filled the user's buffer, break the inner loop.
            // GetQueuedCompletionStatus might have more completions ready, but the user
            // needs to call processEvents again with space.
            if (eventCount >= maxEvents) {
                GANL_IOCP_DEBUG(0, "Max events (" << maxEvents << ") reached in single processEvents call. Breaking loop.");
                // Post the unprocessed completion back? No, GQCS already dequeued it.
                // The caller needs to call again quickly.
                break;
            }

            // Get the PerIoData structure associated with this completion
            PerIoData* perIoData = CONTAINING_RECORD(overlapped, PerIoData, overlapped);
            SOCKET socket = static_cast<SOCKET>(completionKey); // Socket associated with the I/O
            // Determine the error code for *this specific operation*
            // result is TRUE if successful, FALSE if failed.
            // If result is FALSE, lastError (captured right after GQCS) contains the reason.
            ErrorCode operationError = result ? 0 : lastError;

            GANL_IOCP_DEBUG(socket, "Completion received: OpType=" << static_cast<int>(perIoData->opType)
                << ", BytesTransferred=" << bytesTransferred
                << ", Result=" << (result ? "Success" : "Fail")
                << ", Error=" << operationError << " (" << (operationError ? getErrorString(operationError) : "None") << ")"
                << ", PerIoData=" << static_cast<void*>(perIoData));

            IoEvent& event = events[eventCount]; // Reference to the current event slot
            event.bytesTransferred = bytesTransferred;
            event.error = operationError;

            // Get the context associated with this socket (Listener or Connection)
            void* socketContext = nullptr;
            bool isConnection = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = sockets_.find(socket);
                if (it != sockets_.end()) {
                    socketContext = it->second.context;
                    if (it->second.type == SocketType::Connection) {
                        isConnection = true;
                        // Reset pendingRead flag only for completed read operations on connections
                        if (perIoData->opType == PerIoData::OpType::Read) {
                            it->second.pendingRead = false;
                            GANL_IOCP_DEBUG(socket, "Reset pendingRead flag");
                        }
                    }
                }
                else {
                    GANL_IOCP_DEBUG(socket, "Warning: Completion received for unknown socket handle!");
                    // Socket might have been closed concurrently. Skip processing, clean up PerIoData below.
                    delete perIoData; // Clean up the orphaned PerIoData
                    // Try to dequeue the next completion without incrementing eventCount
                    result = GetQueuedCompletionStatus(iocp_, &bytesTransferred, &completionKey, &overlapped, 0);
                    lastError = GetLastError();
                    if (overlapped == nullptr) break; // No more events ready now
                    continue; // Process the next completion
                }
            }

            // Process based on operation type
            switch (perIoData->opType) {
            case PerIoData::OpType::Read:
                event.type = IoEventType::Read; // Default to Read
                event.connection = perIoData->connection; // Should match 'socket' if it's a connection
                event.context = socketContext;

                if (!result) { // Read failed
                    if (operationError == ERROR_HANDLE_EOF || operationError == WSAECONNRESET || operationError == WSAECONNABORTED) {
                        // Graceful close or connection reset by peer
                        event.type = IoEventType::Close;
                        GANL_IOCP_DEBUG(socket, "Read completed with EOF/Reset. BytesTransferred=" << bytesTransferred << ". Reporting Close event.");
                    }
                    else {
                        // Other error
                        event.type = IoEventType::Error;
                        GANL_IOCP_DEBUG(socket, "Read completed with error: " << operationError << ". Reporting Error event.");
                    }
                    // Bytes transferred might be 0 on error, but could be non-zero if some data read before error.
                    // The event structure correctly reports the error code and bytes transferred.
                }
                else { // Read succeeded
                    if (bytesTransferred == 0) {
                        // Read succeeded but returned 0 bytes - this indicates graceful close by peer.
                        event.type = IoEventType::Close;
                        GANL_IOCP_DEBUG(socket, "Read completed successfully with 0 bytes. Reporting Close event.");
                    }
                    else {
                        // Successful read with data
                        GANL_IOCP_DEBUG(socket, "Read completed successfully. BytesTransferred=" << bytesTransferred << ". Reporting Read event.");
                    }
                }
                eventCount++; // Increment count for Read, Close, or Error derived from Read
                break;

            case PerIoData::OpType::Write:
                event.connection = perIoData->connection; // Should match 'socket'
                event.context = socketContext;

                if (!result) { // Write failed
                    if (operationError == WSAECONNRESET || operationError == WSAECONNABORTED) {
                        // Treat connection reset during write as a close/error condition
                        event.type = IoEventType::Close; // Or Error? Close seems reasonable.
                        GANL_IOCP_DEBUG(socket, "Write completed with Reset/Abort: " << operationError << ". Reporting Close event.");
                    }
                    else {
                        event.type = IoEventType::Error;
                        GANL_IOCP_DEBUG(socket, "Write completed with error: " << operationError << ". Reporting Error event.");
                    }
                }
                else { // Write succeeded
                    event.type = IoEventType::Write;
                    GANL_IOCP_DEBUG(socket, "Write completed successfully. BytesTransferred=" << bytesTransferred << ". Reporting Write event.");
                    // Note: If bytesTransferred < requested length, IOCP guarantees it will generate
                    // another completion event when more buffer space is available *without*
                    // requiring us to repost the write for the remaining data. The Connection
                    // layer needs to handle partial writes by tracking remaining data in its output buffer.
                }
                eventCount++; // Increment count for Write, Close, or Error derived from Write
                break;

            case PerIoData::OpType::Accept:
            { // Scope for local variables
                SOCKET listenSocket = socket; // The completion key for Accept is the listener socket
                ErrorCode acceptError = 0; // To capture errors from handleAcceptCompletion

                if (result) { // AcceptEx reported success
                    GANL_IOCP_DEBUG(listenSocket, "AcceptEx completed successfully. Handling completion...");
                    ConnectionHandle newConn = handleAcceptCompletion(perIoData, acceptError);

                    if (newConn != InvalidConnectionHandle) {
                        // --- SUCCESSFUL ACCEPT ---
                        GANL_IOCP_DEBUG(listenSocket, "handleAcceptCompletion succeeded. New connection: " << newConn);
                        event.type = IoEventType::Accept;
                        event.listener = listenSocket;
                        event.connection = newConn; // The NEW connection handle
                        // The context for the *new* connection is NOT set here.
                        // It will be associated by the caller (main loop -> Connection::initialize)
                        // We can, however, pass the listener's context if useful.
                        event.context = socketContext; // Listener's context
                        event.error = 0;
                        event.bytesTransferred = 0; // Not relevant for Accept event itself
                        eventCount++;

                        // --- IMPORTANT: Post the next AcceptEx ---
                        ErrorCode postAcceptError = 0;
                        GANL_IOCP_DEBUG(listenSocket, "Posting next AcceptEx operation.");
                        if (!postAccept(listenSocket, postAcceptError)) {
                            GANL_IOCP_DEBUG(listenSocket, "CRITICAL: Failed to post next AcceptEx: " << getErrorString(postAcceptError) << ". Listener may stop accepting!");
                            // Consider how to handle this - maybe close the listener?
                        }

                    }
                    else {
                        // --- handleAcceptCompletion FAILED ---
                        // Something went wrong after AcceptEx succeeded (e.g., setting options, associating IOCP)
                        GANL_IOCP_DEBUG(listenSocket, "handleAcceptCompletion failed: " << getErrorString(acceptError) << ". Reporting Error event for listener.");
                        event.type = IoEventType::Error;
                        event.listener = listenSocket;
                        event.connection = InvalidConnectionHandle;
                        event.context = socketContext; // Listener's context
                        event.error = acceptError; // The error from handleAcceptCompletion
                        eventCount++;

                        // --- IMPORTANT: Still post the next AcceptEx ---
                        // The listener socket itself is likely okay, so try to recover.
                        ErrorCode postAcceptError = 0;
                        GANL_IOCP_DEBUG(listenSocket, "Posting next AcceptEx operation after handleAcceptCompletion failure.");
                        if (!postAccept(listenSocket, postAcceptError)) {
                            GANL_IOCP_DEBUG(listenSocket, "CRITICAL: Failed to post next AcceptEx: " << getErrorString(postAcceptError) << ". Listener may stop accepting!");
                        }
                    }
                }
                else {
                    // --- AcceptEx FAILED ---
                    // The initial AcceptEx call itself failed asynchronously.
                    GANL_IOCP_DEBUG(listenSocket, "AcceptEx failed asynchronously: " << getErrorString(operationError) << ". Reporting Error event for listener.");
                    event.type = IoEventType::Error;
                    event.listener = listenSocket;
                    event.connection = InvalidConnectionHandle;
                    event.context = socketContext; // Listener's context
                    event.error = operationError; // The error from AcceptEx completion
                    eventCount++;

                    // --- IMPORTANT: Post the next AcceptEx ---
                    // Try to recover and keep listening.
                    // Avoid error code WSAECONNRESET which might happen if client connects/disconnects quickly.
                    if (operationError != WSAECONNRESET && operationError != WSAECONNABORTED) {
                        ErrorCode postAcceptError = 0;
                        GANL_IOCP_DEBUG(listenSocket, "Posting next AcceptEx operation after AcceptEx failure.");
                        if (!postAccept(listenSocket, postAcceptError)) {
                            GANL_IOCP_DEBUG(listenSocket, "CRITICAL: Failed to post next AcceptEx: " << getErrorString(postAcceptError) << ". Listener may stop accepting!");
                        }
                    }
                    else {
                        GANL_IOCP_DEBUG(listenSocket, "AcceptEx failed with WSAECONNRESET/WSAECONNABORTED. Not reporting error, posting next accept.");
                        // Don't report error for simple resets, just post next accept.
                        ErrorCode postAcceptError = 0;
                        if (!postAccept(listenSocket, postAcceptError)) {
                            GANL_IOCP_DEBUG(listenSocket, "CRITICAL: Failed to post next AcceptEx: " << getErrorString(postAcceptError) << ". Listener may stop accepting!");
                        }
                        // We need to consume the current failed completion without adding an event
                        eventCount--; // Backtrack count since we are not adding an event
                    }
                }
            } // End Accept scope
            break;

            default:
                GANL_IOCP_DEBUG(socket, "Warning: Unknown operation type (" << static_cast<int>(perIoData->opType) << ") in completion packet!");
                // Don't increment eventCount for unknown types
                break;

            } // End switch (perIoData->opType)

            // --- Cleanup PerIoData ---
            // This structure is allocated per overlapped operation and must be
            // deleted once the operation completes (successfully or otherwise).
            GANL_IOCP_DEBUG(socket, "Deleting PerIoData structure: " << static_cast<void*>(perIoData));
            delete perIoData;
            perIoData = nullptr; // Avoid double delete if loop continues unexpectedly

            // --- Try to get another completion immediately ---
            // If we haven't filled the user's buffer, check if more completions are ready without waiting.
            if (eventCount < maxEvents) {
                result = GetQueuedCompletionStatus(
                    iocp_,
                    &bytesTransferred,
                    &completionKey,
                    &overlapped,
                    0 // No wait - poll
                );
                lastError = GetLastError(); // Get error immediately

                // If overlapped is NULL after polling, no more completions are immediately available.
                if (overlapped == nullptr) {
                    // Check if the reason was timeout (expected when polling) or a real error
                    if (result == FALSE && lastError != WAIT_TIMEOUT) {
                        // This indicates a potential issue with the IOCP port itself during the poll.
                        GANL_IOCP_DEBUG(0, "GetQueuedCompletionStatus polling failed critically: " << getErrorString(lastError));
                        // Maybe return -1 or handle based on the error? For now, just break.
                    }
                    break; // Exit the do-while loop, no more completions ready now.
                }
                // If overlapped is not NULL, the loop continues to process the next completion.
            }
            else {
                // We filled the buffer, exit loop. Outer code needs to call again.
                break;
            }

        } while (true); // Loop continues as long as GQCS returns completions and eventCount < maxEvents

        return eventCount;
    }

    std::string IocpNetworkEngine::getRemoteAddress(ConnectionHandle conn) {
        SOCKET socket = static_cast<SOCKET>(conn);

        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sockets_.find(socket);
            if (it != sockets_.end() && !it->second.remoteAddress.empty()) {
                // Return cached address if available
                return it->second.remoteAddress;
            }
        }

        // Get the address if not cached
        sockaddr_storage addrStorage;
        int addrLen = sizeof(addrStorage);

        if (getpeername(socket, reinterpret_cast<sockaddr*>(&addrStorage), &addrLen) == SOCKET_ERROR) {
            GANL_IOCP_DEBUG(socket, "getpeername failed: " << WSAGetLastError());
            return "unknown";
        }

        std::string address;

        // First try WSAAddressToStringW for better IPv6 support
        if (addrStorage.ss_family == AF_INET || addrStorage.ss_family == AF_INET6) {
            WCHAR wIpStr[INET6_ADDRSTRLEN * 2]; // Large buffer for WCHAR IP + Port + separators
            DWORD wIpStrLen = sizeof(wIpStr) / sizeof(WCHAR);

            if (WSAAddressToStringW(reinterpret_cast<LPSOCKADDR>(&addrStorage), addrLen, nullptr,
                                   wIpStr, &wIpStrLen) == 0) {
                // Convert WCHAR* to std::string
                try {
                    #pragma warning(push)
                    #pragma warning(disable: 4996) // Disable deprecated warning
                    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> converter;
                    address = converter.to_bytes(wIpStr);
                    #pragma warning(pop)
                }
                catch (const std::exception& e) {
                    GANL_IOCP_DEBUG(socket, "Error converting address: " << e.what());
                    // Fall through to fallback method
                    address.clear();
                }
            }
        }

        // Fallback method using inet_ntop if WSAAddressToStringW failed
        if (address.empty()) {
            char ipStr[INET6_ADDRSTRLEN];
            int port = 0;

            if (addrStorage.ss_family == AF_INET) {
                sockaddr_in* addr4 = reinterpret_cast<sockaddr_in*>(&addrStorage);
                if (InetNtopA(AF_INET, &addr4->sin_addr, ipStr, sizeof(ipStr)) != nullptr) {
                    port = ntohs(addr4->sin_port);
                    address = std::string(ipStr) + ":" + std::to_string(port);
                }
            }
            else if (addrStorage.ss_family == AF_INET6) {
                sockaddr_in6* addr6 = reinterpret_cast<sockaddr_in6*>(&addrStorage);
                if (InetNtopA(AF_INET6, &addr6->sin6_addr, ipStr, sizeof(ipStr)) != nullptr) {
                    port = ntohs(addr6->sin6_port);
                    address = "[" + std::string(ipStr) + "]:" + std::to_string(port);
                }
            }

            if (address.empty()) {
                GANL_IOCP_DEBUG(socket, "Address conversion failed");
                return "unknown";
            }
        }

        // Cache the address
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sockets_.find(socket);
            if (it != sockets_.end()) {
                it->second.remoteAddress = address;
            }
        }

        return address;
    }

    std::string IocpNetworkEngine::getErrorString(ErrorCode error) {
        char* buffer = nullptr;
        DWORD result = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            error,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&buffer,
            0,
            NULL
        );

        if (result == 0 || buffer == nullptr) {
            return "Unknown error code " + std::to_string(error);
        }

        std::string message(buffer);
        LocalFree(buffer);

        // Trim trailing whitespace and newlines
        size_t endpos = message.find_last_not_of(" \n\r\t");
        if (std::string::npos != endpos) {
            message = message.substr(0, endpos + 1);
        }

        return message + " (" + std::to_string(error) + ")";
    }

    // --- Private Helper Methods ---

    IoModel IocpNetworkEngine::getIoModelType() const
    {
        return IoModel::Completion;
    }

    bool IocpNetworkEngine::initializeWinsock(ErrorCode& error) {
        if (wsaInitialized_) {
            return true;
        }

        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            error = result;
            return false;
        }

        wsaInitialized_ = true;
        return true;
    }

    void IocpNetworkEngine::cleanupWinsock() {
        if (wsaInitialized_) {
            WSACleanup();
            wsaInitialized_ = false;
        }
    }

    bool IocpNetworkEngine::createIoCompletionPort(ErrorCode& error) {
        iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
        if (iocp_ == INVALID_HANDLE_VALUE) {
            error = GetLastError();
            return false;
        }
        return true;
    }

    SOCKET IocpNetworkEngine::createSocket(ErrorCode& error) {
        SOCKET sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (sock == INVALID_SOCKET) {
            error = WSAGetLastError();
        }
        return sock;
    }

    bool IocpNetworkEngine::setSocketOptions(SOCKET socket, ErrorCode& error) {
        // Set SO_REUSEADDR to allow faster restart
        int opt = 1;
        if (setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
            error = WSAGetLastError();
            return false;
        }

        // Set TCP_NODELAY to disable Nagle's algorithm
        if (setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, (char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
            error = WSAGetLastError();
            return false;
        }

        // Set non-blocking mode (not strictly necessary with IOCP, but useful for things like connect)
        u_long mode = 1;
        if (ioctlsocket(socket, FIONBIO, &mode) == SOCKET_ERROR) {
            error = WSAGetLastError();
            return false;
        }

        return true;
    }

    bool IocpNetworkEngine::associateWithIocp(HANDLE handle, void* completionKey, ErrorCode& error) {
        HANDLE result = CreateIoCompletionPort(handle, iocp_, (ULONG_PTR)completionKey, 0);
        if (result == nullptr) {
            error = GetLastError();
            return false;
        }
        return true;
    }

    bool IocpNetworkEngine::postAccept(ListenerHandle listener, ErrorCode& error) {
        SOCKET listenSocket = static_cast<SOCKET>(listener);

        // Check if listener is still active
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = listeners_.find(listenSocket);
            if (it == listeners_.end() || !it->second.isListening) {
                error = ERROR_INVALID_HANDLE;
                return false;
            }

            // Increment pending accepts counter
            it->second.pendingAccepts++;
        }

        // Create per-I/O data for accept
        std::unique_ptr<PerIoData> perIoData = std::make_unique<PerIoData>(listener, this);
        if (perIoData->acceptSocket == INVALID_SOCKET) {
            error = WSAGetLastError();

            // Decrement pending accepts counter on failure
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = listeners_.find(listenSocket);
            if (it != listeners_.end()) {
                it->second.pendingAccepts--;
            }

            return false;
        }

        // Use AcceptEx to initiate an overlapped accept
        DWORD bytesReceived = 0;
        BOOL result = lpfnAcceptEx_(
            listenSocket,                  // Listen socket
            perIoData->acceptSocket,       // Accept socket
            perIoData->buffer,             // Buffer for addresses
            0,                            // Receive data length (0 for no data)
            sizeof(sockaddr_in) + 16,     // Local address length
            sizeof(sockaddr_in) + 16,     // Remote address length
            &bytesReceived,               // Bytes received (unused)
            &perIoData->overlapped        // Overlapped structure
        );

        // If AcceptEx returns FALSE and error is WSA_IO_PENDING, that's normal for async operation
        if (result == FALSE && (error = WSAGetLastError()) != ERROR_IO_PENDING) {
            GANL_IOCP_DEBUG(listenSocket, "AcceptEx failed: " << getErrorString(error));

            // Decrement pending accepts counter on failure
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = listeners_.find(listenSocket);
            if (it != listeners_.end()) {
                it->second.pendingAccepts--;
            }

            return false;
        }

        // AcceptEx() operation is pending, release the PerIoData
        perIoData.release();

        return true;
    }

    ConnectionHandle IocpNetworkEngine::handleAcceptCompletion(PerIoData* perIoData, ErrorCode& error) {
        error = 0;
        SOCKET acceptSocket = perIoData->acceptSocket;
        SOCKET listenSocket = static_cast<SOCKET>(perIoData->listenerHandle);

        // Prevent socket from being closed in PerIoData destructor if we successfully handle it
        perIoData->acceptSocket = INVALID_SOCKET;

        // Decrement pending accepts counter (Should happen regardless of success/failure below)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = listeners_.find(listenSocket);
            if (it != listeners_.end()) {
                it->second.pendingAccepts--;
                GANL_IOCP_DEBUG(listenSocket, "Decremented pending accepts to: " << it->second.pendingAccepts);
            }
        }

        // --- Call SO_UPDATE_ACCEPT_CONTEXT ---
        GANL_IOCP_DEBUG(listenSocket, "Calling SO_UPDATE_ACCEPT_CONTEXT on socket " << acceptSocket << " using listening socket " << listenSocket);
        if (setsockopt(acceptSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&listenSocket, sizeof(listenSocket)) == SOCKET_ERROR) {
            error = WSAGetLastError();
            GANL_IOCP_DEBUG(acceptSocket, "UpdateAcceptContext failed: " << getErrorString(error));
            closesocket(acceptSocket); // Close the newly accepted socket on failure
            return InvalidConnectionHandle; // Return error
        }
        GANL_IOCP_DEBUG(acceptSocket, "UpdateAcceptContext succeeded.");


        // --- Get Addresses (This part looks correct) ---
        sockaddr_in* localAddr = nullptr;
        sockaddr_in* remoteAddr = nullptr;
        int localAddrLen = sizeof(sockaddr_in);
        int remoteAddrLen = sizeof(sockaddr_in);

        // Check if lpfnGetAcceptExSockaddrs_ is valid before calling
        if (lpfnGetAcceptExSockaddrs_ == nullptr) {
            GANL_IOCP_DEBUG(acceptSocket, "lpfnGetAcceptExSockaddrs_ is null!");
            error = ERROR_INVALID_FUNCTION; // Or some other suitable error
            closesocket(acceptSocket);
            return InvalidConnectionHandle;
        }

        lpfnGetAcceptExSockaddrs_(
            perIoData->buffer,
            0,
            sizeof(sockaddr_in) + 16,
            sizeof(sockaddr_in) + 16,
            (sockaddr**)&localAddr,
            &localAddrLen,
            (sockaddr**)&remoteAddr,
            &remoteAddrLen
        );

        if (remoteAddr == nullptr) {
            // This can happen if the client disconnects immediately after connecting
            GANL_IOCP_DEBUG(acceptSocket, "GetAcceptExSockaddrs could not retrieve remote address (client likely disconnected).");
            error = WSAECONNABORTED; // Indicate an abort
            closesocket(acceptSocket);
            return InvalidConnectionHandle;
        }

        // Format the remote address string
        char ipStr[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &remoteAddr->sin_addr, ipStr, INET_ADDRSTRLEN) == nullptr) {
            GANL_IOCP_DEBUG(acceptSocket, "inet_ntop failed: " << WSAGetLastError());
            strcpy_s(ipStr, "unknown");
        }
        std::string remoteAddress = std::string(ipStr) + ":" + std::to_string(ntohs(remoteAddr->sin_port));
        GANL_IOCP_DEBUG(acceptSocket, "Retrieved remote address: " << remoteAddress);

        // --- Set Socket Options for the new connection ---
        GANL_IOCP_DEBUG(acceptSocket, "Setting socket options for new connection.");
        if (!setSocketOptions(acceptSocket, error)) {
            GANL_IOCP_DEBUG(acceptSocket, "Failed to set socket options for accepted socket: " << getErrorString(error));
            closesocket(acceptSocket);
            return InvalidConnectionHandle;
        }

        // --- Associate socket with IOCP ---
        GANL_IOCP_DEBUG(acceptSocket, "Associating accepted socket with IOCP.");
        if (!associateWithIocp((HANDLE)acceptSocket, (void*)acceptSocket, error)) {
            GANL_IOCP_DEBUG(acceptSocket, "Failed to associate accepted socket with IOCP: " << getErrorString(error));
            closesocket(acceptSocket);
            return InvalidConnectionHandle;
        }

        // --- Add socket to internal maps ---
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sockets_[acceptSocket] = { SocketType::Connection, nullptr, false, remoteAddress }; // Context starts as null
        }

        GANL_IOCP_DEBUG(acceptSocket, "Connection accepted successfully from " << remoteAddress);
        // The PerIoData used for accept will be deleted by the caller (processEvents)
        return static_cast<ConnectionHandle>(acceptSocket); // Return the new handle
    }

    void IocpNetworkEngine::cancelIoOperations(SOCKET socket) {
        // Cancel all pending I/O operations on the socket
        CancelIoEx((HANDLE)socket, NULL);
    }

    bool IocpNetworkEngine::postWSARecv(ConnectionHandle conn, PerIoData* perIoData, ErrorCode& error) {
        SOCKET socket = static_cast<SOCKET>(conn);
        DWORD flags = 0;
        DWORD bytesReceived = 0;

        // Post an overlapped WSARecv operation
        int result = WSARecv(
            socket,
            &perIoData->wsaBuf,
            1,
            &bytesReceived,
            &flags,
            &perIoData->overlapped,
            NULL
        );

        // If WSARecv returns SOCKET_ERROR and error is WSA_IO_PENDING, that's normal for async operation
        if (result == SOCKET_ERROR && (error = WSAGetLastError()) != ERROR_IO_PENDING) {
            GANL_IOCP_DEBUG(socket, "WSARecv failed: " << getErrorString(error));
            delete perIoData; // Clean up on immediate failure
            return false;
        }

        return true;
    }

    bool IocpNetworkEngine::postWSASend(ConnectionHandle conn, PerIoData* perIoData, ErrorCode& error) {
        SOCKET socket = static_cast<SOCKET>(conn);
        DWORD bytesSent = 0;

        // Post an overlapped WSASend operation
        int result = WSASend(
            socket,
            &perIoData->wsaBuf,
            1,
            &bytesSent,
            0,
            &perIoData->overlapped,
            NULL
        );

        // If WSASend returns SOCKET_ERROR and error is WSA_IO_PENDING, that's normal for async operation
        if (result == SOCKET_ERROR && (error = WSAGetLastError()) != ERROR_IO_PENDING) {
            GANL_IOCP_DEBUG(socket, "WSASend failed: " << getErrorString(error));
            delete perIoData; // Clean up on immediate failure
            return false;
        }

        return true;
    }

} // namespace ganl Return an Accept event
