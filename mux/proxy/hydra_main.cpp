#include "config.h"
#include "hydra_log.h"
#include "session_manager.h"
#include "account_manager.h"

#include <network_engine.h>
#include <network_engine_factory.h>
#include <network_types.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>
#include <sys/socket.h>

static volatile sig_atomic_t g_shutdown = 0;
static volatile sig_atomic_t g_reload = 0;
static volatile sig_atomic_t g_dumpStatus = 0;

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

    // Handle --create-admin
    if (!createAdmin.empty()) {
        char* pw = getpass("Password: ");
        if (!pw || strlen(pw) == 0) {
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

    // Create front-door listener (plain telnet for now)
    const ListenConfig* listenCfg = nullptr;
    for (const auto& lc : config.listeners) {
        if (!lc.tls) {
            listenCfg = &lc;
            break;
        }
    }
    if (!listenCfg && !config.listeners.empty()) {
        listenCfg = &config.listeners[0];
    }
    if (!listenCfg) {
        LOG_ERROR("No listeners configured");
        engine->shutdown();
        logShutdown();
        return 1;
    }

    ganl::ErrorCode err = 0;
    ganl::ListenerHandle listener = engine->createListener(
        listenCfg->host, listenCfg->port, err);
    if (listener == ganl::InvalidListenerHandle) {
        LOG_ERROR("Failed to create listener on %s:%u: %s",
                  listenCfg->host.c_str(), listenCfg->port, strerror(err));
        engine->shutdown();
        logShutdown();
        return 1;
    }

    int tag = 1;
    if (!engine->startListening(listener, &tag, err)) {
        LOG_ERROR("Failed to start listening: %s", strerror(err));
        engine->shutdown();
        logShutdown();
        return 1;
    }
    LOG_INFO("Listening on %s:%u", listenCfg->host.c_str(), listenCfg->port);

    // Log configured games
    for (const auto& game : config.games) {
        LOG_INFO("Game: %s -> %s:%u", game.name.c_str(),
                 game.host.c_str(), game.port);
    }

    LOG_INFO("Hydra ready");

    // ---- Event loop ----
    constexpr int MAX_EVENTS = 32;
    ganl::IoEvent events[MAX_EVENTS];

    while (!g_shutdown) {
        int n = engine->processEvents(100, events, MAX_EVENTS);

        for (int i = 0; i < n; i++) {
            const ganl::IoEvent& ev = events[i];

            switch (ev.type) {
            case ganl::IoEventType::Accept:
                sessionMgr.onAccept(ev.connection);
                break;

            case ganl::IoEventType::ConnectSuccess:
                sessionMgr.onBackDoorConnect(ev.connection);
                break;

            case ganl::IoEventType::ConnectFail:
                sessionMgr.onBackDoorConnectFail(ev.connection, ev.error);
                break;

            case ganl::IoEventType::Read: {
                ganl::ConnectionHandle conn = ev.connection;
                char buf[4096];
                ssize_t nr = recv(static_cast<int>(conn), buf,
                                  sizeof(buf), 0);
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

        if (g_dumpStatus) {
            g_dumpStatus = 0;
            LOG_INFO("Status dump requested (TODO)");
        }
    }

    LOG_INFO("Hydra shutting down");
    engine->shutdown();
    accounts.shutdown();
    logShutdown();
    return 0;
}
