#ifndef CONNECTION_H
#define CONNECTION_H

#include <string>
#include <deque>
#include <cstdint>
#include <vector>
#include <chrono>
#include <openssl/ssl.h>

class Connection {
public:
    Connection(const std::string& world_name, const std::string& host,
               const std::string& port, bool use_ssl);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    // Connect to the remote host. Returns true on success.
    bool connect();
    void disconnect();
    bool is_connected() const { return fd_ >= 0; }

    // Send a line (appends \r\n)
    bool send_line(const std::string& line);

    // Read available data. Returns complete lines accumulated.
    // Call when select() indicates fd is readable.
    std::vector<std::string> read_lines();

    int fd() const { return fd_; }
    const std::string& world_name() const { return world_name_; }
    const std::string& host() const { return host_; }
    const std::string& port() const { return port_; }
    bool uses_ssl() const { return use_ssl_; }
    bool remote_echo() const { return remote_echo_; }

    // Notify the MUD of terminal size change
    void send_naws(uint16_t width, uint16_t height);

    // Prompt detection: check if line_buf_ has a partial line older than
    // the timeout.  If so, return it as a prompt and clear the buffer.
    // Returns empty string if no prompt is pending.
    std::string check_prompt(std::chrono::milliseconds timeout);
    std::string current_prompt() const;

    // True if there's unflushed partial data in line_buf_
    bool has_partial_line() const { return !line_buf_.empty(); }

    // Scrollback
    const std::deque<std::string>& scrollback() const { return scrollback_; }
    void add_to_scrollback(const std::string& line);

private:
    // Telnet negotiation
    void process_telnet(const unsigned char* data, size_t len);
    void send_telnet(uint8_t cmd, uint8_t opt);
    void send_subneg_ttype();
    void send_subneg_charset(bool accepted, const std::string& charset = "");
    void send_subneg_naws(uint16_t width, uint16_t height);
    size_t process_data(const unsigned char* buf, size_t len);

    bool ssl_connect();
    bool write_all(const void* data, size_t len);

    std::string world_name_;
    std::string host_;
    std::string port_;
    bool use_ssl_;

    int fd_ = -1;
    SSL_CTX* ssl_ctx_ = nullptr;
    SSL* ssl_ = nullptr;

    // Line accumulator
    std::string line_buf_;
    std::chrono::steady_clock::time_point line_buf_time_;  // when data last arrived
    std::string last_prompt_;  // last flushed prompt (to avoid re-displaying)

    // Scrollback
    std::deque<std::string> scrollback_;
    static constexpr size_t MAX_SCROLLBACK = 10000;

    // Telnet state machine
    enum class TelState { DATA, IAC, WILL, WONT, DO, DONT, SB, SB_DATA, SB_IAC };
    TelState tel_state_ = TelState::DATA;
    uint8_t sb_option_ = 0;
    std::string sb_buf_;

    // Whether remote has requested echo suppression
    bool remote_echo_ = false;

    // Idle tracking
    std::chrono::steady_clock::time_point last_recv_time_;
    std::chrono::steady_clock::time_point last_send_time_;
public:
    int idle_secs() const {
        auto now = std::chrono::steady_clock::now();
        return (int)std::chrono::duration_cast<std::chrono::seconds>(now - last_recv_time_).count();
    }
    int sidle_secs() const {
        auto now = std::chrono::steady_clock::now();
        return (int)std::chrono::duration_cast<std::chrono::seconds>(now - last_send_time_).count();
    }
private:

    // Terminal size for NAWS (updated by send_naws)
    uint16_t naws_width_ = 80;
    uint16_t naws_height_ = 24;
    bool naws_agreed_ = false;  // true if MUD sent DO NAWS
};

#endif // CONNECTION_H
