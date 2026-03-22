#include "config.h"
#include "hydra_log.h"
#include "front_door.h"
#include "back_door.h"
#include "session_manager.h"
#include "account_manager.h"

#include <network_engine.h>
#include <network_types.h>

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

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

    // Ignore SIGPIPE (handled via write errors)
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

    // Register GANL log callback
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
        // Read password from terminal
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

    // Install signal handlers
    installSignals();

    // Create GANL network engine (auto-detect best model)
    // TODO: Use NetworkEngineFactory or create directly
    LOG_INFO("Initializing network engine");

    // For now, create a placeholder.  The actual engine creation
    // requires the GANL factory which we need to integrate.
    // This is where the event loop will live.

    LOG_INFO("Hydra ready");
    LOG_INFO("Configured games:");
    for (const auto& game : config.games) {
        LOG_INFO("  %s -> %s:%u (%s)", game.name.c_str(),
            game.host.c_str(), game.port,
            game.tls ? "TLS" : "plain");
    }
    LOG_INFO("Configured listeners:");
    for (const auto& lc : config.listeners) {
        LOG_INFO("  %s:%u (%s)", lc.host.c_str(), lc.port,
            lc.tls ? "TLS" : "plain");
    }

    // TODO: Event loop
    // while (!g_shutdown) {
    //     int n = networkEngine->processEvents(100, events, MAX_EVENTS);
    //     for (int i = 0; i < n; i++) {
    //         if (frontDoor.handleEvent(events[i])) continue;
    //         if (backDoor.handleEvent(events[i])) continue;
    //     }
    //     sessionManager.runTimers();
    //     if (g_reload) { g_reload = 0; /* reload config */ }
    //     if (g_dumpStatus) { g_dumpStatus = 0; /* log status */ }
    // }

    LOG_INFO("Hydra shutting down");
    accounts.shutdown();
    logShutdown();
    return 0;
}
