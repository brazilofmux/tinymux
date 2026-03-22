#include "config.h"
#include "hydra_log.h"
#include "front_door.h"
#include "back_door.h"
#include "session_manager.h"
#include "account_manager.h"

#include <network_engine.h>
#include <network_engine_factory.h>
#include <network_types.h>
#include <io_buffer.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
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

// ---- Dumb pipe: bidirectional byte shuttle ----
//
// For Step 5, we skip authentication, sessions, telnet negotiation,
// and color translation.  Each front-door connection is paired with
// one back-door connection.  Bytes flow transparently.

struct PipeEntry {
    ganl::ConnectionHandle partner;  // The other end
    bool isFrontDoor;                // true = front-door, false = back-door
};

// Map from connection handle → pipe entry
static std::map<ganl::ConnectionHandle, PipeEntry> g_pipes;

// Map from front-door handle → pending (not yet connected) back-door handle
static std::map<ganl::ConnectionHandle, ganl::ConnectionHandle> g_pending;

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
    LOG_INFO("Hydra starting (dumb pipe mode)");

    ganl::setLogger(hydraGanlLogCallback);

    // Handle --create-admin (not used in dumb pipe mode, but keep for later)
    if (!createAdmin.empty()) {
        AccountManager accounts;
        if (!accounts.initialize(config.databasePath, errorMsg)) {
            fprintf(stderr, "HYDRA: db error: %s\n", errorMsg.c_str());
            return 1;
        }
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

    // ---- Initialize GANL ----
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

    // We need at least one game configured
    if (config.games.empty()) {
        LOG_ERROR("No games configured");
        engine->shutdown();
        logShutdown();
        return 1;
    }
    const GameConfig& game = config.games[0];
    LOG_INFO("Target game: %s -> %s:%u", game.name.c_str(),
             game.host.c_str(), game.port);

    // ---- Create front-door listener (plain telnet for now) ----
    // Find the first plain (non-TLS) listener, or use the first one
    const ListenConfig* listenCfg = nullptr;
    for (const auto& lc : config.listeners) {
        if (!lc.tls) {
            listenCfg = &lc;
            break;
        }
    }
    if (!listenCfg) {
        // Use first listener regardless
        listenCfg = &config.listeners[0];
        LOG_WARN("No plain listener configured, using first listener "
                 "(TLS not implemented in dumb pipe mode)");
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

    int tag = 1;  // listener context
    if (!engine->startListening(listener, &tag, err)) {
        LOG_ERROR("Failed to start listening: %s", strerror(err));
        engine->shutdown();
        logShutdown();
        return 1;
    }
    LOG_INFO("Listening on %s:%u (plain telnet)",
             listenCfg->host.c_str(), listenCfg->port);

    // ---- Event loop ----
    constexpr int MAX_EVENTS = 32;
    ganl::IoEvent events[MAX_EVENTS];

    LOG_INFO("Hydra ready — dumb pipe to %s:%u",
             game.host.c_str(), game.port);

    while (!g_shutdown) {
        int n = engine->processEvents(100, events, MAX_EVENTS);

        for (int i = 0; i < n; i++) {
            const ganl::IoEvent& ev = events[i];

            switch (ev.type) {
            case ganl::IoEventType::Accept: {
                ganl::ConnectionHandle fdConn = ev.connection;
                LOG_INFO("Front-door accept: handle %lu",
                         (unsigned long)fdConn);

                // Initiate back-door connection to game
                ganl::ErrorCode connectErr = 0;
                ganl::ConnectionHandle bdConn = engine->initiateConnect(
                    game.host, game.port, nullptr, connectErr);

                if (bdConn == ganl::InvalidConnectionHandle) {
                    LOG_ERROR("Failed to connect to %s:%u: %s",
                              game.host.c_str(), game.port,
                              strerror(connectErr));
                    engine->closeConnection(fdConn);
                    break;
                }

                // Store as pending until connect completes
                g_pending[fdConn] = bdConn;
                // Store back-door → front-door mapping for ConnectSuccess
                g_pipes[bdConn] = PipeEntry{fdConn, false};
                LOG_INFO("Back-door connecting: handle %lu -> %s:%u",
                         (unsigned long)bdConn, game.host.c_str(),
                         game.port);
            } break;

            case ganl::IoEventType::ConnectSuccess: {
                ganl::ConnectionHandle bdConn = ev.connection;
                LOG_INFO("Back-door connected: handle %lu",
                         (unsigned long)bdConn);

                // Find the front-door partner
                auto it = g_pipes.find(bdConn);
                if (it == g_pipes.end()) break;
                ganl::ConnectionHandle fdConn = it->second.partner;

                // Complete the pipe: both directions
                g_pipes[fdConn] = PipeEntry{bdConn, true};
                g_pipes[bdConn] = PipeEntry{fdConn, false};
                g_pending.erase(fdConn);

                LOG_INFO("Pipe established: fd=%lu <-> bd=%lu",
                         (unsigned long)fdConn, (unsigned long)bdConn);
            } break;

            case ganl::IoEventType::ConnectFail: {
                ganl::ConnectionHandle bdConn = ev.connection;
                LOG_ERROR("Back-door connect failed: handle %lu err=%d (%s)",
                          (unsigned long)bdConn, ev.error,
                          strerror(ev.error));

                // Find and close the front-door partner
                auto it = g_pipes.find(bdConn);
                if (it != g_pipes.end()) {
                    ganl::ConnectionHandle fdConn = it->second.partner;
                    g_pending.erase(fdConn);
                    g_pipes.erase(fdConn);
                    engine->closeConnection(fdConn);
                }
                g_pipes.erase(bdConn);
            } break;

            case ganl::IoEventType::Read: {
                ganl::ConnectionHandle conn = ev.connection;

                // Read data from the socket
                char buf[4096];
                ssize_t nr = recv(static_cast<int>(conn), buf,
                                  sizeof(buf), 0);
                if (nr <= 0) {
                    // Connection closed or error
                    if (nr == 0) {
                        LOG_INFO("Connection closed: handle %lu",
                                 (unsigned long)conn);
                    } else {
                        LOG_INFO("Read error on handle %lu: %s",
                                 (unsigned long)conn, strerror(errno));
                    }

                    // Close partner too
                    auto it = g_pipes.find(conn);
                    if (it != g_pipes.end()) {
                        ganl::ConnectionHandle partner = it->second.partner;
                        g_pipes.erase(partner);
                        engine->closeConnection(partner);
                    }
                    g_pipes.erase(conn);
                    engine->closeConnection(conn);
                    break;
                }

                // Forward to partner
                auto it = g_pipes.find(conn);
                if (it != g_pipes.end()) {
                    ganl::ConnectionHandle partner = it->second.partner;
                    // Direct write to partner fd
                    ssize_t nw = send(static_cast<int>(partner), buf,
                                      static_cast<size_t>(nr), MSG_NOSIGNAL);
                    if (nw < 0) {
                        LOG_INFO("Write error to partner %lu: %s",
                                 (unsigned long)partner, strerror(errno));
                        // Close both sides
                        g_pipes.erase(partner);
                        g_pipes.erase(conn);
                        engine->closeConnection(partner);
                        engine->closeConnection(conn);
                    }
                }
            } break;

            case ganl::IoEventType::Close:
            case ganl::IoEventType::Error: {
                ganl::ConnectionHandle conn = ev.connection;
                LOG_INFO("Connection %s: handle %lu",
                         ev.type == ganl::IoEventType::Close
                             ? "closed" : "error",
                         (unsigned long)conn);

                auto it = g_pipes.find(conn);
                if (it != g_pipes.end()) {
                    ganl::ConnectionHandle partner = it->second.partner;
                    g_pipes.erase(partner);
                    engine->closeConnection(partner);
                }
                g_pipes.erase(conn);
                engine->closeConnection(conn);
            } break;

            default:
                break;
            }
        }

        if (g_dumpStatus) {
            g_dumpStatus = 0;
            LOG_INFO("Status: %zu active pipes", g_pipes.size() / 2);
        }
    }

    LOG_INFO("Hydra shutting down");
    engine->shutdown();
    logShutdown();
    return 0;
}
