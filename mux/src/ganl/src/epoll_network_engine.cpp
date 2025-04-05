#include "epoll_network_engine.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_NODELAY potentially
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring> // For strerror, memset
#include <cerrno>  // For errno constants
#include <vector>  // For key iteration during shutdown
#include <mutex>

// Define a macro for debug logging
#ifndef NDEBUG // Only compile debug messages if NDEBUG is not defined
#define GANL_EPOLL_DEBUG(fd, x) \
    do { std::cerr << "[Epoll:" << (fd == epollFd_ ? "CTL" : std::to_string(fd)) << "] " << x << std::endl; } while (0)
#else
#define GANL_EPOLL_DEBUG(fd, x) do {} while (0)
#endif


namespace ganl {

// Use constexpr for initial epoll events size
constexpr size_t INITIAL_EPOLL_EVENTS_SIZE = 128;

EpollNetworkEngine::EpollNetworkEngine()
    : epollEvents_(INITIAL_EPOLL_EVENTS_SIZE) { // Pre-allocate epoll event array
    GANL_EPOLL_DEBUG(0, "Engine Created.");
}

EpollNetworkEngine::~EpollNetworkEngine() {
    GANL_EPOLL_DEBUG(0, "Engine Destroyed.");
    shutdown();
}

bool EpollNetworkEngine::initialize() {
    GANL_EPOLL_DEBUG(0, "Initializing...");
    if (epollFd_ != -1) {
        GANL_EPOLL_DEBUG(0, "Already initialized.");
        return true;
    }
    // CLOEXEC prevents the epoll FD from leaking into child processes
    epollFd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epollFd_ == -1) {
        std::cerr << "[Epoll:CTL] FATAL: Failed to create epoll instance: " << strerror(errno) << std::endl;
        return false;
    }
    GANL_EPOLL_DEBUG(epollFd_, "epoll_create1 successful.");
    return true;
}

void EpollNetworkEngine::shutdown() {
    GANL_EPOLL_DEBUG(0, "Shutdown requested.");
    if (epollFd_ == -1) {
        GANL_EPOLL_DEBUG(0, "Already shut down.");
        return;
    }

    // Use a temporary lock to safely copy handles
    std::vector<ListenerHandle> listenerHandles;
    std::vector<ConnectionHandle> connectionHandles;
    {
        std::lock_guard<std::mutex> lock(mutex_); // Lock for map access
        listenerHandles.reserve(listeners_.size());
        for (const auto& pair : listeners_) {
            listenerHandles.push_back(pair.first);
        }

        connectionHandles.reserve(sockets_.size()); // Reserve potentially more than needed
        for (const auto& pair : sockets_) {
            // Check if it's not a listener FD
            if (listeners_.find(pair.first) == listeners_.end()) {
                connectionHandles.push_back(pair.first);
            }
        }
    } // Mutex released here

    GANL_EPOLL_DEBUG(0, "Closing " << listenerHandles.size() << " listeners...");
    for (ListenerHandle handle : listenerHandles) {
        closeListener(handle); // closeListener handles its own locking for map removal
    }

    GANL_EPOLL_DEBUG(0, "Closing " << connectionHandles.size() << " connections...");
    for (ConnectionHandle handle : connectionHandles) {
        closeConnection(handle); // closeConnection handles its own locking for map removal
    }

    // Clear maps after closing FDs (lock needed)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        listeners_.clear();
        sockets_.clear();
    }

    GANL_EPOLL_DEBUG(epollFd_, "Closing epoll FD.");
    close(epollFd_);
    epollFd_ = -1;
    GANL_EPOLL_DEBUG(0, "Shutdown complete.");
}

// --- Listener Management ---

ListenerHandle EpollNetworkEngine::createListener(const std::string& host, uint16_t port, ErrorCode& error) {
    GANL_EPOLL_DEBUG(0, "Creating listener for " << host << ":" << port);
    error = 0; // Clear error initially

    // Use SOCK_CLOEXEC to prevent leaking sockets on exec
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd == -1) {
        error = errno;
        GANL_EPOLL_DEBUG(0, "socket() failed: " << strerror(error));
        return InvalidListenerHandle;
    }
    GANL_EPOLL_DEBUG(fd, "socket() successful.");

    // Set socket options: SO_REUSEADDR allows faster restarts
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        error = errno;
        GANL_EPOLL_DEBUG(fd, "setsockopt(SO_REUSEADDR) failed: " << strerror(error));
        close(fd);
        return InvalidListenerHandle;
    }
    GANL_EPOLL_DEBUG(fd, "setsockopt(SO_REUSEADDR) successful.");

    // Set non-blocking (already done via SOCK_NONBLOCK, but portable way shown below)
    // if (!setNonBlocking(fd, error)) {
    //    GANL_EPOLL_DEBUG(fd, "setNonBlocking failed: " << strerror(error));
    //    close(fd);
    //    return InvalidListenerHandle;
    // }

    // Bind to address
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
        GANL_EPOLL_DEBUG(fd, "Binding to INADDR_ANY:" << port);
    } else {
        GANL_EPOLL_DEBUG(fd, "Binding to " << host << ":" << port);
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            // error = errno; // inet_pton doesn't reliably set errno on failure?
            error = EINVAL; // Use generic invalid argument
            GANL_EPOLL_DEBUG(fd, "inet_pton() failed for host: " << host);
            close(fd);
            return InvalidListenerHandle;
        }
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        error = errno;
        GANL_EPOLL_DEBUG(fd, "bind() failed: " << strerror(error));
        close(fd);
        return InvalidListenerHandle;
    }
    GANL_EPOLL_DEBUG(fd, "bind() successful.");

    // Store basic info under lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_[fd] = SocketInfo{SocketType::Listener, context: nullptr, events: 0};
    }
    GANL_EPOLL_DEBUG(fd, "Listener created successfully.");

    return static_cast<ListenerHandle>(fd);
}

bool EpollNetworkEngine::startListening(ListenerHandle listener, void* listenerContext, ErrorCode& error) {
    int fd = static_cast<int>(listener);
    error = 0;
    GANL_EPOLL_DEBUG(fd, "Starting listening. Context=" << listenerContext);

    // Check socket validity and store listener context under lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto sockIt = sockets_.find(fd);
        if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Listener) {
            GANL_EPOLL_DEBUG(fd, "Error: Invalid or non-listener handle.");
            error = EINVAL;
            return false;
        }
        // Store context
        listeners_[listener] = ListenerInfo{context: listenerContext};
        sockIt->second.context = listenerContext; // Update main map too
    } // Mutex released

    // --- Perform syscalls outside the lock ---
    if (listen(fd, SOMAXCONN) == -1) {
        error = errno;
        GANL_EPOLL_DEBUG(fd, "listen() failed: " << strerror(error));
        // Clean up listener entry? Or let caller handle? Let caller handle.
        return false;
    }
    GANL_EPOLL_DEBUG(fd, "listen() successful.");

    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = EPOLLIN;
    event.data.fd = fd;

    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) == -1) {
        error = errno;
        GANL_EPOLL_DEBUG(fd, "epoll_ctl(ADD) failed: " << strerror(error));
        return false;
    }
    GANL_EPOLL_DEBUG(fd, "epoll_ctl(ADD, EPOLLIN) successful.");
    // --- End syscalls ---

    GANL_EPOLL_DEBUG(fd, "Listener started successfully.");
    return true;
}

void EpollNetworkEngine::closeListener(ListenerHandle listener) {
    int fd = static_cast<int>(listener);
    GANL_EPOLL_DEBUG(fd, "Closing listener...");

    // Remove from maps under lock FIRST
    {
        std::lock_guard<std::mutex> lock(mutex_);
        listeners_.erase(listener);
        sockets_.erase(fd);
    } // Mutex released

    // --- Perform syscalls outside the lock ---
    if (epollFd_ != -1) {
        if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
            if (errno != EBADF && errno != ENOENT) {
                GANL_EPOLL_DEBUG(fd, "Warning: epoll_ctl(DEL) failed: " << strerror(errno));
            }
        } else {
            GANL_EPOLL_DEBUG(fd, "epoll_ctl(DEL) successful.");
        }
    }

    if (close(fd) == -1) {
        GANL_EPOLL_DEBUG(fd, "Warning: close(fd) failed: " << strerror(errno));
    } else {
        GANL_EPOLL_DEBUG(fd, "close(fd) successful.");
    }
    // --- End syscalls ---

    GANL_EPOLL_DEBUG(fd, "Listener closed and removed from maps.");
}

// --- Connection Management ---

bool EpollNetworkEngine::associateContext(ConnectionHandle conn, void* context, ErrorCode& error) {
    int fd = static_cast<int>(conn);
    error = 0;
    GANL_EPOLL_DEBUG(fd, "Associating context=" << context);

    std::lock_guard<std::mutex> lock(mutex_); // Lock for map access
    auto sockIt = sockets_.find(fd);
    if (sockIt == sockets_.end()) {
        GANL_EPOLL_DEBUG(fd, "Error: Cannot associate context, socket not found.");
        error = EBADF; // Use EBADF for bad file descriptor
        return false;
    }
    if (sockIt->second.type != SocketType::Connection) {
        GANL_EPOLL_DEBUG(fd, "Error: Cannot associate context, socket is not a connection.");
        error = EINVAL;
        return false;
    }

    sockIt->second.context = context;
    GANL_EPOLL_DEBUG(fd, "Context associated successfully.");
    return true;
}

void EpollNetworkEngine::closeConnection(ConnectionHandle conn) {
    int fd = static_cast<int>(conn);
    GANL_EPOLL_DEBUG(fd, "Closing connection...");

    // Remove from map under lock FIRST
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_.erase(fd);
    } // Mutex released

    // --- Perform syscalls outside the lock ---
    if (epollFd_ != -1) {
        if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
            if (errno != EBADF && errno != ENOENT) {
                GANL_EPOLL_DEBUG(fd, "Warning: epoll_ctl(DEL) failed: " << strerror(errno));
            }
        } else {
            GANL_EPOLL_DEBUG(fd, "epoll_ctl(DEL) successful.");
        }
    }

    ::shutdown(fd, SHUT_RDWR); // Optional

    if (close(fd) == -1) {
        GANL_EPOLL_DEBUG(fd, "Warning: close(fd) failed: " << strerror(errno));
    } else {
        GANL_EPOLL_DEBUG(fd, "close(fd) successful.");
    }
    // --- End syscalls ---

    GANL_EPOLL_DEBUG(fd, "Connection closed and removed from maps.");
}

// --- I/O Operations ---

// postRead: With epoll ET, we rely on EPOLLIN notification. The Connection object
//           is responsible for reading all available data when notified.
//           This function doesn't need to do anything besides potentially verifying
//           that the socket is still valid and registered for input.
bool EpollNetworkEngine::postRead(ConnectionHandle conn, char* buffer, size_t length, ErrorCode& error) {
    int fd = static_cast<int>(conn);
    error = 0;

    // Verify socket validity under lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto sockIt = sockets_.find(fd);
        if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Connection) {
            GANL_EPOLL_DEBUG(fd, "Warning: postRead on invalid/non-connection handle.");
            error = EBADF;
            return false;
        }
    } // Mutex released

    GANL_EPOLL_DEBUG(fd, "postRead called (No-op for epoll - relying on EPOLLIN notification).");
    return true;
}

// postWrite: Called by Connection when it has data to write.
//            This function ensures EPOLLOUT is registered for the socket to indicate write interest.
//            It does NOT perform the write itself.
bool EpollNetworkEngine::postWrite(ConnectionHandle conn, const char* data, size_t length, ErrorCode& error) {
    int fd = static_cast<int>(conn);
    error = 0;
    GANL_EPOLL_DEBUG(fd, "postWrite called to register write interest");

    uint32_t currentEvents = 0;
    bool needsModification = false;

    // Check current event flags under lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto sockIt = sockets_.find(fd);
        if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Connection) {
            GANL_EPOLL_DEBUG(fd, "Error: postWrite on invalid/non-connection handle.");
            error = EBADF;
            return false;
        }
        currentEvents = sockIt->second.events;
        if (!(currentEvents & EPOLLOUT)) {
            needsModification = true;
        }
    } // Mutex released

    // Perform epoll_ctl modification outside the lock if needed
    if (needsModification) {
        GANL_EPOLL_DEBUG(fd, "EPOLLOUT not set. Adding it via modifyEpollFlags.");
        // modifyEpollFlags handles its own locking for map update after syscall
        return modifyEpollFlags(fd, currentEvents | EPOLLOUT, error);
    } else {
        GANL_EPOLL_DEBUG(fd, "EPOLLOUT already set.");
        return true; // Already interested in writing
    }
}

// --- Event Processing ---

int EpollNetworkEngine::processEvents(int timeoutMs, IoEvent* events, int maxEvents) {
    // ... (epoll_wait call as before) ...
    int nfds = epoll_wait(epollFd_, epollEvents_.data(), epollEvents_.size(), timeoutMs);
    // ... (error handling for nfds == -1 as before) ...

    int eventCount = 0; // Number of IoEvent entries filled

    for (int i = 0; i < nfds && eventCount < maxEvents; ++i) {
        const epoll_event& epEvent = epollEvents_[i];
        int fd = epEvent.data.fd;
        uint32_t revents = epEvent.events;

        // --- Retrieve socket info and context under lock ---
        SocketInfo socketInfoCopy; // Copy info to avoid holding lock during processing
        bool socketFound = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto sockIt = sockets_.find(fd);
            if (sockIt != sockets_.end()) {
                socketInfoCopy = sockIt->second; // Make a copy
                socketFound = true;
            }
        } // Mutex released

        if (!socketFound) {
            GANL_EPOLL_DEBUG(fd, "Warning: Event received for unknown/closed socket (checked map). Ignoring.");
            // Ensure it's removed from epoll now just in case.
            epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
            continue;
        }
        // --- Use socketInfoCopy from now on ---

        // --- Handle Listener Events ---
        if (socketInfoCopy.type == SocketType::Listener) {
            if (revents & EPOLLIN) {
                // ... (accept loop as before, calling acceptConnection) ...
                 while (eventCount < maxEvents) {
                     ErrorCode acceptError = 0;
                     // acceptConnection handles adding new socket to map and epoll
                     ConnectionHandle newConn = acceptConnection(fd, acceptError);

                     if (newConn != InvalidConnectionHandle) {
                          // ... (populate Accept event as before, using socketInfoCopy.context) ...
                           IoEvent& ev = events[eventCount++];
                          ev.type = IoEventType::Accept;
                          ev.listener = fd;
                          ev.connection = newConn;
                          ev.context = socketInfoCopy.context; // Use copied context
                          ev.bytesTransferred = 0;
                          ev.error = 0;
                     } else {
                          // ... (handle acceptError EAGAIN/EWOULDBLOCK or real error as before) ...
                          break;
                     }
                 }
            }
            if (revents & (EPOLLERR | EPOLLHUP)) {
                 // ... (handle listener error as before, using socketInfoCopy.context) ...
                 if (eventCount < maxEvents) {
                     IoEvent& ev = events[eventCount++];
                     ev.type = IoEventType::Error;
                     ev.listener = fd;
                     // ... rest of error event population using socketInfoCopy.context ...
                 }
            }
            continue; // Done processing listener event
        } // end if listener


        // --- Handle Connection Events ---
        if (socketInfoCopy.type == SocketType::Connection) {
            ConnectionHandle connHandle = static_cast<ConnectionHandle>(fd);
            bool connectionClosed = false;

            // Check for errors/hup first
            if (revents & (EPOLLERR | EPOLLHUP)) {
                 // ... (getsockopt SO_ERROR as before) ...
                if (eventCount < maxEvents) {
                     IoEvent& ev = events[eventCount++];
                     // ... (populate Error/Close event as before using socketInfoCopy.context) ...
                     ev.connection = connHandle;
                     ev.context = socketInfoCopy.context; // Use copied context
                }
                connectionClosed = true;
            }

            // Handle Read Readiness
            if (!connectionClosed && (revents & EPOLLIN)) {
                // ... (populate Read event as before using socketInfoCopy.context) ...
                if (eventCount < maxEvents) {
                     IoEvent& ev = events[eventCount++];
                     ev.type = IoEventType::Read;
                     ev.connection = connHandle;
                     ev.context = socketInfoCopy.context; // Use copied context
                     // ...
                }
            }

            // Handle Write Readiness
            if (!connectionClosed && (revents & EPOLLOUT)) {
                // Create and fill Write event
                if (eventCount < maxEvents) {
                    IoEvent& ev = events[eventCount++];
                    ev.type = IoEventType::Write;
                    ev.connection = connHandle;
                    ev.context = socketInfoCopy.context;
                    ev.bytesTransferred = 0; // No specific bytes count for readiness events
                    ev.error = 0;
                }

                // IMPORTANT: Always disable EPOLLOUT after generating Write event
                // The connection will re-register write interest via postWrite if it needs to write more
                if (socketInfoCopy.events & EPOLLOUT) {
                    GANL_EPOLL_DEBUG(fd, "Disabling EPOLLOUT after generating Write event.");
                    ErrorCode modError = 0;
                    // modifyEpollFlags will re-lock briefly to update the map
                    if (!modifyEpollFlags(fd, socketInfoCopy.events & ~EPOLLOUT, modError)) {
                        GANL_EPOLL_DEBUG(fd, "Error disabling EPOLLOUT: " << strerror(modError) << ".");
                    }
                }
            }
        } // end if connection
    } // end for loop through epoll events

    return eventCount;
}

// --- Utility Methods ---

std::string EpollNetworkEngine::getRemoteAddress(ConnectionHandle conn) {
    // We can now use the NetworkAddress class for consistent formatting
    return getRemoteNetworkAddress(conn).toString();
}

NetworkAddress EpollNetworkEngine::getRemoteNetworkAddress(ConnectionHandle conn) {
    int fd = static_cast<int>(conn);
    sockaddr_storage addrStorage; // Use sockaddr_storage for IPv4/IPv6 compatibility
    socklen_t addrLen = sizeof(addrStorage);

    if (getpeername(fd, reinterpret_cast<sockaddr*>(&addrStorage), &addrLen) == -1) {
        GANL_EPOLL_DEBUG(fd, "getpeername failed: " << strerror(errno));
        return NetworkAddress(); // Return invalid address
    }

    // Create and return NetworkAddress from the raw sockaddr
    return NetworkAddress(reinterpret_cast<sockaddr*>(&addrStorage), addrLen);
}

std::string EpollNetworkEngine::getErrorString(ErrorCode error) {
    // strerror is generally thread-safe, but strerror_r is preferred if available/needed.
    return strerror(error);
}

// Helper to set socket non-blocking (using fcntl)
bool EpollNetworkEngine::setNonBlocking(int fd, ErrorCode& error) {
    // Note: SOCK_NONBLOCK flag during socket() creation is preferred
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        error = errno;
        GANL_EPOLL_DEBUG(fd, "fcntl(F_GETFL) failed: " << strerror(error));
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        error = errno;
        GANL_EPOLL_DEBUG(fd, "fcntl(F_SETFL, O_NONBLOCK) failed: " << strerror(error));
        return false;
    }
    GANL_EPOLL_DEBUG(fd, "Set non-blocking successfully.");
    return true;
}

// Helper to accept a new connection
ConnectionHandle EpollNetworkEngine::acceptConnection(ListenerHandle listener, ErrorCode& error) {
    int listenerFd = static_cast<int>(listener);
    sockaddr_storage clientAddr; // Use storage for IPv4/IPv6
    socklen_t clientLen = sizeof(clientAddr);
    error = 0;

    // Use accept4 for SOCK_NONBLOCK | SOCK_CLOEXEC if available (Linux specific)
    // int clientFd = accept4(listenerFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen, SOCK_NONBLOCK | SOCK_CLOEXEC);
    int clientFd = accept(listenerFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);

    if (clientFd == -1) {
        error = errno;
        // EAGAIN/EWOULDBLOCK are not errors, just no connection pending
        // Other errors are real problems
        // if (error != EAGAIN && error != EWOULDBLOCK) {
        //     GANL_EPOLL_DEBUG(listenerFd, "accept() failed: " << strerror(error));
        // }
        return InvalidConnectionHandle;
    }
    GANL_EPOLL_DEBUG(listenerFd, "accept() successful. New FD: " << clientFd);

    // Set non-blocking and close-on-exec if accept4 wasn't used
    if (!setNonBlocking(clientFd, error)) {
        GANL_EPOLL_DEBUG(clientFd, "Failed to set non-blocking on accepted socket: " << strerror(error));
        close(clientFd);
        return InvalidConnectionHandle;
    }
    int flags = fcntl(clientFd, F_GETFD, 0);
    if (flags != -1) {
        fcntl(clientFd, F_SETFD, flags | FD_CLOEXEC);
    } // Ignore error if F_GETFD fails

    // Optional: Set TCP_NODELAY?
    // int opt = 1;
    // setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    // --- Add to map under lock ---
    uint32_t initialEvents = EPOLLIN | EPOLLET;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_[clientFd] = SocketInfo{SocketType::Connection, context: nullptr, events: initialEvents};
    } // Mutex released
    // --- End map update ---

    // --- Add to epoll outside lock ---
    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = initialEvents;
    event.data.fd = clientFd;

    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, clientFd, &event) == -1) {
        error = errno;
        GANL_EPOLL_DEBUG(clientFd, "epoll_ctl(ADD) failed for new connection: " << strerror(error));
        // --- Clean up map entry under lock ---
        {
            std::lock_guard<std::mutex> lock(mutex_);
            sockets_.erase(clientFd);
        } // Mutex released
        close(clientFd);
        return InvalidConnectionHandle;
    }
    // --- End add to epoll ---

    GANL_EPOLL_DEBUG(clientFd, "epoll_ctl(ADD, EPOLLIN | EPOLLET) successful.");
    GANL_EPOLL_DEBUG(clientFd, "New connection socket registered.");
    return static_cast<ConnectionHandle>(clientFd);
}

// Helper to modify epoll registration flags (add/remove EPOLLOUT)
bool EpollNetworkEngine::modifyEpollFlags(int fd, uint32_t newEvents, ErrorCode& error) {
     error = 0;
     epoll_event event;
     memset(&event, 0, sizeof(event));
     event.events = newEvents;
     event.data.fd = fd;

     GANL_EPOLL_DEBUG(fd, "Modifying epoll flags to 0x" << std::hex << newEvents << std::dec);

     // --- Perform syscall outside lock ---
     if (epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &event) == -1) {
         error = errno;
         GANL_EPOLL_DEBUG(fd, "epoll_ctl(MOD) failed: " << strerror(error));
         // If MOD fails with ENOENT, it means the FD was already removed.
         if (error == ENOENT) {
              GANL_EPOLL_DEBUG(fd, "Note: epoll_ctl(MOD) failed with ENOENT, socket likely already removed.");
              // Remove from map just in case
              std::lock_guard<std::mutex> lock(mutex_);
              sockets_.erase(fd);
         }
         return false;
     }
     // --- End syscall ---

     // --- Update stored events in map under lock ---
     {
        std::lock_guard<std::mutex> lock(mutex_);
        auto sockIt = sockets_.find(fd);
        if (sockIt != sockets_.end()) {
            sockIt->second.events = newEvents;
        } else {
            // This could happen if closed concurrently between syscall and map update
            GANL_EPOLL_DEBUG(fd, "Warning: Modified epoll flags for socket not found in map after successful syscall?");
        }
     } // Mutex released
     // --- End map update ---

     GANL_EPOLL_DEBUG(fd, "epoll_ctl(MOD) successful.");
     return true;
}

} // namespace ganl
