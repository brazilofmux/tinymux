#include "connection.h"
#include <sys/socket.h>
#include <sys/select.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <vector>
#include <poll.h>
#include <algorithm>
#include <cctype>

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
static constexpr uint8_t TELOPT_BINARY  = 0;
static constexpr uint8_t TELOPT_TTYPE   = 24;
static constexpr uint8_t TELOPT_NAWS    = 31;
static constexpr uint8_t TELOPT_CHARSET = 42;
static constexpr uint8_t TELOPT_MCCP2   = 86;

// Subnegotiation
static constexpr uint8_t TELQUAL_IS   = 0;
static constexpr uint8_t TELQUAL_SEND = 1;
static constexpr uint8_t CHARSET_REQUEST = 1;
static constexpr uint8_t CHARSET_ACCEPTED = 2;
static constexpr uint8_t CHARSET_REJECTED = 3;

static const char* negotiated_ttype() {
    const char* term = std::getenv("TERM");
    if (term != nullptr && *term != '\0') return term;
    return "xterm-256color";
}

static bool charset_is_utf8(const std::string& name) {
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(),
        [](unsigned char ch) { return static_cast<char>(std::toupper(ch)); });
    return upper == "UTF-8" || upper == "UTF8";
}

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

static bool ssl_wait_writable(SSL* ssl, int ret, int fd, int timeout_ms) {
    for (;;) {
        int err = SSL_get_error(ssl, ret);
        if (err == SSL_ERROR_WANT_READ) return wait_for_fd(fd, POLLIN, timeout_ms);
        if (err == SSL_ERROR_WANT_WRITE) return wait_for_fd(fd, POLLOUT, timeout_ms);
        if (err == SSL_ERROR_SYSCALL && errno == EINTR) continue;
        return false;
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

bool Connection::write_all(const void* data, size_t len) {
    const unsigned char* p = static_cast<const unsigned char*>(data);
    size_t remaining = len;

    while (remaining > 0) {
        if (ssl_) {
            int n = SSL_write(ssl_, p, (int)remaining);
            if (n > 0) {
                p += n;
                remaining -= (size_t)n;
                continue;
            }
            if (!ssl_wait_writable(ssl_, n, fd_, 5000)) return false;
            continue;
        }

        ssize_t n = write(fd_, p, remaining);
        if (n > 0) {
            p += n;
            remaining -= (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (!wait_for_fd(fd_, POLLOUT, 5000)) return false;
            continue;
        }
        return false;
    }

    return true;
}

void Connection::disconnect() {
    mccp_end();
    if (ssl_) { SSL_shutdown(ssl_); SSL_free(ssl_); ssl_ = nullptr; }
    if (ssl_ctx_) { SSL_CTX_free(ssl_ctx_); ssl_ctx_ = nullptr; }
    if (fd_ >= 0) { close(fd_); fd_ = -1; }
    tel_state_ = TelState::DATA;
    line_buf_.clear();
}

bool Connection::send_line(const std::string& line) {
    if (fd_ < 0) return false;
    std::string data;
    data.reserve(line.size() * 2 + 2);
    for (unsigned char ch : line) {
        data.push_back((char)ch);
        if (ch == TEL_IAC) data.push_back((char)TEL_IAC);
    }
    data += "\r\n";
    if (!write_all(data.data(), data.size())) return false;
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
        if (n <= 0) {
            int err = SSL_get_error(ssl_, (int)n);
            if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
                return lines;
            }
            if (err == SSL_ERROR_ZERO_RETURN) {
                disconnect();
            } else {
                disconnect();
            }
            return lines;
        }
    } else {
        n = read(fd_, buf, sizeof(buf));
        if (n <= 0) {
            if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                disconnect();
            }
            return lines;
        }
    }

    // Decompress if MCCP is active, then process through telnet
    // state machine.  Note: MCCP can start mid-stream (process_data
    // returns early when it sees SB MCCP2 SE), so we loop and check
    // mccp_active_ after each process_data call.
    //
    const unsigned char* data = buf;
    size_t datalen = (size_t)n;
    std::vector<unsigned char> inflated;

    if (mccp_active_) {
        if (!mccp_inflate(data, datalen, inflated)) {
            disconnect();
            return lines;
        }
        data = inflated.data();
        datalen = inflated.size();
    }

    size_t pos = 0;
    size_t buf_before = line_buf_.size();
    while (pos < datalen) {
        size_t consumed = process_data(data + pos, datalen - pos);
        pos += consumed;

        // If MCCP just started (process_data returned early after
        // SB MCCP2 SE), the remaining bytes are compressed.
        //
        if (mccp_active_ && pos < datalen) {
            std::vector<unsigned char> rest;
            if (!mccp_inflate(data + pos, datalen - pos, rest)) {
                disconnect();
                return lines;
            }
            // Replace data/datalen with inflated output and restart.
            inflated = std::move(rest);
            data = inflated.data();
            datalen = inflated.size();
            pos = 0;
        }
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
            } else if (b == TELOPT_BINARY || b == TELOPT_CHARSET) {
                send_telnet(TEL_DO, b);
            } else if (b == TELOPT_SGA) {
                send_telnet(TEL_DO, b);
            } else if (b == TELOPT_MCCP2) {
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
            } else if (b == TELOPT_BINARY) {
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
                } else if (sb_option_ == TELOPT_CHARSET && sb_buf_.size() >= 2 &&
                           (uint8_t)sb_buf_[0] == CHARSET_REQUEST) {
                    char sep = sb_buf_[1];
                    size_t start = 2;
                    bool accepted = false;
                    while (start < sb_buf_.size()) {
                        size_t end = sb_buf_.find(sep, start);
                        if (end == std::string::npos) end = sb_buf_.size();
                        std::string candidate = sb_buf_.substr(start, end - start);
                        if (charset_is_utf8(candidate)) {
                            send_subneg_charset(true, "UTF-8");
                            accepted = true;
                            break;
                        }
                        if (end == sb_buf_.size()) break;
                        start = end + 1;
                    }
                    if (!accepted) send_subneg_charset(false);
                } else if (sb_option_ == TELOPT_MCCP2 && sb_buf_.empty()) {
                    // MCCP v2: everything after IAC SE is zlib-compressed.
                    mccp_start();
                    tel_state_ = TelState::DATA;
                    // Return now — remaining bytes in buf are compressed
                    // and must go through inflate before telnet processing.
                    return consumed;
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
    if (fd_ >= 0) write_all(buf, sizeof(buf));
}

void Connection::send_subneg_ttype() {
    const char* ttype = negotiated_ttype();
    size_t tlen = strlen(ttype);
    std::vector<uint8_t> buf;
    buf.push_back(TEL_IAC);
    buf.push_back(TEL_SB);
    buf.push_back(TELOPT_TTYPE);
    buf.push_back(TELQUAL_IS);
    for (size_t i = 0; i < tlen; i++) buf.push_back(ttype[i]);
    buf.push_back(TEL_IAC);
    buf.push_back(TEL_SE);
    if (fd_ >= 0) write_all(buf.data(), buf.size());
}

void Connection::send_subneg_charset(bool accepted, const std::string& charset) {
    std::vector<uint8_t> buf;
    buf.push_back(TEL_IAC);
    buf.push_back(TEL_SB);
    buf.push_back(TELOPT_CHARSET);
    buf.push_back(accepted ? CHARSET_ACCEPTED : CHARSET_REJECTED);
    if (accepted) {
        for (char ch : charset) buf.push_back(static_cast<uint8_t>(ch));
    }
    buf.push_back(TEL_IAC);
    buf.push_back(TEL_SE);
    if (fd_ >= 0) write_all(buf.data(), buf.size());
}

void Connection::send_subneg_naws(uint16_t width, uint16_t height) {
    // RFC 1073: IAC (0xFF) bytes inside SB..SE must be doubled.
    //
    uint8_t buf[13];
    size_t pos = 0;
    buf[pos++] = TEL_IAC;
    buf[pos++] = TEL_SB;
    buf[pos++] = TELOPT_NAWS;

    uint8_t data[4] = {
        (uint8_t)(width >> 8), (uint8_t)(width & 0xFF),
        (uint8_t)(height >> 8), (uint8_t)(height & 0xFF)
    };
    for (int i = 0; i < 4; i++) {
        buf[pos++] = data[i];
        if (data[i] == TEL_IAC) buf[pos++] = TEL_IAC;
    }

    buf[pos++] = TEL_IAC;
    buf[pos++] = TEL_SE;
    if (fd_ >= 0) write_all(buf, pos);
}

void Connection::send_naws(uint16_t width, uint16_t height) {
    naws_width_ = width;
    naws_height_ = height;
    if (fd_ >= 0 && naws_agreed_) send_subneg_naws(width, height);
}

// ---- MCCP v2 (telnet option 86) ----

void Connection::mccp_start() {
    if (mccp_active_) return;
    mccp_zstream_ = {};
    mccp_zstream_.zalloc = Z_NULL;
    mccp_zstream_.zfree = Z_NULL;
    mccp_zstream_.opaque = Z_NULL;
    mccp_zstream_.avail_in = 0;
    mccp_zstream_.next_in = Z_NULL;
    if (inflateInit(&mccp_zstream_) == Z_OK) {
        mccp_active_ = true;
        mccp_zstream_init_ = true;
    }
}

void Connection::mccp_end() {
    if (mccp_zstream_init_) {
        inflateEnd(&mccp_zstream_);
        mccp_zstream_init_ = false;
    }
    mccp_active_ = false;
}

bool Connection::mccp_inflate(const unsigned char* in, size_t inlen,
                              std::vector<unsigned char>& out) {
    mccp_zstream_.next_in = const_cast<unsigned char*>(in);
    mccp_zstream_.avail_in = static_cast<uInt>(inlen);

    unsigned char chunk[8192];
    while (mccp_zstream_.avail_in > 0) {
        mccp_zstream_.next_out = chunk;
        mccp_zstream_.avail_out = sizeof(chunk);

        int ret = inflate(&mccp_zstream_, Z_SYNC_FLUSH);
        size_t have = sizeof(chunk) - mccp_zstream_.avail_out;
        out.insert(out.end(), chunk, chunk + have);

        if (ret == Z_STREAM_END) {
            // Server ended compression. Any remaining bytes in
            // avail_in are uncompressed data.
            if (mccp_zstream_.avail_in > 0) {
                out.insert(out.end(),
                    mccp_zstream_.next_in,
                    mccp_zstream_.next_in + mccp_zstream_.avail_in);
            }
            mccp_end();
            return true;
        }
        if (ret != Z_OK && ret != Z_BUF_ERROR) {
            mccp_end();
            return false;
        }
    }
    return true;
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

std::string Connection::current_prompt() const {
    if (line_buf_.empty() || fd_ < 0) return {};
    std::string prompt = line_buf_;
    if (!prompt.empty() && prompt.back() == '\r') prompt.pop_back();
    return prompt;
}

void Connection::add_to_scrollback(const std::string& line) {
    scrollback_.push_back(line);
    if (scrollback_.size() > MAX_SCROLLBACK)
        scrollback_.pop_front();
}
