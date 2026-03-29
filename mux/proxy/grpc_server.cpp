#ifdef GRPC_ENABLED

#include "grpc_server.h"
#include "work_queue.h"
#include "session_manager.h"
#include "account_manager.h"
#include "process_manager.h"
#include "config.h"
#include "hydra_log.h"
#include "telnet_utils.h"
#include "utf8_utils.h"

#include "hydra.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>
#include <chrono>
#include <fstream>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::ServerReaderWriter;
using grpc::Status;
using grpc::StatusCode;

static std::string issueTypeName(Utf8IssueType type) {
    switch (type) {
    case Utf8IssueType::None:
        return "none";
    case Utf8IssueType::InvalidSequence:
        return "invalid";
    case Utf8IssueType::TruncatedSequence:
        return "truncated";
    }
    return "unknown";
}

static std::string sanitizeProtoTextForLog(const std::string& text,
                                           const char* path,
                                           const std::string& source,
                                           int linkNumber) {
    Utf8Issue issue = findFirstUtf8Issue(text);
    if (!issue.hasIssue()) return text;

    LOG_WARN("Proto UTF-8 issue on %s source=%s link=%d type=%s offset=%zu bytes=%zu hex=[%s]",
             path, source.c_str(), linkNumber, issueTypeName(issue.type).c_str(),
             issue.offset, issue.bytes, hexWindow(text, issue.offset).c_str());
    return sanitizeUtf8(text);
}

// ---- Auth helper: extract session_id from metadata or message field ----

static std::string getSessionId(ServerContext* ctx, const std::string& msgField) {
    // Prefer metadata "authorization" header
    auto md = ctx->client_metadata();
    auto it = md.find("authorization");
    if (it != md.end()) {
        return std::string(it->second.data(), it->second.size());
    }
    // Fall back to per-message session_id field
    return msgField;
}

// ---- Helper: map C++ LinkState to proto LinkState enum ----

static hydra::LinkState toProtoLinkState(LinkState s) {
    switch (s) {
    case LinkState::Connecting:     return hydra::LINK_CONNECTING;
    case LinkState::TlsHandshaking: return hydra::LINK_TLS;
    case LinkState::Negotiating:    return hydra::LINK_NEGOTIATING;
    case LinkState::AutoLoggingIn:  return hydra::LINK_LOGGING_IN;
    case LinkState::Active:         return hydra::LINK_ACTIVE;
    case LinkState::Reconnecting:   return hydra::LINK_RECONNECTING;
    case LinkState::Suspended:      return hydra::LINK_SUSPENDED;
    case LinkState::Dead:           return hydra::LINK_DEAD;
    }
    return hydra::LINK_UNKNOWN;
}

static hydra::SessionState toProtoSessionState(SessionState s) {
    switch (s) {
    case SessionState::Login:    return hydra::SESSION_LOGIN;
    case SessionState::Active:   return hydra::SESSION_ACTIVE;
    case SessionState::Detached: return hydra::SESSION_DETACHED;
    }
    return hydra::SESSION_UNKNOWN;
}

// ---- Helper: populate LinkInfo proto ----

static void fillLinkInfo(hydra::LinkInfo* li, const BackDoorLink& link,
                         size_t idx, size_t activeIdx) {
    li->set_number(static_cast<int>(idx) + 1);
    li->set_game_name(link.gameName);
    li->set_character(link.character);
    li->set_active(idx == activeIdx);
    li->set_gmcp_enabled(link.gmcpEnabled);
    li->set_retry_count(link.retryCount);
    li->set_next_retry(static_cast<int64_t>(link.nextRetry));
    li->set_state(toProtoLinkState(link.state));
}

class HydraServiceImpl final : public hydra::HydraService::Service {
public:
    HydraServiceImpl(SessionManager& sessionMgr, AccountManager& accounts,
                     const HydraConfig& config, ProcessManager& procMgr,
                     WorkQueue& workQueue)
        : sessionMgr_(sessionMgr), accounts_(accounts), config_(config),
          procMgr_(procMgr), workQueue_(workQueue) {}

    // ---- Authentication ----

    Status Authenticate(ServerContext* ctx, const hydra::AuthRequest* req,
                        hydra::AuthResponse* resp) override {
        auto future = workQueue_.enqueue<std::string>(
            [user = req->username(), pw = req->password()]
            (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                return sm.authenticateAndGetSession(user, pw);
            });
        std::string pid = future.get();
        if (pid.empty()) {
            resp->set_success(false);
            resp->set_error("authentication failed");
        } else {
            resp->set_success(true);
            resp->set_session_id(pid);
        }
        return Status::OK;
    }

    Status CreateAccount(ServerContext* ctx, const hydra::CreateAccountRequest* req,
                         hydra::CreateAccountResponse* resp) override {
        // Extract client IP from gRPC peer (format: "ipv4:1.2.3.4:port")
        std::string peer = ctx->peer();
        std::string clientIp;
        {
            auto c1 = peer.find(':');
            if (c1 != std::string::npos) {
                auto c2 = peer.rfind(':');
                if (c2 > c1) clientIp = peer.substr(c1 + 1, c2 - c1 - 1);
                else clientIp = peer.substr(c1 + 1);
            }
        }
        auto future = workQueue_.enqueue<std::pair<std::string, std::string>>(
            [user = req->username(), pw = req->password(), clientIp]
            (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&)
                -> std::pair<std::string, std::string> {
                std::string errorMsg;
                std::string pid = sm.createAccountAndGetSession(user, pw, clientIp, errorMsg);
                return {pid, errorMsg};
            });
        auto [pid, err] = future.get();
        if (pid.empty()) {
            resp->set_success(false);
            resp->set_error(err.empty() ? "account creation failed" : err);
        } else {
            resp->set_success(true);
            resp->set_session_id(pid);
        }
        return Status::OK;
    }

    // ---- Session lifecycle ----

    Status GetSession(ServerContext* ctx, const hydra::SessionRequest* req,
                      hydra::SessionInfo* resp) override {
        std::string sid = getSessionId(ctx, req->session_id());
        auto future = workQueue_.enqueue<bool>(
            [sid, resp]
            (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) return false;
                resp->set_session_id(s->persistId);
                resp->set_username(s->username);
                resp->set_active_link(s->links.empty() ? 0
                    : static_cast<int>(s->activeLink) + 1);
                resp->set_scrollback_lines(static_cast<int>(s->scrollback.count()));
                resp->set_state(toProtoSessionState(s->state));
                resp->set_created(static_cast<int64_t>(s->created));
                resp->set_last_activity(static_cast<int64_t>(s->lastActivity));
                for (size_t i = 0; i < s->links.size(); i++) {
                    fillLinkInfo(resp->add_links(), s->links[i], i, s->activeLink);
                }
                return true;
            });
        if (!future.get()) return Status(StatusCode::NOT_FOUND, "session not found");
        return Status::OK;
    }

    Status DetachSession(ServerContext* ctx, const hydra::SessionRequest* req,
                         hydra::Empty* resp) override {
        std::string sid = getSessionId(ctx, req->session_id());
        auto future = workQueue_.enqueue<bool>(
            [sid](SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) return false;
                s->state = SessionState::Detached;
                return true;
            });
        if (!future.get()) return Status(StatusCode::NOT_FOUND, "session not found");
        return Status::OK;
    }

    Status DestroySession(ServerContext* ctx, const hydra::SessionRequest* req,
                          hydra::Empty* resp) override {
        return Status(StatusCode::UNIMPLEMENTED, "use /quit via telnet for now");
    }

    Status Ping(ServerContext* ctx, const hydra::SessionRequest* req,
                hydra::Empty* resp) override {
        std::string sid = getSessionId(ctx, req->session_id());
        auto future = workQueue_.enqueue<bool>(
            [sid](SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) return false;
                s->lastActivity = time(nullptr);
                return true;
            });
        if (!future.get()) return Status(StatusCode::NOT_FOUND, "session not found");
        return Status::OK;
    }

    // ---- Game catalog ----

    Status ListGames(ServerContext* ctx, const hydra::Empty* req,
                     hydra::GameList* resp) override {
        for (const auto& game : config_.games) {
            auto* gi = resp->add_games();
            gi->set_name(game.name);
            gi->set_host(game.host);
            gi->set_port(game.port);
            gi->set_type(game.type == GameType::Local ? hydra::GAME_LOCAL : hydra::GAME_REMOTE);
            gi->set_autostart(game.autostart);
        }
        return Status::OK;
    }

    // ---- Link management ----

    Status Connect(ServerContext* ctx, const hydra::ConnectRequest* req,
                   hydra::ConnectResponse* resp) override {
        std::string sid = getSessionId(ctx, req->session_id());
        auto future = workQueue_.enqueue<bool>(
            [sid, game = req->game_name(), resp]
            (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) { resp->set_error("session not found"); return false; }
                size_t before = s->links.size();
                sm.connectToGame(*s, game);
                if (s->links.size() > before) {
                    resp->set_success(true);
                    resp->set_link_number(static_cast<int>(s->links.size()));
                    return true;
                }
                resp->set_error("connect failed");
                return false;
            });
        future.get();
        return Status::OK;
    }

    Status SwitchLink(ServerContext* ctx, const hydra::SwitchRequest* req,
                      hydra::SwitchResponse* resp) override {
        std::string sid = getSessionId(ctx, req->session_id());
        auto future = workQueue_.enqueue<bool>(
            [sid, num = req->link_number(), resp]
            (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) { resp->set_error("session not found"); return false; }
                size_t idx = static_cast<size_t>(num - 1);
                if (idx >= s->links.size()) {
                    resp->set_error("invalid link number");
                    return false;
                }
                s->activeLink = idx;
                resp->set_success(true);
                return true;
            });
        future.get();
        return Status::OK;
    }

    Status DisconnectLink(ServerContext* ctx, const hydra::DisconnectRequest* req,
                          hydra::DisconnectResponse* resp) override {
        std::string sid = getSessionId(ctx, req->session_id());
        auto future = workQueue_.enqueue<bool>(
            [sid, num = req->link_number(), resp]
            (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) { resp->set_error("session not found"); return false; }
                size_t idx = static_cast<size_t>(num - 1);
                if (idx >= s->links.size()) {
                    resp->set_error("invalid link number");
                    return false;
                }
                sm.closeLink(*s, idx);
                resp->set_success(true);
                return true;
            });
        future.get();
        return Status::OK;
    }

    Status ListLinks(ServerContext* ctx, const hydra::SessionRequest* req,
                     hydra::LinkList* resp) override {
        std::string sid = getSessionId(ctx, req->session_id());
        auto future = workQueue_.enqueue<bool>(
            [sid, resp]
            (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) return false;
                for (size_t i = 0; i < s->links.size(); i++) {
                    fillLinkInfo(resp->add_links(), s->links[i], i, s->activeLink);
                }
                return true;
            });
        if (!future.get()) return Status(StatusCode::NOT_FOUND, "session not found");
        return Status::OK;
    }

    // ---- Bidirectional I/O (primary channel) ----

    Status GameSession(ServerContext* ctx,
                       ServerReaderWriter<hydra::ServerMessage, hydra::ClientMessage>* stream) override {
        // Read initial metadata for session_id and color_format
        auto md = ctx->client_metadata();
        std::string sid;
        auto it = md.find("authorization");
        if (it != md.end()) {
            sid = std::string(it->second.data(), it->second.size());
        }
        // Also check "session-id" metadata as fallback
        if (sid.empty()) {
            it = md.find("session-id");
            if (it != md.end()) {
                sid = std::string(it->second.data(), it->second.size());
            }
        }
        if (sid.empty()) {
            return Status(StatusCode::UNAUTHENTICATED, "session_id required in metadata");
        }

        // Get output queue
        std::shared_ptr<HydraSession::OutputQueue> oq;
        auto future = workQueue_.enqueue<std::shared_ptr<HydraSession::OutputQueue>>(
            [&sid](SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&)
                -> std::shared_ptr<HydraSession::OutputQueue> {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) return nullptr;
                s->state = SessionState::Active;
                s->lastActivity = time(nullptr);
                return s->outputQueue;
            });
        oq = future.get();
        if (!oq) return Status(StatusCode::NOT_FOUND, "session not found");

        // Register as subscriber (wants both output and GMCP)
        int subId;
        std::shared_ptr<HydraSession::SubscriberQueue> sq;
        {
            std::lock_guard<std::mutex> lock(oq->mutex);
            auto [id, q] = oq->addSubscriber(true, true);
            subId = id;
            sq = q;
        }

        // Replay cached GMCP state into the new subscriber's queue
        {
            auto replayFuture = workQueue_.enqueue<bool>(
                [sid, sq](SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) -> bool {
                    HydraSession* s = sm.findByPersistId(sid);
                    if (!s) return false;
                    sm.replayGmcpCache(*s, sq);
                    return true;
                });
            replayFuture.get();
        }

        // Reader thread: process client input
        std::atomic<bool> done{false};
        std::thread reader([&]() {
            hydra::ClientMessage msg;
            while (stream->Read(&msg)) {
                if (msg.has_input_line()) {
                    workQueue_.enqueue<void>(
                        [sid, line = msg.input_line()]
                        (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                            HydraSession* s = sm.findByPersistId(sid);
                            if (!s) return;
                            BackDoorLink* active = s->getActiveLink();
                            if (active && active->state == LinkState::Active) {
                                std::string data = line + "\r\n";
                                sm.safeWrite(active->handle, data);
                            }
                        });
                } else if (msg.has_ping()) {
                    // Queue pong for the writer loop (don't write from reader thread)
                    HydraSession::OutputItem pongItem;
                    pongItem.puaText = "\x01PONG\x01" +
                        std::to_string(msg.ping().client_timestamp());
                    pongItem.source = "__pong__";
                    pongItem.timestamp = 0;
                    pongItem.linkNumber = 0;
                    {
                        std::lock_guard<std::mutex> lock(oq->mutex);
                        sq->output.push(std::move(pongItem));
                    }
                    oq->cv.notify_all();
                } else if (msg.has_preferences()) {
                    const auto& prefs = msg.preferences();
                    // Update subscriber's color format (under lock — writer reads it)
                    // Skip if COLOR_UNSPECIFIED (0) — means "don't change"
                    if (prefs.color_format() != hydra::COLOR_UNSPECIFIED) {
                        std::lock_guard<std::mutex> lock(oq->mutex);
                        sq->renderFormat = static_cast<HydraSession::RenderFormat>(
                            prefs.color_format());
                    }
                    // Forward terminal size to game via NAWS
                    if (prefs.terminal_width() > 0 || prefs.terminal_height() > 0) {
                        workQueue_.enqueue<void>(
                            [sid, w = prefs.terminal_width(), h = prefs.terminal_height()]
                            (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                                HydraSession* s = sm.findByPersistId(sid);
                                if (!s) return;
                                BackDoorLink* active = s->getActiveLink();
                                if (active && active->handle != ganl::InvalidConnectionHandle) {
                                    // Send NAWS to game: IAC SB NAWS w_hi w_lo h_hi h_lo IAC SE
                                    uint16_t width = w ? static_cast<uint16_t>(w) : 80;
                                    uint16_t height = h ? static_cast<uint16_t>(h) : 24;
                                    sm.safeWrite(active->handle, buildNawsFrame(width, height));
                                }
                            });
                    }
                    // Store terminal_type in session for future TTYPE forwarding
                    if (!prefs.terminal_type().empty()) {
                        workQueue_.enqueue<void>(
                            [sid, ttype = prefs.terminal_type()]
                            (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                                HydraSession* s = sm.findByPersistId(sid);
                                if (s) s->terminalType = ttype;
                            });
                    }
                    LOG_DEBUG("GameSession: client set preferences color=%d width=%u height=%u type=%s",
                              prefs.color_format(), prefs.terminal_width(), prefs.terminal_height(),
                              prefs.terminal_type().c_str());
                } else if (msg.has_gmcp()) {
                    // Forward GMCP to active link
                    workQueue_.enqueue<void>(
                        [sid, pkg = msg.gmcp().package(), json = msg.gmcp().json()]
                        (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                            HydraSession* s = sm.findByPersistId(sid);
                            if (!s) return;
                            BackDoorLink* active = s->getActiveLink();
                            if (active && active->handle != ganl::InvalidConnectionHandle
                                && active->gmcpEnabled) {
                                std::string payload = pkg + " " + json;
                                sm.safeWrite(active->handle, buildGmcpFrame(payload));
                            }
                        });
                }
            }
            done.store(true);
            oq->cv.notify_all();
        });

        // Writer loop: drain this subscriber's own queues
        while (!done.load() && !ctx->IsCancelled()) {
            std::unique_lock<std::mutex> lock(oq->mutex);
            oq->cv.wait_for(lock, std::chrono::milliseconds(500),
                [&sq, &done] { return !sq->output.empty() || !sq->gmcp.empty() || done.load(); });

            // Read renderFormat under lock (writer reads, reader sets)
            auto currentFmt = sq->renderFormat;

            while (!sq->output.empty()) {
                auto item = std::move(sq->output.front());
                sq->output.pop();
                lock.unlock();

                // Check for pong sentinel (queued by reader thread)
                if (item.source == "__pong__") {
                    hydra::ServerMessage msg;
                    auto* pong = msg.mutable_pong();
                    int64_t clientTs = 0;
                    try { clientTs = std::stoll(item.puaText.substr(6)); }
                    catch (...) {}
                    pong->set_client_timestamp(clientTs);
                    pong->set_server_timestamp(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());
                    if (!stream->Write(msg)) { done.store(true); break; }
                } else {
                    hydra::ServerMessage msg;
                    auto* go = msg.mutable_game_output();
                    go->set_text(sanitizeProtoTextForLog(
                        item.render(currentFmt), "grpc bidi output",
                        item.source, item.linkNumber));
                    go->set_source(item.source);
                    go->set_timestamp(static_cast<int64_t>(item.timestamp));
                    go->set_link_number(item.linkNumber);
                    if (!stream->Write(msg)) { done.store(true); break; }
                }
                lock.lock();
            }

            while (!sq->gmcp.empty()) {
                auto item = std::move(sq->gmcp.front());
                sq->gmcp.pop();
                lock.unlock();
                hydra::ServerMessage msg;
                auto* gm = msg.mutable_gmcp();
                gm->set_package(item.package);
                gm->set_json(item.json);
                gm->set_link_number(item.linkNumber);
                if (!stream->Write(msg)) { done.store(true); break; }
                lock.lock();
            }
        }

        {
            std::lock_guard<std::mutex> lock(oq->mutex);
            oq->removeSubscriber(subId);
        }
        reader.join();
        return Status::OK;
    }

    // ---- Legacy unary I/O ----

    Status SendInput(ServerContext* ctx, const hydra::InputRequest* req,
                     hydra::InputResponse* resp) override {
        std::string sid = getSessionId(ctx, req->session_id());
        auto future = workQueue_.enqueue<bool>(
            [sid, line = req->line(), resp]
            (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) { resp->set_error("session not found"); return false; }
                BackDoorLink* active = s->getActiveLink();
                if (!active || active->state != LinkState::Active) {
                    resp->set_error("no active link");
                    return false;
                }
                std::string data = line + "\r\n";
                sm.safeWrite(active->handle, data);
                resp->set_success(true);
                return true;
            });
        future.get();
        return Status::OK;
    }

    Status Subscribe(ServerContext* ctx, const hydra::SessionRequest* req,
                     ServerWriter<hydra::GameOutput>* writer) override {
        std::string sid = getSessionId(ctx, req->session_id());
        std::shared_ptr<HydraSession::OutputQueue> oq;
        auto future = workQueue_.enqueue<std::shared_ptr<HydraSession::OutputQueue>>(
            [&sid](SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&)
                -> std::shared_ptr<HydraSession::OutputQueue> {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) return nullptr;
                return s->outputQueue;
            });
        oq = future.get();
        if (!oq) return Status(StatusCode::NOT_FOUND, "session not found");

        // Forward terminal size to the active game link if provided
        if (req->terminal_width() > 0 || req->terminal_height() > 0) {
            uint32_t w = req->terminal_width();
            uint32_t h = req->terminal_height();
            workQueue_.enqueue<void>(
                [sid, w, h]
                (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                    HydraSession* s = sm.findByPersistId(sid);
                    if (!s) return;
                    BackDoorLink* active = s->getActiveLink();
                    if (active && active->handle != ganl::InvalidConnectionHandle) {
                        uint16_t width = w ? static_cast<uint16_t>(w) : 80;
                        uint16_t height = h ? static_cast<uint16_t>(h) : 24;
                        sm.safeWrite(active->handle, buildNawsFrame(width, height));
                    }
                });
        }

        // Register as subscriber (output only, no GMCP) with requested color format
        // COLOR_UNSPECIFIED (0) → default to TrueColor
        auto renderFmt = req->color_format() == hydra::COLOR_UNSPECIFIED
            ? HydraSession::RenderFormat::TrueColor
            : static_cast<HydraSession::RenderFormat>(req->color_format());
        int subId;
        std::shared_ptr<HydraSession::SubscriberQueue> sq;
        {
            std::lock_guard<std::mutex> lock(oq->mutex);
            auto [id, q] = oq->addSubscriber(true, false, renderFmt);
            subId = id;
            sq = q;
        }

        while (!ctx->IsCancelled()) {
            std::unique_lock<std::mutex> lock(oq->mutex);
            oq->cv.wait_for(lock, std::chrono::milliseconds(500),
                [&sq] { return !sq->output.empty(); });

            while (!sq->output.empty()) {
                auto item = std::move(sq->output.front());
                sq->output.pop();
                lock.unlock();
                hydra::GameOutput msg;
                msg.set_text(sanitizeProtoTextForLog(
                    item.render(sq->renderFormat), "grpc subscribe output",
                    item.source, item.linkNumber));
                msg.set_source(item.source);
                msg.set_timestamp(static_cast<int64_t>(item.timestamp));
                msg.set_link_number(item.linkNumber);
                if (!writer->Write(msg)) {
                    std::lock_guard<std::mutex> lk(oq->mutex);
                    oq->removeSubscriber(subId);
                    return Status::OK;
                }
                lock.lock();
            }
        }

        {
            std::lock_guard<std::mutex> lock(oq->mutex);
            oq->removeSubscriber(subId);
        }
        return Status::OK;
    }

    // ---- GMCP subscription ----

    Status SubscribeGmcp(ServerContext* ctx, const hydra::GmcpSubscribeRequest* req,
                         ServerWriter<hydra::GmcpMessage>* writer) override {
        std::string sid = getSessionId(ctx, req->session_id());
        std::shared_ptr<HydraSession::OutputQueue> oq;
        auto future = workQueue_.enqueue<std::shared_ptr<HydraSession::OutputQueue>>(
            [&sid](SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&)
                -> std::shared_ptr<HydraSession::OutputQueue> {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) return nullptr;
                return s->outputQueue;
            });
        oq = future.get();
        if (!oq) return Status(StatusCode::NOT_FOUND, "session not found");

        // Collect package filters
        std::vector<std::string> filters;
        for (int i = 0; i < req->packages_size(); i++) {
            filters.push_back(req->packages(i));
        }

        // Register as subscriber (GMCP only, no output)
        int subId;
        std::shared_ptr<HydraSession::SubscriberQueue> sq;
        {
            std::lock_guard<std::mutex> lock(oq->mutex);
            auto [id, q] = oq->addSubscriber(false, true);
            subId = id;
            sq = q;
        }

        while (!ctx->IsCancelled()) {
            std::unique_lock<std::mutex> lock(oq->mutex);
            oq->cv.wait_for(lock, std::chrono::milliseconds(500),
                [&sq] { return !sq->gmcp.empty(); });

            while (!sq->gmcp.empty()) {
                auto item = std::move(sq->gmcp.front());
                sq->gmcp.pop();
                lock.unlock();

                // Apply package filter if any
                bool match = filters.empty();
                if (!match) {
                    for (const auto& f : filters) {
                        if (item.package.compare(0, f.size(), f) == 0) {
                            match = true;
                            break;
                        }
                    }
                }

                if (match) {
                    hydra::GmcpMessage msg;
                    msg.set_package(item.package);
                    msg.set_json(item.json);
                    msg.set_link_number(item.linkNumber);
                    if (!writer->Write(msg)) {
                        std::lock_guard<std::mutex> lk(oq->mutex);
                        oq->removeSubscriber(subId);
                        return Status::OK;
                    }
                }
                lock.lock();
            }
        }

        {
            std::lock_guard<std::mutex> lock(oq->mutex);
            oq->removeSubscriber(subId);
        }
        return Status::OK;
    }

    // ---- Scroll-back ----

    Status GetScrollBack(ServerContext* ctx, const hydra::ScrollBackRequest* req,
                         hydra::ScrollBackResponse* resp) override {
        std::string sid = getSessionId(ctx, req->session_id());
        // Resolve color format: UNSPECIFIED → TrueColor
        auto fmt = req->color_format() == hydra::COLOR_UNSPECIFIED
            ? HydraSession::RenderFormat::TrueColor
            : static_cast<HydraSession::RenderFormat>(req->color_format());

        auto future = workQueue_.enqueue<bool>(
            [sid, maxLines = req->max_lines(), fmt, resp]
            (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) return false;
                size_t n = maxLines > 0 ? static_cast<size_t>(maxLines)
                                        : s->scrollback.count();
                struct ReplayCtx {
                    hydra::ScrollBackResponse* resp;
                    HydraSession::RenderFormat fmt;
                };
                ReplayCtx rctx{resp, fmt};
                s->scrollback.replay(n,
                    [](const std::string& text, const std::string& source,
                       time_t timestamp, void* ctx) {
                        auto* rc = static_cast<ReplayCtx*>(ctx);
                        auto* line = rc->resp->add_lines();
                        // Render PUA text at the requested color format
                        HydraSession::OutputItem item;
                        item.puaText = text;
                        line->set_text(sanitizeProtoTextForLog(
                            item.render(rc->fmt), "grpc scrollback",
                            source, 0));
                        line->set_source(source);
                        line->set_timestamp(static_cast<int64_t>(timestamp));
                    },
                    &rctx);
                return true;
            });
        if (!future.get()) return Status(StatusCode::NOT_FOUND, "session not found");
        return Status::OK;
    }

    // ---- Credentials ----

    Status AddCredential(ServerContext* ctx, const hydra::AddCredentialRequest* req,
                         hydra::AddCredentialResponse* resp) override {
        std::string sid = getSessionId(ctx, req->session_id());
        auto future = workQueue_.enqueue<bool>(
            [sid, game = req->game(), character = req->character(),
             verb = req->verb(), name = req->name(), secret = req->secret(), resp]
            (SessionManager& sm, AccountManager& am, const HydraConfig&, ProcessManager&) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) { resp->set_error("session not found"); return false; }
                std::string errorMsg;
                if (am.storeCredential(s->accountId, game, character,
                                       verb, name, secret, errorMsg)) {
                    resp->set_success(true);
                    return true;
                }
                resp->set_error(errorMsg);
                return false;
            });
        future.get();
        return Status::OK;
    }

    Status DeleteCredential(ServerContext* ctx, const hydra::DeleteCredentialRequest* req,
                            hydra::DeleteCredentialResponse* resp) override {
        std::string sid = getSessionId(ctx, req->session_id());
        auto future = workQueue_.enqueue<bool>(
            [sid, game = req->game(), character = req->character(), resp]
            (SessionManager& sm, AccountManager& am, const HydraConfig&, ProcessManager&) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) { resp->set_error("session not found"); return false; }
                resp->set_success(am.deleteCredential(s->accountId, game, character));
                return true;
            });
        future.get();
        return Status::OK;
    }

    Status ListCredentials(ServerContext* ctx, const hydra::ListCredentialsRequest* req,
                           hydra::ListCredentialsResponse* resp) override {
        std::string sid = getSessionId(ctx, req->session_id());
        auto future = workQueue_.enqueue<bool>(
            [sid, resp]
            (SessionManager& sm, AccountManager& am, const HydraConfig&, ProcessManager&) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) return false;
                auto creds = am.listCredentials(s->accountId);
                for (const auto& c : creds) {
                    auto* sc = resp->add_credentials();
                    sc->set_game(c.game);
                    sc->set_character(c.character);
                    sc->set_verb(c.verb);
                    sc->set_name(c.name);
                    sc->set_auto_login(c.autoLogin);
                }
                return true;
            });
        if (!future.get()) return Status(StatusCode::NOT_FOUND, "session not found");
        return Status::OK;
    }

    // ---- Process management ----

    Status StartGame(ServerContext* ctx, const hydra::GameRequest* req,
                     hydra::GameResponse* resp) override {
        std::string sid = getSessionId(ctx, "");
        auto future = workQueue_.enqueue<bool>(
            [sid, gameName = req->game_name(), resp]
            (SessionManager& sm, AccountManager& am, const HydraConfig& cfg, ProcessManager& pm) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s || !am.isAdmin(s->accountId)) {
                    resp->set_error("permission denied (admin required)");
                    return false;
                }
                const GameConfig* game = nullptr;
                for (const auto& g : cfg.games) {
                    if (g.name == gameName) { game = &g; break; }
                }
                if (!game) { resp->set_error("unknown game"); return false; }
                if (game->type != GameType::Local) {
                    resp->set_error("not a local game");
                    return false;
                }
                std::string errorMsg;
                if (pm.startGame(*game, errorMsg)) {
                    resp->set_success(true);
                    resp->set_pid(static_cast<int>(pm.getPid(gameName)));
                    return true;
                }
                resp->set_error(errorMsg);
                return false;
            });
        future.get();
        return Status::OK;
    }

    Status StopGame(ServerContext* ctx, const hydra::GameRequest* req,
                    hydra::GameResponse* resp) override {
        std::string sid = getSessionId(ctx, "");
        auto future = workQueue_.enqueue<bool>(
            [sid, gameName = req->game_name(), resp]
            (SessionManager& sm, AccountManager& am, const HydraConfig&, ProcessManager& pm) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s || !am.isAdmin(s->accountId)) {
                    resp->set_error("permission denied (admin required)");
                    return false;
                }
                resp->set_success(pm.stopGame(gameName));
                if (!resp->success()) resp->set_error("game not running");
                return true;
            });
        future.get();
        return Status::OK;
    }

    Status RestartGame(ServerContext* ctx, const hydra::GameRequest* req,
                       hydra::GameResponse* resp) override {
        std::string sid = getSessionId(ctx, "");
        auto future = workQueue_.enqueue<bool>(
            [sid, gameName = req->game_name(), resp]
            (SessionManager& sm, AccountManager& am, const HydraConfig& cfg, ProcessManager& pm) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s || !am.isAdmin(s->accountId)) {
                    resp->set_error("permission denied (admin required)");
                    return false;
                }
                const GameConfig* game = nullptr;
                for (const auto& g : cfg.games) {
                    if (g.name == gameName) { game = &g; break; }
                }
                if (!game) { resp->set_error("unknown game"); return false; }
                std::string errorMsg;
                if (pm.restartGame(*game, errorMsg)) {
                    resp->set_success(true);
                    resp->set_pid(static_cast<int>(pm.getPid(gameName)));
                    return true;
                }
                resp->set_error(errorMsg);
                return false;
            });
        future.get();
        return Status::OK;
    }

    Status GetGameStatus(ServerContext* ctx, const hydra::GameStatusRequest* req,
                         hydra::GameStatusResponse* resp) override {
        auto future = workQueue_.enqueue<bool>(
            [gameName = req->game_name(), resp]
            (SessionManager&, AccountManager&, const HydraConfig& cfg, ProcessManager& pm) {
                for (const auto& g : cfg.games) {
                    if (g.type != GameType::Local) continue;
                    if (!gameName.empty() && g.name != gameName) continue;
                    auto* pi = resp->add_processes();
                    pi->set_game_name(g.name);
                    pi->set_running(pm.isRunning(g.name));
                    pi->set_pid(static_cast<int>(pm.getPid(g.name)));
                }
                return true;
            });
        future.get();
        return Status::OK;
    }

private:
    SessionManager& sessionMgr_;
    AccountManager& accounts_;
    const HydraConfig& config_;
    ProcessManager& procMgr_;
    WorkQueue& workQueue_;
};

// ---- GrpcServer lifecycle ----

GrpcServer::GrpcServer(SessionManager& sessionMgr, AccountManager& accounts,
                       const HydraConfig& config, ProcessManager& procMgr,
                       WorkQueue& workQueue)
    : sessionMgr_(sessionMgr), accounts_(accounts), config_(config),
      procMgr_(procMgr), workQueue_(workQueue) {
}

GrpcServer::~GrpcServer() {
    shutdown();
}

bool GrpcServer::start(const std::string& listenAddr, std::string& errorMsg) {
    auto service = std::make_unique<HydraServiceImpl>(
        sessionMgr_, accounts_, config_, procMgr_, workQueue_);

    ServerBuilder builder;

    // Use TLS if cert/key are configured for gRPC
    if (!config_.grpcTlsCert.empty() && !config_.grpcTlsKey.empty()) {
        std::ifstream certFile(config_.grpcTlsCert);
        std::ifstream keyFile(config_.grpcTlsKey);
        if (!certFile.is_open() || !keyFile.is_open()) {
            errorMsg = "cannot open gRPC TLS cert/key files";
            return false;
        }
        std::string cert((std::istreambuf_iterator<char>(certFile)),
                         std::istreambuf_iterator<char>());
        std::string key((std::istreambuf_iterator<char>(keyFile)),
                        std::istreambuf_iterator<char>());

        grpc::SslServerCredentialsOptions sslOpts;
        sslOpts.pem_key_cert_pairs.push_back({key, cert});
        builder.AddListeningPort(listenAddr,
                                 grpc::SslServerCredentials(sslOpts));
        LOG_INFO("gRPC using TLS: cert=%s key=%s",
                 config_.grpcTlsCert.c_str(), config_.grpcTlsKey.c_str());
    } else {
        // Without TLS, only allow binding to loopback addresses
        bool isLoopback = (listenAddr.find("127.0.0.1") != std::string::npos
                        || listenAddr.find("[::1]") != std::string::npos
                        || listenAddr.find("localhost") != std::string::npos);
        if (!isLoopback) {
            errorMsg = "gRPC without TLS must bind to loopback "
                       "(127.0.0.1 or [::1]), got: " + listenAddr;
            return false;
        }
        builder.AddListeningPort(listenAddr, grpc::InsecureServerCredentials());
        LOG_INFO("gRPC using insecure credentials on loopback: %s",
                 listenAddr.c_str());
    }

    builder.RegisterService(service.get());

    server_ = builder.BuildAndStart();
    if (!server_) {
        errorMsg = "failed to start gRPC server on " + listenAddr;
        return false;
    }

    LOG_INFO("gRPC server listening on %s", listenAddr.c_str());

    serverThread_ = std::thread([srv = server_.get(), svc = std::move(service)]() {
        srv->Wait();
    });

    return true;
}

void GrpcServer::shutdown() {
    if (server_) {
        server_->Shutdown();
        if (serverThread_.joinable()) {
            serverThread_.join();
        }
        server_.reset();
        LOG_INFO("gRPC server shut down");
    }
}

#endif // GRPC_ENABLED
