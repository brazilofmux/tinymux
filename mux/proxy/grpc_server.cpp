#ifdef GRPC_ENABLED

#include "grpc_server.h"
#include "work_queue.h"
#include "session_manager.h"
#include "account_manager.h"
#include "process_manager.h"
#include "config.h"
#include "hydra_log.h"

#include "hydra.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>
#include <chrono>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::ServerReaderWriter;
using grpc::Status;
using grpc::StatusCode;

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
        auto future = workQueue_.enqueue<std::pair<std::string, std::string>>(
            [user = req->username(), pw = req->password()]
            (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&)
                -> std::pair<std::string, std::string> {
                std::string errorMsg;
                std::string pid = sm.createAccountAndGetSession(user, pw, errorMsg);
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

        oq->subscriberCount.fetch_add(1);
        oq->gmcpSubscriberCount.fetch_add(1);

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
                                int bdFd = static_cast<int>(active->handle);
                                send(bdFd, data.data(), data.size(), MSG_NOSIGNAL);
                            }
                        });
                } else if (msg.has_ping()) {
                    hydra::ServerMessage resp;
                    auto* pong = resp.mutable_pong();
                    pong->set_client_timestamp(msg.ping().client_timestamp());
                    pong->set_server_timestamp(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()).count());
                    stream->Write(resp);
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
                                // Build GMCP telnet frame
                                std::string frame;
                                frame.push_back(static_cast<char>(255)); // IAC
                                frame.push_back(static_cast<char>(250)); // SB
                                frame.push_back(static_cast<char>(201)); // GMCP
                                frame.append(payload);
                                frame.push_back(static_cast<char>(255)); // IAC
                                frame.push_back(static_cast<char>(240)); // SE
                                int bdFd = static_cast<int>(active->handle);
                                send(bdFd, frame.data(), frame.size(), MSG_NOSIGNAL);
                            }
                        });
                }
            }
            done.store(true);
            oq->cv.notify_all();
        });

        // Writer loop: push output + GMCP to client
        while (!done.load() && !ctx->IsCancelled()) {
            std::unique_lock<std::mutex> lock(oq->mutex);
            oq->cv.wait_for(lock, std::chrono::milliseconds(500),
                [&oq, &done] { return !oq->queue.empty() || !oq->gmcpQueue.empty() || done.load(); });

            while (!oq->queue.empty()) {
                auto& item = oq->queue.front();
                hydra::ServerMessage msg;
                auto* go = msg.mutable_game_output();
                go->set_text(item.text);
                go->set_source(item.source);
                go->set_timestamp(static_cast<int64_t>(item.timestamp));
                go->set_link_number(item.linkNumber);
                oq->queue.pop();
                lock.unlock();
                if (!stream->Write(msg)) { done.store(true); break; }
                lock.lock();
            }

            while (!oq->gmcpQueue.empty()) {
                auto& item = oq->gmcpQueue.front();
                hydra::ServerMessage msg;
                auto* gm = msg.mutable_gmcp();
                gm->set_package(item.package);
                gm->set_json(item.json);
                gm->set_link_number(item.linkNumber);
                oq->gmcpQueue.pop();
                lock.unlock();
                if (!stream->Write(msg)) { done.store(true); break; }
                lock.lock();
            }
        }

        oq->subscriberCount.fetch_sub(1);
        oq->gmcpSubscriberCount.fetch_sub(1);
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
                int bdFd = static_cast<int>(active->handle);
                send(bdFd, data.data(), data.size(), MSG_NOSIGNAL);
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

        oq->subscriberCount.fetch_add(1);

        while (!ctx->IsCancelled()) {
            std::unique_lock<std::mutex> lock(oq->mutex);
            oq->cv.wait_for(lock, std::chrono::milliseconds(500),
                [&oq] { return !oq->queue.empty(); });

            while (!oq->queue.empty()) {
                auto& item = oq->queue.front();
                hydra::GameOutput msg;
                msg.set_text(item.text);
                msg.set_source(item.source);
                msg.set_timestamp(static_cast<int64_t>(item.timestamp));
                msg.set_link_number(item.linkNumber);
                oq->queue.pop();
                lock.unlock();
                if (!writer->Write(msg)) {
                    oq->subscriberCount.fetch_sub(1);
                    return Status::OK;
                }
                lock.lock();
            }
        }

        oq->subscriberCount.fetch_sub(1);
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

        oq->gmcpSubscriberCount.fetch_add(1);

        while (!ctx->IsCancelled()) {
            std::unique_lock<std::mutex> lock(oq->mutex);
            oq->cv.wait_for(lock, std::chrono::milliseconds(500),
                [&oq] { return !oq->gmcpQueue.empty(); });

            while (!oq->gmcpQueue.empty()) {
                auto item = oq->gmcpQueue.front();
                oq->gmcpQueue.pop();
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
                        oq->gmcpSubscriberCount.fetch_sub(1);
                        return Status::OK;
                    }
                }
                lock.lock();
            }
        }

        oq->gmcpSubscriberCount.fetch_sub(1);
        return Status::OK;
    }

    // ---- Scroll-back ----

    Status GetScrollBack(ServerContext* ctx, const hydra::ScrollBackRequest* req,
                         hydra::ScrollBackResponse* resp) override {
        std::string sid = getSessionId(ctx, req->session_id());
        auto future = workQueue_.enqueue<bool>(
            [sid, maxLines = req->max_lines(), resp]
            (SessionManager& sm, AccountManager&, const HydraConfig&, ProcessManager&) {
                HydraSession* s = sm.findByPersistId(sid);
                if (!s) return false;
                size_t n = maxLines > 0 ? static_cast<size_t>(maxLines)
                                        : s->scrollback.count();
                struct ReplayCtx { hydra::ScrollBackResponse* resp; };
                ReplayCtx rctx{resp};
                s->scrollback.replay(n,
                    [](const std::string& text, const std::string& source,
                       time_t timestamp, void* ctx) {
                        auto* rc = static_cast<ReplayCtx*>(ctx);
                        auto* line = rc->resp->add_lines();
                        line->set_text(text);
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
        auto future = workQueue_.enqueue<bool>(
            [gameName = req->game_name(), resp]
            (SessionManager&, AccountManager&, const HydraConfig& cfg, ProcessManager& pm) {
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
        auto future = workQueue_.enqueue<bool>(
            [gameName = req->game_name(), resp]
            (SessionManager&, AccountManager&, const HydraConfig&, ProcessManager& pm) {
                resp->set_success(pm.stopGame(gameName));
                if (!resp->success()) resp->set_error("game not running");
                return true;
            });
        future.get();
        return Status::OK;
    }

    Status RestartGame(ServerContext* ctx, const hydra::GameRequest* req,
                       hydra::GameResponse* resp) override {
        auto future = workQueue_.enqueue<bool>(
            [gameName = req->game_name(), resp]
            (SessionManager&, AccountManager&, const HydraConfig& cfg, ProcessManager& pm) {
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
    builder.AddListeningPort(listenAddr, grpc::InsecureServerCredentials());
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
