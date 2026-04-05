#include "epoll_network_engine.h"
#include "connection.h"
#include "slave_spawn_posix.h"
#include <io_buffer.h>
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_NODELAY potentially
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring> // For strerror, memset
#include <cerrno>  // For errno constants
#include <sys/un.h> // For sockaddr_un (Unix domain sockets)
#include <vector>  // For key iteration during shutdown
#include <mutex>
#include <netdb.h> // For getaddrinfo

// Define a macro for debug logging
#ifndef NDEBUG // Only compile debug messages if NDEBUG is not defined
#define GANL_EPOLL_DEBUG(fd, x) \
    do { std::cerr << "[Epoll:" << (fd == epollFd_ ? "CTL" : std::to_string(fd)) << "] " << x << std::endl; } while (0)
#else
#define GANL_EPOLL_DEBUG(fd, x) do {} while (0)
#endif


namespace ganl {

namespace {

void checkNegotiationTimeouts(const std::vector<ConnectionBase*>& connections) {
    for (ConnectionBase* connection : connections) {
        if (connection != nullptr) {
            connection->checkNegotiationTimeout();
        }
    }
}

} // namespace

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

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE; // Allow binding when host empty

    const char* hostCStr = host.empty() ? nullptr : host.c_str();
    std::string portStr = std::to_string(port);

    addrinfo* results = nullptr;
    int gaiResult = ::getaddrinfo(hostCStr, portStr.c_str(), &hints, &results);
    if (gaiResult != 0) {
        if (gaiResult == EAI_SYSTEM) {
            error = errno;
            GANL_EPOLL_DEBUG(0, "getaddrinfo() system error: " << strerror(error));
        } else {
            error = EINVAL;
            GANL_EPOLL_DEBUG(0, "getaddrinfo() failed: " << ::gai_strerror(gaiResult));
        }
        return InvalidListenerHandle;
    }

    int fd = -1;
    int lastErr = 0;

    for (addrinfo* ai = results; ai != nullptr; ai = ai->ai_next) {
        int sockFlags = ai->ai_socktype;
#ifdef SOCK_NONBLOCK
        sockFlags |= SOCK_NONBLOCK;
#endif

        fd = ::socket(ai->ai_family, sockFlags, ai->ai_protocol);
        if (fd == -1) {
            lastErr = errno;
            continue;
        }

        int opt = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
            lastErr = errno;
            GANL_EPOLL_DEBUG(fd, "setsockopt(SO_REUSEADDR) failed: " << strerror(lastErr));
            close(fd);
            fd = -1;
            continue;
        }

        if (ai->ai_family == AF_INET6) {
            int disable = 0;
            setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &disable, sizeof(disable));
        }

        if (bind(fd, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen)) == -1) {
            lastErr = errno;
            GANL_EPOLL_DEBUG(fd, "bind() failed: " << strerror(lastErr));
            close(fd);
            fd = -1;
            continue;
        }

        GANL_EPOLL_DEBUG(fd, "bind() successful using family " << ai->ai_family << ".");
        lastErr = 0;
        break;
    }

    ::freeaddrinfo(results);

    if (fd == -1) {
        error = lastErr != 0 ? lastErr : EADDRNOTAVAIL;
        return InvalidListenerHandle;
    }

    // Store basic info under lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_[fd] = SocketInfo{
            SocketType::Listener,
            context: nullptr,
            events: 0,
            activeReadBuffer: nullptr
        };
    }
    GANL_EPOLL_DEBUG(fd, "Listener created successfully.");

    return static_cast<ListenerHandle>(fd);
}

ListenerHandle EpollNetworkEngine::adoptListener(int fd, ErrorCode& error) {
    error = 0;
    if (fd < 0) {
        error = EBADF;
        return InvalidListenerHandle;
    }

    if (epollFd_ == -1) {
        error = EINVAL;
        return InvalidListenerHandle;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sockets_.find(fd) != sockets_.end()) {
            error = EEXIST;
            return InvalidListenerHandle;
        }
    }

    if (!setNonBlocking(fd, error)) {
        return InvalidListenerHandle;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_[fd] = SocketInfo{
            SocketType::Listener,
            context: nullptr,
            events: 0,
            activeReadBuffer: nullptr
        };
    }

    GANL_EPOLL_DEBUG(fd, "Adopted external listener successfully.");
    return static_cast<ListenerHandle>(fd);
}

ConnectionHandle EpollNetworkEngine::adoptConnection(int fd, void* connectionContext, ErrorCode& error) {
    error = 0;
    if (fd < 0) {
        error = EBADF;
        return InvalidConnectionHandle;
    }

    if (epollFd_ == -1) {
        error = EINVAL;
        return InvalidConnectionHandle;
    }

    // Ensure we are not already tracking this descriptor
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (sockets_.find(fd) != sockets_.end()) {
            error = EEXIST;
            return InvalidConnectionHandle;
        }
    }

    if (!setNonBlocking(fd, error)) {
        return InvalidConnectionHandle;
    }

    uint32_t initialEvents = EPOLLIN | EPOLLET;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_[fd] = SocketInfo{
            SocketType::Connection,
            context: connectionContext,
            events: initialEvents,
            activeReadBuffer: nullptr,
            writeUserContext: nullptr
        };
    }

    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = initialEvents;
    event.data.fd = fd;

    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) == -1) {
        error = errno;
        GANL_EPOLL_DEBUG(fd, "epoll_ctl(ADD) failed for adopted connection: " << strerror(error));
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_.erase(fd);
        return InvalidConnectionHandle;
    }

    GANL_EPOLL_DEBUG(fd, "Adopted external connection successfully.");
    return static_cast<ConnectionHandle>(fd);
}

ConnectionHandle EpollNetworkEngine::initiateConnect(const std::string& host, uint16_t port,
                                                     void* connectionContext, ErrorCode& error) {
    error = 0;
    GANL_EPOLL_DEBUG(0, "Initiating outbound connect to " << host << ":" << port);

    if (epollFd_ == -1) {
        error = EINVAL;
        return InvalidConnectionHandle;
    }

    // Resolve hostname (blocking — adequate for Phase 1 localhost/LAN targets)
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    std::string portStr = std::to_string(port);
    addrinfo* results = nullptr;
    int gaiResult = ::getaddrinfo(host.c_str(), portStr.c_str(), &hints, &results);
    if (gaiResult != 0) {
        error = (gaiResult == EAI_SYSTEM) ? errno : EINVAL;
        GANL_EPOLL_DEBUG(0, "initiateConnect: getaddrinfo failed: " << ::gai_strerror(gaiResult));
        return InvalidConnectionHandle;
    }

    int fd = -1;
    int lastErr = 0;
    bool connectInProgress = false;

    for (addrinfo* ai = results; ai != nullptr; ai = ai->ai_next) {
        int sockFlags = ai->ai_socktype;
#ifdef SOCK_NONBLOCK
        sockFlags |= SOCK_NONBLOCK;
#endif
#ifdef SOCK_CLOEXEC
        sockFlags |= SOCK_CLOEXEC;
#endif

        fd = ::socket(ai->ai_family, sockFlags, ai->ai_protocol);
        if (fd == -1) {
            lastErr = errno;
            continue;
        }

#ifndef SOCK_NONBLOCK
        if (!setNonBlocking(fd, lastErr)) {
            ::close(fd);
            fd = -1;
            continue;
        }
#endif

        int rc = ::connect(fd, ai->ai_addr, static_cast<socklen_t>(ai->ai_addrlen));
        if (rc == 0) {
            // Immediate connect (common for localhost)
            connectInProgress = false;
            break;
        } else if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
            // Normal for non-blocking connect
            connectInProgress = true;
            break;
        } else {
            lastErr = errno;
            GANL_EPOLL_DEBUG(fd, "connect() failed: " << strerror(lastErr));
            ::close(fd);
            fd = -1;
            continue;
        }
    }

    ::freeaddrinfo(results);

    if (fd == -1) {
        error = lastErr != 0 ? lastErr : ECONNREFUSED;
        return InvalidConnectionHandle;
    }

    // Register with epoll
    SocketType sockType = connectInProgress
        ? SocketType::OutboundConnecting
        : SocketType::Connection;
    uint32_t initialEvents = connectInProgress
        ? (EPOLLOUT | EPOLLET)          // Wait for connect completion
        : (EPOLLIN | EPOLLET);          // Already connected, ready for data

    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_[fd] = SocketInfo{
            sockType,
            /*context=*/connectionContext,
            /*events=*/initialEvents,
            /*activeReadBuffer=*/nullptr,
            /*writeUserContext=*/nullptr
        };
    }

    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = initialEvents;
    event.data.fd = fd;

    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) == -1) {
        error = errno;
        GANL_EPOLL_DEBUG(fd, "epoll_ctl(ADD) failed for outbound connect: " << strerror(error));
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_.erase(fd);
        ::close(fd);
        return InvalidConnectionHandle;
    }

    GANL_EPOLL_DEBUG(fd, "Outbound connect initiated ("
        << (connectInProgress ? "in progress" : "immediate") << ").");

    // For immediate connects, keep the socket as OutboundConnecting with
    // EPOLLOUT interest.  The next processEvents() will see EPOLLOUT, check
    // SO_ERROR (which will be 0), and emit ConnectSuccess — consistent with
    // the async path.  No special-casing needed by the caller.
    if (!connectInProgress) {
        GANL_EPOLL_DEBUG(fd, "Connect completed immediately, will emit ConnectSuccess on next poll.");
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sockets_.find(fd);
        if (it != sockets_.end()) {
            it->second.type = SocketType::OutboundConnecting;
            it->second.events = EPOLLOUT | EPOLLET;
        }
        // Events already registered as EPOLLOUT|EPOLLET above, so
        // EPOLLOUT will fire on the next epoll_wait.
    }

    return static_cast<ConnectionHandle>(fd);
}

ConnectionHandle EpollNetworkEngine::initiateUnixConnect(const std::string& path,
                                                         void* connectionContext, ErrorCode& error) {
    error = 0;
    GANL_EPOLL_DEBUG(0, "Initiating outbound Unix connect to " << path);

    if (epollFd_ == -1) {
        error = EINVAL;
        return InvalidConnectionHandle;
    }

    int sockFlags = SOCK_STREAM;
#ifdef SOCK_NONBLOCK
    sockFlags |= SOCK_NONBLOCK;
#endif
#ifdef SOCK_CLOEXEC
    sockFlags |= SOCK_CLOEXEC;
#endif

    int fd = ::socket(AF_UNIX, sockFlags, 0);
    if (fd == -1) {
        error = errno;
        return InvalidConnectionHandle;
    }

#ifndef SOCK_NONBLOCK
    if (!setNonBlocking(fd, error)) {
        ::close(fd);
        return InvalidConnectionHandle;
    }
#endif

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) {
        error = ENAMETOOLONG;
        ::close(fd);
        return InvalidConnectionHandle;
    }
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);

    bool connectInProgress = false;
    int rc = ::connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (rc == 0) {
        connectInProgress = false;
    } else if (errno == EINPROGRESS || errno == EWOULDBLOCK) {
        connectInProgress = true;
    } else {
        error = errno;
        GANL_EPOLL_DEBUG(fd, "Unix connect() failed: " << strerror(error));
        ::close(fd);
        return InvalidConnectionHandle;
    }

    SocketType sockType = connectInProgress
        ? SocketType::OutboundConnecting
        : SocketType::Connection;
    uint32_t initialEvents = connectInProgress
        ? (EPOLLOUT | EPOLLET)
        : (EPOLLIN | EPOLLET);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_[fd] = SocketInfo{
            sockType,
            /*context=*/connectionContext,
            /*events=*/initialEvents,
            /*activeReadBuffer=*/nullptr,
            /*writeUserContext=*/nullptr
        };
    }

    epoll_event event;
    memset(&event, 0, sizeof(event));
    event.events = initialEvents;
    event.data.fd = fd;

    if (epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &event) == -1) {
        error = errno;
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_.erase(fd);
        ::close(fd);
        return InvalidConnectionHandle;
    }

    if (!connectInProgress) {
        GANL_EPOLL_DEBUG(fd, "Unix connect completed immediately.");
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sockets_.find(fd);
        if (it != sockets_.end()) {
            it->second.type = SocketType::OutboundConnecting;
            it->second.events = EPOLLOUT | EPOLLET;
        }
    }

    GANL_EPOLL_DEBUG(fd, "Unix outbound connect initiated.");
    return static_cast<ConnectionHandle>(fd);
}

ConnectionHandle EpollNetworkEngine::spawnSlave(const SlaveSpawnOptions& options, ErrorCode& error) {
#ifndef _WIN32
    return spawnSlavePosix(*this, options, error);
#else
    (void)options;
    error = ENOTSUP;
    return InvalidConnectionHandle;
#endif
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

void EpollNetworkEngine::detachConnection(ConnectionHandle conn) {
    int fd = static_cast<int>(conn);
    GANL_EPOLL_DEBUG(fd, "Detaching connection (no close)...");

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sockets_.find(fd);
        if (it == sockets_.end()) {
            GANL_EPOLL_DEBUG(fd, "Socket not found in map, nothing to detach.");
            return;
        }
        sockets_.erase(it);
    }

    if (epollFd_ != -1) {
        if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
            if (errno != EBADF && errno != ENOENT) {
                GANL_EPOLL_DEBUG(fd, "Warning: epoll_ctl(DEL) failed during detach: " << strerror(errno));
            }
        }
    }

    GANL_EPOLL_DEBUG(fd, "Connection detached (fd left open).");
}

void EpollNetworkEngine::detachListener(ListenerHandle listener) {
    int fd = static_cast<int>(listener);
    GANL_EPOLL_DEBUG(fd, "Detaching listener (no close)...");

    {
        std::lock_guard<std::mutex> lock(mutex_);
        listeners_.erase(listener);
        sockets_.erase(fd);
    }

    if (epollFd_ != -1) {
        if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
            if (errno != EBADF && errno != ENOENT) {
                GANL_EPOLL_DEBUG(fd, "Warning: epoll_ctl(DEL) failed during detach: " << strerror(errno));
            }
        }
    }

    GANL_EPOLL_DEBUG(fd, "Listener detached (fd left open).");
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

    // Check if socket is already closed/removed
    bool socketExists = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        socketExists = sockets_.find(fd) != sockets_.end();

        if (socketExists) {
            // Remove from map under lock
            sockets_.erase(fd);
            GANL_EPOLL_DEBUG(fd, "Socket removed from map.");
        } else {
            GANL_EPOLL_DEBUG(fd, "Socket not found in map, likely already closed.");
            return; // Skip further operations if already removed
        }
    } // Mutex released

    // --- Perform syscalls outside the lock ---
    if (epollFd_ != -1) {
        if (epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr) == -1) {
            if (errno != EBADF && errno != ENOENT) {
                GANL_EPOLL_DEBUG(fd, "Warning: epoll_ctl(DEL) failed: " << strerror(errno));
            } else {
                GANL_EPOLL_DEBUG(fd, "Socket already removed from epoll or bad file descriptor.");
            }
        } else {
            GANL_EPOLL_DEBUG(fd, "epoll_ctl(DEL) successful.");
        }
    }

    // Attempt graceful shutdown first
    if (::shutdown(fd, SHUT_RDWR) == -1) {
        if (errno != ENOTCONN && errno != EBADF) {
            GANL_EPOLL_DEBUG(fd, "Warning: shutdown(fd, SHUT_RDWR) failed: " << strerror(errno));
        } else {
            GANL_EPOLL_DEBUG(fd, "Socket already disconnected or invalid.");
        }
    }

    // Then close the file descriptor
    if (close(fd) == -1) {
        if (errno == EBADF) {
            GANL_EPOLL_DEBUG(fd, "File descriptor already closed or invalid.");
        } else {
            GANL_EPOLL_DEBUG(fd, "Warning: close(fd) failed: " << strerror(errno));
        }
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
//           that the socket is still valid and registered for input and tracking the IoBuffer.
bool EpollNetworkEngine::postRead(ConnectionHandle conn, IoBuffer& buffer, ErrorCode& error) {
    int fd = static_cast<int>(conn);
    error = 0;
    GANL_EPOLL_DEBUG(fd, "postRead(IoBuffer) called.");

    // Verify socket validity under lock and store buffer reference
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto sockIt = sockets_.find(fd);
        if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Connection) {
            GANL_EPOLL_DEBUG(fd, "Warning: postRead(IoBuffer) on invalid/non-connection handle.");
            error = EBADF;
            return false;
        }
        // Store buffer reference
        sockIt->second.activeReadBuffer = &buffer;
        GANL_EPOLL_DEBUG(fd, "Stored IoBuffer reference " << &buffer << " for connection " << fd);
    } // Mutex released

    // For epoll, this is almost a no-op since we're already registered for read events
    GANL_EPOLL_DEBUG(fd, "postRead(IoBuffer) successful (relying on EPOLLIN notification).");
    return true;
}

// postWrite: Called by Connection when it has data to write with user context.
//            This function ensures EPOLLOUT is registered for the socket to indicate write interest.
//            It does NOT perform the write itself.
bool EpollNetworkEngine::postWrite(ConnectionHandle conn, const char* data, size_t length, void* userContext, ErrorCode& error) {
    int fd = static_cast<int>(conn);
    error = 0;
    GANL_EPOLL_DEBUG(fd, "postWrite called to register write interest" << (userContext ? " with context" : ""));

    uint32_t currentEvents = 0;
    bool needsModification = false;

    // Check current event flags under lock and store user context
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto sockIt = sockets_.find(fd);
        if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Connection) {
            GANL_EPOLL_DEBUG(fd, "Error: postWrite on invalid/non-connection handle.");
            error = EBADF;
            return false;
        }

        // Store the user context for the write operation
        sockIt->second.writeUserContext = userContext;
        if (userContext) {
            GANL_EPOLL_DEBUG(fd, "Stored write user context " << userContext);
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

// Backward compatibility implementation that calls the contextual version
bool EpollNetworkEngine::postWrite(ConnectionHandle conn, const char* data, size_t length, ErrorCode& error) {
    return postWrite(conn, data, length, nullptr, error);
}

// --- Event Processing ---

int EpollNetworkEngine::processEvents(int timeoutMs, IoEvent* events, int maxEvents) {
    // ... (epoll_wait call as before) ...
    int nfds = epoll_wait(epollFd_, epollEvents_.data(), epollEvents_.size(), timeoutMs);
    // ... (error handling for nfds == -1 as before) ...

    if (nfds == 0) {
        std::vector<ConnectionBase*> connections;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            connections.reserve(sockets_.size());
            for (const auto& entry : sockets_) {
                if (entry.second.type == SocketType::Connection) {
                    connections.push_back(static_cast<ConnectionBase*>(entry.second.context));
                }
            }
        }
        checkNegotiationTimeouts(connections);
        return 0;
    }

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
                          // Populate the remoteAddress field directly from the socket
                          ev.remoteAddress = getRemoteNetworkAddress(newConn);
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


        // --- Handle Outbound Connecting Events ---
        if (socketInfoCopy.type == SocketType::OutboundConnecting) {
            ConnectionHandle connHandle = static_cast<ConnectionHandle>(fd);

            if (revents & (EPOLLOUT | EPOLLERR | EPOLLHUP)) {
                // Check if connect() succeeded via SO_ERROR
                int sockerr = 0;
                socklen_t errlen = sizeof(sockerr);
                getsockopt(fd, SOL_SOCKET, SO_ERROR,
                           reinterpret_cast<char*>(&sockerr), &errlen);

                if (sockerr == 0) {
                    // Connect succeeded — transition to Connection
                    GANL_EPOLL_DEBUG(fd, "Outbound connect succeeded.");
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        auto sockIt = sockets_.find(fd);
                        if (sockIt != sockets_.end()) {
                            sockIt->second.type = SocketType::Connection;
                        }
                    }

                    // Switch to read interest (normal connection mode)
                    ErrorCode modError = 0;
                    modifyEpollFlags(fd, EPOLLIN | EPOLLET, modError);

                    if (eventCount < maxEvents) {
                        IoEvent& ev = events[eventCount++];
                        ev.type = IoEventType::ConnectSuccess;
                        ev.connection = connHandle;
                        ev.context = socketInfoCopy.context;
                        ev.bytesTransferred = 0;
                        ev.error = 0;
                    }
                } else {
                    // Connect failed
                    GANL_EPOLL_DEBUG(fd, "Outbound connect failed: " << strerror(sockerr));

                    if (eventCount < maxEvents) {
                        IoEvent& ev = events[eventCount++];
                        ev.type = IoEventType::ConnectFail;
                        ev.connection = connHandle;
                        ev.context = socketInfoCopy.context;
                        ev.bytesTransferred = 0;
                        ev.error = sockerr;
                    }

                    // Clean up the failed socket
                    epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
                    ::close(fd);
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        sockets_.erase(fd);
                    }
                }
            }
            continue; // Done processing outbound connecting event
        }

        // --- Handle Connection Events ---
        if (socketInfoCopy.type == SocketType::Connection) {
            ConnectionHandle connHandle = static_cast<ConnectionHandle>(fd);
            bool connectionClosed = false;

            // Check for errors/hup first
            if (revents & (EPOLLERR | EPOLLHUP)) {
                 // We could use getsockopt with SO_ERROR here to get the specific error
                if (eventCount < maxEvents) {
                     IoEvent& ev = events[eventCount++];
                     ev.type = IoEventType::Close; // Or IoEventType::Error based on error detection
                     ev.connection = connHandle;
                     ev.context = socketInfoCopy.context; // Use copied context
                     ev.buffer = socketInfoCopy.activeReadBuffer; // Include IoBuffer reference if available
                     ev.bytesTransferred = 0;
                     ev.error = 0; // Could set to actual error if detected

                     // Clear buffer reference after generating the event for enhanced safety
                     if (socketInfoCopy.activeReadBuffer) {
                         std::lock_guard<std::mutex> lock(mutex_);
                         auto sockIt = sockets_.find(fd);
                         if (sockIt != sockets_.end()) {
                             sockIt->second.activeReadBuffer = nullptr;
                             GANL_EPOLL_DEBUG(fd, "Cleared activeReadBuffer after generating Close/Error event");
                         }
                     }
                }
                connectionClosed = true;
            }

            // Handle Read Readiness
            if (!connectionClosed && (revents & EPOLLIN)) {
                if (eventCount < maxEvents) {
                     IoEvent& ev = events[eventCount++];
                     ev.type = IoEventType::Read;
                     ev.connection = connHandle;
                     ev.context = socketInfoCopy.context; // Use copied context
                     ev.buffer = socketInfoCopy.activeReadBuffer; // Include IoBuffer reference if available
                     ev.bytesTransferred = 0; // No specific bytes count for readiness events
                     ev.error = 0;

                     // Clear buffer reference after generating the event for enhanced safety
                     if (socketInfoCopy.activeReadBuffer) {
                         std::lock_guard<std::mutex> lock(mutex_);
                         auto sockIt = sockets_.find(fd);
                         if (sockIt != sockets_.end()) {
                             sockIt->second.activeReadBuffer = nullptr;
                             GANL_EPOLL_DEBUG(fd, "Cleared activeReadBuffer after generating Read event");
                         }
                     }
                }
            }

            // Handle Write Readiness
            if (!connectionClosed && (revents & EPOLLOUT)) {
                // Create and fill Write event
                if (eventCount < maxEvents) {
                    IoEvent& ev = events[eventCount++];
                    ev.type = IoEventType::Write;
                    ev.connection = connHandle;

                    // Use the write user context if available, otherwise use the connection context
                    if (socketInfoCopy.writeUserContext) {
                        ev.context = socketInfoCopy.writeUserContext;
                        GANL_EPOLL_DEBUG(fd, "Using write user context " << socketInfoCopy.writeUserContext << " for Write event");
                    } else {
                        ev.context = socketInfoCopy.context;
                    }

                    ev.buffer = nullptr; // Write operations don't use buffer reference
                    ev.bytesTransferred = 0; // No specific bytes count for readiness events
                    ev.error = 0;
                }

                // IMPORTANT: Always disable EPOLLOUT after generating Write event
                // The connection will re-register write interest via postWrite if it needs to write more
                if (socketInfoCopy.events & EPOLLOUT) {
                    GANL_EPOLL_DEBUG(fd, "Disabling EPOLLOUT after generating Write event.");
                    ErrorCode modError = 0;

                    // Clear the write user context since we've completed the operation
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        auto sockIt = sockets_.find(fd);
                        if (sockIt != sockets_.end()) {
                            if (sockIt->second.writeUserContext) {
                                GANL_EPOLL_DEBUG(fd, "Cleared write user context after generating Write event");
                                sockIt->second.writeUserContext = nullptr;
                            }
                        }
                    }

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

    // Create a NetworkAddress object from the client address
    NetworkAddress remoteAddr(reinterpret_cast<sockaddr*>(&clientAddr), clientLen);
    GANL_EPOLL_DEBUG(clientFd, "Client address: " << remoteAddr.toString());

    // Set non-blocking for the new socket
    if (!setNonBlocking(clientFd, error)) {
        GANL_EPOLL_DEBUG(clientFd, "Failed to set non-blocking on accepted socket: " << strerror(error));
        close(clientFd);
        return InvalidConnectionHandle;
    }

    // Optional: Set TCP_NODELAY?
    // int opt = 1;
    // setsockopt(clientFd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

    // --- Add to map under lock ---
    uint32_t initialEvents = EPOLLIN | EPOLLET;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_[clientFd] = SocketInfo{
            SocketType::Connection,
            context: nullptr,
            events: initialEvents,
            activeReadBuffer: nullptr,
            writeUserContext: nullptr
        };
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
