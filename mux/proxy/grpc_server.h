#ifndef HYDRA_GRPC_SERVER_H
#define HYDRA_GRPC_SERVER_H

#ifdef GRPC_ENABLED

#include <memory>
#include <string>
#include <thread>

class SessionManager;
class AccountManager;
struct HydraConfig;
class ProcessManager;
class WorkQueue;

namespace grpc { class Server; }

class GrpcServer {
public:
    GrpcServer(SessionManager& sessionMgr, AccountManager& accounts,
               const HydraConfig& config, ProcessManager& procMgr,
               WorkQueue& workQueue);
    ~GrpcServer();

    bool start(const std::string& listenAddr, std::string& errorMsg);
    void shutdown();

private:
    SessionManager& sessionMgr_;
    AccountManager& accounts_;
    const HydraConfig& config_;
    ProcessManager& procMgr_;
    WorkQueue& workQueue_;
    std::unique_ptr<grpc::Server> server_;
    std::thread serverThread_;
};

#endif // GRPC_ENABLED
#endif // HYDRA_GRPC_SERVER_H
