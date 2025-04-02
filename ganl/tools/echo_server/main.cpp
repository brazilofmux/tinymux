#include <ganl/network_engine.h>          // Base interface
#include <ganl/secure_transport.h>
#include <ganl/protocol_handler.h>
#include <ganl/session_manager.h>
#include <ganl/connection.h>
#include <../src/platform/factory/network_engine_factory.h>
#include <../src/ssl/factory/secure_transport_factory.h>
#include "../src/protocol/telnet/telnet_protocol_handler.h"

#include <iostream>
#include <map>
#include <string>
#include <vector>
#include <memory>
#include <thread>
#include <mutex>
#include <chrono>
#include <csignal>
#include <cstring>
#include <condition_variable>
#include <atomic>
#include <set>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

using namespace ganl;

// --- Debug Macros ---
#ifndef NDEBUG
#define GANL_MAIN_DEBUG(x) \
    do { std::cerr << "[Main] " << x << std::endl; } while (0)
#define GANL_SESSION_DEBUG(id, x) \
    do { std::cerr << "[SessionMgr:" << id << "] " << x << std::endl; } while (0)
#else
#define GANL_MAIN_DEBUG(x) do {} while (0)
#define GANL_SESSION_DEBUG(id, x) do {} while (0)
#endif

// --- EchoSessionManager (Keep as is) ---
class EchoSessionManager : public SessionManager {
public:
    EchoSessionManager() = default;
    ~EchoSessionManager() override = default;

    bool initialize() override {
        GANL_SESSION_DEBUG(0, "Initializing.");
        nextSessionId_ = 1;
        return true;
    }

    void shutdown() override {
        GANL_SESSION_DEBUG(0, "Shutdown requested. Closing active connections.");
        std::lock_guard<std::mutex> lock(mutex_);
        activeConnectionCount_ = sessions_.size(); // Track how many connections we asked to close
        GANL_SESSION_DEBUG(0, "Asking " << activeConnectionCount_ << " connections to close.");
        for (auto& pair : sessions_) {
            if (pair.second.connection) {
                // Let the Connection object handle the closing reason notification
                pair.second.connection->close(DisconnectReason::ServerShutdown);
            }
        }
        // Don't clear sessions_ here; let onConnectionClose handle removal.
    }

    SessionId onConnectionOpen(ConnectionHandle conn, const std::string& remoteAddress) override {
        std::lock_guard<std::mutex> lock(mutex_);
        SessionId id = nextSessionId_++;

        Session session;
        session.id = id;
        session.connectionHandle = conn; // Store handle immediately
        session.remoteAddress = remoteAddress;
        session.state = SessionState::Connecting; // Start as Connecting or Handshaking? Let's use Connecting.
        session.stats.connectedAt = time(nullptr);
        session.stats.lastActivity = session.stats.connectedAt;
        // session.connection is set later via registerConnection

        sessions_[id] = std::move(session); // Move session data
        connectionMap_[conn] = id; // Map handle to session ID

        GANL_SESSION_DEBUG(id, "Session created for handle " << conn << " from " << remoteAddress);

        // Broadcast connection message (maybe wait until Running state?)
        // For simplicity, broadcast now.
        std::string message = "*** User " + std::to_string(id) + " (" + remoteAddress + ") has connected ***\r\n";
        // Can't call broadcastMessage yet as connection pointers aren't set.
        // Let's broadcast in registerConnection or when state becomes Running.

        return id;
    }

    // Called by Connection::initialize to associate the SessionId with the created Connection object
    void registerConnection(SessionId sessionId, std::shared_ptr<ConnectionBase> connection) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(sessionId);
        if (it != sessions_.end()) {
            GANL_SESSION_DEBUG(sessionId, "Registering Connection object pointer.");
            it->second.connection = connection;
            it->second.state = SessionState::Connected; // Or maybe Handshaking if TLS/Telnet is ongoing? Connected is simpler for echo.

            // Now we can broadcast the connection message safely
             std::string message = "*** User " + std::to_string(sessionId) + " (" + it->second.remoteAddress + ") has connected ***\r\n";
             broadcastMessage_nolock(message, sessionId); // Use nolock version

        } else {
            GANL_SESSION_DEBUG(sessionId, "Error: Tried to register connection for non-existent session.");
            // Close the connection immediately if session is gone?
            if(connection) {
                connection->close(DisconnectReason::ServerShutdown);
            }
        }
    }

    void onDataReceived(SessionId sessionId, const std::string& commandLine) /* override */ { // Make sure this matches base if it's virtual
        std::unique_lock<std::mutex> lock(mutex_); // Use unique_lock if we might unlock/relock

        auto it = sessions_.find(sessionId);
        if (it == sessions_.end()) {
            GANL_SESSION_DEBUG(sessionId, "Warning: Data received for non-existent session.");
            return;
        }

        Session& session = it->second;
        session.stats.lastActivity = time(nullptr);
        session.stats.commandsProcessed++;
        // Assuming commandLine is already processed (e.g., one line) by ProtocolHandler
        // session.stats.bytesReceived += commandLine.length(); // TODO: Get actual bytes from Connection/Protocol

        GANL_SESSION_DEBUG(sessionId, "Received data: '" << commandLine << "'");

        if (commandLine.empty()) {
             GANL_SESSION_DEBUG(sessionId, "Ignoring empty command line.");
            return;
        }

        // Process commands
        if (commandLine == "/quit" || commandLine == "/exit") {
            std::string nick = session.nickname.empty() ? std::to_string(sessionId) : session.nickname;
            std::string message = "*** " + nick + " has disconnected (Quit) ***\r\n";
            GANL_SESSION_DEBUG(sessionId, "User requested quit.");
            broadcastMessage_nolock(message, sessionId);
            // Unlock before calling connection->close to avoid potential deadlock if close calls back into SessionManager
            lock.unlock();
            session.connection->close(DisconnectReason::UserQuit);
            return;
        } else if (commandLine == "/who") {
            std::string message = "*** Connected users: (" + std::to_string(sessions_.size()) + ") ***\r\n";
            for (const auto& pair : sessions_) {
                 std::string nick = pair.second.nickname.empty() ? std::to_string(pair.first) : pair.second.nickname;
                message += "- " + nick + " (" + pair.second.remoteAddress + ")\r\n";
            }
            lock.unlock(); // Unlock before sending
            sendToSession(sessionId, message);
            return;
        } else if (commandLine.find("/nick ") == 0 && commandLine.length() > 6) {
            std::string oldNick = session.nickname.empty() ? std::to_string(sessionId) : session.nickname;
            session.nickname = commandLine.substr(6); // TODO: Validate nickname
            GANL_SESSION_DEBUG(sessionId, "User changed nick to '" << session.nickname << "'");
            std::string message = "*** " + oldNick + " is now known as " + session.nickname + " ***\r\n";
            broadcastMessage_nolock(message, InvalidSessionId); // Broadcast to all
            return; // Nick change doesn't echo the command itself
        }

        // Echo message back to sender and broadcast to others
        std::string nickname = session.nickname.empty() ? std::to_string(sessionId) : session.nickname;
        std::string message = nickname + "> " + commandLine + "\r\n"; // Add prompt-like format

        broadcastMessage_nolock(message, InvalidSessionId); // Broadcast to all (including sender)
    }


    void onConnectionClose(SessionId sessionId, DisconnectReason reason) override {
        std::lock_guard<std::mutex> lock(mutex_);
        GANL_SESSION_DEBUG(sessionId, "Connection closed event received. Reason: " << static_cast<int>(reason));

        auto it = sessions_.find(sessionId);
        if (it == sessions_.end()) {
             GANL_SESSION_DEBUG(sessionId, "Warning: Close event for non-existent session.");
            return; // Already removed
        }

        // Remove connection mapping first
        connectionMap_.erase(it->second.connectionHandle);

        // Get nickname before erasing session
        std::string nick = it->second.nickname.empty() ? std::to_string(sessionId) : it->second.nickname;

        // Remove session itself
        sessions_.erase(it);
        activeConnectionCount_--; // Decrement count for shutdown tracking

        GANL_SESSION_DEBUG(sessionId, "Session removed. Active connections remaining: " << activeConnectionCount_);

        // Broadcast disconnection message (only if not server shutdown)
        if (reason != DisconnectReason::ServerShutdown) {
            std::string message = "*** " + nick + " has disconnected (" + disconnectReasonToString(reason) + ") ***\r\n";
            broadcastMessage_nolock(message, InvalidSessionId); // Use nolock version
        }

        // Notify shutdown wait if necessary
        if (activeConnectionCount_ == 0) {
            shutdownCV_.notify_all();
        }
    }

    bool sendToSession(SessionId sessionId, const std::string& message) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return sendToSession_nolock(sessionId, message);
    }

    bool broadcastMessage(const std::string& message, SessionId except = InvalidSessionId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return broadcastMessage_nolock(message, except);
    }

    bool disconnectSession(SessionId sessionId, DisconnectReason reason) override {
        std::shared_ptr<ConnectionBase> conn_ptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sessions_.find(sessionId);
            if (it == sessions_.end() || !it->second.connection) {
                GANL_SESSION_DEBUG(sessionId, "Cannot disconnect session, not found or no connection ptr.");
                return false;
            }
            conn_ptr = it->second.connection; // Copy shared_ptr
        } // Unlock mutex

        GANL_SESSION_DEBUG(sessionId, "Externally disconnecting session.");
        conn_ptr->close(reason); // Call close outside the lock
        return true;
    }

    // --- Stubs for unused methods ---
    bool authenticateSession(SessionId sessionId, ConnectionHandle conn,
                           const std::string& username, const std::string& password) override { return true; }
    void onAuthenticationSuccess(SessionId sessionId, int playerId) override {}
    int getPlayerId(SessionId sessionId) override { return 0; }

    SessionState getSessionState(SessionId sessionId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(sessionId);
        return (it == sessions_.end()) ? SessionState::Closed : it->second.state;
    }

    SessionStats getSessionStats(SessionId sessionId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(sessionId);
        return (it == sessions_.end()) ? SessionStats{} : it->second.stats;
    }

    ConnectionHandle getConnectionHandle(SessionId sessionId) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = sessions_.find(sessionId);
        return (it == sessions_.end()) ? InvalidConnectionHandle : it->second.connectionHandle;
    }

    // --- Access Control Stubs ---
    bool isAddressAllowed(const std::string& address) override { return true; }
    bool isAddressRegistered(const std::string& address) override { return false; }
    bool isAddressForbidden(const std::string& address) override { return false; }
    bool isAddressSuspect(const std::string& address) override { return false; }
    std::string getLastSessionErrorString(SessionId sessionId) override { return "No error"; }

    // --- Shutdown Synchronization ---
    void waitForAllClosed(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        GANL_SESSION_DEBUG(0, "Waiting for " << activeConnectionCount_ << " connections to close...");
        shutdownCV_.wait_for(lock, timeout, [this]{ return activeConnectionCount_ == 0; });
        GANL_SESSION_DEBUG(0, "Finished waiting. " << activeConnectionCount_ << " connections still active (if > 0).");
    }

private:
    struct Session {
        SessionId id;
        ConnectionHandle connectionHandle;
        std::string remoteAddress;
        std::string nickname;
        SessionState state;
        SessionStats stats;
        std::shared_ptr<ConnectionBase> connection; // Keeps connection object alive
    };

    std::mutex mutex_;
    std::map<SessionId, Session> sessions_;
    std::map<ConnectionHandle, SessionId> connectionMap_;
    std::atomic<SessionId> nextSessionId_{1};
    std::atomic<size_t> activeConnectionCount_{0}; // Track active connections for shutdown
    std::condition_variable shutdownCV_; // To signal when count reaches zero

    // Non-locking versions for internal use when lock is already held
    bool sendToSession_nolock(SessionId sessionId, const std::string& message) {
        auto it = sessions_.find(sessionId);
        if (it == sessions_.end() || !it->second.connection) {
            return false;
        }
        it->second.connection->sendDataToClient(message);
        it->second.stats.bytesSent += message.length(); // Approx. Needs actual bytes written event?
        return true;
    }

    bool broadcastMessage_nolock(const std::string& message, SessionId except = InvalidSessionId) {
        GANL_SESSION_DEBUG(0, "Broadcasting (except " << except << "): '" << message.substr(0, message.find('\r')) << "'");
        for (auto& pair : sessions_) {
            if (pair.first != except) {
                sendToSession_nolock(pair.first, message);
            }
        }
        return true;
    }

    std::string disconnectReasonToString(DisconnectReason reason) {
         switch(reason) {
             case DisconnectReason::UserQuit: return "User Quit";
             case DisconnectReason::Timeout: return "Timeout";
             case DisconnectReason::NetworkError: return "Network Error";
             case DisconnectReason::ProtocolError: return "Protocol Error";
             case DisconnectReason::TlsError: return "TLS Error";
             case DisconnectReason::ServerShutdown: return "Server Shutdown";
             case DisconnectReason::AdminKick: return "Kicked";
             case DisconnectReason::GameFull: return "Game Full";
             case DisconnectReason::LoginFailed: return "Login Failed";
             default: return "Unknown";
         }
    }

};
// --- End EchoSessionManager ---


// --- Globals and Signal Handling (Keep as is) ---
std::atomic<bool> shutdownRequested{false};

void signalHandler(int signal) {
    GANL_MAIN_DEBUG("Received signal " << signal << ". Requesting shutdown...");
    shutdownRequested = true;
}
// --- End Globals ---

// --- Usage Function (Keep as is) ---
void printUsage(const char* programName) {
    std::cout << "Usage: " << programName << " [OPTIONS]" << std::endl;
    std::cout << "Options:" << std::endl;
    std::cout << "  --port PORT             Port to listen on (default: 4000)" << std::endl;
    std::cout << "  --tls                   Enable TLS (requires --cert and --key)" << std::endl;
    std::cout << "  --cert CERT_FILE        TLS certificate file" << std::endl;
    std::cout << "  --key KEY_FILE          TLS key file" << std::endl;
    std::cout << "  --engine TYPE           Network engine (auto, select, epoll, kqueue, iocp)" << std::endl;
    std::cout << "                          Available: " << NetworkEngineFactory::getAvailableEngineTypes() << std::endl; // Show available
    std::cout << "  --help                  Show this help message" << std::endl;
}
// --- End Usage ---


int main(int argc, char* argv[]) {
    // --- Argument Parsing (Keep as is, maybe add available types to help) ---
    uint16_t port = 4000;
    bool useTls = false;
    std::string certFile;
    std::string keyFile;
    NetworkEngineType engineType = NetworkEngineType::Auto; // Default to Auto

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "--port") && i + 1 < argc) {
            try {
                port = static_cast<uint16_t>(std::stoi(argv[++i]));
            } catch (const std::exception& e) {
                std::cerr << "Error parsing port: " << e.what() << std::endl;
                return 1;
            }
        }
        else if (arg == "--tls") { useTls = true; }
        else if ((arg == "--cert") && i + 1 < argc) { certFile = argv[++i]; }
        else if ((arg == "--key") && i + 1 < argc) { keyFile = argv[++i]; }
        else if ((arg == "--engine") && i + 1 < argc) {
            std::string engineStr = argv[++i];
            if (engineStr == "auto") engineType = NetworkEngineType::Auto;
            else if (engineStr == "select") engineType = NetworkEngineType::Select;
            else if (engineStr == "epoll") engineType = NetworkEngineType::Epoll;
            else if (engineStr == "kqueue") engineType = NetworkEngineType::Kqueue;
            else if (engineStr == "iocp") engineType = NetworkEngineType::IOCP;
            else {
                std::cerr << "Error: Unknown engine type '" << engineStr << "'" << std::endl;
                std::cerr << NetworkEngineFactory::getAvailableEngineTypes() << std::endl;
                return 1;
            }
        } else if (arg == "--help") {
            printUsage(argv[0]);
            // NetworkEngineFactory::getAvailableEngineTypes() is now called within printUsage
            return 0;
        } else { /* ... unknown argument ... */ }
    }
    // --- End Argument Parsing ---

    // --- Validation (Keep as is) ---
    if (useTls && (certFile.empty() || keyFile.empty())) { /* ... error ... */ }
    // --- End Validation ---

    // --- Signal Handling ---
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

#if defined(_WIN32) || defined(WIN32)
    // Windows doesn't have SIGPIPE or sigaction
    // We handle broken pipes through socket error handling instead
    GANL_MAIN_DEBUG("Windows platform - SIGPIPE handling not applicable");
#else
    // Unix-specific SIGPIPE handling
    struct sigaction sa {};
    sa.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &sa, nullptr) == -1) {
        perror("Failed to ignore SIGPIPE");
        // Non-fatal, continue
    }
#endif
    // --- End Signal Handling ---

    GANL_MAIN_DEBUG("Starting server initialization...");

    // Create the network engine using the factory
    GANL_MAIN_DEBUG("Creating network engine (Type requested: " << static_cast<int>(engineType) << ")...");
    std::unique_ptr<NetworkEngine> networkEngine = NetworkEngineFactory::createEngine(engineType);
    if (!networkEngine) {
        std::cerr << "FATAL: Failed to create requested/available network engine." << std::endl;
        std::cerr << NetworkEngineFactory::getAvailableEngineTypes() << std::endl;
        return 1;
    }

    // Create the secure transport using the factory if TLS is enabled
    std::unique_ptr<SecureTransport> secureTransport;
    if (useTls) {
        GANL_MAIN_DEBUG("Creating secure transport...");
        secureTransport = SecureTransportFactory::createTransport(SecureTransportType::Auto);
        if (!secureTransport) {
            std::cerr << "FATAL: Failed to create secure transport." << std::endl;
            std::cerr << SecureTransportFactory::getAvailableTransportTypes() << std::endl;
            networkEngine->shutdown();
            return 1;
        }

        // Create TLS configuration
        TlsConfig tlsConfig;
        tlsConfig.certificateFile = certFile;
        tlsConfig.keyFile = keyFile;
        tlsConfig.verifyPeer = false; // For testing, disable peer verification

        if (!secureTransport->initialize(tlsConfig)) {
            std::cerr << "FATAL: Failed to initialize secure transport." << std::endl;
            networkEngine->shutdown();
            return 1;
        }
    }

    SecureTransport* secureTransportPtr = secureTransport ? secureTransport.get() : nullptr;

    ganl::TelnetProtocolHandler protocolHandler;
    EchoSessionManager sessionManager;
    GANL_MAIN_DEBUG("Core components created.");
    // --- End Create Components ---


    // --- Initialize Components (Keep as is, use networkEngine pointer) ---
    GANL_MAIN_DEBUG("Initializing NetworkEngine...");
    if (!networkEngine->initialize()) { /* ... error ... */ }

    GANL_MAIN_DEBUG("Initializing SessionManager...");
    if (!sessionManager.initialize()) { /* ... error ... */ }
    // --- End Initialize Components ---


    // --- Listener Setup (Keep as is, use networkEngine pointer) ---
    ErrorCode error = 0;
    GANL_MAIN_DEBUG("Creating listener on 0.0.0.0:" << port << "...");
    ListenerHandle listener = networkEngine->createListener("0.0.0.0", port, error);
    if (listener == InvalidListenerHandle) { /* ... error ... */ }

    GANL_MAIN_DEBUG("Starting listener...");
    if (!networkEngine->startListening(listener, nullptr, error)) { /* ... error ... */ }
    // --- End Listener Setup ---

    // --- Startup Messages (Keep as is) ---
    std::cout << "=== EchoServer started ===" << std::endl;
    // Report actual engine type used if desired (requires getType() method or similar)
    // std::cout << "Network engine: " << networkEngine->getTypeName() << std::endl; // Example
    std::cout << NetworkEngineFactory::getAvailableEngineTypes() << std::endl; // Inform user
    std::cout << "Listening on port: " << port << std::endl;
    std::cout << "TLS: " << (useTls ? "Enabled" : "Disabled") << std::endl;
    GANL_MAIN_DEBUG("Entering main event loop...");
    // --- End Startup Messages ---


    // --- Main Event Loop (Keep as is, use networkEngine pointer) ---
    std::vector<IoEvent> events(64);
    while (!shutdownRequested) {
        int count = networkEngine->processEvents(100, events.data(), static_cast<int>(events.size()));
        if (count < 0) { /* ... error ... */ break; }
        for (int i = 0; i < count; ++i) {
            const IoEvent& event = events[i];
            switch (event.type) {
                case IoEventType::Accept: {
                    ConnectionHandle connHandle = event.connection;

                    // ... (Pass *networkEngine by reference) ...

                    std::shared_ptr<ConnectionBase> connection = ConnectionFactory::createConnection(
                        event.connection,
                        *networkEngine,
                        secureTransportPtr, // This can be null if TLS is disabled
                        protocolHandler,
                        sessionManager
                    );

                    // Initialize the connection with TLS flag
                    if (!connection->initialize(useTls)) {
                        GANL_MAIN_DEBUG("Failed to initialize Connection object for handle " << connHandle << ". Closing.");
                        networkEngine->closeConnection(connHandle);
                        continue;
                    }

                    // Get the SessionId created by connection->initialize()
                    SessionId sessionId = connection->getSessionId();
                    if (sessionId == InvalidSessionId) {
                        GANL_MAIN_DEBUG("Connection " << connHandle << " rejected by SessionManager during initialization.");
                        continue;
                    }

                    GANL_MAIN_DEBUG("Connection " << connHandle << " initialized successfully. SessionId: " << sessionId);

                    // Register the created Connection shared_ptr with the SessionManager
                    sessionManager.registerConnection(sessionId, connection);
                    break;
                }
                case IoEventType::Read:
                case IoEventType::Write:
                case IoEventType::Close:
                case IoEventType::Error: {
                     // ... (Event forwarding logic remains the same) ...
                     ConnectionBase* connectionPtr = static_cast<ConnectionBase*>(event.context);
                     if (connectionPtr) {
                         connectionPtr->handleNetworkEvent(event);
                     } else {
                         // ... (Handle null context) ...
                         if (event.type != IoEventType::Close) {
                            networkEngine->closeConnection(event.connection);
                        }
                     }
                     break;
                }
                default: /* ... unknown event ... */ break;
            } // End switch
        } // End for events
    } // End while loop
    // --- End Main Event Loop ---


    // --- Shutdown Sequence (Keep as is, use networkEngine pointer) ---
    GANL_MAIN_DEBUG("Shutdown requested. Cleaning up...");
    GANL_MAIN_DEBUG("Closing listener...");
    networkEngine->closeListener(listener);
    GANL_MAIN_DEBUG("Initiating shutdown of active sessions...");
    sessionManager.shutdown();
    GANL_MAIN_DEBUG("Waiting briefly for connections to close...");
    sessionManager.waitForAllClosed(std::chrono::seconds(5));
    GANL_MAIN_DEBUG("Shutting down SecureTransport...");
    if (secureTransport) {
        secureTransport->shutdown();
    }
    GANL_MAIN_DEBUG("Shutting down NetworkEngine...");
    networkEngine->shutdown();
    std::cout << "=== EchoServer shut down gracefully ===" << std::endl;
    // --- End Shutdown Sequence ---

    return 0;
}
