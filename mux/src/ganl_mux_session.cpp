#include "autoconf.h"
#include "config.h"
#include "externs.h"

#include "ganl_types.h"
#include "ganl_mux_session.h"

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
}

ganl::SessionId MuxSessionManager::onConnectionOpen(ganl::ConnectionHandle conn, const std::string& remoteAddress)
{
    // Create a new session for the given connection
    // This is called when a new connection is established
    ganl::SessionId sessionId = m_nextSessionId++;

    // In a full implementation, we would create a DESC here
    // For now, we'll just store the connection
    return sessionId;
}

void MuxSessionManager::onDataReceived(ganl::SessionId sessionId, const std::string& commandLine)
{
    // Process received data
    // This is called when data is received from a client

    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        DESC* d = it->second;

        // Convert the string to a TinyMUX command
        UTF8* buffer = reinterpret_cast<UTF8*>(alloc_lbuf("onDataReceived"));
        mux_strncpy(buffer, reinterpret_cast<const UTF8*>(commandLine.c_str()), LBUF_SIZE-1);

        // Process as a command
        if (d->flags & DS_CONNECTED) {
            do_command(d, buffer);
        } else {
            // Handle login
            save_command(d, reinterpret_cast<command_block*>(buffer));
        }

        free_lbuf(buffer);
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
    }
}

bool MuxSessionManager::sendToSession(ganl::SessionId sessionId, const std::string& message)
{
    // Send data to a session
    // This is called when data should be sent to a client

    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        DESC* d = it->second;

        // Use TinyMUX's queuing mechanism
        queue_string(d, reinterpret_cast<const UTF8*>(message.c_str()));
        return true;
    }

    return false;
}

bool MuxSessionManager::broadcastMessage(const std::string& message, ganl::SessionId except)
{
    // Broadcast a message to all sessions
    DESC* dp;

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
    }

    return true;
}

bool MuxSessionManager::disconnectSession(ganl::SessionId sessionId, ganl::DisconnectReason reason)
{
    // Disconnect a session
    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        onConnectionClose(sessionId, reason);
        return true;
    }

    return false;
}

bool MuxSessionManager::authenticateSession(ganl::SessionId sessionId, ganl::ConnectionHandle conn,
                                           const std::string& username, const std::string& password)
{
    // Authenticate a session
    // This would normally call connect_player or similar

    // Minimal implementation for now
    return false;
}

void MuxSessionManager::onAuthenticationSuccess(ganl::SessionId sessionId, int playerId)
{
    // Called when authentication is successful
    // This would normally set up the player in the descriptor
    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        DESC* d = it->second;

        // In TinyMUX, playerId is a dbref
        d->player = playerId;
        d->flags |= DS_CONNECTED;
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

    return NOTHING;
}

ganl::SessionState MuxSessionManager::getSessionState(ganl::SessionId sessionId)
{
    // Get session state
    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        DESC* d = it->second;

        if (d->flags & DS_CONNECTED) {
            return ganl::SessionState::Connected;
        } else {
            return ganl::SessionState::Authenticating;
        }
    }

    return ganl::SessionState::Closed;
}

ganl::SessionStats MuxSessionManager::getSessionStats(ganl::SessionId sessionId)
{
    // Get session statistics
    ganl::SessionStats stats;

    auto it = m_sessionMap.find(sessionId);
    if (it != m_sessionMap.end()) {
        DESC* d = it->second;

        // Fill in session stats from TinyMUX descriptor
        stats.bytesReceived = d->input_tot;
        stats.bytesSent = d->output_tot;
        stats.connectedAt = ganl::timeFromCLinearTimeAbsolute(d->connected_at);
        stats.lastActivity = ganl::timeFromCLinearTimeAbsolute(d->last_time);
    }

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
    // Check if an address is allowed to connect
    // TinyMUX has site checking in site_check()

    // This is a minimal implementation
    return true;
}

bool MuxSessionManager::isAddressRegistered(const std::string& address)
{
    // Check if an address is registered
    // Minimal implementation
    return false;
}

bool MuxSessionManager::isAddressForbidden(const std::string& address)
{
    // Check if an address is forbidden
    // Minimal implementation
    return false;
}

bool MuxSessionManager::isAddressSuspect(const std::string& address)
{
    // Check if an address is suspect
    // Minimal implementation
    return false;
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