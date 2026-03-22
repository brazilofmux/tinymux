#ifndef HYDRA_SESSION_MANAGER_H
#define HYDRA_SESSION_MANAGER_H

#include "hydra_types.h"
#include "config.h"
#include "scrollback.h"
#include "account_manager.h"
#include "telnet_bridge.h"
#include "process_manager.h"
#include <network_engine.h>
#include <network_types.h>
#include <map>
#include <string>
#include <vector>

// A single back-door connection to a game server.
struct BackDoorLink {
    ganl::ConnectionHandle handle{ganl::InvalidConnectionHandle};
    std::string         gameName;
    std::string         character;          // logged-in character name
    LinkState           state{LinkState::Dead};
    ganl::ProtocolState protoState;
    const GameConfig*   gameConfig{nullptr};
    bool                gmcpEnabled{false};

    // Reconnect backoff
    int                 retryCount{0};
    time_t              nextRetry{0};
};

struct HydraSession {
    HydraSessionId      id;
    uint32_t            accountId;
    std::string         username;

    std::vector<ganl::ConnectionHandle> frontDoors;

    // Back-door links (Phase 2: multiple per session)
    std::vector<BackDoorLink> links;
    size_t              activeLink{0};      // index into links

    // Active link helper (nullptr if no links or index out of range)
    BackDoorLink* getActiveLink() {
        if (activeLink < links.size()) return &links[activeLink];
        return nullptr;
    }

    ScrollBack          scrollback;

    time_t              created;
    time_t              lastActivity;

    SessionState        state{SessionState::Active};

    std::vector<uint8_t> scrollbackKey;

    // Persistent session ID (random hex string for SQLite storage)
    std::string persistId;
};

// Per front-door connection state (before and after auth)
struct FrontDoorState {
    ganl::ConnectionHandle handle;
    HydraSessionId sessionId{InvalidHydraSessionId};

    enum LoginPhase { AwaitUsername, AwaitPassword, Authenticated };
    LoginPhase loginPhase{AwaitUsername};
    std::string pendingUsername;

    // Line assembly buffer (for telnet line-at-a-time)
    std::string lineBuf;

    // Client capabilities (defaults for Phase 1; auto-detect in future)
    ganl::EncodingType encoding{ganl::EncodingType::Utf8};
    ColorDepth colorDepth{ColorDepth::Ansi256};
    bool gmcpEnabled{false};
};

// Reverse map value: session ID + link index
struct BackDoorMapEntry {
    HydraSessionId sessionId;
    size_t linkIndex;
};

class SessionManager {
public:
    SessionManager(ganl::NetworkEngine& engine,
                   AccountManager& accounts,
                   const HydraConfig& config);
    ~SessionManager();

    void onAccept(ganl::ConnectionHandle handle);
    void onFrontDoorData(ganl::ConnectionHandle handle,
                         const char* data, size_t len);
    void onFrontDoorClose(ganl::ConnectionHandle handle);

    void onBackDoorConnect(ganl::ConnectionHandle bdHandle);
    void onBackDoorConnectFail(ganl::ConnectionHandle bdHandle, int error);
    void onBackDoorData(ganl::ConnectionHandle bdHandle,
                        const char* data, size_t len);
    void onBackDoorClose(ganl::ConnectionHandle bdHandle);

    bool isBackDoor(ganl::ConnectionHandle handle) const;

    void runTimers();
    void shutdownSessions();

private:
    void flushSession(HydraSession& session);
    std::string generatePersistId();
    void resumeSavedSession(FrontDoorState& fd, uint32_t accountId,
                            const std::vector<uint8_t>& sbKey);
    void sendToClient(ganl::ConnectionHandle handle, const std::string& text);
    void processLine(FrontDoorState& fd, const std::string& line);
    void handleLogin(FrontDoorState& fd, const std::string& line);
    void dispatchCommand(HydraSession& session,
                         ganl::ConnectionHandle fdHandle,
                         const std::string& line);
    void showBanner(ganl::ConnectionHandle handle);
    void showGameMenu(HydraSession& session, ganl::ConnectionHandle fdHandle);
    void connectToGame(HydraSession& session, const std::string& gameName);
    void forwardToGame(HydraSession& session,
                       ganl::ConnectionHandle fdHandle,
                       const std::string& line);
    void closeLink(HydraSession& session, size_t linkIdx);

    // Look up session + link from a back-door handle.
    bool findByBackDoor(ganl::ConnectionHandle bdHandle,
                        HydraSession*& session, BackDoorLink*& link,
                        size_t& linkIdx);

    ganl::NetworkEngine& engine_;
    AccountManager& accounts_;
    const HydraConfig& config_;

    std::map<ganl::ConnectionHandle, FrontDoorState> frontDoors_;
    std::map<HydraSessionId, HydraSession> sessions_;
    // Reverse map: back-door handle → (session id, link index)
    std::map<ganl::ConnectionHandle, BackDoorMapEntry> backDoorMap_;

    HydraSessionId nextSessionId_{1};
    time_t lastFlush_{0};

    TelnetBridge bridge_;
    ProcessManager procMgr_;
};

#endif // HYDRA_SESSION_MANAGER_H
