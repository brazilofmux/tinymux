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
                                 HANDLE iocp,
                                 bool use_tls,
                                 int term_width,
                                 int term_height)
    : worldName_(world_name), host_(host), port_(port),
      username_(username), password_(password), gameName_(game_name),
      iocp_(iocp), useTls_(use_tls),
      termWidth_(term_width), termHeight_(term_height) {
    lastRecvTime_ = std::chrono::steady_clock::now();
    lastSendTime_ = lastRecvTime_;
}

HydraConnection::~HydraConnection() {
    disconnect();
}

bool HydraConnection::connect() {
    if (connected_.load()) return true;

    try {
    grpc_ = std::make_unique<GrpcState>();

    // Create channel (TLS by default, plaintext only for local dev)
    std::string target = host_ + ":" + port_;
    if (useTls_) {
        grpc_->channel = grpc::CreateChannel(target,
            grpc::SslCredentials(grpc::SslCredentialsOptions()));
    } else {
        grpc_->channel = grpc::CreateChannel(target,
            grpc::InsecureChannelCredentials());
    }
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
                outputQueue_.push(OutputChunk{"[Hydra] Authentication failed: " + err, false});
            }
            signalOutput();
            grpc_.reset();
            return false;
        }
        sessionId_ = resp.session_id();
    }

    // Open bidi GameSession stream BEFORE connecting to the game,
    // so the subscriber is registered when game output arrives.
    grpc_->sessionCtx = std::make_unique<ClientContext>();
    grpc_->sessionCtx->AddMetadata("authorization", sessionId_);
    grpc_->stream = grpc_->stub->GameSession(grpc_->sessionCtx.get());

    if (!grpc_->stream) {
        std::lock_guard<std::mutex> lock(outputMutex_);
        outputQueue_.push(OutputChunk{"[Hydra] Failed to open GameSession stream", false});
        signalOutput();
        grpc_.reset();
        return false;
    }

    // Send initial preferences (color format, terminal size, type)
    {
        hydra::ClientMessage prefsMsg;
        auto* prefs = prefsMsg.mutable_preferences();
        prefs->set_color_format(static_cast<hydra::ColorFormat>(colorFormat_));
        prefs->set_terminal_width(termWidth_);
        prefs->set_terminal_height(termHeight_);
        prefs->set_terminal_type("TinyMUX-Console");
        grpc_->stream->Write(prefsMsg);
    }

    connected_.store(true);

    // Spawn reader thread
    readerThread_ = std::thread(&HydraConnection::readerLoop, this);

    {
        std::lock_guard<std::mutex> lock(outputMutex_);
        outputQueue_.push(OutputChunk{"[Hydra] Session established (" + sessionId_.substr(0, 8) + "...)", false});
    }
    signalOutput();

    // Connect to game AFTER the stream is open and the reader is
    // consuming output, so the welcome screen isn't lost.
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
            outputQueue_.push(OutputChunk{"[Hydra] Connected to " + gameName_
                + " (link " + std::to_string(resp.link_number()) + ")", false});
        } else {
            std::lock_guard<std::mutex> lock(outputMutex_);
            outputQueue_.push(OutputChunk{"[Hydra] Game connect failed: "
                + (resp.error().empty() ? status.error_message() : resp.error()), false});
        }
        signalOutput();
    }

    return true;
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> lock(outputMutex_);
        outputQueue_.push(OutputChunk{std::string("[Hydra] Connect exception: ") + e.what(), false});
        signalOutput();
        return false;
    } catch (...) {
        std::lock_guard<std::mutex> lock(outputMutex_);
        outputQueue_.push(OutputChunk{"[Hydra] Connect failed with unknown exception", false});
        signalOutput();
        return false;
    }
}

void HydraConnection::disconnect() {
    bool wasConnected = connected_.exchange(false);
    reconnecting_.store(false);
    if (!wasConnected) return;

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
    auto chunks = drain_output_chunks();
    std::vector<std::string> lines;
    lines.reserve(chunks.size());
    for (auto& chunk : chunks) {
        lines.push_back(std::move(chunk.text));
    }
    return lines;
}

std::vector<HydraConnection::OutputChunk> HydraConnection::drain_output_chunks() {
    std::vector<OutputChunk> chunks;
    std::lock_guard<std::mutex> lock(outputMutex_);
    while (!outputQueue_.empty()) {
        chunks.push_back(std::move(outputQueue_.front()));
        outputQueue_.pop();
    }
    return chunks;
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

// ---- Reader thread with reconnect ----

void HydraConnection::pushOutput(const std::string& line) {
    std::lock_guard<std::mutex> lock(outputMutex_);
    outputQueue_.push(OutputChunk{line, false});
    signalOutput();
}

void HydraConnection::readerLoop() {
    hydra::ServerMessage msg;
    while (connected_.load() && grpc_ && grpc_->stream &&
           grpc_->stream->Read(&msg)) {
        lastRecvTime_ = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(outputMutex_);

        if (msg.has_game_output()) {
            outputQueue_.push(OutputChunk{msg.game_output().text(), true});
        } else if (msg.has_gmcp()) {
            outputQueue_.push(OutputChunk{"[GMCP " + msg.gmcp().package() + "] "
                            + msg.gmcp().json(), false});
        } else if (msg.has_notice()) {
            outputQueue_.push(OutputChunk{"[Hydra] " + msg.notice().text(), false});
        } else if (msg.has_link_event()) {
            const auto& ev = msg.link_event();
            outputQueue_.push(OutputChunk{"[Hydra] Link " + std::to_string(ev.link_number())
                + " (" + ev.game_name() + "): "
                + hydra::LinkState_Name(ev.new_state()), false});
        } else if (msg.has_pong()) {
            continue;
        }

        signalOutput();
    }

    // Stream ended — attempt reconnect if not intentional disconnect
    if (connected_.load() && !reconnecting_.load()) {
        attemptReconnect();
    }
}

void HydraConnection::fetchScrollBack() {
    if (!grpc_ || !grpc_->stub || sessionId_.empty()) return;
    try {
        ClientContext ctx;
        ctx.AddMetadata("authorization", sessionId_);
        hydra::ScrollBackRequest req;
        req.set_session_id(sessionId_);
        req.set_max_lines(200);  // fetch last 200 lines on reconnect
        req.set_color_format(static_cast<hydra::ColorFormat>(colorFormat_));
        hydra::ScrollBackResponse resp;
        Status status = grpc_->stub->GetScrollBack(&ctx, req, &resp);
        if (status.ok() && resp.lines_size() > 0) {
            pushOutput("-- scroll-back (" + std::to_string(resp.lines_size()) + " lines) --");
            for (int i = 0; i < resp.lines_size(); i++) {
                std::lock_guard<std::mutex> lock(outputMutex_);
                outputQueue_.push(OutputChunk{resp.lines(i).text(), true});
            }
            signalOutput();
            pushOutput("-- end scroll-back --");
        }
    } catch (...) {
        // Non-fatal — scroll-back is optional
    }
}

void HydraConnection::attemptReconnect() {
    if (reconnecting_.exchange(true)) return;

    pushOutput("[Hydra] Stream lost, attempting reconnect...");

    // Clean up old stream (we're still in the reader thread)
    if (grpc_) {
        grpc_->sessionCtx.reset();
        grpc_->stream.reset();
    }
    connected_.store(false);

    for (int attempt = 1; attempt <= MAX_RECONNECT_ATTEMPTS; attempt++) {
        // Interruptible sleep — check connected_ every 100ms so
        // disconnect() can unblock us promptly.
        for (int w = 0; w < RECONNECT_DELAY_SECS * 10; w++) {
            if (!connected_.load() && !reconnecting_.load()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        if (!grpc_ || !grpc_->stub || sessionId_.empty()) break;

        // Reopen bidi stream with same session token
        grpc_->sessionCtx = std::make_unique<ClientContext>();
        grpc_->sessionCtx->AddMetadata("authorization", sessionId_);
        grpc_->stream = grpc_->stub->GameSession(grpc_->sessionCtx.get());

        if (grpc_->stream) {
            // Resend preferences on the new stream
            {
                hydra::ClientMessage prefsMsg;
                auto* prefs = prefsMsg.mutable_preferences();
                prefs->set_color_format(static_cast<hydra::ColorFormat>(colorFormat_));
                prefs->set_terminal_width(termWidth_);
                prefs->set_terminal_height(termHeight_);
                prefs->set_terminal_type("TinyMUX-Console");
                grpc_->stream->Write(prefsMsg);
            }

            connected_.store(true);
            reconnecting_.store(false);
            pushOutput("[Hydra] Reconnected (attempt " + std::to_string(attempt) + ")");

            // Fetch scroll-back to restore missed output
            fetchScrollBack();

            // Re-enter read loop
            hydra::ServerMessage msg;
            while (connected_.load() && grpc_->stream->Read(&msg)) {
                lastRecvTime_ = std::chrono::steady_clock::now();

                std::lock_guard<std::mutex> lock(outputMutex_);

                if (msg.has_game_output()) {
                    outputQueue_.push(OutputChunk{msg.game_output().text(), true});
                } else if (msg.has_gmcp()) {
                    outputQueue_.push(OutputChunk{"[GMCP " + msg.gmcp().package() + "] "
                                    + msg.gmcp().json(), false});
                } else if (msg.has_notice()) {
                    outputQueue_.push(OutputChunk{"[Hydra] " + msg.notice().text(), false});
                } else if (msg.has_link_event()) {
                    const auto& ev = msg.link_event();
                    outputQueue_.push(OutputChunk{"[Hydra] Link " + std::to_string(ev.link_number())
                        + " (" + ev.game_name() + "): "
                        + hydra::LinkState_Name(ev.new_state()), false});
                } else if (msg.has_pong()) {
                    continue;
                }

                signalOutput();
            }

            // Stream broke again
            if (connected_.load()) {
                pushOutput("[Hydra] Stream lost again, retrying...");
                grpc_->sessionCtx.reset();
                grpc_->stream.reset();
                connected_.store(false);
                continue;
            }
            return;  // intentional disconnect
        }

        pushOutput("[Hydra] Reconnect attempt " + std::to_string(attempt)
            + "/" + std::to_string(MAX_RECONNECT_ATTEMPTS) + " failed");
    }

    reconnecting_.store(false);
    pushOutput("[Hydra] Reconnect failed after "
        + std::to_string(MAX_RECONNECT_ATTEMPTS) + " attempts");
}

void HydraConnection::signalOutput() {
    PostQueuedCompletionStatus(iocp_, 0, IOCP_KEY_HYDRA, nullptr);
}

// ---- Hydra command dispatch ----
// Centralizes /h* command parsing so all clients can delegate here.

static std::string trimStr(const std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    return s.substr(start, s.find_last_not_of(" \t") - start + 1);
}

bool HydraConnection::handleCommand(const std::string& line) {
    if (line.size() < 2 || line[0] != '/') return false;

    // Extract command and args
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

    std::string cmd, args;
    size_t sp = line.find(' ');
    if (sp != std::string::npos) {
        cmd = lower.substr(1, sp - 1);
        args = trimStr(line.substr(sp + 1));
    } else {
        cmd = lower.substr(1);
    }

    if (cmd == "hcreate") {
        size_t sep = args.find(' ');
        if (sep == std::string::npos) {
            pushOutput("[Hydra] Usage: /hcreate <username> <password>");
        } else {
            pushOutput(rpc_create_account(args.substr(0, sep), trimStr(args.substr(sep + 1))));
        }
    } else if (cmd == "hconnect") {
        if (args.empty()) pushOutput("[Hydra] Usage: /hconnect <game>");
        else pushOutput(rpc_connect_game(args));
    } else if (cmd == "hswitch") {
        int link = 0;
        try { link = std::stoi(args); } catch (...) {}
        if (link <= 0) pushOutput("[Hydra] Usage: /hswitch <link#>");
        else pushOutput(rpc_switch_link(link));
    } else if (cmd == "hlinks") {
        for (auto& l : rpc_list_links()) pushOutput(l);
    } else if (cmd == "hdisconnect") {
        int link = 0;
        try { link = std::stoi(args); } catch (...) {}
        if (link <= 0) pushOutput("[Hydra] Usage: /hdisconnect <link#>");
        else pushOutput(rpc_disconnect_link(link));
    } else if (cmd == "hsession") {
        for (auto& l : rpc_get_session()) pushOutput(l);
    } else if (cmd == "hdetach") {
        pushOutput(rpc_detach_session());
    } else if (cmd == "hhelp") {
        pushOutput("[Hydra] Commands:");
        pushOutput("  /hcreate <user> <pass>     - create account");
        pushOutput("  /hconnect <game>           - connect to a game");
        pushOutput("  /hswitch <link#>           - switch active link");
        pushOutput("  /hdisconnect <link#>       - disconnect a link");
        pushOutput("  /hlinks                    - list active links");
        pushOutput("  /hsession                  - show session info");
        pushOutput("  /hdetach                   - detach session");
        pushOutput("  /hhelp                     - this help");
    } else {
        return false;  // not a recognized /h* command
    }
    return true;
}

// ---- Hydra session management RPCs ----

std::string HydraConnection::rpc_create_account(const std::string& username,
                                                 const std::string& password) {
    if (!grpc_ || !grpc_->stub) return "[Hydra] Not connected.";
    try {
        ClientContext ctx;
        hydra::CreateAccountRequest req;
        req.set_username(username);
        req.set_password(password);
        hydra::CreateAccountResponse resp;
        Status status = grpc_->stub->CreateAccount(&ctx, req, &resp);
        if (!status.ok())
            return "[Hydra] RPC error: " + status.error_message();
        if (resp.success()) {
            sessionId_ = resp.session_id();
            return "[Hydra] Account created. Logged in as " + username
                + " (" + sessionId_.substr(0, 8) + "...)";
        }
        return "[Hydra] Create failed: " + resp.error();
    } catch (const std::exception& e) {
        return std::string("[Hydra] Error: ") + e.what();
    }
}

std::string HydraConnection::rpc_connect_game(const std::string& game_name) {
    if (!grpc_ || !grpc_->stub) return "[Hydra] Not connected.";
    try {
        ClientContext ctx;
        ctx.AddMetadata("authorization", sessionId_);
        hydra::ConnectRequest req;
        req.set_session_id(sessionId_);
        req.set_game_name(game_name);
        hydra::ConnectResponse resp;
        Status status = grpc_->stub->Connect(&ctx, req, &resp);
        if (!status.ok())
            return "[Hydra] RPC error: " + status.error_message();
        if (resp.success())
            return "[Hydra] Connected to " + game_name + " (link " + std::to_string(resp.link_number()) + ")";
        return "[Hydra] Connect failed: " + resp.error();
    } catch (const std::exception& e) {
        return std::string("[Hydra] Error: ") + e.what();
    }
}

std::string HydraConnection::rpc_switch_link(int link_number) {
    if (!grpc_ || !grpc_->stub) return "[Hydra] Not connected.";
    try {
        ClientContext ctx;
        ctx.AddMetadata("authorization", sessionId_);
        hydra::SwitchRequest req;
        req.set_session_id(sessionId_);
        req.set_link_number(link_number);
        hydra::SwitchResponse resp;
        Status status = grpc_->stub->SwitchLink(&ctx, req, &resp);
        if (!status.ok())
            return "[Hydra] RPC error: " + status.error_message();
        if (resp.success())
            return "[Hydra] Switched to link " + std::to_string(link_number);
        return "[Hydra] Switch failed: " + resp.error();
    } catch (const std::exception& e) {
        return std::string("[Hydra] Error: ") + e.what();
    }
}

std::vector<std::string> HydraConnection::rpc_list_links() {
    std::vector<std::string> result;
    if (!grpc_ || !grpc_->stub) { result.push_back("[Hydra] Not connected."); return result; }
    try {
        ClientContext ctx;
        ctx.AddMetadata("authorization", sessionId_);
        hydra::SessionRequest req;
        req.set_session_id(sessionId_);
        hydra::LinkList resp;
        Status status = grpc_->stub->ListLinks(&ctx, req, &resp);
        if (!status.ok()) {
            result.push_back("[Hydra] RPC error: " + status.error_message());
            return result;
        }
        if (resp.links_size() == 0) {
            result.push_back("[Hydra] No active links.");
            return result;
        }
        result.push_back("[Hydra] Active links:");
        for (int i = 0; i < resp.links_size(); i++) {
            const auto& li = resp.links(i);
            std::string line = "  Link " + std::to_string(li.number())
                + ": " + li.game_name()
                + " (" + hydra::LinkState_Name(li.state()) + ")";
            if (li.active()) line += " [active]";
            if (!li.character().empty()) line += " as " + li.character();
            result.push_back(line);
        }
    } catch (const std::exception& e) {
        result.push_back(std::string("[Hydra] Error: ") + e.what());
    }
    return result;
}

std::string HydraConnection::rpc_disconnect_link(int link_number) {
    if (!grpc_ || !grpc_->stub) return "[Hydra] Not connected.";
    try {
        ClientContext ctx;
        ctx.AddMetadata("authorization", sessionId_);
        hydra::DisconnectRequest req;
        req.set_session_id(sessionId_);
        req.set_link_number(link_number);
        hydra::DisconnectResponse resp;
        Status status = grpc_->stub->DisconnectLink(&ctx, req, &resp);
        if (!status.ok())
            return "[Hydra] RPC error: " + status.error_message();
        if (resp.success())
            return "[Hydra] Disconnected link " + std::to_string(link_number);
        return "[Hydra] Disconnect failed: " + resp.error();
    } catch (const std::exception& e) {
        return std::string("[Hydra] Error: ") + e.what();
    }
}

std::vector<std::string> HydraConnection::rpc_get_session() {
    std::vector<std::string> result;
    if (!grpc_ || !grpc_->stub) { result.push_back("[Hydra] Not connected."); return result; }
    try {
        ClientContext ctx;
        ctx.AddMetadata("authorization", sessionId_);
        hydra::SessionRequest req;
        req.set_session_id(sessionId_);
        hydra::SessionInfo resp;
        Status status = grpc_->stub->GetSession(&ctx, req, &resp);
        if (!status.ok()) {
            result.push_back("[Hydra] RPC error: " + status.error_message());
            return result;
        }
        result.push_back("[Hydra] Session " + resp.session_id().substr(0, 8) + "...");
        result.push_back("  User: " + resp.username());
        result.push_back("  State: " + hydra::SessionState_Name(resp.state()));
        result.push_back("  Active link: " + std::to_string(resp.active_link()));
        result.push_back("  Links: " + std::to_string(resp.links_size()));
        result.push_back("  Scrollback: " + std::to_string(resp.scrollback_lines()) + " lines");
    } catch (const std::exception& e) {
        result.push_back(std::string("[Hydra] Error: ") + e.what());
    }
    return result;
}

std::string HydraConnection::rpc_detach_session() {
    if (!grpc_ || !grpc_->stub) return "[Hydra] Not connected.";
    try {
        ClientContext ctx;
        ctx.AddMetadata("authorization", sessionId_);
        hydra::SessionRequest req;
        req.set_session_id(sessionId_);
        hydra::Empty resp;
        Status status = grpc_->stub->DetachSession(&ctx, req, &resp);
        if (!status.ok())
            return "[Hydra] RPC error: " + status.error_message();
        connected_.store(false);
        return "[Hydra] Session detached. Reconnect to resume.";
    } catch (const std::exception& e) {
        return std::string("[Hydra] Error: ") + e.what();
    }
}

#endif // HYDRA_GRPC
