#ifndef GANL_SESSION_MANAGER_H
#define GANL_SESSION_MANAGER_H

#include "network_types.h"
#include <string>

namespace ganl {

// Session statistics structure
struct SessionStats {
    size_t bytesReceived{0};
    size_t bytesSent{0};
    size_t commandsProcessed{0};
    time_t connectedAt{0};
    time_t lastActivity{0};
};

/**
 * SessionManager - Interface for managing user sessions
 */
class SessionManager {
public:
    virtual ~SessionManager() = default;

    /**
     * Initialize the session manager
     *
     * @return true on success, false on failure
     */
    virtual bool initialize() = 0;

    /**
     * Shut down the session manager
     */
    virtual void shutdown() = 0;

    /**
     * Called when a new connection is established
     *
     * @param conn Connection handle
     * @param remoteAddress Remote address string
     * @return Session ID
     */
    virtual SessionId onConnectionOpen(ConnectionHandle conn, const std::string& remoteAddress) = 0;

    /**
     * Called when data is received from a connection
     *
     * @param sessionId Session ID
     * @param commandLine Command line text
     */
    virtual void onDataReceived(SessionId sessionId, const std::string& commandLine) = 0;

    /**
     * Called when a connection is closed
     *
     * @param sessionId Session ID
     * @param reason Disconnect reason
     */
    virtual void onConnectionClose(SessionId sessionId, DisconnectReason reason) = 0;

    /**
     * Send a message to a specific session
     *
     * @param sessionId Session ID
     * @param message Message text
     * @return true on success, false on failure
     */
    virtual bool sendToSession(SessionId sessionId, const std::string& message) = 0;

    /**
     * Broadcast a message to all sessions
     *
     * @param message Message text
     * @param except Session ID to exclude (optional)
     * @return true on success, false on failure
     */
    virtual bool broadcastMessage(const std::string& message, SessionId except = InvalidSessionId) = 0;

    /**
     * Disconnect a session
     *
     * @param sessionId Session ID
     * @param reason Disconnect reason
     * @return true on success, false on failure
     */
    virtual bool disconnectSession(SessionId sessionId, DisconnectReason reason) = 0;

    /**
     * Authenticate a session
     *
     * @param sessionId Session ID
     * @param conn Connection handle
     * @param username Username
     * @param password Password
     * @return true if authentication succeeded, false otherwise
     */
    virtual bool authenticateSession(SessionId sessionId, ConnectionHandle conn,
                                    const std::string& username, const std::string& password) = 0;

    /**
     * Called when authentication is successful
     *
     * @param sessionId Session ID
     * @param playerId Player ID
     */
    virtual void onAuthenticationSuccess(SessionId sessionId, int playerId) = 0;

    /**
     * Get player ID for a session
     *
     * @param sessionId Session ID
     * @return Player ID
     */
    virtual int getPlayerId(SessionId sessionId) = 0;

    /**
     * Get session state
     *
     * @param sessionId Session ID
     * @return Session state
     */
    virtual SessionState getSessionState(SessionId sessionId) = 0;

    /**
     * Get session statistics
     *
     * @param sessionId Session ID
     * @return Session statistics
     */
    virtual SessionStats getSessionStats(SessionId sessionId) = 0;

    /**
     * Get connection handle for a session
     *
     * @param sessionId Session ID
     * @return Connection handle
     */
    virtual ConnectionHandle getConnectionHandle(SessionId sessionId) = 0;

    /**
     * Check if an address is allowed to connect
     *
     * @param address IP address
     * @return true if allowed, false otherwise
     */
    virtual bool isAddressAllowed(const std::string& address) = 0;

    /**
     * Check if an address is registered
     *
     * @param address IP address
     * @return true if registered, false otherwise
     */
    virtual bool isAddressRegistered(const std::string& address) = 0;

    /**
     * Check if an address is forbidden
     *
     * @param address IP address
     * @return true if forbidden, false otherwise
     */
    virtual bool isAddressForbidden(const std::string& address) = 0;

    /**
     * Check if an address is suspect
     *
     * @param address IP address
     * @return true if suspect, false otherwise
     */
    virtual bool isAddressSuspect(const std::string& address) = 0;

    /**
     * Get last session error string
     *
     * @param sessionId Session ID
     * @return Error string
     */
    virtual std::string getLastSessionErrorString(SessionId sessionId) = 0;
};

} // namespace ganl

#endif // GANL_SESSION_MANAGER_H
