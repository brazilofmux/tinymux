// hydra_connection.cpp -- gRPC/Hydra transport for Windows Console client.
#ifdef HYDRA_GRPC

#include "hydra_connection.h"
#include "connection.h"  // for IOCP_KEY_HYDRA
#include "secure_util.h"

#include "hydra.grpc.pb.h"
#include <grpcpp/grpcpp.h>

using grpc::ChannelArguments;
using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using grpc::Status;

// Internal gRPC state — kept in .cpp to avoid grpc headers in .h
struct HydraConnection::GrpcState {
    std::shared_ptr<Channel> channel;
    std::unique_ptr<hydra::HydraService::Stub> stub;
};

struct HydraConnection::StreamState {
    std::shared_ptr<ClientContext> context;
    std::shared_ptr<ClientReaderWriter<hydra::ClientMessage, hydra::ServerMessage>> stream;
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
    lastPingTime_ = lastRecvTime_;
}

HydraConnection::~HydraConnection() {
    disconnect();
}

bool HydraConnection::connect() {
    if (connected_.load()) return true;

    try {
        stopRequested_.store(false);
        reconnecting_.store(false);
        auto grpcState = std::make_shared<GrpcState>();

        std::string target = host_ + ":" + port_;
        ChannelArguments args;
        args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, KEEPALIVE_INTERVAL_SECS * 1000);
        args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 10000);
        args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);
        args.SetInt(GRPC_ARG_HTTP2_MAX_PINGS_WITHOUT_DATA, 0);
        args.SetInt(GRPC_ARG_HTTP2_MIN_RECV_PING_INTERVAL_WITHOUT_DATA_MS, 10000);
        args.SetInt(GRPC_ARG_HTTP2_MIN_SENT_PING_INTERVAL_WITHOUT_DATA_MS, 10000);

        if (useTls_) {
            grpcState->channel = grpc::CreateCustomChannel(target,
                grpc::SslCredentials(grpc::SslCredentialsOptions()), args);
        } else {
            grpcState->channel = grpc::CreateCustomChannel(target,
                grpc::InsecureChannelCredentials(), args);
        }
        grpcState->stub = hydra::HydraService::NewStub(grpcState->channel);

        {
            ClientContext authCtx;
            hydra::AuthRequest req;
            req.set_username(username_);
            req.set_password(password_);
            hydra::AuthResponse resp;

            Status status = grpcState->stub->Authenticate(&authCtx, req, &resp);
            if (!status.ok() || !resp.success()) {
                std::string err = resp.error().empty() ? status.error_message()
                                                       : resp.error();
                pushOutput("[Hydra] Authentication failed: " + err);
                return false;
            }
            sessionId_ = resp.session_id();
            secure_zero(password_);
        }

        {
            std::lock_guard<std::mutex> lock(grpcMutex_);
            grpc_ = grpcState;
        }

        if (!openStream()) {
            std::lock_guard<std::mutex> lock(grpcMutex_);
            if (grpc_ == grpcState) {
                grpc_.reset();
            }
            return false;
        }

        connected_.store(true);
        pushOutput("[Hydra] Session established (" + sessionId_.substr(0, 8) + "...)");

        keepaliveThread_ = std::thread(&HydraConnection::keepaliveLoop, this);

        // Connect to the initial game only after the stream is already live.
        if (!gameName_.empty()) {
            auto activeGrpc = currentGrpcState();
            if (!activeGrpc || !activeGrpc->stub) {
                pushOutput("[Hydra] Game connect failed: transport unavailable");
                return true;
            }
            ClientContext connCtx;
            connCtx.AddMetadata("authorization", sessionId_);
            hydra::ConnectRequest req;
            req.set_session_id(sessionId_);
            req.set_game_name(gameName_);
            hydra::ConnectResponse resp;

            Status status = activeGrpc->stub->Connect(&connCtx, req, &resp);
            if (status.ok() && resp.success()) {
                pushOutput("[Hydra] Connected to " + gameName_
                    + " (link " + std::to_string(resp.link_number()) + ")");
            } else {
                pushOutput("[Hydra] Game connect failed: "
                    + (resp.error().empty() ? status.error_message() : resp.error()));
            }
        }

        return true;
    } catch (const std::exception& e) {
        pushOutput(std::string("[Hydra] Connect exception: ") + e.what());
        return false;
    } catch (...) {
        pushOutput("[Hydra] Connect failed with unknown exception");
        return false;
    }
}

void HydraConnection::disconnect() {
    stopRequested_.store(true);
    reconnecting_.store(false);
    connected_.store(false);

    // Wake the reconnect loop if it's waiting.
    shutdownCv_.notify_all();

    std::shared_ptr<StreamState> streamState;
    {
        std::lock_guard<std::mutex> lock(streamStateMutex_);
        streamState = streamState_;
    }

    if (streamState && streamState->context) {
        streamState->context->TryCancel();
    }

    if (streamState && streamState->stream) {
        std::lock_guard<std::mutex> writeLock(writeMutex_);
        streamState->stream->WritesDone();
    }

    if (readerThread_.joinable()) {
        readerThread_.join();
    }
    if (keepaliveThread_.joinable()) {
        keepaliveThread_.join();
    }

    {
        std::lock_guard<std::mutex> lock(streamStateMutex_);
        streamState_.reset();
    }
    {
        std::lock_guard<std::mutex> lock(grpcMutex_);
        grpc_.reset();
    }
}

bool HydraConnection::is_connected() const {
    return connected_.load();
}

bool HydraConnection::send_line(const std::string& line) {
    hydra::ClientMessage msg;
    msg.set_input_line(line);

    if (!sendClientMessage(msg)) {
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
    std::lock_guard<std::mutex> lock(outputMutex_);
    return !streamLineBuffer_.empty();
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

std::shared_ptr<HydraConnection::GrpcState> HydraConnection::currentGrpcState() const {
    std::lock_guard<std::mutex> lock(grpcMutex_);
    return grpc_;
}

void HydraConnection::pushOutput(const std::string& line) {
    std::lock_guard<std::mutex> lock(outputMutex_);
    outputQueue_.push(OutputChunk{line, false});
    signalOutput();
}

bool HydraConnection::openStream(bool startReaderThread) {
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub) {
        return false;
    }

    auto streamState = std::make_shared<StreamState>();
    streamState->context = std::make_shared<ClientContext>();
    streamState->context->AddMetadata("authorization", sessionId_);
    streamState->stream = std::shared_ptr<ClientReaderWriter<hydra::ClientMessage, hydra::ServerMessage>>(
        grpcState->stub->GameSession(streamState->context.get()).release());

    if (!streamState->stream) {
        pushOutput("[Hydra] Failed to open GameSession stream");
        return false;
    }

    {
        std::lock_guard<std::mutex> lock(streamStateMutex_);
        streamState_ = streamState;
    }

    if (!sendPreferences()) {
        {
            std::lock_guard<std::mutex> lock(streamStateMutex_);
            if (streamState_ == streamState) {
                streamState_.reset();
            }
        }
        pushOutput("[Hydra] Failed to send initial GameSession preferences");
        return false;
    }

    lastRecvTime_ = std::chrono::steady_clock::now();
    lastSendTime_ = lastRecvTime_;
    lastPingTime_ = lastRecvTime_;

    if (startReaderThread && readerThread_.joinable() &&
        readerThread_.get_id() != std::this_thread::get_id()) {
        readerThread_.join();
    }
    if (startReaderThread) {
        readerThread_ = std::thread(
            static_cast<void (HydraConnection::*)(std::shared_ptr<StreamState>)>(
                &HydraConnection::readerLoop),
            this, streamState);
    }
    return true;
}

bool HydraConnection::sendClientMessage(const hydra::ClientMessage& msg) {
    if (stopRequested_.load()) {
        return false;
    }

    std::shared_ptr<StreamState> streamState;
    {
        std::lock_guard<std::mutex> lock(streamStateMutex_);
        streamState = streamState_;
    }
    if (!streamState || !streamState->stream) {
        return false;
    }

    std::lock_guard<std::mutex> writeLock(writeMutex_);
    return streamState->stream->Write(msg);
}

bool HydraConnection::sendPreferences() {
    hydra::ClientMessage prefsMsg;
    auto* prefs = prefsMsg.mutable_preferences();
    prefs->set_color_format(static_cast<hydra::ColorFormat>(colorFormat_));
    prefs->set_terminal_width(termWidth_);
    prefs->set_terminal_height(termHeight_);
    prefs->set_terminal_type("TinyMUX-Console");
    if (!sendClientMessage(prefsMsg)) {
        return false;
    }
    lastSendTime_ = std::chrono::steady_clock::now();
    return true;
}

void HydraConnection::readerLoop() {
    std::shared_ptr<StreamState> streamState;
    {
        std::lock_guard<std::mutex> lock(streamStateMutex_);
        streamState = streamState_;
    }
    if (streamState) {
        readerLoop(streamState);
    }
}

void HydraConnection::readerLoop(std::shared_ptr<StreamState> streamState) {
    hydra::ServerMessage msg;
    while (!stopRequested_.load() && connected_.load() && streamState &&
           streamState->stream && streamState->stream->Read(&msg)) {
        lastRecvTime_ = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(outputMutex_);

        if (msg.has_game_output()) {
            const auto& output = msg.game_output();
            streamLineBuffer_ += output.text();

            size_t nl = 0;
            while ((nl = streamLineBuffer_.find('\n')) != std::string::npos) {
                streamLineBuffer_.erase(0, nl + 1);
            }
            if (output.end_of_record()) {
                streamLineBuffer_.clear();
            }

            outputQueue_.push(OutputChunk{output.text(), true, output.end_of_record()});
        } else if (msg.has_gmcp()) {
            outputQueue_.push(OutputChunk{"[GMCP " + msg.gmcp().package() + "] "
                            + msg.gmcp().json(), false, false});
        } else if (msg.has_notice()) {
            outputQueue_.push(OutputChunk{"[Hydra] " + msg.notice().text(), false, false});
        } else if (msg.has_link_event()) {
            const auto& ev = msg.link_event();
            outputQueue_.push(OutputChunk{"[Hydra] Link " + std::to_string(ev.link_number())
                + " (" + ev.game_name() + "): "
                + hydra::LinkState_Name(ev.new_state()), false, false});
        } else if (msg.has_pong()) {
            continue;
        }

        signalOutput();
    }

    Status status;
    if (streamState && streamState->stream) {
        status = streamState->stream->Finish();
    }

    if (stopRequested_.load() || !connected_.load()) {
        return;
    }

    if (!status.ok()) {
        std::string detail = status.error_message().empty()
            ? "stream ended"
            : status.error_message();
        pushOutput("[Hydra] Stream interrupted: " + detail);
    }

    if (!reconnecting_.load()) {
        attemptReconnect();
    }
}

void HydraConnection::fetchScrollBack() {
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub || sessionId_.empty()) return;
    try {
        ClientContext ctx;
        ctx.AddMetadata("authorization", sessionId_);
        hydra::ScrollBackRequest req;
        req.set_session_id(sessionId_);
        req.set_max_lines(200);  // fetch last 200 lines on reconnect
        req.set_color_format(static_cast<hydra::ColorFormat>(colorFormat_));
        hydra::ScrollBackResponse resp;
        Status status = grpcState->stub->GetScrollBack(&ctx, req, &resp);
        if (status.ok() && resp.lines_size() > 0) {
            pushOutput("-- scroll-back (" + std::to_string(resp.lines_size()) + " lines) --");
            for (int i = 0; i < resp.lines_size(); i++) {
                std::lock_guard<std::mutex> lock(outputMutex_);
                outputQueue_.push(OutputChunk{resp.lines(i).text(), false});
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

    {
        std::lock_guard<std::mutex> lock(streamStateMutex_);
        streamState_.reset();
    }

    pushOutput("[Hydra] Stream lost, attempting reconnect...");

    for (int attempt = 1; attempt <= MAX_RECONNECT_ATTEMPTS; attempt++) {
        // Wait for reconnect delay or until disconnect() signals us.
        {
            std::unique_lock<std::mutex> lock(shutdownMutex_);
            if (shutdownCv_.wait_for(lock,
                    std::chrono::seconds(RECONNECT_DELAY_SECS),
                    [this] { return stopRequested_.load() || !reconnecting_.load(); })) {
                return;  // disconnect() woke us — stop reconnecting
            }
        }

        auto grpcState = currentGrpcState();
        if (stopRequested_.load() || !grpcState || !grpcState->stub || sessionId_.empty()) {
            break;
        }

        if (openStream(false)) {
            reconnecting_.store(false);
            pushOutput("[Hydra] Reconnected (attempt " + std::to_string(attempt) + ")");
            fetchScrollBack();

            std::shared_ptr<StreamState> streamState;
            {
                std::lock_guard<std::mutex> lock(streamStateMutex_);
                streamState = streamState_;
            }
            if (streamState) {
                readerLoop(streamState);
            }
            return;
        }

        pushOutput("[Hydra] Reconnect attempt " + std::to_string(attempt)
            + "/" + std::to_string(MAX_RECONNECT_ATTEMPTS) + " failed");
    }

    reconnecting_.store(false);
    connected_.store(false);
    pushOutput("[Hydra] Reconnect failed after "
        + std::to_string(MAX_RECONNECT_ATTEMPTS) + " attempts");
    signalOutput();
}

void HydraConnection::signalOutput() {
    PostQueuedCompletionStatus(iocp_, 0, IOCP_KEY_HYDRA, nullptr);
}

void HydraConnection::keepaliveLoop() {
    std::unique_lock<std::mutex> lock(shutdownMutex_);
    while (!stopRequested_.load() && connected_.load()) {
        if (shutdownCv_.wait_for(lock, std::chrono::seconds(KEEPALIVE_INTERVAL_SECS),
                [this] { return stopRequested_.load() || !connected_.load(); })) {
            break;
        }

        if (reconnecting_.load()) {
            continue;
        }

        auto now = std::chrono::steady_clock::now();
        auto recvIdle = std::chrono::duration_cast<std::chrono::seconds>(now - lastRecvTime_).count();
        auto sendIdle = std::chrono::duration_cast<std::chrono::seconds>(now - lastSendTime_).count();
        auto pingIdle = std::chrono::duration_cast<std::chrono::seconds>(now - lastPingTime_).count();

        if (recvIdle < KEEPALIVE_IDLE_SECS || sendIdle < KEEPALIVE_IDLE_SECS ||
            pingIdle < KEEPALIVE_INTERVAL_SECS) {
            continue;
        }

        hydra::ClientMessage msg;
        auto* ping = msg.mutable_ping();
        ping->set_client_timestamp(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());

        if (sendClientMessage(msg)) {
            lastPingTime_ = now;
            lastSendTime_ = now;
        } else {
            std::shared_ptr<StreamState> streamState;
            {
                std::lock_guard<std::mutex> streamLock(streamStateMutex_);
                streamState = streamState_;
            }
            if (streamState && streamState->context) {
                streamState->context->TryCancel();
            }
        }
    }
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
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub) return "[Hydra] Not connected.";
    try {
        ClientContext ctx;
        hydra::CreateAccountRequest req;
        req.set_username(username);
        req.set_password(password);
        hydra::CreateAccountResponse resp;
        Status status = grpcState->stub->CreateAccount(&ctx, req, &resp);
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
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub) return "[Hydra] Not connected.";
    try {
        ClientContext ctx;
        ctx.AddMetadata("authorization", sessionId_);
        hydra::ConnectRequest req;
        req.set_session_id(sessionId_);
        req.set_game_name(game_name);
        hydra::ConnectResponse resp;
        Status status = grpcState->stub->Connect(&ctx, req, &resp);
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
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub) return "[Hydra] Not connected.";
    try {
        ClientContext ctx;
        ctx.AddMetadata("authorization", sessionId_);
        hydra::SwitchRequest req;
        req.set_session_id(sessionId_);
        req.set_link_number(link_number);
        hydra::SwitchResponse resp;
        Status status = grpcState->stub->SwitchLink(&ctx, req, &resp);
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
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub) { result.push_back("[Hydra] Not connected."); return result; }
    try {
        ClientContext ctx;
        ctx.AddMetadata("authorization", sessionId_);
        hydra::SessionRequest req;
        req.set_session_id(sessionId_);
        hydra::LinkList resp;
        Status status = grpcState->stub->ListLinks(&ctx, req, &resp);
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
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub) return "[Hydra] Not connected.";
    try {
        ClientContext ctx;
        ctx.AddMetadata("authorization", sessionId_);
        hydra::DisconnectRequest req;
        req.set_session_id(sessionId_);
        req.set_link_number(link_number);
        hydra::DisconnectResponse resp;
        Status status = grpcState->stub->DisconnectLink(&ctx, req, &resp);
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
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub) { result.push_back("[Hydra] Not connected."); return result; }
    try {
        ClientContext ctx;
        ctx.AddMetadata("authorization", sessionId_);
        hydra::SessionRequest req;
        req.set_session_id(sessionId_);
        hydra::SessionInfo resp;
        Status status = grpcState->stub->GetSession(&ctx, req, &resp);
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
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub) return "[Hydra] Not connected.";
    try {
        ClientContext ctx;
        ctx.AddMetadata("authorization", sessionId_);
        hydra::SessionRequest req;
        req.set_session_id(sessionId_);
        hydra::Empty resp;
        Status status = grpcState->stub->DetachSession(&ctx, req, &resp);
        if (!status.ok())
            return "[Hydra] RPC error: " + status.error_message();
        connected_.store(false);
        return "[Hydra] Session detached. Reconnect to resume.";
    } catch (const std::exception& e) {
        return std::string("[Hydra] Error: ") + e.what();
    }
}

#endif // HYDRA_GRPC
