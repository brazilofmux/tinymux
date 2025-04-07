#include "kqueue_network_engine.h"
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h> // For TCP_NODELAY potentially
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h> // For strerror, memset
#include <errno.h>
#include <sys/time.h>
#include <vector> // For key iteration during shutdown

// Define a macro for debug logging
#ifndef NDEBUG // Only compile debug messages if NDEBUG is not defined
#define GANL_KQUEUE_DEBUG(fd, x) \
    do { std::cerr << "[Kqueue:" << (fd == kqueueFd_ ? "CTL" : std::to_string(fd)) << "] " << x << std::endl; } while (0)
#else
#define GANL_KQUEUE_DEBUG(fd, x) do {} while (0)
#endif

namespace ganl {

KqueueNetworkEngine::KqueueNetworkEngine()
    : keventResults_(128) { // Pre-allocate event result array
    GANL_KQUEUE_DEBUG(0, "Engine Created.");
}

KqueueNetworkEngine::~KqueueNetworkEngine() {
    GANL_KQUEUE_DEBUG(0, "Engine Destroyed.");
    shutdown();
}

bool KqueueNetworkEngine::initialize() {
    GANL_KQUEUE_DEBUG(0, "Initializing...");
    if (kqueueFd_ != -1) {
        GANL_KQUEUE_DEBUG(0, "Already initialized.");
        return true;
    }
    kqueueFd_ = kqueue();
    if (kqueueFd_ == -1) {
        std::cerr << "[Kqueue:CTL] FATAL: Failed to create kqueue instance: " << strerror(errno) << std::endl;
        return false;
    }
    GANL_KQUEUE_DEBUG(kqueueFd_, "kqueue() successful.");
    return true;
}

void KqueueNetworkEngine::shutdown() {
    GANL_KQUEUE_DEBUG(0, "Shutdown requested.");
    if (kqueueFd_ == -1) {
        GANL_KQUEUE_DEBUG(0, "Already shut down or not initialized.");
        return;
    }

    // Two-phase shutdown to avoid holding mutex during blocking system calls

    // Phase 1: Capture all the handles we need to close under the lock
    std::vector<int> fdToClose;
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Collect all socket fds (both listeners and connections)
        for (const auto& pair : sockets_) {
            fdToClose.push_back(pair.first);
        }

        // Count for logging
        size_t listenerCount = 0;
        for (const auto& pair : sockets_) {
            if (pair.second.type == SocketType::Listener) {
                listenerCount++;
            }
        }

        GANL_KQUEUE_DEBUG(0, "Closing " << listenerCount << " listeners and "
                           << (fdToClose.size() - listenerCount) << " connections...");
    }

    // Phase 2: Close the sockets and unregister from kqueue
    // Doing this outside the lock to prevent blocking while holding mutex
    for (int fd : fdToClose) {
        if (kqueueFd_ != -1) {
            // Unregister both read and write filters for all sockets
            struct kevent ev[2];
            EV_SET(&ev[0], fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
            EV_SET(&ev[1], fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
            kevent(kqueueFd_, ev, 2, nullptr, 0, nullptr); // Ignore errors
        }
        close(fd); // Ignore errors
    }

    // Phase 3: Clean up the map with lock
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_.clear();
    }

    // Close the kqueue fd
    GANL_KQUEUE_DEBUG(kqueueFd_, "Closing kqueue FD.");
    close(kqueueFd_);
    kqueueFd_ = -1;
    GANL_KQUEUE_DEBUG(0, "Shutdown complete.");
}


// --- Listener Management ---

ListenerHandle KqueueNetworkEngine::createListener(const std::string& host, uint16_t port, ErrorCode& error) {
    GANL_KQUEUE_DEBUG(0, "Creating listener for " << host << ":" << port);
    error = 0;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == -1) {
        error = errno;
        GANL_KQUEUE_DEBUG(0, "socket() failed: " << strerror(error));
        return InvalidListenerHandle;
    }
    GANL_KQUEUE_DEBUG(fd, "socket() successful.");

    // Set socket options: SO_REUSEADDR allows faster restarts
    int opt = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        error = errno;
        GANL_KQUEUE_DEBUG(fd, "setsockopt(SO_REUSEADDR) failed: " << strerror(error));
        close(fd);
        return InvalidListenerHandle;
    }
    GANL_KQUEUE_DEBUG(fd, "setsockopt(SO_REUSEADDR) successful.");

    // Set non-blocking
    if (!setNonBlocking(fd, error)) {
        GANL_KQUEUE_DEBUG(fd, "setNonBlocking failed: " << strerror(error));
        close(fd);
        return InvalidListenerHandle;
    }
     // Set close-on-exec
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
    } // Ignore error

    // Bind to address
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = INADDR_ANY;
        GANL_KQUEUE_DEBUG(fd, "Binding to INADDR_ANY:" << port);
    } else {
        GANL_KQUEUE_DEBUG(fd, "Binding to " << host << ":" << port);
        if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
            error = EINVAL;
            GANL_KQUEUE_DEBUG(fd, "inet_pton() failed for host: " << host);
            close(fd);
            return InvalidListenerHandle;
        }
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == -1) {
        error = errno;
        GANL_KQUEUE_DEBUG(fd, "bind() failed: " << strerror(error));
        close(fd);
        return InvalidListenerHandle;
    }
    GANL_KQUEUE_DEBUG(fd, "bind() successful.");

    // Store basic info (listener context is stored in startListening)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_[fd] = SocketInfo{
            SocketType::Listener,
            context: nullptr,     // Context will be set in startListening
            activeReadBuffer: nullptr,
            remoteAddress: ""
        };
    }
    GANL_KQUEUE_DEBUG(fd, "Listener created successfully.");

    return static_cast<ListenerHandle>(fd);
}

bool KqueueNetworkEngine::startListening(ListenerHandle listener, void* listenerContext, ErrorCode& error) {
    int fd = static_cast<int>(listener);
    error = 0;
    GANL_KQUEUE_DEBUG(fd, "Starting listening. Context=" << listenerContext);

    // Check if it's a known socket and is a listener
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto sockIt = sockets_.find(fd);
        if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Listener) {
            GANL_KQUEUE_DEBUG(fd, "Error: Invalid or non-listener handle.");
            error = EINVAL;
            return false;
        }
        // Store context in main socket map as well
         sockIt->second.context = listenerContext;
    }


    if (listen(fd, SOMAXCONN) == -1) {
        error = errno;
        GANL_KQUEUE_DEBUG(fd, "listen() failed: " << strerror(error));
        return false;
    }
    GANL_KQUEUE_DEBUG(fd, "listen() successful.");

    // Add listener FD to kqueue for reading (accept events)
    if (!updateKevent(fd, EVFILT_READ, EV_ADD | EV_ENABLE, error)) {
        GANL_KQUEUE_DEBUG(fd, "Failed to add listener to kqueue: " << strerror(error));
        return false;
    }

    // Context is already stored in sockets_ map

    GANL_KQUEUE_DEBUG(fd, "Listener started successfully.");
    return true;
}

void KqueueNetworkEngine::closeListener(ListenerHandle listener) {
    int fd = static_cast<int>(listener);
    GANL_KQUEUE_DEBUG(fd, "Closing listener...");

    // Remove from kqueue (ignore errors, might already be removed)
    if (kqueueFd_ != -1) {
        ErrorCode ignoredError = 0;
        updateKevent(fd, EVFILT_READ, EV_DELETE, ignoredError); // Attempt removal
    }

    // Close socket FD
    if (close(fd) == -1) {
        GANL_KQUEUE_DEBUG(fd, "Warning: close(fd) failed: " << strerror(errno));
    } else {
        GANL_KQUEUE_DEBUG(fd, "close(fd) successful.");
    }

    // Remove from map
    {
        std::lock_guard<std::mutex> lock(mutex_);
        sockets_.erase(fd);
    }
    GANL_KQUEUE_DEBUG(fd, "Listener closed and removed from map.");
}

// --- Connection Management ---

bool KqueueNetworkEngine::associateContext(ConnectionHandle conn, void* context, ErrorCode& error) {
    int fd = static_cast<int>(conn);
    error = 0;
    GANL_KQUEUE_DEBUG(fd, "Associating context=" << context);

    std::lock_guard<std::mutex> lock(mutex_);
    auto sockIt = sockets_.find(fd);
    if (sockIt == sockets_.end()) {
        GANL_KQUEUE_DEBUG(fd, "Error: Cannot associate context, socket not found.");
        error = EBADF;
        return false;
    }
    if (sockIt->second.type != SocketType::Connection) {
        GANL_KQUEUE_DEBUG(fd, "Error: Cannot associate context, socket is not a connection.");
        error = EINVAL;
        return false;
    }

    sockIt->second.context = context;
    GANL_KQUEUE_DEBUG(fd, "Context associated successfully.");
    return true;
}

void KqueueNetworkEngine::closeConnection(ConnectionHandle conn) {
    int fd = static_cast<int>(conn);
    GANL_KQUEUE_DEBUG(fd, "Closing connection...");

    // First check if socket is already closed/removed
    bool socketExists = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        socketExists = sockets_.find(fd) != sockets_.end();

        if (socketExists) {
            // Remove from map under lock
            sockets_.erase(fd);
            GANL_KQUEUE_DEBUG(fd, "Socket removed from map.");
        } else {
            GANL_KQUEUE_DEBUG(fd, "Socket not found in map, likely already closed.");
            return; // Skip further operations if already removed
        }
    }

    // Remove filters from kqueue (ignore errors)
    if (kqueueFd_ != -1) {
        ErrorCode ignoredError = 0;
        updateKevent(fd, EVFILT_READ, EV_DELETE, ignoredError);
        updateKevent(fd, EVFILT_WRITE, EV_DELETE, ignoredError);
    }

    // Attempt graceful shutdown first
    if (::shutdown(fd, SHUT_RDWR) == -1) {
        if (errno != ENOTCONN && errno != EBADF) {
            GANL_KQUEUE_DEBUG(fd, "Warning: shutdown(fd, SHUT_RDWR) failed: " << strerror(errno));
        } else {
            GANL_KQUEUE_DEBUG(fd, "Socket already disconnected or invalid.");
        }
    }

    // Then close the file descriptor
    if (close(fd) == -1) {
        if (errno == EBADF) {
            GANL_KQUEUE_DEBUG(fd, "File descriptor already closed or invalid.");
        } else {
            GANL_KQUEUE_DEBUG(fd, "Warning: close(fd) failed: " << strerror(errno));
        }
    } else {
        GANL_KQUEUE_DEBUG(fd, "close(fd) successful.");
    }

    GANL_KQUEUE_DEBUG(fd, "Connection closed and removed from maps.");
}


// --- I/O Operations ---

bool KqueueNetworkEngine::postRead(ConnectionHandle conn, IoBuffer& buffer, ErrorCode& error) {
    int fd = static_cast<int>(conn);
    error = 0;
    GANL_KQUEUE_DEBUG(fd, "postRead called with IoBuffer@" << &buffer << " (Ensuring EVFILT_READ enabled)");

    // Verify socket exists and is a connection
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto sockIt = sockets_.find(fd);
        if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Connection) {
            GANL_KQUEUE_DEBUG(fd, "Error: postRead on invalid/non-connection handle.");
            error = EBADF;
            return false;
        }

        // Store the IoBuffer reference
        sockIt->second.activeReadBuffer = &buffer;
    }

    // Ensure EVFILT_READ is enabled. Using EV_ADD will add it if not present,
    // or modify it if already present (effectively enabling if disabled).
    if (!updateKevent(fd, EVFILT_READ, EV_ADD | EV_ENABLE, error)) {
        GANL_KQUEUE_DEBUG(fd, "Failed to enable EVFILT_READ: " << strerror(error));

        // Reset buffer reference on failure
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto sockIt = sockets_.find(fd);
            if (sockIt != sockets_.end()) {
                sockIt->second.activeReadBuffer = nullptr;
            }
        }

        // If error is EEXIST or similar, it might already be enabled, which is okay.
        // However, updateKevent should handle this. If it returns false, assume failure.
        return false;
    }

    GANL_KQUEUE_DEBUG(fd, "EVFILT_READ ensured with IoBuffer@" << &buffer);
    return true;
}

bool KqueueNetworkEngine::postWrite(ConnectionHandle conn, const char* data, size_t length, void* userContext, ErrorCode& error) {
    int fd = static_cast<int>(conn);
    error = 0;
    GANL_KQUEUE_DEBUG(fd, "postWrite called" << (userContext ? " with user context" : "") << " (Enabling EVFILT_WRITE)");

    // Verify socket exists and is a connection and store user context
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto sockIt = sockets_.find(fd);
        if (sockIt == sockets_.end() || sockIt->second.type != SocketType::Connection) {
            GANL_KQUEUE_DEBUG(fd, "Error: postWrite on invalid/non-connection handle.");
            error = EBADF;
            return false;
        }

        // Store the user context for the write operation
        sockIt->second.writeUserContext = userContext;
        if (userContext) {
            GANL_KQUEUE_DEBUG(fd, "Stored write user context " << userContext);
        }
    }

    // Enable EVFILT_WRITE. EV_ADD | EV_ENABLE ensures it's added and active.
    // Kqueue's write filter is level-triggered by default, meaning it stays active
    // as long as the socket buffer has space. We disable it in processEvents
    // after generating the Write event.
    if (!updateKevent(fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, error)) {
        GANL_KQUEUE_DEBUG(fd, "Failed to enable EVFILT_WRITE: " << strerror(error));

        // Clear the user context on failure
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto sockIt = sockets_.find(fd);
            if (sockIt != sockets_.end()) {
                sockIt->second.writeUserContext = nullptr;
            }
        }

        return false;
    }

    GANL_KQUEUE_DEBUG(fd, "EVFILT_WRITE enabled.");
    return true;
}

// Backward compatibility implementation
bool KqueueNetworkEngine::postWrite(ConnectionHandle conn, const char* data, size_t length, ErrorCode& error) {
    return postWrite(conn, data, length, nullptr, error);
}

// --- Event Processing ---

int KqueueNetworkEngine::processEvents(int timeoutMs, IoEvent* events, int maxEvents) {
    // Prepare timeout struct
    struct timespec timeoutSpec;
    struct timespec* timeoutPtr = NULL;

    if (timeoutMs >= 0) {
        timeoutSpec.tv_sec = timeoutMs / 1000;
        timeoutSpec.tv_nsec = (timeoutMs % 1000) * 1000000;
        timeoutPtr = &timeoutSpec;
    }

    // GANL_KQUEUE_DEBUG(kqueueFd_, "Waiting for events with timeout=" << timeoutMs << "ms");
    int nfds = kevent(kqueueFd_, NULL, 0, keventResults_.data(), keventResults_.size(), timeoutPtr);
    // GANL_KQUEUE_DEBUG(kqueueFd_, "kevent returned " << nfds);

    if (nfds == -1) {
        if (errno == EINTR) {
            GANL_KQUEUE_DEBUG(kqueueFd_, "kevent interrupted by signal (EINTR).");
            return 0; // Interrupted, no events processed
        }
        std::cerr << "[Kqueue:CTL] CRITICAL: kevent failed: " << strerror(errno) << std::endl;
        return -1; // Indicate critical error
    }

    int eventCount = 0; // Number of IoEvent entries filled

    for (int i = 0; i < nfds && eventCount < maxEvents; ++i) {
        const struct kevent& kev = keventResults_[i];
        int fd = static_cast<int>(kev.ident);
        void* currentContext = nullptr; // Store context retrieved from map

        // Retrieve socket info and context under lock
        bool isListener = false;
        bool isConnection = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto sockIt = sockets_.find(fd);
            if (sockIt == sockets_.end()) {
                GANL_KQUEUE_DEBUG(fd, "Warning: Event received for unknown/closed socket. Ignoring.");
                // Might have been closed concurrently. Kqueue should remove automatically on close?
                continue;
            }
            currentContext = sockIt->second.context;
            if (sockIt->second.type == SocketType::Listener) {
                isListener = true;
            } else {
                isConnection = true;
            }
        } // Release lock

        GANL_KQUEUE_DEBUG(fd, "Processing kevent: filter=" << kev.filter << ", flags=0x" << std::hex << kev.flags << std::dec << ", data=" << kev.data);


        // --- Handle Listener Events ---
        if (isListener && kev.filter == EVFILT_READ) {
            GANL_KQUEUE_DEBUG(fd, "Listener EVFILT_READ - Accepting connections...");
            // Accept multiple connections if available (level-triggered)
             while (eventCount < maxEvents) {
                 ErrorCode acceptError = 0;
                 ConnectionHandle newConn = acceptConnection(fd, acceptError);

                 if (newConn != InvalidConnectionHandle) {
                      GANL_KQUEUE_DEBUG(fd, "Accepted new connection handle: " << newConn);
                      IoEvent& ev = events[eventCount++];
                      ev.type = IoEventType::Accept;
                      ev.listener = fd;
                      ev.connection = newConn;
                      ev.context = currentContext; // Listener context stored in SocketInfo
                      ev.bytesTransferred = 0;
                      ev.error = 0;
                      ev.remoteAddress = getRemoteNetworkAddress(newConn); // Set the remote address
                 } else {
                      // Error accepting
                      if (acceptError == EAGAIN || acceptError == EWOULDBLOCK) {
                           GANL_KQUEUE_DEBUG(fd, "accept() returned EAGAIN/EWOULDBLOCK. No more connections pending.");
                      } else {
                           // Real accept error - report?
                            std::cerr << "[Kqueue:" << fd << "] Error accepting connection: " << strerror(acceptError) << std::endl;
                            // Generate an error event for the listener?
                            if (eventCount < maxEvents) {
                                IoEvent& ev = events[eventCount++];
                                ev.type = IoEventType::Error;
                                ev.listener = fd;
                                ev.connection = InvalidConnectionHandle;
                                ev.error = acceptError;
                                ev.context = currentContext;
                            }
                      }
                      break; // Stop trying to accept on this listener for now
                 }
            } // end while accept loop
        } // end if listener


        // --- Handle Connection Events ---
        else if (isConnection) {
            ConnectionHandle connHandle = static_cast<ConnectionHandle>(fd);
            bool connectionClosed = false; // Flag to prevent processing read/write after close/error

            // Check for EV_EOF first (often indicates graceful close or error)
            if (kev.flags & EV_EOF) {
                 GANL_KQUEUE_DEBUG(fd, "EV_EOF detected.");
                 if (eventCount < maxEvents) {
                     IoEvent& ev = events[eventCount++];
                     ev.type = IoEventType::Close; // Treat EOF as Close
                     ev.connection = connHandle;
                     ev.context = currentContext;
                     ev.bytesTransferred = 0;
                     // kev.data often contains the socket error code on EOF
                     ev.error = static_cast<ErrorCode>(kev.data);

                     // Include IoBuffer in Close event if available
                     IoBuffer* bufferRef = nullptr;
                     {
                         std::lock_guard<std::mutex> lock(mutex_);
                         auto sockIt = sockets_.find(fd);
                         if (sockIt != sockets_.end()) {
                             bufferRef = sockIt->second.activeReadBuffer;
                             // Clear the buffer reference after generating the event
                             sockIt->second.activeReadBuffer = nullptr;
                         }
                     }

                     if (bufferRef) {
                         ev.buffer = bufferRef;
                         GANL_KQUEUE_DEBUG(fd, "Generated Close event with IoBuffer@" << bufferRef);
                     } else {
                         GANL_KQUEUE_DEBUG(fd, "Generated Close event without IoBuffer");
                     }
                 }
                 connectionClosed = true;
            }

            // Check for EV_ERROR (independent of EOF)
            if (kev.flags & EV_ERROR) {
                GANL_KQUEUE_DEBUG(fd, "EV_ERROR detected.");
                // If we already generated a Close event from EOF, don't generate another error event unless needed?
                // Let's prioritize Error if EV_ERROR is set. Overwrite previous Close if necessary.
                int currentEventIndex = connectionClosed ? eventCount - 1 : eventCount;

                // Only get the buffer reference if we haven't already processed it in EV_EOF
                IoBuffer* bufferRef = nullptr;
                if (!connectionClosed) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto sockIt = sockets_.find(fd);
                    if (sockIt != sockets_.end()) {
                        bufferRef = sockIt->second.activeReadBuffer;
                        // Clear the buffer reference after generating the event
                        sockIt->second.activeReadBuffer = nullptr;
                    }
                }

                if (currentEventIndex < maxEvents) {
                    if (!connectionClosed) eventCount++; // Only increment if not overwriting Close
                    IoEvent& ev = events[currentEventIndex];
                    ev.type = IoEventType::Error;
                    ev.connection = connHandle;
                    ev.context = currentContext;
                    ev.bytesTransferred = 0;
                    // kev.data contains the error code when EV_ERROR is set
                    ev.error = static_cast<ErrorCode>(kev.data);

                    // Include buffer in Error event if available
                    if (bufferRef) {
                        ev.buffer = bufferRef;
                        GANL_KQUEUE_DEBUG(fd, "Generated Error event with IoBuffer@" << bufferRef);
                    } else {
                        GANL_KQUEUE_DEBUG(fd, "Generated Error event. Error code: " << ev.error << " (" << getErrorString(ev.error) << ")");
                    }
                }
                connectionClosed = true;
            }

            // Handle Read Readiness (if not closed/errored)
            if (!connectionClosed && kev.filter == EVFILT_READ) {
                GANL_KQUEUE_DEBUG(fd, "EVFILT_READ detected. Available bytes hint: " << kev.data);
                 if (eventCount < maxEvents) {
                     IoEvent& ev = events[eventCount++];
                     ev.type = IoEventType::Read;
                     ev.connection = connHandle;
                     ev.context = currentContext;
                     // kev.data provides hint of bytes available, but we signal readiness (0)
                     ev.bytesTransferred = 0; // Signal readiness
                     ev.error = 0;

                     // Include IoBuffer in the event
                     IoBuffer* bufferRef = nullptr;
                     {
                         std::lock_guard<std::mutex> lock(mutex_);
                         auto sockIt = sockets_.find(fd);
                         if (sockIt != sockets_.end()) {
                             bufferRef = sockIt->second.activeReadBuffer;
                             // Clear the buffer reference after generating the event
                             sockIt->second.activeReadBuffer = nullptr;
                         }
                     }

                     if (bufferRef) {
                         ev.buffer = bufferRef;
                         GANL_KQUEUE_DEBUG(fd, "Generated Read event with IoBuffer@" << bufferRef);
                     } else {
                         GANL_KQUEUE_DEBUG(fd, "Generated Read event without IoBuffer");
                     }
                     // NOTE: Connection::handleRead *must* loop read() until EAGAIN because filter is level-triggered
                 } else {
                     GANL_KQUEUE_DEBUG(fd, "Max events reached, skipping EVFILT_READ handling.");
                 }
            }

            // Handle Write Readiness (if not closed/errored)
            if (!connectionClosed && kev.filter == EVFILT_WRITE) {
                GANL_KQUEUE_DEBUG(fd, "EVFILT_WRITE detected. Available buffer space hint: " << kev.data);
                 if (eventCount < maxEvents) {
                    IoEvent& ev = events[eventCount++];
                    ev.type = IoEventType::Write;
                    ev.connection = connHandle;

                    // Check if a write user context is available
                    void* writeContext = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        auto sockIt = sockets_.find(fd);
                        if (sockIt != sockets_.end()) {
                            writeContext = sockIt->second.writeUserContext;
                        }
                    }

                    // Use the write user context if available, otherwise use the connection context
                    if (writeContext) {
                        ev.context = writeContext;
                        GANL_KQUEUE_DEBUG(fd, "Using write user context " << writeContext << " for Write event");
                    } else {
                        ev.context = currentContext;
                    }

                    // kev.data provides hint of buffer space, but we signal readiness (0)
                    ev.bytesTransferred = 0; // Signal readiness
                    ev.error = 0;
                    ev.buffer = nullptr; // Write operations don't use buffer reference
                    GANL_KQUEUE_DEBUG(fd, "Generated Write event.");
                    // NOTE: Connection::handleWrite *must* loop write() until EAGAIN or buffer empty

                    // IMPORTANT: Disable EVFILT_WRITE after generating the event
                    // to prevent spinning, as it's level-triggered.
                    ErrorCode disableError = 0;
                    GANL_KQUEUE_DEBUG(fd, "Disabling EVFILT_WRITE after generating Write event.");

                    // Clear the write user context since we've completed the operation
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        auto sockIt = sockets_.find(fd);
                        if (sockIt != sockets_.end()) {
                            if (sockIt->second.writeUserContext) {
                                GANL_KQUEUE_DEBUG(fd, "Cleared write user context after generating Write event");
                                sockIt->second.writeUserContext = nullptr;
                            }
                        }
                    }

                    // Use EV_DISABLE instead of EV_DELETE if we might need it again soon.
                    // Using EV_DELETE might be simpler if postWrite always uses EV_ADD. Let's use EV_DISABLE.
                    if (!updateKevent(fd, EVFILT_WRITE, EV_DISABLE, disableError)) {
                          GANL_KQUEUE_DEBUG(fd, "Error disabling EVFILT_WRITE: " << getErrorString(disableError));
                          // Consider generating an error event or closing?
                    } else {
                          GANL_KQUEUE_DEBUG(fd, "EVFILT_WRITE disabled.");
                    }

                 } else {
                      GANL_KQUEUE_DEBUG(fd, "Max events reached, skipping EVFILT_WRITE handling.");
                 }
            }
        } // end if connection
    } // end for loop through kevents

    return eventCount;
}


// --- Utility Methods ---

std::string KqueueNetworkEngine::getRemoteAddress(ConnectionHandle conn) {
    // We can leverage the NetworkAddress class for consistent formatting
    return getRemoteNetworkAddress(conn).toString();
}

NetworkAddress KqueueNetworkEngine::getRemoteNetworkAddress(ConnectionHandle conn) {
    int fd = static_cast<int>(conn);
    sockaddr_storage addrStorage; // Use sockaddr_storage for IPv4/IPv6 compatibility
    socklen_t addrLen = sizeof(addrStorage);

    if (getpeername(fd, reinterpret_cast<sockaddr*>(&addrStorage), &addrLen) == -1) {
        GANL_KQUEUE_DEBUG(fd, "getpeername failed: " << strerror(errno));
        return NetworkAddress(); // Return invalid address
    }

    // Create and return NetworkAddress from the raw sockaddr
    return NetworkAddress(reinterpret_cast<sockaddr*>(&addrStorage), addrLen);
}

std::string KqueueNetworkEngine::getErrorString(ErrorCode error) {
    return strerror(error);
}

bool KqueueNetworkEngine::setNonBlocking(int fd, ErrorCode& error) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        error = errno;
        GANL_KQUEUE_DEBUG(fd, "fcntl(F_GETFL) failed: " << strerror(error));
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        error = errno;
        GANL_KQUEUE_DEBUG(fd, "fcntl(F_SETFL, O_NONBLOCK) failed: " << strerror(error));
        return false;
    }
    GANL_KQUEUE_DEBUG(fd, "Set non-blocking successfully.");
    return true;
}

ConnectionHandle KqueueNetworkEngine::acceptConnection(ListenerHandle listener, ErrorCode& error) {
    int listenerFd = static_cast<int>(listener);
    sockaddr_storage clientAddr;
    socklen_t clientLen = sizeof(clientAddr);
    error = 0;

    int clientFd = accept(listenerFd, reinterpret_cast<sockaddr*>(&clientAddr), &clientLen);

    if (clientFd == -1) {
        error = errno;
        // Don't log EAGAIN/EWOULDBLOCK as errors here, handled in processEvents
        return InvalidConnectionHandle;
    }
    GANL_KQUEUE_DEBUG(listenerFd, "accept() successful. New FD: " << clientFd);

    // Create a NetworkAddress object from the client address immediately after accept
    NetworkAddress remoteAddr(reinterpret_cast<sockaddr*>(&clientAddr), clientLen);
    GANL_KQUEUE_DEBUG(clientFd, "Client address: " << remoteAddr.toString());

    // Set non-blocking and close-on-exec for the new socket
    if (!setNonBlocking(clientFd, error)) {
        GANL_KQUEUE_DEBUG(clientFd, "Failed to set non-blocking on accepted socket: " << strerror(error));
        close(clientFd);
        return InvalidConnectionHandle;
    }
    int flags = fcntl(clientFd, F_GETFD, 0);
    if (flags != -1) {
        fcntl(clientFd, F_SETFD, flags | FD_CLOEXEC);
    }

    // Add the new connection socket to kqueue for reading
    // Use EV_ADD | EV_ENABLE. Default is level-triggered.
    if (!updateKevent(clientFd, EVFILT_READ, EV_ADD | EV_ENABLE, error)) {
        GANL_KQUEUE_DEBUG(clientFd, "Failed to add new connection to kqueue: " << strerror(error));
        close(clientFd);
        return InvalidConnectionHandle;
    }
    GANL_KQUEUE_DEBUG(clientFd, "Added EVFILT_READ for new connection.");

    // Store info about the new socket
    {
        std::lock_guard<std::mutex> lock(mutex_);

        // Use the address we already captured
        std::string remoteAddrStr = remoteAddr.toString();

        sockets_[clientFd] = SocketInfo{
            SocketType::Connection,
            context: nullptr,
            activeReadBuffer: nullptr,
            remoteAddress: remoteAddrStr,
            writeUserContext: nullptr
        };
    }
    GANL_KQUEUE_DEBUG(clientFd, "New connection socket registered.");

    return static_cast<ConnectionHandle>(clientFd);
}

// Helper to add/modify/delete kqueue events
bool KqueueNetworkEngine::updateKevent(int fd, short filter, u_short flags, ErrorCode& error) {
     if (kqueueFd_ == -1) {
        error = EBADF; // Kqueue not initialized
        return false;
     }

     struct kevent ev;
     // EV_SET(&ev, ident, filter, flags, fflags, data, udata);
     // udata can be used to pass context pointer, but requires care as kevent() takes an array.
     // Using the map lookup in processEvents is safer/simpler for now.
     EV_SET(&ev, fd, filter, flags, 0, 0, nullptr);

     GANL_KQUEUE_DEBUG(fd, "Updating kevent: filter=" << filter << ", flags=0x" << std::hex << flags << std::dec);

     if (kevent(kqueueFd_, &ev, 1, nullptr, 0, nullptr) == -1) {
        error = errno;
        // Don't log ENOENT error for DELETE operations, it's expected if already removed.
        if (!(flags & EV_DELETE && error == ENOENT)) {
             GANL_KQUEUE_DEBUG(fd, "kevent() update failed: " << strerror(error));
        }
        // Check for EBADF which might mean the fd was closed concurrently
        if (error == EBADF) {
            std::lock_guard<std::mutex> lock(mutex_);
            sockets_.erase(fd); // Clean up map if fd is bad
        }
        return false;
     }

     GANL_KQUEUE_DEBUG(fd, "kevent() update successful.");
     return true;
}


} // namespace ganl
