#ifndef GANL_NETWORK_TYPES_H
#define GANL_NETWORK_TYPES_H

#include <cstdint>
#include <string>

namespace ganl {

// Handle types
using ConnectionHandle = uintptr_t;
using ListenerHandle = uintptr_t;
using SessionId = uint64_t;

constexpr ConnectionHandle InvalidConnectionHandle = 0;
constexpr ListenerHandle InvalidListenerHandle = 0;
constexpr SessionId InvalidSessionId = 0;

// Error code type
using ErrorCode = int;

// Event types
enum class IoEventType {
    None,
    Accept,
    ConnectSuccess,
    ConnectFail,
    Read,
    Write,
    Close,
    Error
};

// Event structure
struct IoEvent {
    ConnectionHandle connection{InvalidConnectionHandle};
    ListenerHandle listener{InvalidListenerHandle};
    IoEventType type{IoEventType::None};
    size_t bytesTransferred{0};
    ErrorCode error{0};
    void* context{nullptr};
};

// TLS result type
enum class TlsResult {
    Success,
    WantRead,
    WantWrite,
    Error,
    Closed
};

// TLS configuration
struct TlsConfig {
    std::string certificateFile;
    std::string keyFile;
    std::string password;
    bool verifyPeer{false};
};

// Session state
enum class SessionState {
    Connecting,
    Handshaking,
    Authenticating,
    Connected,
    Closing,
    Closed
};

// Connection state
enum class ConnectionState {
    Initializing,
    TlsHandshaking,
    TelnetNegotiating,
    Running,
    Closing,
    Closed
};

// Disconnect reasons
enum class DisconnectReason {
    Unknown,
    UserQuit,
    Timeout,
    NetworkError,
    ProtocolError,
    TlsError,
    ServerShutdown,
    AdminKick,
    GameFull,
    LoginFailed
};

enum class EncodingType {
    ASCII,
    Latin1,
    UTF8,
    CP437,
    CP1252
};

struct ProtocolState {
    bool telnetBinary{false};
    bool telnetEcho{false};
    bool telnetSGA{false};
    bool telnetEOR{false};
    EncodingType encoding{EncodingType::ASCII};
    bool supportsANSI{false};
    bool supportsMXP{false};
    uint16_t width{80};
    uint16_t height{24};
};

enum class IoModel {
    Unknown,
    Readiness,  // select, poll, epoll, kqueue
    Completion  // IOCP, io_uring (potentially)
};

} // namespace ganl

#endif // GANL_NETWORK_TYPES_H
