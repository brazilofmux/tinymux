// hydra_connection.h -- gRPC connection to a game via Hydra proxy.
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
// Implements IConnection so the IOCP event loop can treat it like a telnet
// connection.  Output from the gRPC reader thread is posted to the IOCP
// via PostQueuedCompletionStatus with IOCP_KEY_HYDRA.
class HydraConnection : public IConnection {
public:
    struct OutputChunk {
        std::string text;
        bool is_stream_text = false;
    };

    HydraConnection(const std::string& world_name,
                    const std::string& host,
                    const std::string& port,
                    const std::string& username,
                    const std::string& password,
                    const std::string& game_name,
                    HANDLE iocp,
                    bool use_tls = true,
                    int term_width = 80,
                    int term_height = 24);
    ~HydraConnection() override;

    HydraConnection(const HydraConnection&) = delete;
    HydraConnection& operator=(const HydraConnection&) = delete;

    // Connect: authenticate, open bidi stream, spawn reader thread.
    bool connect();

    // IConnection interface
    void disconnect() override;
    bool is_connected() const override;
    bool send_line(const std::string& line) override;

    const std::string& world_name() const override { return worldName_; }
    const std::string& host() const override { return host_; }
    const std::string& port() const override { return port_; }

    std::string check_prompt(int timeout_ms) override;
    bool has_partial_line() const override;

    const std::deque<std::string>& scrollback() const override { return scrollback_; }
    void add_to_scrollback(const std::string& line) override;

    int idle_secs() const override;
    int send_idle_secs() const override;

    bool is_hydra() const override { return true; }

    // Drain output queue — called from main loop on IOCP_KEY_HYDRA.
    std::vector<std::string> drain_output();
    std::vector<OutputChunk> drain_output_chunks();

    // Hydra session token
    const std::string& session_id() const { return sessionId_; }

    // Intercept /h* commands from user input.  Returns true if the
    // line was a Hydra command (handled internally), false if it should
    // be forwarded to the game.  Output is pushed to the output queue.
    bool handleCommand(const std::string& line);

    // Hydra session management RPCs (unary, may block briefly)
    std::string rpc_create_account(const std::string& username, const std::string& password);
    std::string rpc_connect_game(const std::string& game_name);
    std::string rpc_switch_link(int link_number);
    std::vector<std::string> rpc_list_links();
    std::string rpc_disconnect_link(int link_number);
    std::vector<std::string> rpc_get_session();
    std::string rpc_detach_session();

private:
    void readerLoop();
    void signalOutput();
    void pushOutput(const std::string& line);
    void attemptReconnect();
    void fetchScrollBack();

    static constexpr int MAX_RECONNECT_ATTEMPTS = 5;
    static constexpr int RECONNECT_DELAY_SECS = 3;

    std::string worldName_;
    std::string host_;
    std::string port_;
    std::string username_;
    std::string password_;
    std::string gameName_;
    std::string sessionId_;
    HANDLE iocp_;
    bool useTls_;
    int termWidth_;
    int termHeight_;
    int colorFormat_{1};  // hydra::ANSI_TRUECOLOR=1, hydra::PUA_UTF8=4

public:
    // Set color format before connect(). Default is ANSI_TRUECOLOR (1).
    // Win32 GUI should set PUA_UTF8 (4) for custom rendering.
    void setColorFormat(int fmt) { colorFormat_ = fmt; }
private:

    // gRPC state (opaque — actual types in .cpp to avoid grpc headers here)
    struct GrpcState;
    std::unique_ptr<GrpcState> grpc_;

    // Reader thread pushes lines here
    std::mutex outputMutex_;
    std::queue<OutputChunk> outputQueue_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> reconnecting_{false};
    std::thread readerThread_;

    // Shutdown signal — wakes the reconnect loop immediately.
    std::mutex shutdownMutex_;
    std::condition_variable shutdownCv_;

    // Scrollback
    std::deque<std::string> scrollback_;
    static constexpr size_t MAX_SCROLLBACK = 10000;

    // Idle tracking
    std::chrono::steady_clock::time_point lastRecvTime_;
    std::chrono::steady_clock::time_point lastSendTime_;
};

#endif // HYDRA_GRPC
#endif // HYDRA_CONNECTION_H
