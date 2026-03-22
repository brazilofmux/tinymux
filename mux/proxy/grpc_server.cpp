#ifdef GRPC_ENABLED

#include "grpc_server.h"
#include "session_manager.h"
#include "account_manager.h"
#include "config.h"
#include "hydra_log.h"

#include "hydra.grpc.pb.h"

#include <grpcpp/grpcpp.h>
#include <grpcpp/server_builder.h>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;

class HydraServiceImpl final : public hydra::HydraService::Service {
public:
    HydraServiceImpl(SessionManager& sessionMgr, AccountManager& accounts,
                     const HydraConfig& config)
        : sessionMgr_(sessionMgr), accounts_(accounts), config_(config) {}

    Status Authenticate(ServerContext* ctx, const hydra::AuthRequest* req,
                        hydra::AuthResponse* resp) override {
        std::vector<uint8_t> sbKey;
        uint32_t accountId = accounts_.authenticate(
            req->username(), req->password(), sbKey);

        if (accountId == 0) {
            resp->set_success(false);
            resp->set_error("authentication failed");
            return Status::OK;
        }

        // Generate a session token (for now, use account ID as string)
        resp->set_success(true);
        resp->set_session_id(std::to_string(accountId));
        return Status::OK;
    }

    Status ListGames(ServerContext* ctx, const hydra::Empty* req,
                     hydra::GameList* resp) override {
        for (const auto& game : config_.games) {
            auto* gi = resp->add_games();
            gi->set_name(game.name);
            gi->set_host(game.host);
            gi->set_port(game.port);
            gi->set_type(game.type == GameType::Local ? "local" : "remote");
        }
        return Status::OK;
    }

    Status ListLinks(ServerContext* ctx, const hydra::LinkListRequest* req,
                     hydra::LinkList* resp) override {
        // TODO: look up session by session_id and list its links
        (void)req;
        return Status::OK;
    }

    Status Connect(ServerContext* ctx, const hydra::ConnectRequest* req,
                   hydra::ConnectResponse* resp) override {
        // TODO: look up session, call connectToGame
        (void)req;
        resp->set_success(false);
        resp->set_error("not yet implemented via gRPC");
        return Status::OK;
    }

    Status SendInput(ServerContext* ctx, const hydra::InputRequest* req,
                     hydra::InputResponse* resp) override {
        // TODO: look up session, forward input to active link
        (void)req;
        resp->set_success(false);
        return Status::OK;
    }

    Status Subscribe(ServerContext* ctx, const hydra::SubscribeRequest* req,
                     ServerWriter<hydra::GameOutput>* writer) override {
        // TODO: subscribe to session output stream
        (void)req;
        (void)writer;
        return Status::OK;
    }

private:
    SessionManager& sessionMgr_;
    AccountManager& accounts_;
    const HydraConfig& config_;
};

GrpcServer::GrpcServer(SessionManager& sessionMgr, AccountManager& accounts,
                       const HydraConfig& config)
    : sessionMgr_(sessionMgr), accounts_(accounts), config_(config) {
}

GrpcServer::~GrpcServer() {
    shutdown();
}

bool GrpcServer::start(const std::string& listenAddr, std::string& errorMsg) {
    auto service = std::make_unique<HydraServiceImpl>(
        sessionMgr_, accounts_, config_);

    ServerBuilder builder;
    builder.AddListeningPort(listenAddr, grpc::InsecureServerCredentials());
    builder.RegisterService(service.get());

    server_ = builder.BuildAndStart();
    if (!server_) {
        errorMsg = "failed to start gRPC server on " + listenAddr;
        return false;
    }

    LOG_INFO("gRPC server listening on %s", listenAddr.c_str());

    // Run the server in a background thread
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
