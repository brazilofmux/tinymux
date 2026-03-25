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
    WebSocket,
    WsGameSession,  // WebSocket carrying protobuf GameSession
    GrpcWeb,
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

// Client color depth capability
enum class ColorDepth {
    None,       // strip all color
    Ansi16,     // basic 16-color SGR
    Ansi256,    // xterm-256
    TrueColor,  // 24-bit RGB
};

#endif // HYDRA_TYPES_H
