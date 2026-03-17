#include "connection.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <vector>
#include <poll.h>

// Return the number of trailing bytes that form an incomplete UTF-8 sequence.
// If the string ends with a valid complete sequence, returns 0.
static size_t utf8_incomplete_tail(const std::string& s) {
    if (s.empty()) return 0;
    // Walk backwards over continuation bytes (10xxxxxx)
    size_t i = s.size();
    size_t conts = 0;
    while (i > 0 && (static_cast<unsigned char>(s[i - 1]) & 0xC0) == 0x80) {
        i--;
        conts++;
        if (conts > 3) return 0; // not valid UTF-8, don't touch
    }
    if (i == 0) return 0; // all continuation bytes with no leader — don't touch
    unsigned char lead = static_cast<unsigned char>(s[i - 1]);
    size_t expected;
    if (lead < 0x80) return 0;       // ASCII, complete
    else if (lead < 0xC0) return 0;  // stray continuation, don't touch
    else if (lead < 0xE0) expected = 2;
    else if (lead < 0xF0) expected = 3;
    else expected = 4;
    size_t have = 1 + conts; // leader + continuations
    if (have < expected) return have; // incomplete
    return 0; // complete
}

// Telnet protocol bytes
static constexpr uint8_t TEL_IAC  = 255;
static constexpr uint8_t TEL_DONT = 254;
static constexpr uint8_t TEL_DO   = 253;
static constexpr uint8_t TEL_WONT = 252;
static constexpr uint8_t TEL_WILL = 251;
static constexpr uint8_t TEL_SB   = 250;
static constexpr uint8_t TEL_SE   = 240;

// Telnet options
static constexpr uint8_t TELOPT_ECHO    = 1;
static constexpr uint8_t TELOPT_SGA     = 3;
static constexpr uint8_t TELOPT_TTYPE   = 24;
static constexpr uint8_t TELOPT_NAWS    = 31;

// Subnegotiation
static constexpr uint8_t TELQUAL_IS   = 0;
static constexpr uint8_t TELQUAL_SEND = 1;

static bool wait_for_fd(int fd, short events, int timeout_ms) {
    struct pollfd pfd{};
    pfd.fd = fd;
    pfd.events = events;

    while (true) {
        int rc = poll(&pfd, 1, timeout_ms);
        if (rc > 0) return (pfd.revents & (events | POLLERR | POLLHUP)) != 0;
        if (rc == 0) return false;
        if (errno != EINTR) return false;
    }
}

Connection::Connection(const std::string& world_name, const std::string& host,
                       const std::string& port, bool use_ssl)
    : world_name_(world_name), host_(host), port_(port), use_ssl_(use_ssl)
{}

Connection::~Connection() {
    disconnect();
}

bool Connection::connect() {
    struct addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    int err = getaddrinfo(host_.c_str(), port_.c_str(), &hints, &res);
    if (err != 0 || !res) return false;

    for (auto* rp = res; rp; rp = rp->ai_next) {
        fd_ = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd_ < 0) continue;

        int flags = fcntl(fd_, F_GETFL, 0);
        if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(fd_);
            fd_ = -1;
            continue;
        }

        if (::connect(fd_, rp->ai_addr, rp->ai_addrlen) == 0) break;

        if (errno == EINPROGRESS && wait_for_fd(fd_, POLLOUT, 5000)) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            if (getsockopt(fd_, SOL_SOCKET, SO_ERROR, &so_error, &len) == 0 && so_error == 0) {
                break;
            }
        }

        close(fd_);
        fd_ = -1;
    }
    freeaddrinfo(res);

    if (fd_ < 0) return false;

    if (use_ssl_) {
        if (!ssl_connect()) {
            close(fd_);
            fd_ = -1;
            return false;
        }
    }

    auto now = std::chrono::steady_clock::now();
    last_recv_time_ = now;
    last_send_time_ = now;
    return true;
}

bool Connection::ssl_connect() {
    ssl_ctx_ = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx_) return false;

    // Don't verify certs for MUD connections (matches TF behavior)
    SSL_CTX_set_verify(ssl_ctx_, SSL_VERIFY_NONE, nullptr);

    ssl_ = SSL_new(ssl_ctx_);
    if (!ssl_) return false;

    SSL_set_fd(ssl_, fd_);

    for (;;) {
        int ret = SSL_connect(ssl_);
        if (ret == 1) return true;

        int err = SSL_get_error(ssl_, ret);
        if (err == SSL_ERROR_WANT_READ) {
            if (!wait_for_fd(fd_, POLLIN, 5000)) break;
            continue;
        }
        if (err == SSL_ERROR_WANT_WRITE) {
            if (!wait_for_fd(fd_, POLLOUT, 5000)) break;
            continue;
        }
        break;
    }

    SSL_free(ssl_); ssl_ = nullptr;
    SSL_CTX_free(ssl_ctx_); ssl_ctx_ = nullptr;
    return false;
}

void Connection::disconnect() {
    if (ssl_) { SSL_shutdown(ssl_); SSL_free(ssl_); ssl_ = nullptr; }
    if (ssl_ctx_) { SSL_CTX_free(ssl_ctx_); ssl_ctx_ = nullptr; }
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
    tel_state_ = TelState::DATA;
    line_buf_.clear();
}

bool Connection::send_line(const std::string& line) {
    if (fd_ < 0) return false;
    std::string data = line + "\r\n";
    const char* p = data.c_str();
    size_t remaining = data.size();

    while (remaining > 0) {
        ssize_t n;
        if (ssl_) {
            n = SSL_write(ssl_, p, (int)remaining);
        } else {
            n = write(fd_, p, remaining);
        }
        if (n <= 0) return false;
        p += n;
        remaining -= n;
    }
    last_send_time_ = std::chrono::steady_clock::now();
    return true;
}

std::vector<std::string> Connection::read_lines() {
    std::vector<std::string> lines;
    if (fd_ < 0) return lines;

    unsigned char buf[4096];
    ssize_t n;

    if (ssl_) {
        n = SSL_read(ssl_, buf, sizeof(buf));
    } else {
        n = read(fd_, buf, sizeof(buf));
    }

    if (n <= 0) {
        if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
            disconnect();
        }
        return lines;
    }

    // Process through telnet state machine, accumulate lines
    size_t pos = 0;
    size_t buf_before = line_buf_.size();
    while (pos < (size_t)n) {
        pos += process_data(buf + pos, n - pos);
    }

    // If new data arrived in line_buf_, record timestamps
    if (line_buf_.size() > buf_before) {
        auto now = std::chrono::steady_clock::now();
        line_buf_time_ = now;
        last_recv_time_ = now;
    }

    // If we got a newline, the prompt (if displayed) was superseded — clear it
    // so the full line displays normally.
    // (We check after extraction below.)

    // Extract complete lines from line_buf_, protecting incomplete UTF-8.
    //
    // A line boundary is at '\n'. After extracting all complete lines,
    // the remainder stays in line_buf_. Before emitting any line, verify
    // it doesn't end with a truncated UTF-8 sequence — if it does, the
    // split landed inside a multi-byte char and we need to push those
    // trailing bytes back into line_buf_ for the next read.
    size_t start = 0;
    for (size_t i = 0; i < line_buf_.size(); i++) {
        if (line_buf_[i] == '\n') {
            std::string line = line_buf_.substr(start, i - start);
            // Strip trailing \r
            if (!line.empty() && line.back() == '\r') line.pop_back();
            lines.push_back(std::move(line));
            start = i + 1;
        }
    }
    if (start > 0) {
        line_buf_.erase(0, start);
    }

    // If we extracted complete lines, any previously displayed prompt
    // was part of one of them — clear it to avoid duplication.
    if (!lines.empty()) {
        last_prompt_.clear();
    }

    // Guard the remainder in line_buf_: if it ends mid-UTF-8-sequence,
    // that's fine — it stays until more bytes arrive. But also check
    // the last emitted line for a truncated trailing sequence and move
    // those bytes back into line_buf_ if needed.
    if (!lines.empty()) {
        std::string& last = lines.back();
        size_t trim = utf8_incomplete_tail(last);
        if (trim > 0) {
            // Move the incomplete bytes to the front of line_buf_
            line_buf_.insert(0, last, last.size() - trim, trim);
            last.erase(last.size() - trim);
        }
    }

    return lines;
}

size_t Connection::process_data(const unsigned char* buf, size_t len) {
    size_t consumed = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t b = buf[i];
        consumed++;

        switch (tel_state_) {
        case TelState::DATA:
            if (b == TEL_IAC) {
                tel_state_ = TelState::IAC;
            } else {
                line_buf_ += (char)b;
            }
            break;

        case TelState::IAC:
            switch (b) {
            case TEL_WILL: tel_state_ = TelState::WILL; break;
            case TEL_WONT: tel_state_ = TelState::WONT; break;
            case TEL_DO:   tel_state_ = TelState::DO;   break;
            case TEL_DONT: tel_state_ = TelState::DONT; break;
            case TEL_SB:   tel_state_ = TelState::SB;   break;
            case TEL_IAC:  line_buf_ += (char)255; tel_state_ = TelState::DATA; break;
            default:       tel_state_ = TelState::DATA; break;
            }
            break;

        case TelState::WILL:
            if (b == TELOPT_ECHO) {
                remote_echo_ = true;
                send_telnet(TEL_DO, b);
            } else if (b == TELOPT_SGA) {
                send_telnet(TEL_DO, b);
            } else {
                send_telnet(TEL_DONT, b);
            }
            tel_state_ = TelState::DATA;
            break;

        case TelState::WONT:
            if (b == TELOPT_ECHO) remote_echo_ = false;
            send_telnet(TEL_DONT, b);
            tel_state_ = TelState::DATA;
            break;

        case TelState::DO:
            if (b == TELOPT_TTYPE) {
                send_telnet(TEL_WILL, b);
            } else if (b == TELOPT_NAWS) {
                send_telnet(TEL_WILL, b);
                naws_agreed_ = true;
                send_subneg_naws(naws_width_, naws_height_);
            } else {
                send_telnet(TEL_WONT, b);
            }
            tel_state_ = TelState::DATA;
            break;

        case TelState::DONT:
            send_telnet(TEL_WONT, b);
            tel_state_ = TelState::DATA;
            break;

        case TelState::SB:
            sb_option_ = b;
            sb_buf_.clear();
            tel_state_ = TelState::SB_DATA;
            break;

        case TelState::SB_DATA:
            if (b == TEL_IAC) {
                tel_state_ = TelState::SB_IAC;
            } else {
                sb_buf_ += (char)b;
            }
            break;

        case TelState::SB_IAC:
            if (b == TEL_SE) {
                // Process subnegotiation
                if (sb_option_ == TELOPT_TTYPE && !sb_buf_.empty() && (uint8_t)sb_buf_[0] == TELQUAL_SEND) {
                    send_subneg_ttype();
                }
                tel_state_ = TelState::DATA;
            } else if (b == TEL_IAC) {
                sb_buf_ += (char)255;
                tel_state_ = TelState::SB_DATA;
            } else {
                tel_state_ = TelState::DATA;
            }
            break;
        }
    }
    return consumed;
}

void Connection::send_telnet(uint8_t cmd, uint8_t opt) {
    uint8_t buf[3] = { TEL_IAC, cmd, opt };
    if (ssl_) {
        SSL_write(ssl_, buf, 3);
    } else if (fd_ >= 0) {
        write(fd_, buf, 3);
    }
}

void Connection::send_subneg_ttype() {
    const char* ttype = "xterm-256color";
    size_t tlen = strlen(ttype);
    std::vector<uint8_t> buf;
    buf.push_back(TEL_IAC);
    buf.push_back(TEL_SB);
    buf.push_back(TELOPT_TTYPE);
    buf.push_back(TELQUAL_IS);
    for (size_t i = 0; i < tlen; i++) buf.push_back(ttype[i]);
    buf.push_back(TEL_IAC);
    buf.push_back(TEL_SE);
    if (ssl_) {
        SSL_write(ssl_, buf.data(), (int)buf.size());
    } else if (fd_ >= 0) {
        write(fd_, buf.data(), buf.size());
    }
}

void Connection::send_subneg_naws(uint16_t width, uint16_t height) {
    uint8_t buf[9] = {
        TEL_IAC, TEL_SB, TELOPT_NAWS,
        (uint8_t)(width >> 8), (uint8_t)(width & 0xFF),
        (uint8_t)(height >> 8), (uint8_t)(height & 0xFF),
        TEL_IAC, TEL_SE
    };
    if (ssl_) {
        SSL_write(ssl_, buf, 9);
    } else if (fd_ >= 0) {
        write(fd_, buf, 9);
    }
}

void Connection::send_naws(uint16_t width, uint16_t height) {
    naws_width_ = width;
    naws_height_ = height;
    if (fd_ >= 0 && naws_agreed_) send_subneg_naws(width, height);
}

std::string Connection::check_prompt(std::chrono::milliseconds timeout) {
    if (line_buf_.empty() || fd_ < 0) return {};

    auto elapsed = std::chrono::steady_clock::now() - line_buf_time_;
    if (elapsed < timeout) return {};

    // Partial line has been sitting long enough — treat it as a prompt.
    // Strip trailing \r if present.
    std::string prompt = line_buf_;
    if (!prompt.empty() && prompt.back() == '\r') prompt.pop_back();

    // Only return if it's different from what we last displayed
    if (prompt == last_prompt_) return {};

    last_prompt_ = prompt;
    return prompt;
}

void Connection::add_to_scrollback(const std::string& line) {
    scrollback_.push_back(line);
    if (scrollback_.size() > MAX_SCROLLBACK)
        scrollback_.pop_front();
}
