#ifndef GANL_MUX_SESSION_H
#define GANL_MUX_SESSION_H

#include "autoconf.h"
#include <unordered_map>
#include <string>
#include "ganl_types.h"
#include "ganl/include/session_manager.h"
#include "interface.h"

// This class bridges between GANL's session management and TinyMUX's player management
// It implements the ganl::SessionManager interface and maps it to TinyMUX's DESC structure

class MuxSessionManager : public ganl::SessionManager {
public:
    MuxSessionManager();
    virtual ~MuxSessionManager();

    // SessionManager interface implementation
    virtual bool initialize() override;
    virtual void shutdown() override;

    virtual ganl::SessionId onConnectionOpen(ganl::ConnectionHandle conn,
                                          const std::string& remoteAddress) override;

    virtual void onDataReceived(ganl::SessionId sessionId,
                               const std::string& commandLine) override;

    virtual void onConnectionClose(ganl::SessionId sessionId,
                                  ganl::DisconnectReason reason) override;

    virtual bool sendToSession(ganl::SessionId sessionId,
                              const std::string& message) override;

    virtual bool broadcastMessage(const std::string& message,
                                 ganl::SessionId except = ganl::InvalidSessionId) override;

    virtual bool disconnectSession(ganl::SessionId sessionId,
                                  ganl::DisconnectReason reason) override;

    virtual bool authenticateSession(ganl::SessionId sessionId,
                                    ganl::ConnectionHandle conn,
                                    const std::string& username,
                                    const std::string& password) override;

    virtual void onAuthenticationSuccess(ganl::SessionId sessionId,
                                        int playerId) override;

    virtual int getPlayerId(ganl::SessionId sessionId) override;

    virtual ganl::SessionState getSessionState(ganl::SessionId sessionId) override;

    virtual ganl::SessionStats getSessionStats(ganl::SessionId sessionId) override;

    virtual ganl::ConnectionHandle getConnectionHandle(ganl::SessionId sessionId) override;

    virtual bool isAddressAllowed(const std::string& address) override;

    virtual bool isAddressRegistered(const std::string& address) override;

    virtual bool isAddressForbidden(const std::string& address) override;

    virtual bool isAddressSuspect(const std::string& address) override;

    virtual std::string getLastSessionErrorString(ganl::SessionId sessionId) override;

    // TinyMUX-specific methods
    DESC* getDescriptor(ganl::SessionId sessionId);
    ganl::SessionId getSessionIdFromDesc(DESC* d);
    void mapConnectionToDesc(ganl::ConnectionHandle connection, DESC* d);

    // Iterators for sessions
    class SessionIterator {
    public:
        SessionIterator(const std::unordered_map<ganl::SessionId, DESC*>::const_iterator& it)
            : m_it(it) {}

        ganl::SessionId operator*() const {
            return m_it->first;
        }

        bool operator==(const SessionIterator& other) const {
            return m_it == other.m_it;
        }

        bool operator!=(const SessionIterator& other) const {
            return m_it != other.m_it;
        }

        SessionIterator& operator++() {
            ++m_it;
            return *this;
        }

    private:
        std::unordered_map<ganl::SessionId, DESC*>::const_iterator m_it;
    };

    SessionIterator begin() const {
        return SessionIterator(m_sessionMap.begin());
    }

    SessionIterator end() const {
        return SessionIterator(m_sessionMap.end());
    }

private:
    // Map of GANL session IDs to TinyMUX descriptors
    std::unordered_map<ganl::SessionId, DESC*> m_sessionMap;

    // Map of connection handles to descriptors
    std::unordered_map<ganl::ConnectionHandle, DESC*> m_connectionMap;

    // Map of descriptors to session IDs
    std::unordered_map<DESC*, ganl::SessionId> m_descToSessionMap;

    // Counter for generating unique session IDs
    ganl::SessionId m_nextSessionId;

    // Error messages for sessions
    std::unordered_map<ganl::SessionId, std::string> m_sessionErrors;
};

extern MuxSessionManager g_muxSessionManager;

#endif // GANL_MUX_SESSION_H