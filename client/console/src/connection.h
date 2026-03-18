// connection.h -- IOCP-based async TCP connection with Telnet and Schannel TLS.
#ifndef CONNECTION_H
#define CONNECTION_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>

#include <string>
#include <deque>
#include <vector>
#include <cstdint>
#include <chrono>
#include "telnet.h"

// Completion key sentinel — overlapped completions use the Connection pointer
// as the key; this sentinel identifies console input events posted to the IOCP.
constexpr ULONG_PTR IOCP_KEY_CONSOLE = 0;
constexpr ULONG_PTR IOCP_KEY_TIMER   = 1;

// Per-I/O data attached to OVERLAPPED operations.
enum class IoOp { Read, Write, Connect };
struct IoContext {
    OVERLAPPED  overlapped;
    IoOp        op;
    WSABUF      wsabuf;
    char        buffer[8192];
};

class Connection {
public:
    Connection(const std::string& world_name, const std::string& host,
               const std::string& port, bool use_ssl, HANDLE iocp);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Begin async connect (posts completion to IOCP).
    bool begin_connect();

    // Called when IOCP signals a completion for this connection.
    // Returns lines received (may be empty).
    std::vector<std::string> on_completion(IoContext* ctx, DWORD bytes_transferred, DWORD error);

    void disconnect();
    bool is_connected() const { return socket_ != INVALID_SOCKET && connected_; }

    // Send a line (appends \r\n). Synchronous send for simplicity in Phase 1.
    bool send_line(const std::string& line);

    const std::string& world_name() const { return world_name_; }
    const std::string& host() const { return host_; }
    const std::string& port() const { return port_; }
    bool uses_ssl() const { return use_ssl_; }
    bool remote_echo() const { return remote_echo_; }

    void send_naws(uint16_t width, uint16_t height);

    // Prompt detection
    std::string check_prompt(int timeout_ms);
    bool has_partial_line() const { return !line_buf_.empty(); }

    // Scrollback
    const std::deque<std::string>& scrollback() const { return scrollback_; }
    void add_to_scrollback(const std::string& line);

    SOCKET socket() const { return socket_; }

private:
    // Begin an async read on the socket.
    bool begin_read();

    // Telnet processing
    void process_telnet(const unsigned char* data, size_t len,
                        std::vector<std::string>& lines_out);
    size_t process_data(const unsigned char* buf, size_t len,
                        std::vector<std::string>& lines_out);
    void send_telnet(uint8_t cmd, uint8_t opt);
    void send_subneg_ttype();
    void send_subneg_charset(bool accepted, const std::string& charset = "");
    void send_raw(const void* data, size_t len);

    std::string world_name_;
    std::string host_;
    std::string port_;
    bool use_ssl_;
    HANDLE iocp_;

    SOCKET socket_ = INVALID_SOCKET;
    bool connected_ = false;

    IoContext read_ctx_;
    IoContext connect_ctx_;

    // Line accumulator
    std::string line_buf_;
    std::chrono::steady_clock::time_point line_buf_time_;

    // Scrollback
    std::deque<std::string> scrollback_;
    static constexpr size_t MAX_SCROLLBACK = 10000;

    // Telnet state machine
    enum class TelState { DATA, IAC, WILL, WONT, DO, DONT, SB, SB_DATA, SB_IAC };
    TelState tel_state_ = TelState::DATA;
    uint8_t sb_option_ = 0;
    std::string sb_buf_;

    bool remote_echo_ = false;

    // NAWS
    uint16_t naws_width_ = 80;
    uint16_t naws_height_ = 24;
    bool naws_agreed_ = false;

    // ConnectEx function pointer (loaded once per socket)
    static LPFN_CONNECTEX ConnectEx_;
    static bool LoadConnectEx(SOCKET s);
};

#endif // CONNECTION_H
