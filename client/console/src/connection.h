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
#include <memory>
#include <unordered_map>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include "iconnection.h"
#include "telnet.h"
#include "schannel_tls.h"

// Completion key sentinels for the IOCP event loop.
constexpr ULONG_PTR IOCP_KEY_CONSOLE = 0;
constexpr ULONG_PTR IOCP_KEY_TIMER   = 1;
constexpr ULONG_PTR IOCP_KEY_HYDRA   = 2;

// Per-I/O data attached to OVERLAPPED operations.
enum class IoOp { Read, Write, Connect };
struct IoContext {
    OVERLAPPED  overlapped;
    IoOp        op;
    WSABUF      wsabuf;
    char        buffer[8192];
};

class Connection : public IConnection {
public:
    Connection(const std::string& world_name, const std::string& host,
               const std::string& port, bool use_ssl, HANDLE iocp);
    ~Connection() override;

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Begin async connect (posts completion to IOCP).
    bool begin_connect();

    // Called when IOCP signals a completion for this connection.
    // Returns lines received (may be empty).
    std::vector<std::string> on_completion(IoContext* ctx, DWORD bytes_transferred, DWORD error);

    // IConnection interface
    void disconnect() override;
    bool is_connected() const override { return socket_ != INVALID_SOCKET && connected_; }
    bool send_line(const std::string& line) override;

    const std::string& world_name() const override { return world_name_; }
    const std::string& host() const override { return host_; }
    const std::string& port() const override { return port_; }
    bool uses_ssl() const override { return use_ssl_; }
    bool remote_echo() const override { return remote_echo_; }

    void send_naws(uint16_t width, uint16_t height) override;

    std::string check_prompt(int timeout_ms) override;
    bool has_partial_line() const override { return !line_buf_.empty(); }

    const std::deque<std::string>& scrollback() const override { return scrollback_; }
    void add_to_scrollback(const std::string& line) override;

    int idle_secs() const override;
    int send_idle_secs() const override;

    // GMCP
    const std::unordered_map<std::string, std::string>& gmcp_data() const { return gmcp_; }
    const std::string& gmcp_get(const std::string& pkg) const;

    // MSSP
    const std::unordered_map<std::string, std::string>& mssp_data() const { return mssp_; }

    // Prompt
    const std::string& current_prompt() const { return last_prompt_; }

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

    // TLS
    std::unique_ptr<SchannelSession> tls_;

    // GMCP data (package -> last JSON payload)
    std::unordered_map<std::string, std::string> gmcp_;
    static const std::string empty_string_;

    // MSSP data (key -> value)
    std::unordered_map<std::string, std::string> mssp_;

    // Subneg handlers
    void handle_gmcp_subneg(const std::string& data);
    void handle_mssp_subneg(const std::string& data);

    // Idle tracking
    std::chrono::steady_clock::time_point last_recv_time_;
    std::chrono::steady_clock::time_point last_send_time_;

    // Prompt
    std::string last_prompt_;

    // ConnectEx function pointer (loaded once per socket)
    static LPFN_CONNECTEX ConnectEx_;
    static bool LoadConnectEx(SOCKET s);
};

#endif // CONNECTION_H
