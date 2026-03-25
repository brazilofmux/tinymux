#include "config.h"
#include "hydra_log.h"
#include "session_manager.h"
#include "account_manager.h"

#ifdef GRPC_ENABLED
#include "grpc_server.h"
#include "work_queue.h"
#endif

#include <network_engine.h>
#include <network_engine_factory.h>
#include <network_types.h>
#include <openssl_transport.h>
#include <secure_transport.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#endif

static volatile sig_atomic_t g_shutdown = 0;
static volatile sig_atomic_t g_reload = 0;
static volatile sig_atomic_t g_dumpStatus = 0;

#if defined(_WIN32)

static BOOL WINAPI ConsoleCtrlHandler(DWORD ctrlType) {
    switch (ctrlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
            g_shutdown = 1;
            return TRUE;
    }
    return FALSE;
}

static void installSignals() {
    SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);
}

// Read a password from the console without echoing.
static std::string readPassword(const char* prompt) {
    fprintf(stderr, "%s", prompt);
    HANDLE hStdin = GetStdHandle(STD_INPUT_HANDLE);
    DWORD oldMode;
    GetConsoleMode(hStdin, &oldMode);
    SetConsoleMode(hStdin, oldMode & ~ENABLE_ECHO_INPUT);

    char buf[256];
    DWORD nRead = 0;
    ReadConsoleA(hStdin, buf, sizeof(buf) - 1, &nRead, nullptr);
    SetConsoleMode(hStdin, oldMode);
    fprintf(stderr, "\n");

    // Strip trailing \r\n
    while (nRead > 0 && (buf[nRead - 1] == '\n' || buf[nRead - 1] == '\r')) {
        nRead--;
    }
    return std::string(buf, nRead);
}

#else // POSIX

static void signalHandler(int sig) {
    switch (sig) {
        case SIGTERM:
        case SIGINT:
            g_shutdown = 1;
            break;
        case SIGHUP:
            g_reload = 1;
            break;
        case SIGUSR1:
            g_dumpStatus = 1;
            break;
    }
}

static void installSignals() {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT,  &sa, nullptr);
    sigaction(SIGHUP,  &sa, nullptr);
    sigaction(SIGUSR1, &sa, nullptr);

    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, nullptr);
}

static std::string readPassword(const char* prompt) {
    char* pw = getpass(prompt);
    return pw ? std::string(pw) : std::string();
}

#endif // _WIN32

static void usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [-c config_file] [--create-admin username]\n"
        "\n"
        "Options:\n"
        "  -c <file>              Configuration file (default: hydra.conf)\n"
        "  --create-admin <user>  Create an admin account and exit\n"
        "  -h, --help             Show this help\n",
        prog);
}

int main(int argc, char* argv[]) {
    std::string configPath = "hydra.conf";
    std::string createAdmin;
    bool showHelp = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            configPath = argv[++i];
        } else if (strcmp(argv[i], "--create-admin") == 0 && i + 1 < argc) {
            createAdmin = argv[++i];
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            showHelp = true;
        }
    }

    if (showHelp) {
        usage(argv[0]);
        return 0;
    }

    // Load configuration
    HydraConfig config;
    std::string errorMsg;
    if (!loadConfig(configPath, config, errorMsg)) {
        fprintf(stderr, "HYDRA: config error: %s\n", errorMsg.c_str());
        return 1;
    }

    // Initialize logging
    if (!logInit(config.logFile, config.logLevel)) {
        return 1;
    }
    LOG_INFO("Hydra starting");

    ganl::setLogger(hydraGanlLogCallback);

    // Initialize account database
    AccountManager accounts;
    if (!accounts.initialize(config.databasePath, errorMsg)) {
        LOG_ERROR("Database init failed: %s", errorMsg.c_str());
        logShutdown();
        return 1;
    }
    LOG_INFO("Database opened: %s", config.databasePath.c_str());

    // Load master key — try environment variable, then file, then auto-generate.
    // Priority: HYDRA_MASTER_KEY env > master_key config > auto-generate.
    if (accounts.loadMasterKeyFromEnv("HYDRA_MASTER_KEY", errorMsg)) {
        // Loaded from environment — preferred for containers/systemd
    } else if (!config.masterKeyPath.empty()) {
        if (accounts.loadMasterKey(config.masterKeyPath, errorMsg)) {
            // Loaded from file
        } else {
            // File doesn't exist — try to auto-generate
            LOG_INFO("Master key file not found, generating: %s",
                     config.masterKeyPath.c_str());
            if (accounts.generateMasterKey(config.masterKeyPath, errorMsg)) {
                LOG_INFO("Master key auto-generated at %s",
                         config.masterKeyPath.c_str());
            } else {
                LOG_WARN("Master key not available: %s (auto-login disabled)",
                         errorMsg.c_str());
            }
        }
    } else {
        LOG_INFO("No master key configured (auto-login disabled)");
    }

    // Handle --create-admin
    if (!createAdmin.empty()) {
        std::string pw = readPassword("Password: ");
        if (pw.empty()) {
            fprintf(stderr, "HYDRA: password required\n");
            return 1;
        }
        uint32_t accountId = 0;
        if (!accounts.createAccount(createAdmin, pw, true,
                                    accountId, errorMsg)) {
            fprintf(stderr, "HYDRA: create-admin failed: %s\n",
                    errorMsg.c_str());
            return 1;
        }
        fprintf(stderr, "HYDRA: admin account '%s' created (id=%u)\n",
                createAdmin.c_str(), accountId);
        return 0;
    }

    installSignals();

    // Initialize GANL network engine
    auto engine = ganl::NetworkEngineFactory::createEngine();
    if (!engine) {
        LOG_ERROR("Failed to create network engine");
        logShutdown();
        return 1;
    }
    if (!engine->initialize()) {
        LOG_ERROR("Failed to initialize network engine");
        logShutdown();
        return 1;
    }
    LOG_INFO("Network engine initialized");

    // Create session manager
    SessionManager sessionMgr(*engine, accounts, config);

    // Create listeners — tag 1 = telnet, tag 2 = websocket, tag 3 = grpc-web
    // Tags 4-6 are the TLS variants.
    static int tagTelnet = 1;
    static int tagWebSocket = 2;
    static int tagGrpcWeb = 3;
    static int tagTelnetTls = 4;
    static int tagWebSocketTls = 5;
    static int tagGrpcWebTls = 6;
    bool anyListener = false;

    // Initialize TLS transport if any listener needs it
    std::unique_ptr<ganl::OpenSSLTransport> tlsTransport;
    for (const auto& lc : config.listeners) {
        if (lc.tls && !lc.certFile.empty() && !lc.keyFile.empty()) {
            tlsTransport = std::make_unique<ganl::OpenSSLTransport>();
            ganl::TlsConfig tlsCfg;
            tlsCfg.certificateFile = lc.certFile;
            tlsCfg.keyFile = lc.keyFile;
            if (!tlsTransport->initialize(tlsCfg)) {
                LOG_ERROR("Failed to initialize TLS with cert=%s key=%s",
                          lc.certFile.c_str(), lc.keyFile.c_str());
                tlsTransport.reset();
            } else {
                LOG_INFO("TLS initialized: cert=%s key=%s",
                         lc.certFile.c_str(), lc.keyFile.c_str());
            }
            break;  // one TLS context serves all TLS listeners
        }
    }

    for (const auto& lc : config.listeners) {
        if (lc.tls && !tlsTransport) {
            LOG_WARN("Skipping TLS listener %s:%u — TLS not available",
                     lc.host.c_str(), lc.port);
            continue;
        }

        ganl::ErrorCode err = 0;
        ganl::ListenerHandle lh = engine->createListener(
            lc.host, lc.port, err);
        if (lh == ganl::InvalidListenerHandle) {
            LOG_ERROR("Failed to create listener on %s:%u: %s",
                      lc.host.c_str(), lc.port, strerror(err));
            continue;
        }

        int* tag;
        const char* protoName;
        if (lc.websocket) {
            tag = lc.tls ? &tagWebSocketTls : &tagWebSocket;
            protoName = lc.tls ? "websocket+tls" : "websocket";
        } else if (lc.grpcWeb) {
            tag = lc.tls ? &tagGrpcWebTls : &tagGrpcWeb;
            protoName = lc.tls ? "grpc-web+tls" : "grpc-web";
        } else {
            tag = lc.tls ? &tagTelnetTls : &tagTelnet;
            protoName = lc.tls ? "telnet+tls" : "telnet";
        }

        if (!engine->startListening(lh, tag, err)) {
            LOG_ERROR("Failed to start listening on %s:%u: %s",
                      lc.host.c_str(), lc.port, strerror(err));
            continue;
        }

        LOG_INFO("Listening on %s:%u (%s)",
                 lc.host.c_str(), lc.port, protoName);
        anyListener = true;
    }

    if (!anyListener) {
        LOG_ERROR("No listeners could be created");
        engine->shutdown();
        logShutdown();
        return 1;
    }

    // Log configured games
    for (const auto& game : config.games) {
        LOG_INFO("Game: %s -> %s:%u", game.name.c_str(),
                 game.host.c_str(), game.port);
    }

    // Start gRPC server if configured and compiled in
#ifdef GRPC_ENABLED
    WorkQueue workQueue;
    std::unique_ptr<GrpcServer> grpcServer;
    if (!config.grpcListenAddr.empty()) {
        grpcServer = std::make_unique<GrpcServer>(
            sessionMgr, accounts, config, sessionMgr.processMgr(), workQueue);
        std::string grpcErr;
        if (!grpcServer->start(config.grpcListenAddr, grpcErr)) {
            LOG_WARN("gRPC: %s", grpcErr.c_str());
            grpcServer.reset();
        }
    }
#endif

    // Restore saved sessions from SQLite (reconnect back-door links)
    sessionMgr.restoreAllSessions();

    LOG_INFO("Hydra ready");

    // ---- Event loop ----
    constexpr int MAX_EVENTS = 32;
    ganl::IoEvent events[MAX_EVENTS];

    while (!g_shutdown) {
        int n = engine->processEvents(100, events, MAX_EVENTS);

        for (int i = 0; i < n; i++) {
            const ganl::IoEvent& ev = events[i];

            switch (ev.type) {
            case ganl::IoEventType::Accept: {
                std::string clientIp = ev.remoteAddress.isValid()
                    ? ev.remoteAddress.toString() : "";

                // For TLS listeners, start the TLS handshake on the
                // accepted connection before proceeding to the protocol.
                bool isTls = (ev.context == &tagTelnetTls
                           || ev.context == &tagWebSocketTls
                           || ev.context == &tagGrpcWebTls);
                if (isTls && tlsTransport) {
                    if (!tlsTransport->createSessionContext(ev.connection, true)) {
                        LOG_WARN("TLS session context failed for %s", clientIp.c_str());
                        engine->closeConnection(ev.connection);
                        break;
                    }
                    // Mark in session manager that this front-door needs TLS
                    // The handshake will complete during subsequent Read events
                    // via processIncoming before data reaches the protocol layer.
                    sessionMgr.setFrontDoorTls(ev.connection, tlsTransport.get());
                }

                if (ev.context == &tagWebSocket || ev.context == &tagWebSocketTls) {
                    sessionMgr.onAcceptWebSocket(ev.connection, clientIp);
                } else if (ev.context == &tagGrpcWeb || ev.context == &tagGrpcWebTls) {
                    sessionMgr.onAcceptGrpcWeb(ev.connection, clientIp);
                } else {
                    sessionMgr.onAccept(ev.connection, clientIp);
                }
            } break;

            case ganl::IoEventType::ConnectSuccess:
                sessionMgr.onBackDoorConnect(ev.connection);
                break;

            case ganl::IoEventType::ConnectFail:
                sessionMgr.onBackDoorConnectFail(ev.connection, ev.error);
                break;

            case ganl::IoEventType::Read: {
                ganl::ConnectionHandle conn = ev.connection;
                char buf[4096];
#if defined(_WIN32)
                int nr = recv(static_cast<SOCKET>(conn), buf,
                              static_cast<int>(sizeof(buf)), 0);
#else
                ssize_t nr = recv(static_cast<int>(conn), buf,
                                  sizeof(buf), 0);
#endif
                if (nr <= 0) {
                    if (sessionMgr.isBackDoor(conn)) {
                        sessionMgr.onBackDoorClose(conn);
                    } else {
                        sessionMgr.onFrontDoorClose(conn);
                    }
                    engine->closeConnection(conn);
                    break;
                }

                if (sessionMgr.isBackDoor(conn)) {
                    sessionMgr.onBackDoorData(conn, buf,
                                              static_cast<size_t>(nr));
                } else {
                    sessionMgr.onFrontDoorData(conn, buf,
                                               static_cast<size_t>(nr));
                }
            } break;

            case ganl::IoEventType::Close:
            case ganl::IoEventType::Error: {
                ganl::ConnectionHandle conn = ev.connection;
                if (sessionMgr.isBackDoor(conn)) {
                    sessionMgr.onBackDoorClose(conn);
                } else {
                    sessionMgr.onFrontDoorClose(conn);
                }
                engine->closeConnection(conn);
            } break;

            default:
                break;
            }
        }

        sessionMgr.runTimers();
        sessionMgr.drainWsGameSessions();

#ifdef GRPC_ENABLED
        // Process work items posted by gRPC threads
        workQueue.processPending(sessionMgr, accounts, config,
                                 sessionMgr.processMgr());
#endif

        if (g_dumpStatus) {
            g_dumpStatus = 0;
            sessionMgr.dumpStatus();
        }
    }

    LOG_INFO("Hydra shutting down");
#ifdef GRPC_ENABLED
    if (grpcServer) grpcServer->shutdown();
#endif
    sessionMgr.shutdownSessions();
    engine->shutdown();
    accounts.shutdown();
    logShutdown();
    return 0;
}
