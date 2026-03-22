#ifndef HYDRA_GRPC_SERVER_H
#define HYDRA_GRPC_SERVER_H

#ifdef GRPC_ENABLED

#include <cstdint>
#include <memory>
#include <string>
#include <thread>

class SessionManager;
class AccountManager;
struct HydraConfig;

namespace grpc { class Server; }

class GrpcServer {
public:
    GrpcServer(SessionManager& sessionMgr, AccountManager& accounts,
               const HydraConfig& config);
    ~GrpcServer();

    // Start the gRPC server on the given address (e.g. "0.0.0.0:4204").
    bool start(const std::string& listenAddr, std::string& errorMsg);

    // Shut down the gRPC server.
    void shutdown();

private:
    SessionManager& sessionMgr_;
    AccountManager& accounts_;
    const HydraConfig& config_;
    std::unique_ptr<grpc::Server> server_;
    std::thread serverThread_;
};

#endif // GRPC_ENABLED
#endif // HYDRA_GRPC_SERVER_H
