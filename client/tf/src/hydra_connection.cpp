#ifdef HYDRA_GRPC

#include "hydra_connection.h"

#include "hydra.grpc.pb.h"
#include <grpcpp/grpcpp.h>

#include <sys/eventfd.h>
#include <unistd.h>
#include <algorithm>
#include <sstream>

using grpc::Channel;
using grpc::ClientContext;
using grpc::ClientReaderWriter;
using grpc::Status;
using hydra::GameOutput;
using hydra::ServerMessage;

// Internal gRPC state — kept in .cpp to avoid grpc headers in .h
struct HydraConnection::GrpcState {
    std::shared_ptr<Channel> channel;
    std::unique_ptr<hydra::HydraService::Stub> stub;
};

struct HydraConnection::StreamState {
    std::shared_ptr<ClientContext> context;
    std::shared_ptr<ClientReaderWriter<hydra::ClientMessage, hydra::ServerMessage>> stream;
};

// ---- Helpers ----

void HydraConnection::pushOutput(const std::string& line) {
    std::lock_guard<std::mutex> lock(outputMutex_);
    outputQueue_.push(line);
    signalOutput();
}

void HydraConnection::signalOutput() {
    uint64_t val = 1;
    ::write(eventFd_, &val, sizeof(val));
}

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t");
    return s.substr(start, end - start + 1);
}

static std::string strip_trailing_cr(std::string s) {
    if (!s.empty() && s.back() == '\r') s.pop_back();
    return s;
}

// ---- Construction / Destruction ----

HydraConnection::HydraConnection(const std::string& world_name,
                                 const std::string& host,
                                 const std::string& port,
                                 const std::string& username,
                                 const std::string& password,
                                 const std::string& game_name,
                                 bool use_tls)
    : worldName_(world_name), host_(host), port_(port),
      username_(username), password_(password), gameName_(game_name),
      useTls_(use_tls) {
    eventFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    lastRecvTime_ = std::chrono::steady_clock::now();
    lastSendTime_ = lastRecvTime_;
    lastPingTime_ = lastRecvTime_;
}

HydraConnection::~HydraConnection() {
    disconnect();
    if (eventFd_ >= 0) {
        ::close(eventFd_);
        eventFd_ = -1;
    }
}

// ---- Connect / Disconnect ----

bool HydraConnection::connect() {
    if (connected_.load()) return true;

    stopRequested_.store(false);
    reconnecting_.store(false);

    auto grpcState = std::make_shared<GrpcState>();

    std::string target = host_ + ":" + port_;
    if (useTls_) {
        grpcState->channel = grpc::CreateChannel(target,
            grpc::SslCredentials(grpc::SslCredentialsOptions()));
    } else {
        grpcState->channel = grpc::CreateChannel(target,
            grpc::InsecureChannelCredentials());
    }
    grpcState->stub = hydra::HydraService::NewStub(grpcState->channel);

    // Authenticate
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
    }

    {
        std::lock_guard<std::mutex> lock(grpcMutex_);
        grpc_ = grpcState;
    }

    // Open bidi stream
    if (!openStream()) {
        std::lock_guard<std::mutex> lock(grpcMutex_);
        if (grpc_ == grpcState) {
            grpc_.reset();
        }
        return false;
    }

    // Connect to initial game if specified once the stream is live.
    if (!gameName_.empty()) {
        cmdConnect(gameName_);
    }

    pushOutput("[Hydra] Session established (" + sessionId_.substr(0, 8) + "...)");
    return true;
}

std::shared_ptr<HydraConnection::GrpcState> HydraConnection::currentGrpcState() const {
    std::lock_guard<std::mutex> lock(grpcMutex_);
    return grpc_;
}

std::shared_ptr<HydraConnection::StreamState> HydraConnection::currentStreamState() const {
    std::lock_guard<std::mutex> lock(streamStateMutex_);
    return streamState_;
}

bool HydraConnection::openStream(bool startReaderThread) {
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub) return false;

    auto streamState = std::make_shared<StreamState>();
    streamState->context = std::make_shared<ClientContext>();
    streamState->context->AddMetadata("authorization", sessionId_);

    auto stream = grpcState->stub->GameSession(streamState->context.get());
    if (!stream) {
        pushOutput("[Hydra] Failed to open GameSession stream");
        return false;
    }
    streamState->stream = std::shared_ptr<ClientReaderWriter<hydra::ClientMessage, hydra::ServerMessage>>(stream.release());

    {
        std::lock_guard<std::mutex> lock(streamStateMutex_);
        streamState_ = streamState;
    }

    connected_.store(true);

    // Send initial preferences only after the stream is marked live.
    sendPreferences();

    if (startReaderThread) {
        readerThread_ = std::thread(
            static_cast<void (HydraConnection::*)(std::shared_ptr<StreamState>)>(&HydraConnection::readerLoop),
            this,
            streamState);
    }
    return true;
}

void HydraConnection::disconnect() {
    stopRequested_.store(true);
    connected_.store(false);
    reconnecting_.store(false);

    auto streamState = currentStreamState();

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

// ---- I/O ----

bool HydraConnection::send_line(const std::string& line) {
    // Intercept Hydra commands (prefixed with /h)
    if (line.size() > 1 && line[0] == '/') {
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);

        if (lower.substr(0, 9) == "/hconnect") {
            cmdConnect(trim(line.substr(9)));
            return true;
        } else if (lower.substr(0, 8) == "/hswitch") {
            cmdSwitch(trim(line.substr(8)));
            return true;
        } else if (lower.substr(0, 13) == "/hdisconnect") {
            cmdDisconnect(trim(line.substr(13)));
            return true;
        } else if (lower == "/hlinks") {
            cmdLinks();
            return true;
        } else if (lower == "/hgames") {
            cmdGames();
            return true;
        } else if (lower.substr(0, 8) == "/hscroll") {
            cmdScroll(trim(line.substr(8)));
            return true;
        } else if (lower.substr(0, 9) == "/haddcred") {
            cmdAddCred(trim(line.substr(9)));
            return true;
        } else if (lower.substr(0, 9) == "/hdelcred") {
            cmdDelCred(trim(line.substr(9)));
            return true;
        } else if (lower == "/hcreds") {
            cmdCreds();
            return true;
        } else if (lower.substr(0, 7) == "/hstart") {
            cmdStart(trim(line.substr(7)));
            return true;
        } else if (lower.substr(0, 6) == "/hstop") {
            cmdStop(trim(line.substr(6)));
            return true;
        } else if (lower.substr(0, 9) == "/hrestart") {
            cmdRestart(trim(line.substr(9)));
            return true;
        } else if (lower.substr(0, 8) == "/hstatus") {
            cmdStatus(trim(line.substr(8)));
            return true;
        } else if (lower == "/hdetach") {
            auto grpcState = currentGrpcState();
            if (!grpcState || !grpcState->stub || sessionId_.empty()) return true;
            ClientContext ctx;
            ctx.AddMetadata("authorization", sessionId_);
            hydra::SessionRequest req;
            req.set_session_id(sessionId_);
            hydra::Empty resp;
            grpcState->stub->DetachSession(&ctx, req, &resp);
            pushOutput("[Hydra] Session detached.");
            return true;
        } else if (lower == "/hhelp") {
            pushOutput("[Hydra] Commands:");
            pushOutput("  /hconnect <game>       - connect to a game");
            pushOutput("  /hswitch <link#>       - switch active link");
            pushOutput("  /hdisconnect <link#>   - disconnect a link");
            pushOutput("  /hlinks                - list active links");
            pushOutput("  /hgames                - list available games");
            pushOutput("  /hscroll [n]           - fetch server scroll-back");
            pushOutput("  /haddcred <g> <c> <v> <n> <s> - add credential");
            pushOutput("  /hdelcred <game> [char]        - delete credential");
            pushOutput("  /hcreds                - list stored credentials");
            pushOutput("  /hstart <game>         - start a local game");
            pushOutput("  /hstop <game>          - stop a local game");
            pushOutput("  /hrestart <game>       - restart a local game");
            pushOutput("  /hstatus [game]        - show process status");
            pushOutput("  /hdetach               - detach Hydra session");
            pushOutput("  /hhelp                 - this help");
            return true;
        }
    }

    // Regular input — forward to active game link via bidi stream
    hydra::ClientMessage msg;
    msg.set_input_line(line);

    if (!sendClientMessage(msg)) {
        return false;
    }

    lastSendTime_ = std::chrono::steady_clock::now();
    return true;
}

std::vector<std::string> HydraConnection::read_lines() {
    std::vector<std::string> lines;

    // Drain the eventfd
    uint64_t val;
    ::read(eventFd_, &val, sizeof(val));

    // Keepalive: send ping if interval elapsed
    if (connected_.load()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            now - lastPingTime_).count();
        if (elapsed >= PING_INTERVAL_SECS) {
            sendPing();
            lastPingTime_ = now;
        }
    }

    std::lock_guard<std::mutex> lock(outputMutex_);
    while (!outputQueue_.empty()) {
        lines.push_back(std::move(outputQueue_.front()));
        outputQueue_.pop();
    }

    return lines;
}

int HydraConnection::fd() const {
    return eventFd_;
}

void HydraConnection::send_naws(uint16_t width, uint16_t height) {
    termWidth_ = width;
    termHeight_ = height;
    sendPreferences();
}

bool HydraConnection::sendClientMessage(const hydra::ClientMessage& msg) {
    if (!connected_.load()) return false;

    auto streamState = currentStreamState();
    if (!streamState || !streamState->stream) return false;

    std::lock_guard<std::mutex> writeLock(writeMutex_);
    return streamState->stream->Write(msg);
}

void HydraConnection::sendPreferences() {
    hydra::ClientMessage msg;
    auto* prefs = msg.mutable_preferences();
    prefs->set_color_format(static_cast<hydra::ColorFormat>(currentColorFormat_));
    prefs->set_terminal_width(termWidth_);
    prefs->set_terminal_height(termHeight_);
    prefs->set_terminal_type("TitanFugue");
    sendClientMessage(msg);
}

std::string HydraConnection::check_prompt(std::chrono::milliseconds) {
    std::lock_guard<std::mutex> lock(outputMutex_);
    if (currentPrompt_.empty() || currentPrompt_ == lastPrompt_) return "";
    lastPrompt_ = currentPrompt_;
    return currentPrompt_;
}

std::string HydraConnection::current_prompt() const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    return currentPrompt_;
}

bool HydraConnection::has_partial_line() const {
    std::lock_guard<std::mutex> lock(outputMutex_);
    return !lineBuf_.empty();
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

int HydraConnection::sidle_secs() const {
    auto now = std::chrono::steady_clock::now();
    return static_cast<int>(
        std::chrono::duration_cast<std::chrono::seconds>(now - lastSendTime_).count());
}

// ---- Hydra command handlers ----

void HydraConnection::cmdConnect(const std::string& gameName) {
    if (gameName.empty()) {
        pushOutput("[Hydra] Usage: /hconnect <game>");
        return;
    }
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub || sessionId_.empty()) {
        pushOutput("[Hydra] Not authenticated");
        return;
    }

    ClientContext ctx;
    ctx.AddMetadata("authorization", sessionId_);
    hydra::ConnectRequest req;
    req.set_session_id(sessionId_);
    req.set_game_name(gameName);
    hydra::ConnectResponse resp;

    Status status = grpcState->stub->Connect(&ctx, req, &resp);
    if (status.ok() && resp.success()) {
        pushOutput("[Hydra] Connected to " + gameName
            + " (link " + std::to_string(resp.link_number()) + ")");
    } else {
        pushOutput("[Hydra] Connect failed: "
            + (resp.error().empty() ? status.error_message() : resp.error()));
    }
}

void HydraConnection::cmdSwitch(const std::string& args) {
    if (args.empty()) {
        pushOutput("[Hydra] Usage: /hswitch <link#>");
        return;
    }
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub || sessionId_.empty()) return;

    int linkNum = 0;
    try { linkNum = std::stoi(args); } catch (...) {
        pushOutput("[Hydra] Invalid link number");
        return;
    }

    ClientContext ctx;
    ctx.AddMetadata("authorization", sessionId_);
    hydra::SwitchRequest req;
    req.set_session_id(sessionId_);
    req.set_link_number(linkNum);
    hydra::SwitchResponse resp;

    Status status = grpcState->stub->SwitchLink(&ctx, req, &resp);
    if (status.ok() && resp.success()) {
        pushOutput("[Hydra] Switched to link " + std::to_string(linkNum));
    } else {
        pushOutput("[Hydra] Switch failed: "
            + (resp.error().empty() ? status.error_message() : resp.error()));
    }
}

void HydraConnection::cmdDisconnect(const std::string& args) {
    if (args.empty()) {
        pushOutput("[Hydra] Usage: /hdisconnect <link#>");
        return;
    }
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub || sessionId_.empty()) return;

    int linkNum = 0;
    try { linkNum = std::stoi(args); } catch (...) {
        pushOutput("[Hydra] Invalid link number");
        return;
    }

    ClientContext ctx;
    ctx.AddMetadata("authorization", sessionId_);
    hydra::DisconnectRequest req;
    req.set_session_id(sessionId_);
    req.set_link_number(linkNum);
    hydra::DisconnectResponse resp;

    Status status = grpcState->stub->DisconnectLink(&ctx, req, &resp);
    if (status.ok() && resp.success()) {
        pushOutput("[Hydra] Link " + std::to_string(linkNum) + " disconnected");
    } else {
        pushOutput("[Hydra] Disconnect failed: "
            + (resp.error().empty() ? status.error_message() : resp.error()));
    }
}

void HydraConnection::cmdLinks() {
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub || sessionId_.empty()) return;

    ClientContext ctx;
    ctx.AddMetadata("authorization", sessionId_);
    hydra::SessionRequest req;
    req.set_session_id(sessionId_);
    hydra::LinkList resp;

    Status status = grpcState->stub->ListLinks(&ctx, req, &resp);
    if (!status.ok()) {
        pushOutput("[Hydra] ListLinks failed: " + status.error_message());
        return;
    }

    if (resp.links_size() == 0) {
        pushOutput("[Hydra] No active links.");
        return;
    }

    for (int i = 0; i < resp.links_size(); i++) {
        const auto& li = resp.links(i);
        std::string marker = li.active() ? "*" : " ";
        std::string line = "  [" + marker + std::to_string(li.number()) + "] "
            + li.game_name();
        if (!li.character().empty()) line += " (" + li.character() + ")";
        line += " — " + hydra::LinkState_Name(li.state());
        pushOutput(line);
    }
}

void HydraConnection::cmdGames() {
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub) return;

    ClientContext ctx;
    if (!sessionId_.empty()) ctx.AddMetadata("authorization", sessionId_);
    hydra::Empty req;
    hydra::GameList resp;

    Status status = grpcState->stub->ListGames(&ctx, req, &resp);
    if (!status.ok()) {
        pushOutput("[Hydra] ListGames failed: " + status.error_message());
        return;
    }

    pushOutput("[Hydra] Available games:");
    for (int i = 0; i < resp.games_size(); i++) {
        const auto& g = resp.games(i);
        std::string line = "  " + g.name() + " (" + g.host() + ":" + std::to_string(g.port()) + ")";
        if (g.type() == hydra::GAME_LOCAL) line += " [local]";
        pushOutput(line);
    }
}

void HydraConnection::cmdScroll(const std::string& args) {
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub || sessionId_.empty()) return;

    int maxLines = 50;
    if (!args.empty()) {
        try { maxLines = std::stoi(args); } catch (...) {}
    }

    ClientContext ctx;
    ctx.AddMetadata("authorization", sessionId_);
    hydra::ScrollBackRequest req;
    req.set_session_id(sessionId_);
    req.set_max_lines(maxLines);
    hydra::ScrollBackResponse resp;

    Status status = grpcState->stub->GetScrollBack(&ctx, req, &resp);
    if (!status.ok()) {
        pushOutput("[Hydra] GetScrollBack failed: " + status.error_message());
        return;
    }

    pushOutput("-- Server scroll-back (" + std::to_string(resp.lines_size()) + " lines) --");
    for (int i = 0; i < resp.lines_size(); i++) {
        pushOutput(resp.lines(i).text());
    }
    pushOutput("-- End server scroll-back --");
}

// ---- Keepalive ----

void HydraConnection::sendPing() {
    hydra::ClientMessage msg;
    auto* ping = msg.mutable_ping();
    auto now = std::chrono::system_clock::now();
    ping->set_client_timestamp(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count());

    sendClientMessage(msg);
}

// ---- Credential management ----

void HydraConnection::cmdAddCred(const std::string& args) {
    // Parse: <game> <character> <verb> <name> <secret>
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub || sessionId_.empty()) return;

    std::istringstream ss(args);
    std::string game, character, verb, name, secret;
    ss >> game >> character >> verb >> name;
    std::getline(ss, secret);
    secret = trim(secret);

    if (game.empty() || character.empty() || verb.empty() || name.empty() || secret.empty()) {
        pushOutput("[Hydra] Usage: /haddcred <game> <character> <verb> <name> <secret>");
        pushOutput("[Hydra] Example: /haddcred LocalMUX player1 connect player1 mypassword");
        return;
    }

    ClientContext ctx;
    ctx.AddMetadata("authorization", sessionId_);
    hydra::AddCredentialRequest req;
    req.set_session_id(sessionId_);
    req.set_game(game);
    req.set_character(character);
    req.set_verb(verb);
    req.set_name(name);
    req.set_secret(secret);
    hydra::AddCredentialResponse resp;

    Status status = grpcState->stub->AddCredential(&ctx, req, &resp);
    if (status.ok() && resp.success()) {
        pushOutput("[Hydra] Credential stored for " + game + "/" + character);
    } else {
        pushOutput("[Hydra] AddCredential failed: "
            + (resp.error().empty() ? status.error_message() : resp.error()));
    }
}

void HydraConnection::cmdDelCred(const std::string& args) {
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub || sessionId_.empty()) return;

    std::istringstream ss(args);
    std::string game, character;
    ss >> game >> character;

    if (game.empty()) {
        pushOutput("[Hydra] Usage: /hdelcred <game> [character]");
        return;
    }

    ClientContext ctx;
    ctx.AddMetadata("authorization", sessionId_);
    hydra::DeleteCredentialRequest req;
    req.set_session_id(sessionId_);
    req.set_game(game);
    if (!character.empty()) req.set_character(character);
    hydra::DeleteCredentialResponse resp;

    Status status = grpcState->stub->DeleteCredential(&ctx, req, &resp);
    if (status.ok() && resp.success()) {
        pushOutput("[Hydra] Credential(s) deleted.");
    } else {
        pushOutput("[Hydra] DeleteCredential failed: "
            + (resp.error().empty() ? status.error_message() : resp.error()));
    }
}

void HydraConnection::cmdCreds() {
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub || sessionId_.empty()) return;

    ClientContext ctx;
    ctx.AddMetadata("authorization", sessionId_);
    hydra::ListCredentialsRequest req;
    req.set_session_id(sessionId_);
    hydra::ListCredentialsResponse resp;

    Status status = grpcState->stub->ListCredentials(&ctx, req, &resp);
    if (!status.ok()) {
        pushOutput("[Hydra] ListCredentials failed: " + status.error_message());
        return;
    }

    if (resp.credentials_size() == 0) {
        pushOutput("[Hydra] No stored credentials.");
        return;
    }

    pushOutput("[Hydra] Stored credentials:");
    for (int i = 0; i < resp.credentials_size(); i++) {
        const auto& c = resp.credentials(i);
        std::string line = "  " + c.game() + "/" + c.character()
            + "  verb=" + c.verb() + " name=" + c.name();
        if (c.auto_login()) line += " [auto]";
        pushOutput(line);
    }
}

// ---- Process management ----

void HydraConnection::cmdStart(const std::string& args) {
    if (args.empty()) { pushOutput("[Hydra] Usage: /hstart <game>"); return; }
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub) return;

    ClientContext ctx;
    if (!sessionId_.empty()) ctx.AddMetadata("authorization", sessionId_);
    hydra::GameRequest req;
    req.set_game_name(args);
    hydra::GameResponse resp;

    Status status = grpcState->stub->StartGame(&ctx, req, &resp);
    if (status.ok() && resp.success()) {
        pushOutput("[Hydra] Started " + args + " (pid " + std::to_string(resp.pid()) + ")");
    } else {
        pushOutput("[Hydra] Start failed: "
            + (resp.error().empty() ? status.error_message() : resp.error()));
    }
}

void HydraConnection::cmdStop(const std::string& args) {
    if (args.empty()) { pushOutput("[Hydra] Usage: /hstop <game>"); return; }
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub) return;

    ClientContext ctx;
    if (!sessionId_.empty()) ctx.AddMetadata("authorization", sessionId_);
    hydra::GameRequest req;
    req.set_game_name(args);
    hydra::GameResponse resp;

    Status status = grpcState->stub->StopGame(&ctx, req, &resp);
    if (status.ok() && resp.success()) {
        pushOutput("[Hydra] Stopping " + args);
    } else {
        pushOutput("[Hydra] Stop failed: "
            + (resp.error().empty() ? status.error_message() : resp.error()));
    }
}

void HydraConnection::cmdRestart(const std::string& args) {
    if (args.empty()) { pushOutput("[Hydra] Usage: /hrestart <game>"); return; }
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub) return;

    ClientContext ctx;
    if (!sessionId_.empty()) ctx.AddMetadata("authorization", sessionId_);
    hydra::GameRequest req;
    req.set_game_name(args);
    hydra::GameResponse resp;

    Status status = grpcState->stub->RestartGame(&ctx, req, &resp);
    if (status.ok() && resp.success()) {
        pushOutput("[Hydra] Restarted " + args + " (pid " + std::to_string(resp.pid()) + ")");
    } else {
        pushOutput("[Hydra] Restart failed: "
            + (resp.error().empty() ? status.error_message() : resp.error()));
    }
}

void HydraConnection::cmdStatus(const std::string& args) {
    auto grpcState = currentGrpcState();
    if (!grpcState || !grpcState->stub) return;

    ClientContext ctx;
    if (!sessionId_.empty()) ctx.AddMetadata("authorization", sessionId_);
    hydra::GameStatusRequest req;
    if (!args.empty()) req.set_game_name(args);
    hydra::GameStatusResponse resp;

    Status status = grpcState->stub->GetGameStatus(&ctx, req, &resp);
    if (!status.ok()) {
        pushOutput("[Hydra] GetGameStatus failed: " + status.error_message());
        return;
    }

    if (resp.processes_size() == 0) {
        pushOutput("[Hydra] No local games configured.");
        return;
    }

    pushOutput("[Hydra] Game processes:");
    for (int i = 0; i < resp.processes_size(); i++) {
        const auto& p = resp.processes(i);
        std::string line = "  " + p.game_name() + ": ";
        if (p.running()) {
            line += "running (pid " + std::to_string(p.pid()) + ")";
        } else {
            line += "stopped";
        }
        pushOutput(line);
    }
}

// ---- Reader thread with reconnect ----

void HydraConnection::processGameOutput(const GameOutput& out) {
    if (!out.text().empty()) {
        lineBuf_.append(out.text());

        size_t start = 0;
        for (size_t i = 0; i < lineBuf_.size(); i++) {
            if (lineBuf_[i] != '\n') continue;
            std::string line = strip_trailing_cr(lineBuf_.substr(start, i - start));
            outputQueue_.push(std::move(line));
            start = i + 1;
        }
        if (start > 0) {
            lineBuf_.erase(0, start);
            currentPrompt_.clear();
            lastPrompt_.clear();
        }
    }

    if (out.end_of_record()) {
        currentPrompt_ = strip_trailing_cr(lineBuf_);
    }
}

void HydraConnection::processServerMessage(const ServerMessage& msg) {
    if (msg.has_game_output()) {
        processGameOutput(msg.game_output());
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
    }
}

void HydraConnection::readerLoop() {
    readerLoop(currentStreamState());
}

void HydraConnection::readerLoop(std::shared_ptr<StreamState> streamState) {
    if (!streamState || !streamState->stream) {
        return;
    }

    ServerMessage msg;
    while (connected_.load() && !stopRequested_.load() &&
           streamState->stream->Read(&msg)) {
        lastRecvTime_ = std::chrono::steady_clock::now();

        std::lock_guard<std::mutex> lock(outputMutex_);
        if (msg.has_pong()) continue;
        processServerMessage(msg);
        signalOutput();
    }

    // Stream ended — attempt reconnect if we didn't disconnect intentionally
    if (connected_.load() && !stopRequested_.load() && !reconnecting_.load()) {
        attemptReconnect();
    }
}

void HydraConnection::attemptReconnect() {
    if (stopRequested_.load() || reconnecting_.exchange(true)) return;

    pushOutput("[Hydra] Stream lost, attempting reconnect...");
    connected_.store(false);
    {
        std::lock_guard<std::mutex> lock(streamStateMutex_);
        streamState_.reset();
    }

    for (int attempt = 1; attempt <= MAX_RECONNECT_ATTEMPTS; attempt++) {
        if (stopRequested_.load()) {
            break;
        }
        std::this_thread::sleep_for(
            std::chrono::seconds(RECONNECT_DELAY_SECS));

        auto grpcState = currentGrpcState();
        if (!grpcState || !grpcState->stub || sessionId_.empty()) break;

        if (openStream(false)) {
            auto streamState = currentStreamState();
            pushOutput("[Hydra] Reconnected (attempt " + std::to_string(attempt) + ")");
            readerLoop(streamState);

            // Stream broke again — retry
            if (connected_.load() && !stopRequested_.load()) {
                pushOutput("[Hydra] Stream lost again, retrying...");
                connected_.store(false);
                std::lock_guard<std::mutex> lock(streamStateMutex_);
                streamState_.reset();
                reconnecting_.store(true);
                continue;
            }
            reconnecting_.store(false);
            return;  // intentional disconnect
        }

        pushOutput("[Hydra] Reconnect attempt " + std::to_string(attempt)
            + "/" + std::to_string(MAX_RECONNECT_ATTEMPTS) + " failed");
    }

    reconnecting_.store(false);
    if (!stopRequested_.load()) {
        pushOutput("[Hydra] Reconnect failed after " + std::to_string(MAX_RECONNECT_ATTEMPTS) + " attempts");
    }
}

#endif // HYDRA_GRPC
