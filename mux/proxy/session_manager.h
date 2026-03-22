#ifndef HYDRA_SESSION_MANAGER_H
#define HYDRA_SESSION_MANAGER_H

#include "hydra_types.h"
#include "config.h"
#include "scrollback.h"
#include "account_manager.h"
#include "telnet_bridge.h"
#include "process_manager.h"
#include "websocket.h"
#include <network_engine.h>
#include <network_types.h>
#include <atomic>
#include <condition_variable>
#include <map>
#include <mutex>
#include <queue>
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

    // gRPC output queue for Subscribe streaming.
    // Heap-allocated so HydraSession stays movable.
    struct OutputItem {
        std::string text;       // ANSI TrueColor rendered
        std::string source;     // game name
        time_t timestamp;
        int linkNumber;         // 1-based
    };
    // GMCP message queued for gRPC consumers.
    struct GmcpItem {
        std::string package;    // e.g. "Char.Vitals"
        std::string json;
        int linkNumber;         // 1-based
    };

    struct OutputQueue {
        std::mutex mutex;
        std::condition_variable cv;
        std::queue<OutputItem> queue;
        std::queue<GmcpItem> gmcpQueue;
        std::atomic<int> subscriberCount{0};
        std::atomic<int> gmcpSubscriberCount{0};
    };
    std::shared_ptr<OutputQueue> outputQueue{std::make_shared<OutputQueue>()};
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

    // Protocol type
    FrontDoorProto proto{FrontDoorProto::Telnet};
    WsState wsState;        // only used if proto == WebSocket
    std::string httpBuf;    // only used if proto == GrpcWeb (HTTP request accumulation)

    // Client IP address (for rate limiting)
    std::string clientIp;

    // grpc-web Subscribe stream state — when set, this fd receives
    // chunked game output frames instead of processing HTTP requests.
    bool grpcWebSubscribed{false};
    std::string grpcWebSessionId;   // which session we're subscribed to
    bool grpcWebTextMode{false};    // base64 encoding
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

    void onAccept(ganl::ConnectionHandle handle, const std::string& clientIp = "");
    void onAcceptWebSocket(ganl::ConnectionHandle handle, const std::string& clientIp = "");
    void onAcceptGrpcWeb(ganl::ConnectionHandle handle, const std::string& clientIp = "");
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

    // ---- gRPC support ----

    // Find session by persistent ID (random hex, survives restart).
    HydraSession* findByPersistId(const std::string& persistId);

    // Authenticate and create/resume a session. Returns persistId, or empty on failure.
    std::string authenticateAndGetSession(const std::string& username,
                                          const std::string& password);

    // Create account and auto-login. Returns persistId, or empty on failure.
    std::string createAccountAndGetSession(const std::string& username,
                                           const std::string& password,
                                           std::string& errorOut);

    // Eagerly restore all saved sessions from SQLite on startup.
    // Links are reconnected; scroll-back stays encrypted until client authenticates.
    void restoreAllSessions();

    // Expose internals for gRPC work items (called from main thread only).
    const HydraConfig& config() const { return config_; }
    AccountManager& accounts() { return accounts_; }
    ProcessManager& processMgr() { return procMgr_; }

    // Public session operations (used by gRPC work items and dispatchCommand).
    void connectToGame(HydraSession& session, const std::string& gameName);
    void closeLink(HydraSession& session, size_t linkIdx);

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
    void forwardToGame(HydraSession& session,
                       ganl::ConnectionHandle fdHandle,
                       const std::string& line);
    void handleGrpcWebRequest(FrontDoorState& fd);

    // Parse links_json and reconstruct BackDoorLink objects + activeLink.
    struct SavedLinkInfo {
        std::string game;
        std::string character;
    };
    static bool parseLinksJson(const std::string& json,
                               std::vector<SavedLinkInfo>& links,
                               size_t& activeLink);
    void restoreSessionLinks(HydraSession& session,
                             const std::vector<SavedLinkInfo>& savedLinks);

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

    // ---- Rate limiting state ----
    struct IpTracker {
        int connectionCount{0};         // current live connections from this IP
        std::vector<time_t> connectTimes;  // timestamps of recent connections (for rate)
        int failedLogins{0};            // consecutive failed login attempts
        time_t lockoutUntil{0};         // locked out until this time (0 = not locked)
    };
    std::map<std::string, IpTracker> ipTrackers_;

    std::string getClientIp(ganl::ConnectionHandle handle) const;
    bool checkConnectionLimits(const std::string& ip);
    void recordConnect(const std::string& ip);
    void recordDisconnect(const std::string& ip);
    void recordLoginFailure(const std::string& ip);
    void clearLoginFailures(const std::string& ip);
    bool isLockedOut(const std::string& ip);

    TelnetBridge bridge_;
    ProcessManager procMgr_;
};

#endif // HYDRA_SESSION_MANAGER_H
