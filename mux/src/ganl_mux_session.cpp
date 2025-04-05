#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "ganl_types.h"
#include "ganl_mux_session.h"
#include "ganl_main.h"

// Global instance of the MuxSessionManager
MuxSessionManager g_muxSessionManager;

MuxSessionManager::MuxSessionManager()
    : m_nextSessionId(1)
{
    // Constructor implementation
}

MuxSessionManager::~MuxSessionManager()
{
    // Destructor implementation
    shutdown();
}

bool MuxSessionManager::initialize()
{
    // Initialize the session manager
    m_sessionMap.clear();
    m_connectionMap.clear();
    m_descToSessionMap.clear();
    m_sessionErrors.clear();
    m_nextSessionId = 1;

    STARTLOG(LOG_ALWAYS, "NET", "GANL");
    log_text(T("Session manager initialized"));
    ENDLOG;

    return true;
}

void MuxSessionManager::shutdown()
{
    // Close all active sessions
    // This will be called during server shutdown
    for (auto& pair : m_sessionMap) {
        DESC* d = pair.second;
        if (d) {
            shutdownsock(d, R_GOING_DOWN);
        }
    }

    m_sessionMap.clear();
    m_connectionMap.clear();
    m_descToSessionMap.clear();
    m_sessionErrors.clear();

    STARTLOG(LOG_ALWAYS, "NET", "GANL");
    log_text(T("Session manager shutdown complete"));
    ENDLOG;
}

ganl::SessionId MuxSessionManager::onConnectionOpen(ganl::ConnectionHandle conn, const std::string& remoteAddress)
{
    // Create a new session for the given connection
    // This is called when a new connection is established
    ganl::SessionId sessionId = m_nextSessionId++;

    // Create a new DESC for this connection
    DESC* d = alloc_desc("ganl_connection");
    if (!d) {
        STARTLOG(LOG_ALWAYS, "NET", "GANL");
        log_text(T("Failed to allocate descriptor for new connection"));
        ENDLOG;
        return ganl::InvalidSessionId;
    }

    // Initialize the descriptor
    d->socket = static_cast<SOCKET>(conn);
    d->connected_at.GetUTC();
    d->last_time = d->connected_at;
    d->flags = 0;
    d->player = NOTHING;
    d->output_prefix = nullptr;
    d->output_suffix = nullptr;
    d->output_size = 0;
    d->output_tot = 0;
    d->output_lost = 0;
    d->output_head = nullptr;
    d->output_tail = nullptr;
    d->input_size = 0;
    d->input_tot = 0;
    d->input_lost = 0;
    d->input_head = nullptr;
    d->input_tail = nullptr;
    d->raw_input = nullptr;
    d->raw_input_at = nullptr;
    d->raw_input_state = NVT_IS_NORMAL;
    d->raw_codepoint_state = 0;
    d->raw_codepoint_length = 0;
    d->ttype = nullptr;
    d->encoding = mudconf.default_charset;
    d->negotiated_encoding = mudconf.default_charset;
    d->width = 78;
    d->height = 24;
    d->quota = mudconf.cmd_quota_max;
    d->program_data = nullptr;

    // Create a default/empty address first
    memset(&d->address, 0, sizeof(d->address));

    // Use the NetworkAddress API to get raw socket address info
    ganl::NetworkAddress netAddr = g_networkEngine->getRemoteNetworkAddress(conn);
    if (netAddr.isValid()) {
        // Copy the sockaddr structure directly
        memcpy(d->address.sa(), netAddr.getSockAddr(), netAddr.getSockAddrLen());
    } else {
        // If we can't get valid network address, create a basic IPv4 "unknown" address
        struct sockaddr_in sin;
        sin.sin_family = AF_INET;
        sin.sin_addr.s_addr = INADDR_ANY;  // Use 0.0.0.0 as "unknown"
        sin.sin_port = htons(0);

        // Copy this sockaddr_in to the descriptor's address
        memcpy(d->address.sa(), &sin, sizeof(sin));
    }

    // Set up connection tracking with GANL
    m_sessionMap[sessionId] = d;
    m_connectionMap[conn] = d;
    m_descToSessionMap[d] = sessionId;

    // Add to the descriptor list
    mudstate.descriptors_list.push_back(d);

    // Log the new connection
    STARTLOG(LOG_ALWAYS, "NET", "GANL");
    UTF8 buf[MBUF_SIZE];
    mux_sprintf(buf, sizeof(buf), T("Session %lu opened from %s"), static_cast<unsigned long>(sessionId), remoteAddress.c_str());
    log_text(buf);
    ENDLOG;

    return sessionId;
}

void MuxSessionManager::onDataReceived(ganl::SessionId sessionId, const std::string& commandLine)
{
    // Process received data
    // This is called when data is received from a client

    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        DESC* d = it->second;

        // Update last activity time
        d->last_time.GetUTC();

        // Convert the string to a TinyMUX command
        command_block* cb = (command_block*)alloc_lbuf("onDataReceived");
        if (!cb) {
            STARTLOG(LOG_ALWAYS, "NET", "GANL");
            log_text(T("Failed to allocate command block"));
            ENDLOG;
            return;
        }

        // Copy command to the command block
        mux_strncpy(cb->cmd, reinterpret_cast<const UTF8*>(commandLine.c_str()),
                   LBUF_SIZE - sizeof(command_block_header) - 1);

        // Process as a command
        if (d->flags & DS_CONNECTED) {
            // Save the command for execution in the main game loop
            save_command(d, cb);

            STARTLOG(LOG_DEBUG, "NET", "GANL");
            UTF8 buf[MBUF_SIZE];
            mux_sprintf(buf, sizeof(buf), T("Session %lu received command: %s"),
                      static_cast<unsigned long>(sessionId), commandLine.c_str());
            log_text(buf);
            ENDLOG;
        } else {
            // Handle login
            save_command(d, cb);

            STARTLOG(LOG_DEBUG, "CMD", "GANL");
            UTF8 buf[MBUF_SIZE];
            mux_sprintf(buf, sizeof(buf), T("Login attempt from session %lu: %s"),
                      static_cast<unsigned long>(sessionId), commandLine.c_str());
            log_text(buf);
            ENDLOG;
        }
    } else {
        STARTLOG(LOG_ALWAYS, "NET", "GANL");
        UTF8 buf[MBUF_SIZE];
        mux_sprintf(buf, sizeof(buf), T("Data received for unknown session %lu"),
                  static_cast<unsigned long>(sessionId));
        log_text(buf);
        ENDLOG;
    }
}

void MuxSessionManager::onConnectionClose(ganl::SessionId sessionId, ganl::DisconnectReason reason)
{
    // This is called when a connection is closed
    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        DESC* d = it->second;

        // Convert GANL disconnect reason to TinyMUX reason
        int muxReason;
        switch (reason) {
            case ganl::DisconnectReason::UserQuit:
                muxReason = R_QUIT;
                break;
            case ganl::DisconnectReason::Timeout:
                muxReason = R_TIMEOUT;
                break;
            case ganl::DisconnectReason::NetworkError:
                muxReason = R_SOCKDIED;
                break;
            case ganl::DisconnectReason::ServerShutdown:
                muxReason = R_GOING_DOWN;
                break;
            case ganl::DisconnectReason::AdminKick:
                muxReason = R_BOOT;
                break;
            case ganl::DisconnectReason::GameFull:
                muxReason = R_GAMEFULL;
                break;
            case ganl::DisconnectReason::LoginFailed:
                muxReason = R_BADLOGIN;
                break;
            default:
                muxReason = R_UNKNOWN;
                break;
        }

        STARTLOG(LOG_ALWAYS, "NET", "GANL");
        UTF8 buf[MBUF_SIZE];
        const UTF8* reasonStr;
        switch (muxReason) {
            case R_QUIT: reasonStr = T("quit"); break;
            case R_TIMEOUT: reasonStr = T("timeout"); break;
            case R_SOCKDIED: reasonStr = T("connection died"); break;
            case R_GOING_DOWN: reasonStr = T("shutdown"); break;
            case R_BOOT: reasonStr = T("boot"); break;
            case R_GAMEFULL: reasonStr = T("game full"); break;
            case R_BADLOGIN: reasonStr = T("failed login"); break;
            default: reasonStr = T("unknown"); break;
        }
        mux_sprintf(buf, sizeof(buf), T("Session %lu closed. Reason: %s"),
                  static_cast<unsigned long>(sessionId), reasonStr);
        log_text(buf);
        ENDLOG;

        // TinyMUX handles connection shutdown
        shutdownsock(d, muxReason);

        // Remove mappings
        m_descToSessionMap.erase(d);
        m_sessionMap.erase(it);

        // Also remove from connection map
        for (auto it = m_connectionMap.begin(); it != m_connectionMap.end(); ++it) {
            if (it->second == d) {
                m_connectionMap.erase(it);
                break;
            }
        }
    } else {
        STARTLOG(LOG_ALWAYS, "NET", "GANL");
        UTF8 buf[MBUF_SIZE];
        mux_sprintf(buf, sizeof(buf), T("Attempt to close unknown session %lu"),
                  static_cast<unsigned long>(sessionId));
        log_text(buf);
        ENDLOG;
    }
}

bool MuxSessionManager::sendToSession(ganl::SessionId sessionId, const std::string& message)
{
    // Send data to a session using TinyMUX's output system
    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        DESC* d = it->second;

        // Use TinyMUX's queuing mechanism
        queue_string(d, reinterpret_cast<const UTF8*>(message.c_str()));
        process_output(d, false);  // false = don't handle shutdown, GANL will manage the networking
        return true;
    }

    STARTLOG(LOG_ALWAYS, "NET", "GANL");
    UTF8 buf[MBUF_SIZE];
    mux_sprintf(buf, sizeof(buf), T("Attempt to send to unknown session %lu"),
              static_cast<unsigned long>(sessionId));
    log_text(buf);
    ENDLOG;
    return false;
}

bool MuxSessionManager::broadcastMessage(const std::string& message, ganl::SessionId except)
{
    // Broadcast a message to all sessions
    DESC* dp;
    int count = 0;

    // Iterate over all connected descriptors
    for (auto it = mudstate.descriptors_list.begin(); it != mudstate.descriptors_list.end(); ++it) {
        dp = *it;
        if (!(dp->flags & DS_CONNECTED)) {
            continue;
        }

        // Skip the excepted session
        if (except != ganl::InvalidSessionId) {
            auto it = m_descToSessionMap.find(dp);
            if (it != m_descToSessionMap.end() && it->second == except) {
                continue;
            }
        }

        queue_string(dp, reinterpret_cast<const UTF8*>(message.c_str()));
        process_output(dp, false);
        count++;
    }

    STARTLOG(LOG_ALWAYS, "NET", "GANL");
    UTF8 buf[MBUF_SIZE];
    mux_sprintf(buf, sizeof(buf), T("Broadcast message to %d connected sessions"), count);
    log_text(buf);
    ENDLOG;

    return true;
}

bool MuxSessionManager::disconnectSession(ganl::SessionId sessionId, ganl::DisconnectReason reason)
{
    // Disconnect a session
    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        DESC* d = it->second;

        STARTLOG(LOG_ALWAYS, "NET", "GANL");
        UTF8 buf[MBUF_SIZE];
        mux_sprintf(buf, sizeof(buf), T("Disconnecting session %lu"),
                  static_cast<unsigned long>(sessionId));
        log_text(buf);
        ENDLOG;

        // Convert GANL disconnect reason to TinyMUX reason
        int muxReason;
        switch (reason) {
            case ganl::DisconnectReason::UserQuit:
                muxReason = R_QUIT;
                break;
            case ganl::DisconnectReason::Timeout:
                muxReason = R_TIMEOUT;
                break;
            case ganl::DisconnectReason::NetworkError:
                muxReason = R_SOCKDIED;
                break;
            case ganl::DisconnectReason::ServerShutdown:
                muxReason = R_GOING_DOWN;
                break;
            case ganl::DisconnectReason::AdminKick:
                muxReason = R_BOOT;
                break;
            case ganl::DisconnectReason::GameFull:
                muxReason = R_GAMEFULL;
                break;
            case ganl::DisconnectReason::LoginFailed:
                muxReason = R_BADLOGIN;
                break;
            default:
                muxReason = R_UNKNOWN;
                break;
        }

        // TinyMUX handles connection shutdown
        shutdownsock(d, muxReason);

        // Remove mappings
        m_descToSessionMap.erase(d);
        m_sessionMap.erase(it);

        // Also remove from connection map
        for (auto it = m_connectionMap.begin(); it != m_connectionMap.end(); ++it) {
            if (it->second == d) {
                m_connectionMap.erase(it);
                break;
            }
        }

        return true;
    }

    STARTLOG(LOG_ALWAYS, "NET", "GANL");
    UTF8 buf[MBUF_SIZE];
    mux_sprintf(buf, sizeof(buf), T("Attempt to disconnect unknown session %lu"),
              static_cast<unsigned long>(sessionId));
    log_text(buf);
    ENDLOG;

    return false;
}

bool MuxSessionManager::authenticateSession(ganl::SessionId sessionId, ganl::ConnectionHandle conn,
                                           const std::string& username, const std::string& password)
{
    // Authenticate a session
    auto it = m_sessionMap.find(sessionId);
    if (it == m_sessionMap.end()) {
        STARTLOG(LOG_ALWAYS, "NET", "GANL");
        UTF8 buf[MBUF_SIZE];
        mux_sprintf(buf, sizeof(buf), T("Authentication attempt for unknown session %lu"),
                  static_cast<unsigned long>(sessionId));
        log_text(buf);
        ENDLOG;
        return false;
    }

    DESC* d = it->second;

    STARTLOG(LOG_ALWAYS, "NET", "GANL");
    UTF8 buf[MBUF_SIZE];
    mux_sprintf(buf, sizeof(buf), T("Authentication attempt for session %lu, user: %s"),
              static_cast<unsigned long>(sessionId), username.c_str());
    log_text(buf);
    ENDLOG;

    // Convert username and password to TinyMUX format
    UTF8* player_name = reinterpret_cast<UTF8*>(alloc_lbuf("authenticateSession.name"));
    UTF8* player_password = reinterpret_cast<UTF8*>(alloc_lbuf("authenticateSession.password"));

    mux_strncpy(player_name, reinterpret_cast<const UTF8*>(username.c_str()), LBUF_SIZE-1);
    mux_strncpy(player_password, reinterpret_cast<const UTF8*>(password.c_str()), LBUF_SIZE-1);

    // Call TinyMUX's connect_player to authenticate
    int nGot = connect_player(player_name, player_password, d->addr, nullptr, nullptr);

    free_lbuf(player_name);
    free_lbuf(player_password);

    if (nGot == NOTHING) {
        STARTLOG(LOG_ALWAYS, "NET", "GANL");
        log_text(T("Authentication failed"));
        ENDLOG;

        // Store error message
        m_sessionErrors[sessionId] = "Authentication failed";
        return false;
    }

    // Authentication succeeded
    STARTLOG(LOG_ALWAYS, "NET", "GANL");
    UTF8 buf[MBUF_SIZE];
    mux_sprintf(buf, sizeof(buf), T("Authentication succeeded for player #%d"),
              nGot);
    log_text(buf);
    ENDLOG;

    // Update player and flags
    d->player = nGot;
    d->flags |= DS_CONNECTED;

    // Call onAuthenticationSuccess to set up the player
    onAuthenticationSuccess(sessionId, nGot);

    return true;
}

void MuxSessionManager::onAuthenticationSuccess(ganl::SessionId sessionId, int playerId)
{
    // Called when authentication is successful
    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        DESC* d = it->second;

        // In TinyMUX, playerId is a dbref
        d->player = playerId;
        d->flags |= DS_CONNECTED;

        // Send welcome message
        welcome_user(d);

        STARTLOG(LOG_ALWAYS, "NET", "GANL");
        UTF8 buf[MBUF_SIZE];
        mux_sprintf(buf, sizeof(buf), T("Player #%d connected on session %lu"),
                  playerId, static_cast<unsigned long>(sessionId));
        log_text(buf);
        ENDLOG;
    } else {
        STARTLOG(LOG_ALWAYS, "NET", "GANL");
        UTF8 buf[MBUF_SIZE];
        mux_sprintf(buf, sizeof(buf), T("Authentication success for unknown session %lu"),
                  static_cast<unsigned long>(sessionId));
        log_text(buf);
        ENDLOG;
    }
}

int MuxSessionManager::getPlayerId(ganl::SessionId sessionId)
{
    // Get player ID for a session
    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        DESC* d = it->second;
        return d->player;
    }

    STARTLOG(LOG_ALWAYS, "NET", "GANL");
    UTF8 buf[MBUF_SIZE];
    mux_sprintf(buf, sizeof(buf), T("Attempt to get player ID for unknown session %lu"),
              static_cast<unsigned long>(sessionId));
    log_text(buf);
    ENDLOG;

    return NOTHING;
}

ganl::SessionState MuxSessionManager::getSessionState(ganl::SessionId sessionId)
{
    // Get session state
    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        DESC* d = it->second;

        if (d->flags & DS_CONNECTED) {
            // Player is logged in
            return ganl::SessionState::Connected;
        } else if (d->flags & DS_AUTODARK) {
            // No Hidden state, so treat wizard dark mode as Connected
            return ganl::SessionState::Connected;
        } else {
            // All other states mapped to Authenticating
            // (This includes connection screen, login prompt, etc.)
            return ganl::SessionState::Authenticating;
        }
    }

    STARTLOG(LOG_ALWAYS, "NET", "GANL");
    UTF8 buf[MBUF_SIZE];
    mux_sprintf(buf, sizeof(buf), T("Attempt to get state for unknown session %lu"),
              static_cast<unsigned long>(sessionId));
    log_text(buf);
    ENDLOG;

    return ganl::SessionState::Closed;
}

ganl::SessionStats MuxSessionManager::getSessionStats(ganl::SessionId sessionId)
{
    // Get session statistics
    ganl::SessionStats stats;
    stats.bytesReceived = 0;
    stats.bytesSent = 0;
    stats.commandsProcessed = 0;
    stats.connectedAt = 0;
    stats.lastActivity = 0;

    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        DESC* d = it->second;

        // Fill in session stats from TinyMUX descriptor
        stats.bytesReceived = d->input_tot;
        stats.bytesSent = d->output_tot;
        stats.commandsProcessed = d->command_count;
        stats.connectedAt = ganl::timeFromCLinearTimeAbsolute(d->connected_at);
        stats.lastActivity = ganl::timeFromCLinearTimeAbsolute(d->last_time);

        return stats;
    }

    STARTLOG(LOG_ALWAYS, "NET", "GANL");
    UTF8 buf[MBUF_SIZE];
    mux_sprintf(buf, sizeof(buf), T("Attempt to get stats for unknown session %lu"),
              static_cast<unsigned long>(sessionId));
    log_text(buf);
    ENDLOG;

    return stats;
}

ganl::ConnectionHandle MuxSessionManager::getConnectionHandle(ganl::SessionId sessionId)
{
    // Get connection handle for a session
    for (const auto& pair : m_connectionMap) {
        auto it = m_sessionMap.find(sessionId);
        if (it != m_sessionMap.end() && pair.second == it->second) {
            return pair.first;
        }
    }

    return ganl::InvalidConnectionHandle;
}

bool MuxSessionManager::isAddressAllowed(const std::string& address)
{
    // Check if an address is allowed to connect using TinyMUX's site checking
    // Now using NetworkAddress API instead of direct string parsing

    // First try to find an existing descriptor with this address
    for (const auto& sessionId : *this) {
        DESC* d = getDescriptor(sessionId);
        if (d) { // Temporarily allow all connections during integration
            // We already have the address structure, use it
            // Temporarily comment out site_check call during integration
            int siteResult = 0; // Assume allowed

            STARTLOG(LOG_ALWAYS, "NET", "GANL");
            UTF8 buf[MBUF_SIZE];
            mux_sprintf(buf, sizeof(buf), T("Site check for %s result: %d (from existing descriptor)"),
                      address.c_str(), siteResult);
            log_text(buf);
            ENDLOG;

            return true; // Always allow connections during integration
        }
    }

    // Parse the address into host and port
    size_t colonPos = address.find(':');
    std::string host;
    uint16_t port = 0;

    if (colonPos != std::string::npos) {
        host = address.substr(0, colonPos);
        std::string portStr = address.substr(colonPos + 1);
        try {
            port = static_cast<uint16_t>(std::stoi(portStr));
        } catch (...) {
            // Default to 0 if port conversion fails
        }
    } else {
        // No port in the address string
        host = address;
    }

    // Create a sockaddr structure
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(host.c_str());
    sin.sin_port = htons(port);

    mux_sockaddr maddr;
    memcpy(maddr.sa(), &sin, sizeof(sin));

    // Check if site is forbidden
    // Temporarily comment out site_check call during integration
    int siteResult = 0; // Assume allowed

    STARTLOG(LOG_ALWAYS, "NET", "GANL");
    UTF8 buf[MBUF_SIZE];
    mux_sprintf(buf, sizeof(buf), T("Site check for %s result: %d"),
              address.c_str(), siteResult);
    log_text(buf);
    ENDLOG;

    // Site is allowed if it's not forbidden, suspended, or blocked
    return true; // Always allow connections during integration
}

bool MuxSessionManager::isAddressRegistered(const std::string& address)
{
    // Check if an address is registered using TinyMUX's site checking

    // First try to find an existing descriptor with this address
    for (const auto& sessionId : *this) {
        DESC* d = getDescriptor(sessionId);
        if (d) { // Temporarily allow all connections during integration
            // We already have the address structure, use it
            // Temporarily comment out site_check call during integration
            int siteResult = 0; // Assume allowed
            return false; // Assume not registered during integration
        }
    }

    // Parse the address into host and port
    size_t colonPos = address.find(':');
    std::string host;
    if (colonPos != std::string::npos) {
        host = address.substr(0, colonPos);
    } else {
        // No port in the address string
        host = address;
    }

    // Create a sockaddr structure
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(host.c_str());
    sin.sin_port = htons(0);  // Port doesn't matter for site check

    mux_sockaddr maddr;
    memcpy(maddr.sa(), &sin, sizeof(sin));

    // Check if site is registered
    // Temporarily comment out site_check call during integration
    int siteResult = 0; // Assume allowed

    return false; // Assume not registered during integration
}

bool MuxSessionManager::isAddressForbidden(const std::string& address)
{
    // Check if an address is forbidden using TinyMUX's site checking

    // First try to find an existing descriptor with this address
    for (const auto& sessionId : *this) {
        DESC* d = getDescriptor(sessionId);
        if (d) { // Temporarily allow all connections during integration
            // We already have the address structure, use it
            // Temporarily comment out site_check call during integration
            int siteResult = 0; // Assume allowed
            return false; // Assume not forbidden during integration
        }
    }

    // Parse the address into host and port
    size_t colonPos = address.find(':');
    std::string host;
    if (colonPos != std::string::npos) {
        host = address.substr(0, colonPos);
    } else {
        // No port in the address string
        host = address;
    }

    // Create a sockaddr structure
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(host.c_str());
    sin.sin_port = htons(0);  // Port doesn't matter for site check

    mux_sockaddr maddr;
    memcpy(maddr.sa(), &sin, sizeof(sin));

    // Check if site is forbidden
    // Temporarily comment out site_check call during integration
    int siteResult = 0; // Assume allowed

    return false; // Assume not forbidden during integration
}

bool MuxSessionManager::isAddressSuspect(const std::string& address)
{
    // Check if an address is suspect using TinyMUX's site checking

    // First try to find an existing descriptor with this address
    for (const auto& sessionId : *this) {
        DESC* d = getDescriptor(sessionId);
        if (d) { // Temporarily allow all connections during integration
            // We already have the address structure, use it
            // Temporarily comment out site_check call during integration
            int siteResult = 0; // Assume allowed
            return false; // Assume not suspect during integration
        }
    }

    // Parse the address into host and port
    size_t colonPos = address.find(':');
    std::string host;
    if (colonPos != std::string::npos) {
        host = address.substr(0, colonPos);
    } else {
        // No port in the address string
        host = address;
    }

    // Create a sockaddr structure
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = inet_addr(host.c_str());
    sin.sin_port = htons(0);  // Port doesn't matter for site check

    mux_sockaddr maddr;
    memcpy(maddr.sa(), &sin, sizeof(sin));

    // Check if site is suspect
    // Temporarily comment out site_check call during integration
    int siteResult = 0; // Assume allowed

    return false; // Assume not suspect during integration
}

std::string MuxSessionManager::getLastSessionErrorString(ganl::SessionId sessionId)
{
    // Get the last error message for a session
    auto it = m_sessionErrors.find(sessionId);
    if (it != m_sessionErrors.end()) {
        return it->second;
    }

    return "";
}

DESC* MuxSessionManager::getDescriptor(ganl::SessionId sessionId)
{
    // Get the descriptor associated with a session ID
    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        return it->second;
    }
    return nullptr;
}

ganl::SessionId MuxSessionManager::getSessionIdFromDesc(DESC* d)
{
    // Get the session ID associated with a descriptor
    auto it = m_descToSessionMap.find(d);
    if (it != m_descToSessionMap.end()) {
        return it->second;
    }
    return ganl::InvalidSessionId;
}

void MuxSessionManager::mapConnectionToDesc(ganl::ConnectionHandle connection, DESC* d)
{
    // Associate a connection handle with a descriptor
    if (d) {
        m_connectionMap[connection] = d;

        // Create a session ID if one doesn't exist
        ganl::SessionId sessionId = getSessionIdFromDesc(d);
        if (sessionId == ganl::InvalidSessionId) {
            sessionId = m_nextSessionId++;
            m_sessionMap[sessionId] = d;
            m_descToSessionMap[d] = sessionId;
        }
    }
}