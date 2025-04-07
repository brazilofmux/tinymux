#include "wselect_network_engine.h"
#include <iostream>
#include <sstream>
#include <algorithm>
#include <vector>
#include <locale> // For codecvt (might need C++11/14/17 depending on exact usage)
#include <codecvt> // For std::wstring_convert (Deprecated in C++17, but often available)

// Define a macro for debug logging
#ifndef NDEBUG // Only compile debug messages if NDEBUG is not defined
#define GANL_WSELECT_DEBUG(sock, x) \
    do { std::cerr << "[WSelect:" << (sock == 0 ? "Global" : std::to_string(static_cast<unsigned long long>(sock))) << "] " << x << std::endl; } while (0)
#else
#define GANL_WSELECT_DEBUG(sock, x) do {} while (0)
#endif

namespace ganl {

    // --- Constructor / Destructor ---

    WSelectNetworkEngine::WSelectNetworkEngine() {
        GANL_WSELECT_DEBUG(0, "Engine Created.");
    }

    WSelectNetworkEngine::~WSelectNetworkEngine() {
        GANL_WSELECT_DEBUG(0, "Engine Destroyed.");
        shutdown();
    }

    // --- Platform Abstraction Helpers ---

    void WSelectNetworkEngine::closeSocket(SocketFD sock) {
        // Attempt graceful shutdown first
        if (::shutdown(sock, SD_BOTH) == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSAENOTCONN && error != WSAECONNRESET && error != WSAECONNABORTED && error != WSAENOTSOCK) {
                GANL_WSELECT_DEBUG(sock, "Warning: shutdown(sock, SD_BOTH) failed: " << getErrorString(translateError(error)));
            } else {
                GANL_WSELECT_DEBUG(sock, "Socket already disconnected or invalid.");
            }
        }

        // Then close the file descriptor
        if (::closesocket(sock) == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSAENOTSOCK) {
                GANL_WSELECT_DEBUG(sock, "Warning: closesocket(sock) failed: " << getErrorString(translateError(error)));
            } else {
                GANL_WSELECT_DEBUG(sock, "Socket already closed or invalid.");
            }
        } else {
            GANL_WSELECT_DEBUG(sock, "Socket closed successfully.");
        }
    }

    ErrorCode WSelectNetworkEngine::getLastError() {
        return WSAGetLastError();
    }
    // --- End Platform Abstraction ---

    IoModel WSelectNetworkEngine::getIoModelType() const {
        return IoModel::Readiness;
    }

    // --- Initialization / Shutdown ---

    bool WSelectNetworkEngine::initialize() {
        GANL_WSELECT_DEBUG(0, "Initializing...");

        if (initialized_) {
            GANL_WSELECT_DEBUG(0, "Already initialized.");
            return true;
        }

        // Initialize Winsock
        if (!wsaInitialized_) {
            WSADATA wsaData;
            int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (result != 0) {
                // Use result directly as ErrorCode, as translateError isn't needed here
                std::cerr << "[WSelect:0] FATAL: WSAStartup failed: " << getErrorString(result) << std::endl;
                return false;
            }
            if (LOBYTE(wsaData.wVersion) != 2 || HIBYTE(wsaData.wVersion) != 2) {
                std::cerr << "[WSelect:0] FATAL: Could not find a usable version of Winsock.dll" << std::endl;
                WSACleanup();
                return false;
            }
            wsaInitialized_ = true;
            GANL_WSELECT_DEBUG(0, "WSAStartup successful. Winsock 2.2 initialized.");
        }

        // Clear master sets
        FD_ZERO(&masterReadFds_);
        FD_ZERO(&masterWriteFds_);
        FD_ZERO(&masterErrorFds_);
        monitoredSockets_.clear(); // Clear socket tracking

        initialized_ = true;
        GANL_WSELECT_DEBUG(0, "Initialization successful.");
        return true;
    }

    void WSelectNetworkEngine::shutdown() {
        GANL_WSELECT_DEBUG(0, "Shutdown requested.");

        if (!initialized_) {
            GANL_WSELECT_DEBUG(0, "Already shut down or not initialized.");
            return;
        }

        // Create copies of socket handles to avoid iterator invalidation
        std::vector<SocketFD> allHandles;
        allHandles.reserve(sockets_.size());
        for (const auto& pair : sockets_) {
            allHandles.push_back(pair.first);
        }

        GANL_WSELECT_DEBUG(0, "Closing " << allHandles.size() << " sockets (listeners and connections)...");
        for (SocketFD handle : allHandles) {
            // Check if the socket still exists in the map before trying to close
            // (it might have been closed already if shutdown is called multiple times, though unlikely)
            auto sockIt = sockets_.find(handle);
            if (sockIt != sockets_.end()) {
                if (sockIt->second.type == SocketType::Listener) {
                     closeListener(handle); // Uses removeSocketInternal
                } else {
                     closeConnection(handle); // Uses removeSocketInternal
                }
            }
        }

        // Clear remaining data structures (should be empty now)
        sockets_.clear();
        monitoredSockets_.clear();

        // Reset master fd_sets
        FD_ZERO(&masterReadFds_);
        FD_ZERO(&masterWriteFds_);
        FD_ZERO(&masterErrorFds_);

        // Cleanup Winsock
        if (wsaInitialized_) {
            WSACleanup();
            wsaInitialized_ = false;
            GANL_WSELECT_DEBUG(0, "WSACleanup called.");
        }

        initialized_ = false;
        GANL_WSELECT_DEBUG(0, "Shutdown complete.");
    }

    // --- Listener Management ---

    ListenerHandle WSelectNetworkEngine::createListener(const std::string& host, uint16_t port, ErrorCode& error) {
        GANL_WSELECT_DEBUG(0, "Creating listener for " << host << ":" << port);
        error = 0;
        if (!initialized_) { error = ENXIO; return InvalidListenerHandle; } // Engine not ready

        SocketFD sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET_FD) {
            error = getLastError();
            GANL_WSELECT_DEBUG(0, "socket() failed: " << getErrorString(translateError(error)));
            return InvalidListenerHandle;
        }
        GANL_WSELECT_DEBUG(sock, "socket() successful.");

        // SO_REUSEADDR on Windows has slightly different semantics, but generally useful
        char opt = 1; // Use char for Windows setsockopt data
        if (::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == SOCKET_ERROR) {
            error = getLastError();
            GANL_WSELECT_DEBUG(sock, "setsockopt(SO_REUSEADDR) failed: " << getErrorString(translateError(error)));
            closeSocket(sock);
            return InvalidListenerHandle;
        }

        if (!setNonBlocking(sock, error)) {
            GANL_WSELECT_DEBUG(sock, "setNonBlocking failed: " << getErrorString(error));
            closeSocket(sock);
            return InvalidListenerHandle;
        }

        sockaddr_in addr;
        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        InetPtonA(AF_INET, host.empty() ? "0.0.0.0" : host.c_str(), &addr.sin_addr); // Use Winsock specific function

        GANL_WSELECT_DEBUG(sock, "Binding to " << (host.empty() ? "INADDR_ANY" : host.c_str()) << ":" << port);
        if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
            error = getLastError();
            GANL_WSELECT_DEBUG(sock, "bind() failed: " << getErrorString(translateError(error)));
            closeSocket(sock);
            return InvalidListenerHandle;
        }
        GANL_WSELECT_DEBUG(sock, "bind() successful.");

        // Add socket to internal tracking BUT do not add to select sets yet
        // Context is null until startListening is called
        addSocketInternal(sock, SocketInfo{ SocketType::Listener, nullptr, false, false, nullptr, "" });
        GANL_WSELECT_DEBUG(sock, "Listener socket created (FD=" << sock << ").");

        return static_cast<ListenerHandle>(sock);
    }

    bool WSelectNetworkEngine::startListening(ListenerHandle listener, void* listenerContext, ErrorCode& error) {
        SocketFD sock = static_cast<SocketFD>(listener);
        error = 0;
        if (!initialized_) { error = ENXIO; return false; }
        GANL_WSELECT_DEBUG(sock, "Starting listening. Context=" << listenerContext);

        auto sockIt = sockets_.find(sock);
        if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Listener) {
            GANL_WSELECT_DEBUG(sock, "Error: Invalid or non-listener handle.");
            error = EINVAL;
            return false;
        }

        if (::listen(sock, SOMAXCONN) == SOCKET_ERROR) {
            error = getLastError();
            GANL_WSELECT_DEBUG(sock, "listen() failed: " << getErrorString(translateError(error)));
            return false;
        }
        GANL_WSELECT_DEBUG(sock, "listen() successful.");

        // Store context and mark for reading
        sockIt->second.context = listenerContext; // Store listener context here
        sockIt->second.wantRead = true; // Listeners always want to accept (read-ready)

        // Add to master FD sets
        if (masterReadFds_.fd_count < FD_SETSIZE && masterErrorFds_.fd_count < FD_SETSIZE) {
            FD_SET(sock, &masterReadFds_);
            FD_SET(sock, &masterErrorFds_); // Also check errors
        }
        else {
            GANL_WSELECT_DEBUG(sock, "Warning: FD_SETSIZE limit reached, cannot add listener to select sets.");
            error = ENOBUFS; // Or a more specific error
            // Clean up: remove from internal tracking if it cannot be monitored
            removeSocketInternal(sock); // Safe to call even if not fully added to sets
            return false;
        }

        GANL_WSELECT_DEBUG(sock, "Listener started successfully and added to select sets.");
        return true;
    }

    void WSelectNetworkEngine::closeListener(ListenerHandle listener) {
        SocketFD sock = static_cast<SocketFD>(listener);
        if (!initialized_) return; // Avoid issues during shutdown race

        // Check if it exists before trying to remove/close
        if (sockets_.count(sock)) {
             GANL_WSELECT_DEBUG(sock, "Closing listener...");
             removeSocketInternal(sock); // Removes from maps and sets
             closeSocket(sock);
             GANL_WSELECT_DEBUG(sock, "Listener closed and removed.");
        } else {
             GANL_WSELECT_DEBUG(sock, "Attempted to close listener that was not found (already closed?).");
        }
    }

    // --- Connection Management ---

    bool WSelectNetworkEngine::associateContext(ConnectionHandle conn, void* context, ErrorCode& error) {
        SocketFD sock = static_cast<SocketFD>(conn);
        error = 0;
        if (!initialized_) { error = ENXIO; return false; }
        GANL_WSELECT_DEBUG(sock, "Associating context=" << context);

        auto sockIt = sockets_.find(sock);
        if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Connection) {
            GANL_WSELECT_DEBUG(sock, "Error: Cannot associate context, invalid handle or not a connection.");
            error = EINVAL;
            return false;
        }

        sockIt->second.context = context; // Associate ConnectionBase*
        GANL_WSELECT_DEBUG(sock, "Context associated successfully.");
        return true;
    }

    void WSelectNetworkEngine::closeConnection(ConnectionHandle conn) {
        SocketFD sock = static_cast<SocketFD>(conn);
        if (!initialized_) return;

        // Check if it exists before trying to remove/close
        bool socketExists = false;
        {
            socketExists = sockets_.count(sock) > 0;

            if (socketExists) {
                GANL_WSELECT_DEBUG(sock, "Closing connection...");
                removeSocketInternal(sock); // Removes from maps and sets
            } else {
                GANL_WSELECT_DEBUG(sock, "Socket not found in map, likely already closed.");
                return; // Skip further operations if already removed
            }
        }

        // Close the socket with proper error handling
        closeSocket(sock);
        GANL_WSELECT_DEBUG(sock, "Connection closed and removed.");
    }

    // --- I/O Operations ---

    bool WSelectNetworkEngine::postRead(ConnectionHandle conn, IoBuffer& buffer, ErrorCode& error) {
        SocketFD sock = static_cast<SocketFD>(conn);
        error = 0;
        if (!initialized_) { error = ENXIO; return false; }

        auto sockIt = sockets_.find(sock);
        if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Connection) {
            GANL_WSELECT_DEBUG(sock, "Error: postRead on invalid/non-connection handle.");
            error = EINVAL;
            return false;
        }

        // Store the IoBuffer reference
        sockIt->second.activeReadBuffer = &buffer;

        // For select readiness model, we just register interest
        if (!sockIt->second.wantRead) {
            GANL_WSELECT_DEBUG(sock, "postRead: Setting wantRead=true and updating FD sets. IoBuffer@" << &buffer);
            sockIt->second.wantRead = true;
            // Update master FD sets
            if (masterReadFds_.fd_count < FD_SETSIZE && masterErrorFds_.fd_count < FD_SETSIZE) {
                FD_SET(sock, &masterReadFds_);
                FD_SET(sock, &masterErrorFds_); // Monitor for errors if interested in read/write
            }
            else {
                GANL_WSELECT_DEBUG(sock, "Warning: FD_SETSIZE limit reached, cannot add socket to select read set.");
                sockIt->second.wantRead = false; // Revert interest if cannot add
                sockIt->second.activeReadBuffer = nullptr; // Reset buffer reference
                error = ENOBUFS;
                return false;
            }
        }
        else {
            GANL_WSELECT_DEBUG(sock, "postRead: wantRead already true. Updating IoBuffer reference to " << &buffer);
        }
        return true;
    }

    bool WSelectNetworkEngine::postWrite(ConnectionHandle conn, const char* data, size_t length, void* userContext, ErrorCode& error) {
        SocketFD sock = static_cast<SocketFD>(conn);
        error = 0;
        if (!initialized_) { error = ENXIO; return false; }
        GANL_WSELECT_DEBUG(sock, "Posting write interest" << (userContext ? " with user context" : "") << " (data/length ignored by engine).");

        auto sockIt = sockets_.find(sock);
        if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Connection) {
            GANL_WSELECT_DEBUG(sock, "Error: postWrite on invalid/non-connection handle.");
            error = EINVAL;
            return false;
        }

        // Store the user context for the write operation
        sockIt->second.writeUserContext = userContext;
        if (userContext) {
            GANL_WSELECT_DEBUG(sock, "Stored write user context " << userContext);
        }

        // This engine is stateless regarding write data. Just register interest.
        if (!sockIt->second.wantWrite) {
            GANL_WSELECT_DEBUG(sock, "postWrite: Setting wantWrite=true and updating FD sets.");
            sockIt->second.wantWrite = true;
            // Update master FD sets
            if (masterWriteFds_.fd_count < FD_SETSIZE && masterErrorFds_.fd_count < FD_SETSIZE) {
                FD_SET(sock, &masterWriteFds_);
                FD_SET(sock, &masterErrorFds_); // Monitor for errors if interested in read/write
            }
            else {
                GANL_WSELECT_DEBUG(sock, "Warning: FD_SETSIZE limit reached, cannot add socket to select write set.");
                sockIt->second.wantWrite = false; // Revert interest
                sockIt->second.writeUserContext = nullptr; // Clear user context on failure
                error = ENOBUFS;
                return false;
            }
        }
        else {
            GANL_WSELECT_DEBUG(sock, "postWrite: wantWrite already true.");
        }

        GANL_WSELECT_DEBUG(sock, "Write interest registered successfully.");
        return true;
    }

    // Backward compatibility implementation
    bool WSelectNetworkEngine::postWrite(ConnectionHandle conn, const char* data, size_t length, ErrorCode& error) {
        return postWrite(conn, data, length, nullptr, error);
    }


    // --- Event Processing ---

    int WSelectNetworkEngine::processEvents(int timeoutMs, IoEvent* events, int maxEvents) {
        if (!initialized_) {
            GANL_WSELECT_DEBUG(0, "Error: Engine not initialized.");
            return -1;
        }
        if (monitoredSockets_.empty()) {
            // select() with empty sets might behave oddly or block indefinitely.
            // If timeout is 0, return 0. If > 0, sleep.
            // If timeout is negative (infinite), this prevents a busy loop.
            if (timeoutMs == 0) return 0;
            if (timeoutMs > 0) Sleep(timeoutMs); // Use Windows Sleep
            return 0;
        }


        // Prepare timeout
        struct timeval timeout;
        struct timeval* timeoutPtr = nullptr;
        if (timeoutMs >= 0) {
            timeout.tv_sec = timeoutMs / 1000;
            timeout.tv_usec = (timeoutMs % 1000) * 1000;
            timeoutPtr = &timeout;
        }

        // Create local working copies of the master fd_sets
        fd_set workingReadFds = masterReadFds_;
        fd_set workingWriteFds = masterWriteFds_;
        fd_set workingErrorFds = masterErrorFds_;

        // Call select() - first arg is ignored on Windows
        GANL_WSELECT_DEBUG(0, "Calling select() for up to " << monitoredSockets_.size() << " sockets.");
        int nfds = ::select(0, &workingReadFds, &workingWriteFds, &workingErrorFds, timeoutPtr);
        GANL_WSELECT_DEBUG(0, "select() returned " << nfds);

        if (nfds == SOCKET_ERROR) {
            ErrorCode wsaError = getLastError();
            if (wsaError == WSAEINTR) { // Check Windows interrupt code
                GANL_WSELECT_DEBUG(0, "select() interrupted (WSAEINTR).");
                return 0; // Not an error, just return 0 events
            }
            std::cerr << "[WSelect:0] CRITICAL: select() failed: " << getErrorString(translateError(wsaError)) << std::endl;
            // Consider generating error events for all monitored sockets? Or just return -1?
            return -1; // Indicate critical failure
        }

        if (nfds == 0) return 0; // Timeout

        int eventCount = 0;
        std::vector<SocketFD> fdsToCloseOnError;

        // Create a copy of monitored sockets to iterate safely, as processing might remove sockets
        std::vector<SocketFD> currentSockets = monitoredSockets_;

        // Iterate through the sockets that were monitored *before* the select call
        for (SocketFD sock : currentSockets) {
            if (eventCount >= maxEvents) break; // Stop if event buffer full

            // Check if socket is still managed (might have been closed by processing a previous event or error handling)
            auto sockIt = sockets_.find(sock);
            if (sockIt == sockets_.end()) {
                GANL_WSELECT_DEBUG(sock, "Skipping check for socket removed during event processing.");
                continue;
            }
            SocketInfo& info = sockIt->second;
            void* contextPtr = info.context; // Get associated ConnectionBase* or listener context

            bool isReadyRead = FD_ISSET(sock, &workingReadFds);
            bool isReadyWrite = FD_ISSET(sock, &workingWriteFds);
            bool hasError = FD_ISSET(sock, &workingErrorFds);

            if (!isReadyRead && !isReadyWrite && !hasError) continue; // Not ready

            // --- Handle Errors First ---
            if (hasError) {
                int socketError = 0;
                int optLen = sizeof(socketError);
                if (::getsockopt(sock, SOL_SOCKET, SO_ERROR, (char*)&socketError, &optLen) == SOCKET_ERROR) {
                    socketError = getLastError();
                    GANL_WSELECT_DEBUG(sock, "Error selected, getsockopt(SO_ERROR) failed: " << getErrorString(translateError(socketError)));
                }
                else {
                    GANL_WSELECT_DEBUG(sock, "Error selected, getsockopt(SO_ERROR) reports: " << getErrorString(translateError(socketError)));
                }

                // Translate 0 error from getsockopt to something meaningful if possible
                // Often means connection reset or aborted locally.
                ErrorCode finalError = translateError(socketError == 0 ? WSAECONNABORTED : socketError);

                if (eventCount < maxEvents) {
                    IoEvent& ev = events[eventCount++];
                    ev.type = IoEventType::Error;
                    ev.connection = (info.type == SocketType::Connection) ? sock : InvalidConnectionHandle;
                    ev.listener = (info.type == SocketType::Listener) ? sock : InvalidListenerHandle;
                    ev.bytesTransferred = 0;
                    ev.error = finalError;
                    ev.context = contextPtr;
                    GANL_WSELECT_DEBUG(sock, "Generated Error event. Code=" << ev.error);
                }
                fdsToCloseOnError.push_back(sock); // Mark for closure after loop
                continue; // Skip further processing for this errored socket
            }

            // --- Handle Listener Read (Accept) ---
            if (info.type == SocketType::Listener && isReadyRead) {
                GANL_WSELECT_DEBUG(sock, "Listener readable. Accepting connections...");
                while (eventCount < maxEvents) {
                    ErrorCode acceptError = 0;
                    ConnectionHandle newConn = acceptConnection(sock, acceptError); // acceptConnection adds new socket to maps/sets
                    if (newConn != InvalidConnectionHandle) {
                        IoEvent& ev = events[eventCount++];
                        ev.type = IoEventType::Accept;
                        ev.listener = sock;
                        ev.connection = newConn;
                        ev.bytesTransferred = 0; // Not relevant for accept
                        ev.error = 0;
                        ev.context = contextPtr; // Use listener's context for accept event
                        ev.remoteAddress = getRemoteNetworkAddress(newConn); // Set the remote address
                        GANL_WSELECT_DEBUG(sock, "Generated Accept event for new connection " << newConn << " from " << ev.remoteAddress.toString());
                    }
                    else {
                        if (acceptError != WSAEWOULDBLOCK) {
                            GANL_WSELECT_DEBUG(sock, "Error accepting connection: " << getErrorString(acceptError));
                            // Generate a listener error event
                            if (eventCount < maxEvents) {
                                IoEvent& ev = events[eventCount++];
                                ev.type = IoEventType::Error;
                                ev.listener = sock;
                                ev.connection = InvalidConnectionHandle;
                                ev.error = acceptError;
                                ev.context = contextPtr;
                                GANL_WSELECT_DEBUG(sock, "Generated Listener Error event during accept. Code=" << acceptError);
                            }
                             // Should we close the listener on accept error? Maybe not.
                        } // else: WSAEWOULDBLOCK is expected, stop accept loop
                        break; // Stop trying to accept on this listener for this event cycle
                    }
                }
            } // End Listener Handling

            // --- Handle Connection Read ---
            // Must check info.type again as listener handling might fill event buffer
            if (info.type == SocketType::Connection && isReadyRead) {
                GANL_WSELECT_DEBUG(sock, "Connection readable.");
                // Don't clear wantRead here. The Connection must explicitly call postRead(false)
                // or close the connection if it doesn't want more read events.

                // But we can clear the buffer reference after generating the event
                IoBuffer* bufferRef = info.activeReadBuffer;
                info.activeReadBuffer = nullptr; // Reset buffer reference after this event cycle
                if (eventCount < maxEvents) {
                    IoEvent& ev = events[eventCount++];
                    ev.type = IoEventType::Read;
                    ev.connection = sock;
                    ev.listener = InvalidListenerHandle;
                    ev.bytesTransferred = 0; // Readiness: signal ready, don't report bytes
                    ev.error = 0;
                    ev.context = contextPtr; // Connection's context (ConnectionBase*)

                    // Include the IoBuffer in the event
                    if (info.activeReadBuffer) {
                        ev.buffer = info.activeReadBuffer;
                        GANL_WSELECT_DEBUG(sock, "Generated Read event with IoBuffer@" << info.activeReadBuffer);
                    } else {
                        GANL_WSELECT_DEBUG(sock, "Generated Read event without IoBuffer");
                    }
                }
            } // End Connection Read Handling

            // --- Handle Connection Write ---
            // Must check info.type again
            if (info.type == SocketType::Connection && isReadyWrite) {
                GANL_WSELECT_DEBUG(sock, "Connection writable.");

                // Clear write interest *immediately* after detecting readiness.
                // The handler (ConnectionBase) must call postWrite() again if it
                // still has data to send after handling this event.
                info.wantWrite = false;
                FD_CLR(sock, &masterWriteFds_);
                // Update error set monitoring: only monitor if still interested in read or write
                if (!info.wantRead) { // If not interested in read anymore either
                     FD_CLR(sock, &masterErrorFds_);
                }

                if (eventCount < maxEvents) {
                    IoEvent& ev = events[eventCount++];
                    ev.type = IoEventType::Write;
                    ev.connection = sock;
                    ev.listener = InvalidListenerHandle;
                    ev.bytesTransferred = 0; // Readiness: signal ready, don't report bytes
                    ev.error = 0;

                    // Use the write user context if available, otherwise use the connection context
                    if (info.writeUserContext) {
                        ev.context = info.writeUserContext;
                        GANL_WSELECT_DEBUG(sock, "Using write user context " << info.writeUserContext << " for Write event");
                    } else {
                        ev.context = contextPtr;
                    }

                    // Clear the write user context after generating the event
                    sockIt->second.writeUserContext = nullptr;
                    if (info.writeUserContext) {
                        GANL_WSELECT_DEBUG(sock, "Cleared write user context after generating Write event");
                    }

                    GANL_WSELECT_DEBUG(sock, "Generated Write event. Cleared wantWrite and removed from masterWriteFds.");
                }

            } // End Connection Write Handling

        } // end for loop over monitored sockets

        // Close sockets marked due to errors AFTER processing all other events
        for (SocketFD sock : fdsToCloseOnError) {
            // Check if socket still exists before closing (might have been closed by handler)
            auto sockIt = sockets_.find(sock);
            if (sockIt != sockets_.end()) {
                 GANL_WSELECT_DEBUG(sock, "Closing socket " << sock << " due to earlier error.");
                 // Decide whether to call closeConnection or closeListener based on type
                 if (sockIt->second.type == SocketType::Connection) {
                    closeConnection(sock); // Removes from maps and sets
                 } else {
                    closeListener(sock); // Removes from maps and sets
                 }
                // Note: The Error event was already generated above.
                // We could optionally generate a Close event here too, but it might be redundant.
            }
        }

        return eventCount;
    }

    // --- Utility Methods ---

    std::string WSelectNetworkEngine::getRemoteAddress(ConnectionHandle conn) {
        // We can now leverage the NetworkAddress class for consistent formatting
        return getRemoteNetworkAddress(conn).toString();
    }

    NetworkAddress WSelectNetworkEngine::getRemoteNetworkAddress(ConnectionHandle conn) {
        SocketFD sock = static_cast<SocketFD>(conn);
        sockaddr_storage addrStorage;
        int addrLen = sizeof(addrStorage); // Use int for Windows socklen_t equivalent

        if (::getpeername(sock, reinterpret_cast<sockaddr*>(&addrStorage), &addrLen) == SOCKET_ERROR) {
            GANL_WSELECT_DEBUG(sock, "getpeername failed: " << getErrorString(translateError(getLastError())));
            return NetworkAddress(); // Return invalid address
        }

        // Create and return NetworkAddress from the raw sockaddr
        return NetworkAddress(reinterpret_cast<struct sockaddr*>(&addrStorage), static_cast<socklen_t>(addrLen));
    }

    std::string WSelectNetworkEngine::getErrorString(ErrorCode error) {
        // Use FormatMessageA for Windows errors
        char* buffer = nullptr;
        DWORD result = FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            error, // Pass the error code directly
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&buffer,
            0,
            NULL
        );

        if (result == 0 || buffer == nullptr) {
            // If FormatMessage fails, provide a basic string
            return "Winsock error " + std::to_string(error);
        }

        std::string message(buffer);
        LocalFree(buffer);

        // Trim trailing whitespace (often added by FormatMessage)
        size_t endpos = message.find_last_not_of(" \n\r\t");
        if (std::string::npos != endpos) {
            message = message.substr(0, endpos + 1);
        }

        return message + " (" + std::to_string(error) + ")"; // Include code
    }

    // --- Private Helper Methods ---

    bool WSelectNetworkEngine::setNonBlocking(SocketFD sock, ErrorCode& error) {
        u_long mode = 1; // 1 to enable non-blocking
        if (ioctlsocket(sock, FIONBIO, &mode) == SOCKET_ERROR) {
            error = getLastError();
            GANL_WSELECT_DEBUG(sock, "ioctlsocket(FIONBIO) failed: " << getErrorString(translateError(error)));
            return false;
        }
        GANL_WSELECT_DEBUG(sock, "Set non-blocking successfully.");
        return true;
    }

    ConnectionHandle WSelectNetworkEngine::acceptConnection(ListenerHandle listener, ErrorCode& error) {
        SocketFD listenerSock = static_cast<SocketFD>(listener);
        sockaddr_storage clientAddr;
        int clientLen = sizeof(clientAddr);
        error = 0;

        SocketFD clientSock = ::accept(listenerSock, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);

        if (clientSock == INVALID_SOCKET_FD) {
            error = getLastError();
            // Only log non-blocking errors as actual errors
            if (error != WSAEWOULDBLOCK) {
                GANL_WSELECT_DEBUG(listenerSock, "accept() failed: " << getErrorString(translateError(error)));
            }
            return InvalidConnectionHandle;
        }
        GANL_WSELECT_DEBUG(listenerSock, "accept() successful. New socket: " << clientSock);

        // Create a NetworkAddress object from the client address immediately after accept
        NetworkAddress remoteAddr(reinterpret_cast<sockaddr*>(&clientAddr), static_cast<socklen_t>(clientLen));
        GANL_WSELECT_DEBUG(clientSock, "Client address: " << remoteAddr.toString());

        if (!setNonBlocking(clientSock, error)) {
            GANL_WSELECT_DEBUG(clientSock, "Failed to set non-blocking on accepted socket: " << getErrorString(error));
            closeSocket(clientSock);
            return InvalidConnectionHandle;
        }

        // Store socket info and add to master sets
        // New connections initially want read, context is null until associateContext
        SocketInfo newInfo{ SocketType::Connection, nullptr, true, false, nullptr, remoteAddr.toString(), nullptr };
        addSocketInternal(clientSock, newInfo); // Adds to sockets_, monitoredSockets_, and master FD sets

        GANL_WSELECT_DEBUG(clientSock, "New connection registered from " << remoteAddr.toString());
        return static_cast<ConnectionHandle>(clientSock);
    }

    // Add socket to internal maps and master FD sets
    void WSelectNetworkEngine::addSocketInternal(SocketFD sock, SocketInfo info) {
        if (sockets_.count(sock)) {
            GANL_WSELECT_DEBUG(sock, "Warning: Socket already exists in internal map during add.");
            // Optionally update info here if needed, e.g.: sockets_[sock] = info;
            return; // Or proceed to update FD sets based on new info? For now, just return.
        }

        // Check FD_SETSIZE limit before adding to sets or map
        bool canAddRead = info.wantRead && (masterReadFds_.fd_count < FD_SETSIZE);
        bool canAddWrite = info.wantWrite && (masterWriteFds_.fd_count < FD_SETSIZE);
        // Error set needs space if either read or write interest is being added
        bool needErrorSpace = (info.wantRead || info.wantWrite);
        bool canAddError = !needErrorSpace || (masterErrorFds_.fd_count < FD_SETSIZE);

        if ((info.wantRead && !canAddRead) || (info.wantWrite && !canAddWrite) || !canAddError) {
            GANL_WSELECT_DEBUG(sock, "Error: FD_SETSIZE limit reached. Cannot add socket " << sock << " to select sets.");
            // Important: Do not add to sockets_ map or monitoredSockets_ if it can't be monitored
            return;
        }

        sockets_[sock] = info;
        monitoredSockets_.push_back(sock); // Add to list for iteration

        // Update master sets based on initial interest specified in info
        if (info.wantRead) FD_SET(sock, &masterReadFds_);
        if (info.wantWrite) FD_SET(sock, &masterWriteFds_);
        if (needErrorSpace) FD_SET(sock, &masterErrorFds_);

        GANL_WSELECT_DEBUG(sock, "Socket added internally. wantRead=" << info.wantRead << ", wantWrite=" << info.wantWrite);
    }

    // Remove socket from internal maps and master FD sets
    void WSelectNetworkEngine::removeSocketInternal(SocketFD sock) {
        // Clear from FD sets FIRST (using FD_CLR is safe even if not set)
        FD_CLR(sock, &masterReadFds_);
        FD_CLR(sock, &masterWriteFds_);
        FD_CLR(sock, &masterErrorFds_);

        // Remove from main map
        sockets_.erase(sock);
        // Removed listener map, no need to erase from listeners_
        // Removed pending writes map, no need to erase from pendingWrites_

        // Remove from monitored list (efficiently, if possible)
        auto it = std::find(monitoredSockets_.begin(), monitoredSockets_.end(), sock);
        if (it != monitoredSockets_.end()) {
            // More efficient removal for vector if order doesn't matter:
            // *it = monitoredSockets_.back();
            // monitoredSockets_.pop_back();
            // But preserving order might be simpler to reason about, cost is minor unless huge churn
             monitoredSockets_.erase(it);
        }
        GANL_WSELECT_DEBUG(sock, "Socket removed internally.");
    }


    // Map common Winsock errors to POSIX errno values for consistency
    ErrorCode WSelectNetworkEngine::translateError(int wsaError) {
        // Keep the mapping consistent with what application layer might expect
        // based on POSIX standards where possible.
        switch (wsaError) {
            // Direct mappings or close equivalents
        case WSAEWOULDBLOCK:    return EAGAIN; // EWOULDBLOCK often same as EAGAIN on POSIX
        case WSAEINPROGRESS:    return EINPROGRESS;
        case WSAEALREADY:       return EALREADY;
        case WSAENOTSOCK:       return ENOTSOCK;
        case WSAEDESTADDRREQ:   return EDESTADDRREQ;
        case WSAEMSGSIZE:       return EMSGSIZE;
        case WSAEPROTOTYPE:     return EPROTOTYPE;
        case WSAENOPROTOOPT:    return ENOPROTOOPT;
        case WSAEPROTONOSUPPORT:return EPROTONOSUPPORT;
        //case WSAESOCKTNOSUPPORT:return ESOCKTNOSUPPORT; // No direct equivalent? Use EOPNOTSUPP?
        case WSAEOPNOTSUPP:     return EOPNOTSUPP;
        //case WSAEPFNOSUPPORT:   return EPFNOSUPPORT; // No direct equivalent? Use EOPNOTSUPP?
        case WSAEAFNOSUPPORT:   return EAFNOSUPPORT;
        case WSAEADDRINUSE:     return EADDRINUSE;
        case WSAEADDRNOTAVAIL:  return EADDRNOTAVAIL;
        case WSAENETDOWN:       return ENETDOWN;
        case WSAENETUNREACH:    return ENETUNREACH;
        case WSAENETRESET:      return ENETRESET;
        case WSAECONNABORTED:   return ECONNABORTED;
        case WSAECONNRESET:     return ECONNRESET;
        case WSAENOBUFS:        return ENOBUFS;
        case WSAEISCONN:        return EISCONN;
        case WSAENOTCONN:       return ENOTCONN;
        case WSAESHUTDOWN:      return EPIPE; // EPIPE often used for operations on shutdown sockets
        case WSAETIMEDOUT:      return ETIMEDOUT;
        case WSAECONNREFUSED:   return ECONNREFUSED;
        case WSAEHOSTDOWN:      return EHOSTUNREACH; // POSIX might not have EHOSTDOWN
        case WSAEHOSTUNREACH:   return EHOSTUNREACH;
        case WSAEINTR:          return EINTR; // Important for select interruption
        case WSAEINVAL:         return EINVAL; // Argument validity
        case WSAEMFILE:         return EMFILE; // Too many open files/sockets for process
        // Less direct mappings - consider returning original WSA error code?
        case WSAENAMETOOLONG:   return ENAMETOOLONG; // If defined on POSIX system include
        // Consider adding mappings for WSAEACCES, WSAEFAULT if needed
        default:
            // For unmapped errors, maybe return the original WSA error code
            // or a generic error like EIO. Returning original code might be
            // better for Windows-specific debugging. Let's return EIO for now for interface consistency.
            GANL_WSELECT_DEBUG(0, "Warning: Unmapped WSAError " << wsaError << ". Returning EIO.");
            return EIO; // Generic I/O error
            // return wsaError; // Alternative: return original code
        }
    }

} // namespace ganl
