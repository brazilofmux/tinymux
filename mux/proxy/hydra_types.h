#ifndef HYDRA_TYPES_H
#define HYDRA_TYPES_H

// Forward declarations and common types for Hydra.

#include <network_types.h>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

// Hydra session ID (distinct from GANL SessionId)
using HydraSessionId = uint64_t;
constexpr HydraSessionId InvalidHydraSessionId = 0;

// Front-door protocol type
enum class FrontDoorProto {
    Telnet,
    // WebSocket,  // Phase 3
    // Grpc,       // Phase 3
};

// Back-door link state
enum class LinkState {
    Connecting,
    TlsHandshaking,
    Negotiating,
    AutoLoggingIn,
    Active,
    Reconnecting,
    Suspended,
    Dead,
};

// Session state
enum class SessionState {
    Login,
    Active,
    Detached,
};

// Game transport type
enum class GameTransport {
    Tcp,
    Unix,
};

// Game type
enum class GameType {
    Local,
    Remote,
};

#endif // HYDRA_TYPES_H
