#ifndef HYDRA_CONFIG_H
#define HYDRA_CONFIG_H

#include "hydra_types.h"
#include <network_types.h>
#include <string>
#include <vector>

struct ListenConfig {
    std::string     host;       // bind address
    uint16_t        port;       // listen port
    bool            tls{false};        // TLS-wrapped
    bool            websocket{false};  // WebSocket protocol (HTTP upgrade)
    bool            grpcWeb{false};    // grpc-web protocol (HTTP/1.1 POST)
    std::string     certFile;   // TLS certificate path
    std::string     keyFile;    // TLS key path
};

struct GameConfig {
    std::string         name;
    GameTransport       transport{GameTransport::Tcp};
    std::string         host;           // for Tcp
    uint16_t            port{0};        // for Tcp
    std::string         socketPath;     // for Unix
    GameType            type{GameType::Remote};

    // Local game management
    std::string         binary;
    std::string         workdir;
    bool                autostart{false};

    // Reconnect policy
    bool                reconnect{true};
    std::vector<int>    retrySchedule;  // seconds between retries

    // Back-door TLS (tls_required defaults to yes — explicit opt-out needed)
    bool                tlsRequired{true};
    bool                tls{false};
    bool                tlsVerify{true};
    std::string         tlsCaFile;

    // Protocol
    ganl::EncodingType  charset{ganl::EncodingType::Utf8};
};

struct HydraConfig {
    // Network
    std::vector<ListenConfig>   listeners;

    // Database
    std::string                 databasePath{"hydra.sqlite"};

    // Master key
    std::string                 masterKeyPath;

    // Session defaults
    size_t                      scrollbackLines{10000};
    int                         sessionIdleTimeout{86400};      // 24h
    int                         detachedSessionTimeout{86400};  // 24h
    int                         linkReconnectTimeout{300};      // 5m

    // Front-door TLS policy
    bool                        allowPlaintext{false};     // allow non-TLS front-door connections

    // Resource limits
    int                         maxSessionsPerAccount{1};
    int                         maxFrontDoorsPerSession{3};
    int                         maxLinksPerSession{5};
    size_t                      maxScrollbackMemoryMb{64};
    int                         maxConnectionsPerIp{10};
    int                         connectRateLimit{5};        // per minute per IP
    int                         failedLoginLockout{5};      // failures before lockout
    int                         failedLoginLockoutMinutes{5};

    // CORS (for grpc-web)
    std::vector<std::string>    corsOrigins;     // allowed origins (empty = deny all)

    // gRPC (optional, requires --enable-grpc at configure time)
    std::string                 grpcListenAddr;  // e.g. "0.0.0.0:4204"

    // Logging
    std::string                 logFile{"hydra.log"};
    std::string                 logLevel{"info"};

    // Games
    std::vector<GameConfig>     games;
};

// Parse a configuration file.  Returns true on success.
// On failure, returns false and sets errorMsg.
bool loadConfig(const std::string& path, HydraConfig& config,
                std::string& errorMsg);

#endif // HYDRA_CONFIG_H
