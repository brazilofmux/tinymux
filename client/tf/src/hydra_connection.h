#ifndef HYDRA_CONNECTION_H
#define HYDRA_CONNECTION_H

#ifdef HYDRA_GRPC

#include "iconnection.h"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace grpc { class Channel; class ClientContext; }

// A connection to a game server via Hydra's gRPC GameSession bidi stream.
// Presents the same IConnection interface as Connection (telnet) so the
// main event loop can poll() on it alongside telnet connections.
//
// Hydra commands intercepted from user input:
//   /hconnect <game>     — connect to a game via Hydra
//   /hswitch <link#>     — switch active link
//   /hdisconnect <link#> — disconnect a specific link
//   /hlinks              — list active links
//   /hgames              — list available games
//   /hscroll [n]         — fetch scroll-back from server
//
// All other input is forwarded to the active game link.
class HydraConnection : public IConnection {
public:
    HydraConnection(const std::string& world_name,
                    const std::string& host,
                    const std::string& port,
                    const std::string& username,
                    const std::string& password,
                    const std::string& game_name);
    ~HydraConnection() override;

    HydraConnection(const HydraConnection&) = delete;
    HydraConnection& operator=(const HydraConnection&) = delete;

    // IConnection interface
    bool connect() override;
    void disconnect() override;
    bool is_connected() const override;

    bool send_line(const std::string& line) override;
    std::vector<std::string> read_lines() override;

    int fd() const override;
    const std::string& world_name() const override { return worldName_; }
    const std::string& host() const override { return host_; }
    const std::string& port() const override { return port_; }

    std::string check_prompt(std::chrono::milliseconds timeout) override;
    std::string current_prompt() const override;
    bool has_partial_line() const override;

    const std::deque<std::string>& scrollback() const override { return scrollback_; }
    void add_to_scrollback(const std::string& line) override;

    int idle_secs() const override;
    int sidle_secs() const override;

    bool is_hydra() const override { return true; }

    // Hydra session token (for restart serialization)
    const std::string& session_id() const { return sessionId_; }

private:
    void readerLoop();
    void signalOutput();
    void pushOutput(const std::string& line);

    // Open (or reopen) the bidi GameSession stream.
    bool openStream();

    // Hydra command handlers (called from send_line on main thread)
    void cmdConnect(const std::string& gameName);
    void cmdSwitch(const std::string& args);
    void cmdDisconnect(const std::string& args);
    void cmdLinks();
    void cmdGames();
    void cmdScroll(const std::string& args);
    void cmdAddCred(const std::string& args);
    void cmdDelCred(const std::string& args);
    void cmdCreds();
    void cmdStart(const std::string& args);
    void cmdStop(const std::string& args);
    void cmdRestart(const std::string& args);
    void cmdStatus(const std::string& args);

    // Keepalive
    void sendPing();
    static constexpr int PING_INTERVAL_SECS = 60;

    // Reconnect logic
    void attemptReconnect();
    static constexpr int MAX_RECONNECT_ATTEMPTS = 5;
    static constexpr int RECONNECT_DELAY_SECS = 3;

    std::string worldName_;
    std::string host_;
    std::string port_;
    std::string username_;
    std::string password_;
    std::string gameName_;      // game to /connect after auth
    std::string sessionId_;     // Hydra persistent session token

    // eventfd for waking the poll() loop when output arrives
    int eventFd_ = -1;

    // gRPC state (opaque — actual types in .cpp to avoid grpc headers here)
    struct GrpcState;
    std::unique_ptr<GrpcState> grpc_;

    // Reader thread pushes lines here
    std::mutex outputMutex_;
    std::queue<std::string> outputQueue_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> reconnecting_{false};
    std::thread readerThread_;

    // Scrollback
    std::deque<std::string> scrollback_;
    static constexpr size_t MAX_SCROLLBACK = 10000;

    // Idle tracking
    std::chrono::steady_clock::time_point lastRecvTime_;
    std::chrono::steady_clock::time_point lastSendTime_;
    std::chrono::steady_clock::time_point lastPingTime_;
};

#endif // HYDRA_GRPC
#endif // HYDRA_CONNECTION_H
