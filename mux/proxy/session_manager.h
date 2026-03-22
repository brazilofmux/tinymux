#ifndef HYDRA_SESSION_MANAGER_H
#define HYDRA_SESSION_MANAGER_H

#include "hydra_types.h"
#include "config.h"
#include "scrollback.h"
#include "account_manager.h"
#include "telnet_bridge.h"
#include <network_engine.h>
#include <network_types.h>
#include <map>
#include <string>
#include <vector>

struct HydraSession {
    HydraSessionId      id;
    uint32_t            accountId;
    std::string         username;

    std::vector<ganl::ConnectionHandle> frontDoors;

    // Back-door link (Phase 1: one link per session)
    ganl::ConnectionHandle backDoor{ganl::InvalidConnectionHandle};
    std::string         gameName;
    LinkState           linkState{LinkState::Dead};

    ScrollBack          scrollback;

    time_t              created;
    time_t              lastActivity;

    SessionState        state{SessionState::Active};

    std::vector<uint8_t> scrollbackKey;

    // Game protocol state (charset from config; telnet negotiation in future)
    ganl::ProtocolState gameProtoState;
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
};

class SessionManager {
public:
    SessionManager(ganl::NetworkEngine& engine,
                   AccountManager& accounts,
                   const HydraConfig& config);
    ~SessionManager();

    // Called when a new front-door connection is accepted.
    void onAccept(ganl::ConnectionHandle handle);

    // Called when data is available on a front-door connection.
    // raw data from recv() — may contain partial lines.
    void onFrontDoorData(ganl::ConnectionHandle handle,
                         const char* data, size_t len);

    // Called when a front-door connection closes.
    void onFrontDoorClose(ganl::ConnectionHandle handle);

    // Called when a back-door connect succeeds.
    void onBackDoorConnect(ganl::ConnectionHandle bdHandle);

    // Called when a back-door connect fails.
    void onBackDoorConnectFail(ganl::ConnectionHandle bdHandle, int error);

    // Called when data arrives from a back-door connection.
    void onBackDoorData(ganl::ConnectionHandle bdHandle,
                        const char* data, size_t len);

    // Called when a back-door connection closes.
    void onBackDoorClose(ganl::ConnectionHandle bdHandle);

    // Is this handle a known back-door connection?
    bool isBackDoor(ganl::ConnectionHandle handle) const;

    // Run periodic timers.
    void runTimers();

private:
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

    HydraSession* findSessionByBackDoor(ganl::ConnectionHandle bdHandle);

    ganl::NetworkEngine& engine_;
    AccountManager& accounts_;
    const HydraConfig& config_;

    std::map<ganl::ConnectionHandle, FrontDoorState> frontDoors_;
    std::map<HydraSessionId, HydraSession> sessions_;
    // Reverse map: back-door handle → session id
    std::map<ganl::ConnectionHandle, HydraSessionId> backDoorMap_;

    HydraSessionId nextSessionId_{1};

    TelnetBridge bridge_;
};

#endif // HYDRA_SESSION_MANAGER_H
