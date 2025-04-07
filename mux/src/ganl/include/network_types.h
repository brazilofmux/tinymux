#ifndef GANL_NETWORK_TYPES_H
#define GANL_NETWORK_TYPES_H

#include <cstdint>
#include <string>

// Include system-specific socket headers
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif

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

// Network address class for raw socket address access
class NetworkAddress {
public:
    enum class Family { IPv4, IPv6 };

    // Default constructor creates an invalid/empty address
    NetworkAddress() = default;

    // Construct from sockaddr (makes a copy of the address)
    NetworkAddress(const struct sockaddr* addr, socklen_t addrLen);

    // Get address family
    Family getFamily() const;

    // Get string representation (IP:port format)
    std::string toString() const;

    // Raw access methods
    const struct sockaddr* getSockAddr() const;
    socklen_t getSockAddrLen() const;

    // Basic convenience method
    uint16_t getPort() const;

    // Check if address is valid
    bool isValid() const;

private:
    // Use sockaddr_storage to store any type of address
    struct sockaddr_storage storage_{};
    socklen_t addrLen_{0};
    bool valid_{false};
};

// Event structure
struct IoEvent {
    ConnectionHandle connection{InvalidConnectionHandle};
    ListenerHandle listener{InvalidListenerHandle};
    IoEventType type{IoEventType::None};
    size_t bytesTransferred{0};
    ErrorCode error{0};
    void* context{nullptr};
    class IoBuffer* buffer{nullptr}; // Reference to the buffer used for I/O operation
    NetworkAddress remoteAddress;    // Remote network address for Accept events
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

// Use different names to avoid conflict with TinyMUX macros
enum class EncodingType {
    Ascii,    // Was ASCII - renamed to avoid conflict with TinyMUX ASCII macro
    Latin1,
    Utf8,     // Was UTF8 - renamed to match TinyMUX style
    Cp437,    // Was CP437 - renamed to match style
    Cp1252    // Was CP1252 - renamed to match style
};

struct ProtocolState {
    bool telnetBinary{false};
    bool telnetEcho{false};
    bool telnetSGA{false};
    bool telnetEOR{false};
    EncodingType encoding{EncodingType::Ascii}; // Updated to use renamed enum value
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
