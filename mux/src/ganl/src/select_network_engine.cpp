#include "select_network_engine.h"
#include <iostream>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h> // For timeval
#include <netinet/in.h>
#include <netinet/tcp.h> // Optional
#include <arpa/inet.h>
#include <string.h> // For memset
#include <cerrno> // Use <cerrno>
#include <cstring> // Use <cstring> for strerror
#include <vector>
#include <algorithm> // std::max
#include <mutex>     // Include mutex header

// Define a macro for debug logging
#ifndef NDEBUG // Only compile debug messages if NDEBUG is not defined
#define GANL_SELECT_DEBUG(fd, x) \
    do { std::cerr << "[Select:" << ((fd == -1) ? "CTL" : std::to_string(fd)) << "] " << x << std::endl; } while (0)
#else
#define GANL_SELECT_DEBUG(fd, x) do {} while (0)
#endif

// Platform-specific definitions (Unix for this file)
using SocketFD = int;
const SocketFD INVALID_SOCKET_FD = -1;


namespace ganl {

// Use constexpr for any fixed-size initial values
constexpr int MAX_SOCKET_FDS = FD_SETSIZE;  // Maximum FDs select() can handle

SelectNetworkEngine::SelectNetworkEngine() {
    GANL_SELECT_DEBUG(-1, "Engine Created. Max FDs supported: " << MAX_SOCKET_FDS);
    // Constructor doesn't need to initialize fd_sets if initialize does it
}

SelectNetworkEngine::~SelectNetworkEngine() {
    GANL_SELECT_DEBUG(-1, "Engine Destroyed.");
    shutdown(); // Ensure shutdown is called
}

IoModel SelectNetworkEngine::getIoModelType() const {
    return IoModel::Readiness;
}

// --- Platform Abstraction Helpers ---
void SelectNetworkEngine::closeSocket(SocketFD fd) {
    // Attempt graceful shutdown first
    if (::shutdown(fd, SHUT_RDWR) == -1) {
        if (errno != ENOTCONN && errno != EBADF) {
            GANL_SELECT_DEBUG(fd, "Warning: shutdown(fd, SHUT_RDWR) failed: " << strerror(errno));
        } else {
            GANL_SELECT_DEBUG(fd, "Socket already disconnected or invalid.");
        }
    }

    // Then close the file descriptor
    if (::close(fd) == -1) {
        if (errno == EBADF) {
            GANL_SELECT_DEBUG(fd, "File descriptor already closed or invalid.");
        } else {
            GANL_SELECT_DEBUG(fd, "Warning: close(fd) failed: " << strerror(errno));
        }
    } else {
        GANL_SELECT_DEBUG(fd, "close(fd) successful.");
    }
}

ErrorCode SelectNetworkEngine::getLastError() {
    return errno;
}
// --- End Platform Abstraction ---


bool SelectNetworkEngine::initialize() {
    std::lock_guard<std::mutex> lock(mutex_); // Lock for initialization state
    GANL_SELECT_DEBUG(-1, "Initializing...");

    if (initialized_) {
        GANL_SELECT_DEBUG(-1, "Already initialized.");
        return true;
    }

    // Reset all master fd_sets
    FD_ZERO(&masterReadFds_);
    FD_ZERO(&masterWriteFds_);
    FD_ZERO(&masterErrorFds_);
    maxFd_ = -1;

    initialized_ = true;
    GANL_SELECT_DEBUG(-1, "Initialization successful.");
    return true;
}

void SelectNetworkEngine::shutdown() {
    GANL_SELECT_DEBUG(-1, "Shutdown requested.");

    // Retrieve handles under lock
    std::vector<SocketFD> listenerHandles;
    std::vector<SocketFD> connectionHandles;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            GANL_SELECT_DEBUG(-1, "Already shut down or not initialized.");
            return; // Exit early if not initialized
        }

        listenerHandles.reserve(sockets_.size()); // Reserve generously
        connectionHandles.reserve(sockets_.size());
        for (const auto& pair : sockets_) {
            if (pair.second.type == SocketType::Listener) {
                listenerHandles.push_back(pair.first);
            } else {
                connectionHandles.push_back(pair.first);
            }
        }
        // Mark as uninitialized *before* starting to close FDs to prevent re-entry issues
        initialized_ = false;
    } // Release lock before closing FDs

    // Close listeners (closeListener acquires its own lock)
    GANL_SELECT_DEBUG(-1, "Closing " << listenerHandles.size() << " listeners...");
    for (SocketFD handle : listenerHandles) {
        closeListener(static_cast<ListenerHandle>(handle));
    }

    // Close connections (closeConnection acquires its own lock)
    GANL_SELECT_DEBUG(-1, "Closing " << connectionHandles.size() << " connections...");
    for (SocketFD handle : connectionHandles) {
        closeConnection(static_cast<ConnectionHandle>(handle));
    }

    // Final cleanup of internal state (already marked uninitialized)
    {
       std::lock_guard<std::mutex> lock(mutex_); // Lock for final cleanup
       sockets_.clear(); // Ensure maps are cleared
       // pendingWrites_ map is removed
       FD_ZERO(&masterReadFds_);
       FD_ZERO(&masterWriteFds_);
       FD_ZERO(&masterErrorFds_);
       maxFd_ = -1;
    }

    GANL_SELECT_DEBUG(-1, "Shutdown complete.");
}

// --- Listener Management ---

ListenerHandle SelectNetworkEngine::createListener(const std::string& host, uint16_t port, ErrorCode& error) {
    GANL_SELECT_DEBUG(-1, "Creating listener for " << host << ":" << port);
    error = 0;

    // --- Perform syscalls outside lock ---
    SocketFD fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET_FD) {
        error = getLastError();
        GANL_SELECT_DEBUG(-1, "socket() failed: " << getErrorString(error));
        return InvalidListenerHandle;
    }
    GANL_SELECT_DEBUG(fd, "socket() successful.");

    int opt = 1;
    if (::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        error = getLastError();
        GANL_SELECT_DEBUG(fd, "setsockopt(SO_REUSEADDR) failed: " << getErrorString(error));
        closeSocket(fd);
        return InvalidListenerHandle;
    }

    if (!setNonBlocking(fd, error)) {
        GANL_SELECT_DEBUG(fd, "setNonBlocking failed: " << getErrorString(error));
        closeSocket(fd);
        return InvalidListenerHandle;
    }

    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            error = EINVAL;
            GANL_SELECT_DEBUG(fd, "inet_pton() failed for host: " << host);
            closeSocket(fd);
            return InvalidListenerHandle;
        }
    }

    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        error = getLastError();
        GANL_SELECT_DEBUG(fd, "bind() failed: " << getErrorString(error));
        closeSocket(fd);
        return InvalidListenerHandle;
    }
    GANL_SELECT_DEBUG(fd, "bind() successful.");
    // --- End syscalls ---

    // Store basic socket info under lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
             // Engine shut down between syscalls and lock acquisition
             GANL_SELECT_DEBUG(fd, "Engine shut down during listener creation. Aborting.");
             closeSocket(fd);
             error = ESHUTDOWN; // Indicate shutdown
             return InvalidListenerHandle;
        }
        sockets_[fd] = SocketInfo{SocketType::Listener, nullptr, false, false};
        // Don't update FD sets or maxFd until startListening
    }
    GANL_SELECT_DEBUG(fd, "Listener socket created (FD=" << fd << ").");

    return static_cast<ListenerHandle>(fd);
}

bool SelectNetworkEngine::startListening(ListenerHandle listener, void* listenerContext, ErrorCode& error) {
    SocketFD fd = static_cast<SocketFD>(listener);
    error = 0;
    GANL_SELECT_DEBUG(fd, "Starting listening. Context=" << listenerContext);

    // --- Perform syscall outside lock ---
    if (::listen(fd, SOMAXCONN) == -1) {
        error = getLastError();
        GANL_SELECT_DEBUG(fd, "listen() failed: " << getErrorString(error));
        return false;
    }
    GANL_SELECT_DEBUG(fd, "listen() successful.");
    // --- End syscall ---

    // Store context and update FD sets under lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
             GANL_SELECT_DEBUG(fd, "Engine shut down during startListening. Aborting.");
             error = ESHUTDOWN;
             return false;
        }
        auto sockIt = sockets_.find(fd);
        if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Listener) {
            GANL_SELECT_DEBUG(fd, "Error: Invalid or non-listener handle.");
            error = EINVAL;
            return false;
        }
        // Store context directly in SocketInfo
        sockIt->second.context = listenerContext;
        sockIt->second.wantRead = true; // Listeners always want to read (accept)
        // Listeners don't want write
        sockIt->second.wantWrite = false;

        updateFdSets(fd, sockIt->second); // Use locked version
    }

    GANL_SELECT_DEBUG(fd, "Listener started successfully.");
    return true;
}

void SelectNetworkEngine::closeListener(ListenerHandle listener) {
    SocketFD fd = static_cast<SocketFD>(listener);
    GANL_SELECT_DEBUG(fd, "Closing listener...");

    // Remove from tracking under lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_ && sockets_.find(fd) == sockets_.end()) {
             GANL_SELECT_DEBUG(fd, "Listener already removed or engine shutdown.");
             return; // Avoid closing FD twice if shutdown already handled it
        }
        removeFd(fd); // Use locked version
    } // Release lock

    // Close the socket descriptor outside lock
    closeSocket(fd);

    GANL_SELECT_DEBUG(fd, "Listener closed and removed.");
}

// --- Connection Management ---

bool SelectNetworkEngine::associateContext(ConnectionHandle conn, void* context, ErrorCode& error) {
    SocketFD fd = static_cast<SocketFD>(conn);
    error = 0;
    GANL_SELECT_DEBUG(fd, "Associating context=" << context);

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        error = ESHUTDOWN;
        return false;
    }
    auto sockIt = sockets_.find(fd);
    if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Connection) {
        GANL_SELECT_DEBUG(fd, "Error: Cannot associate context, invalid handle or not a connection.");
        error = EINVAL;
        return false;
    }

    sockIt->second.context = context;
    GANL_SELECT_DEBUG(fd, "Context associated successfully.");
    return true;
}

void SelectNetworkEngine::closeConnection(ConnectionHandle conn) {
    SocketFD fd = static_cast<SocketFD>(conn);
    GANL_SELECT_DEBUG(fd, "Closing connection...");

    // Remove from tracking under lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
         if (!initialized_ && sockets_.find(fd) == sockets_.end()) {
             GANL_SELECT_DEBUG(fd, "Connection already removed or engine shutdown.");
             return; // Avoid closing FD twice
        }
        removeFd(fd); // Use locked version
    } // Release lock

    // Close the socket descriptor outside lock
    closeSocket(fd);

    GANL_SELECT_DEBUG(fd, "Connection closed and removed.");
}

// --- I/O Operations ---

bool SelectNetworkEngine::postRead(ConnectionHandle conn, IoBuffer& buffer, ErrorCode& error) {
    SocketFD fd = static_cast<SocketFD>(conn);
    error = 0;
    GANL_SELECT_DEBUG(fd, "postRead(IoBuffer) called.");

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        error = ESHUTDOWN;
        return false;
    }
    auto sockIt = sockets_.find(fd);
    if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Connection) {
        GANL_SELECT_DEBUG(fd, "Error: postRead on invalid/non-connection handle.");
        error = EINVAL;
        return false;
    }

    // Store buffer reference
    sockIt->second.activeReadBuffer = &buffer;
    GANL_SELECT_DEBUG(fd, "Stored IoBuffer reference " << &buffer << " for connection " << fd);

    // Ensure read interest is registered
    if (!sockIt->second.wantRead) {
        GANL_SELECT_DEBUG(fd, "Setting wantRead=true and updating FD sets.");
        sockIt->second.wantRead = true;
        updateFdSets(fd, sockIt->second); // Use locked version
    } else {
         GANL_SELECT_DEBUG(fd, "wantRead already true.");
    }

    return true;
}

bool SelectNetworkEngine::postWrite(ConnectionHandle conn, const char* data, size_t length, void* userContext, ErrorCode& error) {
    SocketFD fd = static_cast<SocketFD>(conn);
    error = 0;
    GANL_SELECT_DEBUG(fd, "postWrite called to register write interest with context " << userContext);

    std::lock_guard<std::mutex> lock(mutex_);
    if (!initialized_) {
        error = ESHUTDOWN;
        return false;
    }
    auto sockIt = sockets_.find(fd);
    if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Connection) {
        GANL_SELECT_DEBUG(fd, "Error: postWrite on invalid/non-connection handle.");
        error = EINVAL;
        return false;
    }

    // Store the user context for the write operation
    sockIt->second.writeUserContext = userContext;
    GANL_SELECT_DEBUG(fd, "Stored write user context " << userContext << " for connection " << fd);

    // Ensure write interest is set
    if (!sockIt->second.wantWrite) {
        GANL_SELECT_DEBUG(fd, "Setting wantWrite=true and updating FD sets.");
        sockIt->second.wantWrite = true;
        updateFdSets(fd, sockIt->second); // Use locked version
    } else {
         GANL_SELECT_DEBUG(fd, "wantWrite already true, updating context.");
    }

    GANL_SELECT_DEBUG(fd, "Write interest registered successfully with context " << userContext);
    return true;
}

// --- Event Processing ---

int SelectNetworkEngine::processEvents(int timeoutMs, IoEvent* events, int maxEvents) {
    fd_set currentWorkingReadFds;
    fd_set currentWorkingWriteFds;
    fd_set currentWorkingErrorFds;
    SocketFD currentMaxFd;

    // Prepare fd_sets and maxFd under lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            GANL_SELECT_DEBUG(-1, "Warning: processEvents called on uninitialized/shutdown engine.");
            return 0; // Or -1? Let's return 0 as no events processed.
        }
        // Copy master sets to working sets for select() call
        currentWorkingReadFds = masterReadFds_;
        currentWorkingWriteFds = masterWriteFds_;
        currentWorkingErrorFds = masterErrorFds_;
        currentMaxFd = maxFd_;
    } // Release lock before potentially blocking select() call

    // Prepare timeout structure
    struct timeval timeout;
    struct timeval* timeoutPtr = nullptr;
    if (timeoutMs >= 0) {
        timeout.tv_sec = timeoutMs / 1000;
        timeout.tv_usec = (timeoutMs % 1000) * 1000;
        timeoutPtr = &timeout;
    }

    // Call select()
    int nfds = ::select(currentMaxFd + 1, &currentWorkingReadFds, &currentWorkingWriteFds, &currentWorkingErrorFds, timeoutPtr);

    if (nfds == -1) {
        if (errno == EINTR) {
            GANL_SELECT_DEBUG(-1, "select() interrupted by signal (EINTR).");
            return 0; // Interrupted, no events processed
        }
        std::cerr << "[Select:CTL] CRITICAL: select() failed: " << getErrorString(getLastError()) << std::endl;
        // Consider more drastic action? For now, return error.
        return -1;
    }

    if (nfds == 0) {
        // Timeout
        return 0;
    }

    int eventCount = 0;
    std::vector<std::pair<SocketFD, void*>> fdsToClose; // Store FD and context

    // --- Process ready FDs ---
    // Iterate only up to the maxFd *read before the select call*.
    // New FDs added concurrently will be processed in the next iteration.
    for (SocketFD fd = 0; fd <= currentMaxFd && nfds > 0 && eventCount < maxEvents; ++fd) {

        bool isReadyRead = FD_ISSET(fd, &currentWorkingReadFds);
        bool isReadyWrite = FD_ISSET(fd, &currentWorkingWriteFds);
        bool hasError = FD_ISSET(fd, &currentWorkingErrorFds);

        if (!isReadyRead && !isReadyWrite && !hasError) {
            continue; // This FD is not ready or not in our set
        }

        nfds--; // Found a ready FD we might process

        // --- Get socket info and context under lock ---
        SocketInfo infoCopy;
        bool socketFound = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!initialized_) continue; // Check if engine was shut down during select
            auto sockIt = sockets_.find(fd);
            if (sockIt != sockets_.end()) {
                infoCopy = sockIt->second; // Make a copy
                socketFound = true;
            } else {
                 // FD was ready but removed concurrently? Clear from master sets just in case.
                 FD_CLR(fd, &masterReadFds_);
                 FD_CLR(fd, &masterWriteFds_);
                 FD_CLR(fd, &masterErrorFds_);
                 // No need to update maxFd here, removeFd handles it if called later
            }
        } // --- Release lock ---

        if (!socketFound) {
            GANL_SELECT_DEBUG(fd, "Warning: select returned ready for FD not found in map. Ignoring.");
            continue;
        }

        void* contextPtr = infoCopy.context; // Use copied context

        // --- Handle Errors First ---
        if (hasError) {
            int socketError = 0;
            socklen_t errLen = sizeof(socketError);
            // Perform getsockopt outside lock
            if (::getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &errLen) == -1) {
                socketError = getLastError();
                GANL_SELECT_DEBUG(fd, "Error detected by select(), getsockopt(SO_ERROR) failed: " << getErrorString(socketError));
            } else {
                 GANL_SELECT_DEBUG(fd, "Error detected by select(), getsockopt(SO_ERROR) reports: " << getErrorString(socketError));
                 if (socketError == 0) socketError = EPIPE; // Treat 0 as generic error if select flagged it
            }

             if (eventCount < maxEvents) {
                 IoEvent& ev = events[eventCount++];
                 ev.type = IoEventType::Error;
                 ev.connection = (infoCopy.type == SocketType::Connection) ? fd : InvalidConnectionHandle;
                 ev.listener = (infoCopy.type == SocketType::Listener) ? fd : InvalidListenerHandle;
                 ev.bytesTransferred = 0;
                 ev.error = socketError;
                 ev.context = contextPtr;
                 ev.buffer = (infoCopy.type == SocketType::Connection) ? infoCopy.activeReadBuffer : nullptr;
                 GANL_SELECT_DEBUG(fd, "Generated Error event. Code=" << ev.error << ", Buffer=" << ev.buffer);
            }
            // Mark for closure after processing this event cycle
            fdsToClose.push_back({fd, contextPtr});
            continue; // Don't process read/write if error occurred
        }

        // --- Handle Listener Read (Accept) ---
        if (infoCopy.type == SocketType::Listener && isReadyRead) {
            GANL_SELECT_DEBUG(fd, "Listener readable. Accepting connections...");
            // Loop accept until EAGAIN/EWOULDBLOCK (acceptConnection is non-blocking)
            while (eventCount < maxEvents) {
                 ErrorCode acceptError = 0;
                 // acceptConnection performs syscalls outside lock, adds to map/sets under its own lock
                 ConnectionHandle newConn = acceptConnection(fd, acceptError);

                 if (newConn != InvalidConnectionHandle) {
                     GANL_SELECT_DEBUG(fd, "Accepted new connection handle: " << newConn);
                     IoEvent& ev = events[eventCount++];
                     ev.type = IoEventType::Accept;
                     ev.listener = fd;
                     ev.connection = newConn;
                     ev.context = contextPtr; // Listener's context
                     ev.bytesTransferred = 0;
                     ev.error = 0;
                     ev.buffer = nullptr; // Accept events don't have associated buffers
                     ev.remoteAddress = getRemoteNetworkAddress(newConn); // Set the remote address
                 } else {
                     if (acceptError != EAGAIN && acceptError != EWOULDBLOCK) {
                         GANL_SELECT_DEBUG(fd, "Error accepting connection: " << getErrorString(acceptError));
                         if (eventCount < maxEvents) {
                            IoEvent& ev = events[eventCount++];
                            ev.type = IoEventType::Error;
                            ev.listener = fd;
                            ev.connection = InvalidConnectionHandle;
                            ev.bytesTransferred = 0;
                            ev.error = acceptError;
                            ev.context = contextPtr;
                            ev.buffer = nullptr; // Listener errors don't have associated buffers
                         }
                         // Mark listener for closure? Or just report error? Report error for now.
                         // fdsToClose.push_back({fd, contextPtr});
                     } else {
                         // Accept queue empty for now
                     }
                     break; // Stop trying to accept on this listener for this event cycle
                 }
            }
            // Don't continue to connection handling for this FD
            continue;
        }

        // --- Handle Connection Read ---
        if (infoCopy.type == SocketType::Connection && isReadyRead) {
            GANL_SELECT_DEBUG(fd, "Connection readable.");
            // Only generate event if maxEvents not reached
            if (eventCount < maxEvents) {
                IoEvent& ev = events[eventCount++];
                ev.type = IoEventType::Read;
                ev.connection = fd;
                ev.context = contextPtr;
                ev.bytesTransferred = 0; // Indicate readiness
                ev.error = 0;
                ev.buffer = infoCopy.activeReadBuffer; // Include IoBuffer reference if available
                GANL_SELECT_DEBUG(fd, "Generated Read event. Buffer=" << ev.buffer);

                // Clear the buffer reference after generating the event
                if (infoCopy.activeReadBuffer) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto sockIt = sockets_.find(fd);
                    if (sockIt != sockets_.end()) {
                        sockIt->second.activeReadBuffer = nullptr;
                        GANL_SELECT_DEBUG(fd, "Cleared activeReadBuffer after generating Read event");
                    }
                }

                // Connection::handleRead will perform the actual read()
                // Do NOT clear wantRead here for level-triggered select
            } else {
                 GANL_SELECT_DEBUG(fd, "Max events reached, skipping Read event generation.");
            }
        }

        // --- Handle Connection Write ---
        if (infoCopy.type == SocketType::Connection && isReadyWrite) {
            GANL_SELECT_DEBUG(fd, "Connection writable.");
            bool generateWriteEvent = false;
            // Must check and clear wantWrite atomically under lock
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!initialized_) continue; // Re-check initialization state
                auto sockIt = sockets_.find(fd);
                if (sockIt != sockets_.end() && sockIt->second.wantWrite) {
                     // Clear write interest immediately to prevent busy loop
                     sockIt->second.wantWrite = false;
                     updateFdSets(fd, sockIt->second); // Update master sets
                     generateWriteEvent = true; // Flag that we should generate the event
                     GANL_SELECT_DEBUG(fd, "Cleared wantWrite and updated fd sets.");
                } else {
                      GANL_SELECT_DEBUG(fd, "Write ready, but wantWrite was already false or socket removed.");
                }
            } // Release lock

            // Generate event outside the lock if flagged
            if (generateWriteEvent && eventCount < maxEvents) {
                IoEvent& ev = events[eventCount++];
                ev.type = IoEventType::Write;
                ev.connection = fd;

                // Use the write user context if available, otherwise use the connection context
                void* writeContext = infoCopy.writeUserContext;
                if (writeContext) {
                    ev.context = writeContext;
                    GANL_SELECT_DEBUG(fd, "Using write user context " << writeContext << " for Write event");
                } else {
                    ev.context = contextPtr;
                    GANL_SELECT_DEBUG(fd, "No write user context, using connection context " << contextPtr);
                }

                ev.bytesTransferred = 0; // Indicate readiness
                ev.error = 0;
                ev.buffer = nullptr; // Write events don't use buffer reference
                GANL_SELECT_DEBUG(fd, "Generated Write event with context " << ev.context);

                // Also clear the user context since we're done with this write operation
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto sockIt = sockets_.find(fd);
                    if (sockIt != sockets_.end()) {
                        sockIt->second.writeUserContext = nullptr;
                    }
                }

                // Connection::handleWrite will perform actual write() and call postWrite again if needed
            } else if (generateWriteEvent) {
                 GANL_SELECT_DEBUG(fd, "Max events reached, skipping Write event generation (but wantWrite cleared).");
            }
        }

    } // end for loop through FDs

    // Close connections that encountered errors
    for (const auto& pair : fdsToClose) {
         SocketFD fdToClose = pair.first;
         void* savedContext = pair.second;
         GANL_SELECT_DEBUG(fdToClose, "Closing connection due to earlier error.");
         // closeConnection acquires its own lock to remove from maps/sets
         closeConnection(static_cast<ConnectionHandle>(fdToClose));

         // Generate a Close event AFTER closing locally
         if (eventCount < maxEvents) {
              IoEvent& ev = events[eventCount++];
              ev.type = IoEventType::Close;
              ev.connection = fdToClose;
              ev.context = savedContext; // Use saved context
              ev.bytesTransferred = 0;
              ev.error = 0; // Error was reported previously
              // Note: Buffer reference is already handled in the initial Error event
              ev.buffer = nullptr; // Socket is already closed, no buffer reference available
              GANL_SELECT_DEBUG(fdToClose, "Generated Close event after error.");
         }
    }

    return eventCount;
}

// --- Utility Methods ---

std::string SelectNetworkEngine::getRemoteAddress(ConnectionHandle conn) {
    // We can leverage the NetworkAddress class for consistent formatting
    return getRemoteNetworkAddress(conn).toString();
}

NetworkAddress SelectNetworkEngine::getRemoteNetworkAddress(ConnectionHandle conn) {
    SocketFD fd = static_cast<SocketFD>(conn);
    sockaddr_storage addrStorage;
    socklen_t addrLen = sizeof(addrStorage);

    // Perform syscall outside lock
    if (::getpeername(fd, reinterpret_cast<sockaddr*>(&addrStorage), &addrLen) == -1) {
        // Log error potentially based on errno
        return NetworkAddress(); // Return invalid address
    }

    // Create and return NetworkAddress from the raw sockaddr
    return NetworkAddress(reinterpret_cast<sockaddr*>(&addrStorage), addrLen);
}

std::string SelectNetworkEngine::getErrorString(ErrorCode error) {
    return strerror(error);
}

// --- Private Helper Methods ---

// Assumes called WITHOUT lock held
bool SelectNetworkEngine::setNonBlocking(SocketFD fd, ErrorCode& error) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        error = getLastError();
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        error = getLastError();
        return false;
    }
    return true;
}

// Assumes called WITHOUT lock held for accept() call
// Acquires lock internally to update maps/sets
ConnectionHandle SelectNetworkEngine::acceptConnection(ListenerHandle listener, ErrorCode& error) {
    SocketFD listenerFd = static_cast<SocketFD>(listener);
    sockaddr_storage clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    error = 0;

    // Perform accept syscall outside lock
    SocketFD clientFd = ::accept(listenerFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);

    if (clientFd == INVALID_SOCKET_FD) {
        error = getLastError();
        return InvalidConnectionHandle;
    }
    GANL_SELECT_DEBUG(listenerFd, "accept() successful. New FD: " << clientFd);

    // Create a NetworkAddress object from the client address immediately after accept
    NetworkAddress remoteAddr(reinterpret_cast<sockaddr*>(&clientAddr), clientLen);
    GANL_SELECT_DEBUG(clientFd, "Client address: " << remoteAddr.toString());

    // Perform non-blocking set outside lock
    if (!setNonBlocking(clientFd, error)) {
        GANL_SELECT_DEBUG(clientFd, "Failed to set non-blocking on accepted socket: " << getErrorString(error));
        closeSocket(clientFd);
        return InvalidConnectionHandle;
    }

    // Add to maps and FD sets under lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
             GANL_SELECT_DEBUG(clientFd, "Engine shut down during accept processing. Closing new socket.");
             error = ESHUTDOWN;
             closeSocket(clientFd); // Close the newly accepted socket
             return InvalidConnectionHandle;
        }
        // New connections always want read initially, never write until requested
        SocketInfo newInfo{SocketType::Connection, nullptr, true, false, nullptr, nullptr};
        sockets_[clientFd] = newInfo;
        updateFdSets(clientFd, newInfo); // Use locked version
    }

    GANL_SELECT_DEBUG(clientFd, "New connection registered.");
    return static_cast<ConnectionHandle>(clientFd);
}


// MUST be called with mutex_ HELD
void SelectNetworkEngine::updateMaxFd() {
    if (sockets_.empty()) {
        maxFd_ = -1;
    } else {
        // std::map keys are sorted, last element has the highest key
        maxFd_ = sockets_.rbegin()->first;
    }
}

// MUST be called with mutex_ HELD
void SelectNetworkEngine::updateFdSets(SocketFD fd, const SocketInfo& info) {
     // Read set
     if (info.wantRead) FD_SET(fd, &masterReadFds_);
     else FD_CLR(fd, &masterReadFds_);

     // Write set
     if (info.wantWrite) FD_SET(fd, &masterWriteFds_);
     else FD_CLR(fd, &masterWriteFds_);

     // Error set (always check errors for active sockets)
     // (Technically only need to check if wantRead or wantWrite is true, but checking all is safer)
     FD_SET(fd, &masterErrorFds_); // Always check errors for sockets we manage

     // Update maxFd if this FD is higher than current max
     if (fd > maxFd_) {
         maxFd_ = fd;
         // GANL_SELECT_DEBUG(fd, "Updated maxFd_ to " << maxFd_);
     }
}

// MUST be called with mutex_ HELD
void SelectNetworkEngine::removeFd(SocketFD fd) {
     // Clear from FD sets
     FD_CLR(fd, &masterReadFds_);
     FD_CLR(fd, &masterWriteFds_);
     FD_CLR(fd, &masterErrorFds_);

     // Remove from maps
     sockets_.erase(fd);
     // listeners_ map is removed
     // pendingWrites_ map is removed

     // Update maxFd if we removed the highest one
     if (fd == maxFd_) {
         updateMaxFd();
         GANL_SELECT_DEBUG(fd, "Recalculated maxFd_ to " << maxFd_ << " after removing " << fd);
     }
}


} // namespace ganl
