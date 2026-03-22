// hydra_connection.cpp -- gRPC/Hydra transport for Windows Console client.
#ifdef HYDRA_GRPC

#include "hydra_connection.h"
#include "connection.h"  // for IOCP_KEY_HYDRA

#include "hydra.grpc.pb.h"
#include <grpcpp/grpcpp.h>

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using grpc::Status;

// Internal gRPC state — kept in .cpp to avoid grpc headers in .h
struct HydraConnection::GrpcState {
    std::shared_ptr<Channel> channel;
    std::unique_ptr<hydra::HydraService::Stub> stub;
    std::unique_ptr<ClientContext> sessionCtx;
    std::unique_ptr<ClientReaderWriter<hydra::ClientMessage, hydra::ServerMessage>> stream;
};

HydraConnection::HydraConnection(const std::string& world_name,
                                 const std::string& host,
                                 const std::string& port,
                                 const std::string& username,
                                 const std::string& password,
                                 const std::string& game_name,
                                 HANDLE iocp)
    : worldName_(world_name), host_(host), port_(port),
      username_(username), password_(password), gameName_(game_name),
      iocp_(iocp) {
    lastRecvTime_ = std::chrono::steady_clock::now();
    lastSendTime_ = lastRecvTime_;
}

HydraConnection::~HydraConnection() {
    disconnect();
}

bool HydraConnection::connect() {
    if (connected_.load()) return true;

    grpc_ = std::make_unique<GrpcState>();

    // Create channel
    std::string target = host_ + ":" + port_;
    grpc_->channel = grpc::CreateChannel(target,
        grpc::InsecureChannelCredentials());
    grpc_->stub = hydra::HydraService::NewStub(grpc_->channel);

    // Authenticate
    {
        ClientContext authCtx;
        hydra::AuthRequest req;
        req.set_username(username_);
        req.set_password(password_);
        hydra::AuthResponse resp;

        Status status = grpc_->stub->Authenticate(&authCtx, req, &resp);
        if (!status.ok() || !resp.success()) {
            std::string err = resp.error().empty() ? status.error_message()
                                                   : resp.error();
            {
                std::lock_guard<std::mutex> lock(outputMutex_);
                outputQueue_.push("[Hydra] Authentication failed: " + err);
            }
            signalOutput();
            grpc_.reset();
            return false;
        }
        sessionId_ = resp.session_id();
    }

    // Connect to game if specified
    if (!gameName_.empty()) {
        ClientContext connCtx;
        connCtx.AddMetadata("authorization", sessionId_);
        hydra::ConnectRequest req;
        req.set_session_id(sessionId_);
        req.set_game_name(gameName_);
        hydra::ConnectResponse resp;

        Status status = grpc_->stub->Connect(&connCtx, req, &resp);
        if (status.ok() && resp.success()) {
            std::lock_guard<std::mutex> lock(outputMutex_);
            outputQueue_.push("[Hydra] Connected to " + gameName_
                + " (link " + std::to_string(resp.link_number()) + ")");
        } else {
            std::lock_guard<std::mutex> lock(outputMutex_);
            outputQueue_.push("[Hydra] Game connect failed: "
                + (resp.error().empty() ? status.error_message() : resp.error()));
        }
        signalOutput();
    }

    // Open bidi GameSession stream
    grpc_->sessionCtx = std::make_unique<ClientContext>();
    grpc_->sessionCtx->AddMetadata("authorization", sessionId_);
    grpc_->stream = grpc_->stub->GameSession(grpc_->sessionCtx.get());

    if (!grpc_->stream) {
        std::lock_guard<std::mutex> lock(outputMutex_);
        outputQueue_.push("[Hydra] Failed to open GameSession stream");
        signalOutput();
        grpc_.reset();
        return false;
    }

    connected_.store(true);

    // Spawn reader thread
    readerThread_ = std::thread(&HydraConnection::readerLoop, this);

    {
        std::lock_guard<std::mutex> lock(outputMutex_);
        outputQueue_.push("[Hydra] Session established (" + sessionId_.substr(0, 8) + "...)");
    }
    signalOutput();

    return true;
}

void HydraConnection::disconnect() {
    if (!connected_.load()) return;
    connected_.store(false);

    // Cancel the gRPC context to unblock the reader
    if (grpc_ && grpc_->sessionCtx) {
        grpc_->sessionCtx->TryCancel();
    }

    // Close the write side of the stream
    if (grpc_ && grpc_->stream) {
        grpc_->stream->WritesDone();
    }

    if (readerThread_.joinable()) {
        readerThread_.join();
    }

    grpc_.reset();
}

bool HydraConnection::is_connected() const {
    return connected_.load();
}

bool HydraConnection::send_line(const std::string& line) {
    if (!connected_.load() || !grpc_ || !grpc_->stream) return false;

    hydra::ClientMessage msg;
    msg.set_input_line(line);

    if (!grpc_->stream->Write(msg)) {
        return false;
    }

    lastSendTime_ = std::chrono::steady_clock::now();
    return true;
}

std::vector<std::string> HydraConnection::drain_output() {
    std::vector<std::string> lines;
    std::lock_guard<std::mutex> lock(outputMutex_);
    while (!outputQueue_.empty()) {
        lines.push_back(std::move(outputQueue_.front()));
        outputQueue_.pop();
    }
    return lines;
}

std::string HydraConnection::check_prompt(int) {
    return "";  // Hydra doesn't do client-side prompt detection
}

bool HydraConnection::has_partial_line() const {
    return false;
}

void HydraConnection::add_to_scrollback(const std::string& line) {
    scrollback_.push_back(line);
    while (scrollback_.size() > MAX_SCROLLBACK) {
        scrollback_.pop_front();
    }
}

int HydraConnection::idle_secs() const {
    auto now = std::chrono::steady_clock::now();
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(now - lastRecvTime_).count());
}

int HydraConnection::send_idle_secs() const {
    auto now = std::chrono::steady_clock::now();
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(now - lastSendTime_).count());
}

// ---- Reader thread ----

void HydraConnection::readerLoop() {
    hydra::ServerMessage msg;
    while (connected_.load() && grpc_->stream->Read(&msg)) {
        lastRecvTime_ = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(outputMutex_);

        if (msg.has_game_output()) {
            outputQueue_.push(msg.game_output().text());
        } else if (msg.has_gmcp()) {
            outputQueue_.push("[GMCP " + msg.gmcp().package() + "] "
                            + msg.gmcp().json());
        } else if (msg.has_notice()) {
            outputQueue_.push("[Hydra] " + msg.notice().text());
        } else if (msg.has_link_event()) {
            const auto& ev = msg.link_event();
            outputQueue_.push("[Hydra] Link " + std::to_string(ev.link_number())
                + " (" + ev.game_name() + "): "
                + hydra::LinkState_Name(ev.new_state()));
        } else if (msg.has_pong()) {
            // Silently absorb pongs
            continue;
        }

        signalOutput();
    }

    // Stream ended
    if (connected_.load()) {
        connected_.store(false);
        std::lock_guard<std::mutex> lock(outputMutex_);
        outputQueue_.push("[Hydra] Connection lost");
        signalOutput();
    }
}

void HydraConnection::signalOutput() {
    // Post to the IOCP to wake the main event loop.
    PostQueuedCompletionStatus(iocp_, 0, IOCP_KEY_HYDRA, nullptr);
}

#endif // HYDRA_GRPC
