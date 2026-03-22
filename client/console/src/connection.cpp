// connection.cpp -- IOCP async TCP connection with Telnet state machine.
#include "connection.h"
#include <cstring>

LPFN_CONNECTEX Connection::ConnectEx_ = nullptr;
const std::string Connection::empty_string_;

bool Connection::LoadConnectEx(SOCKET s) {
    if (ConnectEx_) return true;
    GUID guid = WSAID_CONNECTEX;
    DWORD bytes = 0;
    int rc = WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
                      &guid, sizeof(guid),
                      &ConnectEx_, sizeof(ConnectEx_),
                      &bytes, nullptr, nullptr);
    return rc == 0;
}

Connection::Connection(const std::string& world_name, const std::string& host,
                       const std::string& port, bool use_ssl, HANDLE iocp)
    : world_name_(world_name), host_(host), port_(port),
      use_ssl_(use_ssl), iocp_(iocp) {
    memset(&read_ctx_, 0, sizeof(read_ctx_));
    read_ctx_.op = IoOp::Read;
    read_ctx_.wsabuf.buf = read_ctx_.buffer;
    read_ctx_.wsabuf.len = sizeof(read_ctx_.buffer);

    memset(&connect_ctx_, 0, sizeof(connect_ctx_));
    connect_ctx_.op = IoOp::Connect;

    auto now = std::chrono::steady_clock::now();
    last_recv_time_ = now;
    last_send_time_ = now;
}

Connection::~Connection() {
    disconnect();
}

bool Connection::begin_connect() {
    // Resolve address
    struct addrinfo hints = {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    struct addrinfo* result = nullptr;
    int rc = getaddrinfo(host_.c_str(), port_.c_str(), &hints, &result);
    if (rc != 0 || !result) return false;

    // Create socket matching the address family
    socket_ = WSASocketW(result->ai_family, SOCK_STREAM, IPPROTO_TCP,
                          nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (socket_ == INVALID_SOCKET) {
        freeaddrinfo(result);
        return false;
    }

    // Load ConnectEx
    if (!LoadConnectEx(socket_)) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        freeaddrinfo(result);
        return false;
    }

    // ConnectEx requires the socket to be bound first
    struct sockaddr_storage bind_addr = {};
    int bind_len;
    if (result->ai_family == AF_INET6) {
        struct sockaddr_in6* a6 = (struct sockaddr_in6*)&bind_addr;
        a6->sin6_family = AF_INET6;
        a6->sin6_addr = in6addr_any;
        a6->sin6_port = 0;
        bind_len = sizeof(struct sockaddr_in6);
    } else {
        struct sockaddr_in* a4 = (struct sockaddr_in*)&bind_addr;
        a4->sin_family = AF_INET;
        a4->sin_addr.s_addr = INADDR_ANY;
        a4->sin_port = 0;
        bind_len = sizeof(struct sockaddr_in);
    }
    if (bind(socket_, (struct sockaddr*)&bind_addr, bind_len) != 0) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        freeaddrinfo(result);
        return false;
    }

    // Associate with IOCP
    CreateIoCompletionPort((HANDLE)socket_, iocp_, (ULONG_PTR)this, 0);

    // Begin async connect
    memset(&connect_ctx_.overlapped, 0, sizeof(connect_ctx_.overlapped));
    BOOL ok = ConnectEx_(socket_, result->ai_addr, (int)result->ai_addrlen,
                         nullptr, 0, nullptr, &connect_ctx_.overlapped);
    freeaddrinfo(result);

    if (!ok && WSAGetLastError() != ERROR_IO_PENDING) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
        return false;
    }
    return true;
}

bool Connection::begin_read() {
    if (socket_ == INVALID_SOCKET) return false;
    memset(&read_ctx_.overlapped, 0, sizeof(read_ctx_.overlapped));
    read_ctx_.wsabuf.buf = read_ctx_.buffer;
    read_ctx_.wsabuf.len = sizeof(read_ctx_.buffer);
    DWORD flags = 0;
    int rc = WSARecv(socket_, &read_ctx_.wsabuf, 1, nullptr, &flags,
                     &read_ctx_.overlapped, nullptr);
    if (rc == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        return false;
    }
    return true;
}

std::vector<std::string> Connection::on_completion(IoContext* ctx, DWORD bytes, DWORD error) {
    std::vector<std::string> lines;

    if (ctx->op == IoOp::Connect) {
        if (error != 0) {
            disconnect();
            return lines;
        }
        // Enable SO_UPDATE_CONNECT_CONTEXT so getpeername works
        setsockopt(socket_, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, nullptr, 0);
        connected_ = true;

        // Perform TLS handshake if SSL requested
        if (use_ssl_) {
            tls_ = std::make_unique<SchannelSession>();
            if (!tls_->handshake(socket_, host_)) {
                // Handshake failed
                tls_.reset();
                disconnect();
                return lines;
            }
        }

        // Send initial telnet negotiations
        send_telnet(TEL_WILL, TELOPT_NAWS);
        send_telnet(TEL_WILL, TELOPT_TTYPE);
        send_telnet(TEL_WILL, TELOPT_CHARSET);
        send_telnet(TEL_DO, TELOPT_SGA);
        send_telnet(TEL_DO, TELOPT_ECHO);

        // Start reading
        begin_read();
        return lines;
    }

    if (ctx->op == IoOp::Write) {
        // Async write completed — free the dynamically allocated context.
        if (error != 0) {
            connected_ = false;
        }
        delete ctx;
        return lines;
    }

    if (ctx->op == IoOp::Read) {
        if (error != 0 || bytes == 0) {
            // Connection closed or error
            connected_ = false;
            return lines;
        }
        line_buf_time_ = std::chrono::steady_clock::now();
        last_recv_time_ = line_buf_time_;

        if (tls_) {
            // Decrypt through TLS
            std::vector<char> plaintext;
            int rc = tls_->decrypt(ctx->buffer, bytes, plaintext);
            if (rc < 0) {
                connected_ = false;
                return lines;
            }
            if (!plaintext.empty()) {
                process_telnet((const unsigned char*)plaintext.data(),
                              plaintext.size(), lines);
            }
            // There may be more complete records in the pending buffer
            while (rc == 1) {
                plaintext.clear();
                rc = tls_->decrypt(nullptr, 0, plaintext);
                if (!plaintext.empty()) {
                    process_telnet((const unsigned char*)plaintext.data(),
                                  plaintext.size(), lines);
                }
                if (rc <= 0) break;
            }
        } else {
            process_telnet((const unsigned char*)ctx->buffer, bytes, lines);
        }

        // Continue reading
        begin_read();
    }

    return lines;
}

void Connection::disconnect() {
    stop_log();
    if (tls_ && socket_ != INVALID_SOCKET) {
        tls_->shutdown(socket_);
        tls_.reset();
    }
    if (socket_ != INVALID_SOCKET) {
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
    connected_ = false;
}

bool Connection::send_line(const std::string& line) {
    if (!connected_) return false;
    std::string data = line + "\r\n";
    send_raw(data.data(), data.size());
    last_send_time_ = std::chrono::steady_clock::now();
    return true;
}

void Connection::send_raw(const void* data, size_t len) {
    if (socket_ == INVALID_SOCKET || !connected_) return;

    const char* sendbuf;
    size_t sendlen;
    std::vector<char> encrypted;

    if (tls_) {
        if (!tls_->encrypt(data, len, encrypted)) return;
        sendbuf = encrypted.data();
        sendlen = encrypted.size();
    } else {
        sendbuf = (const char*)data;
        sendlen = len;
    }

    // Async send via WSASend with overlapped I/O.
    // Allocate a write context that persists until completion.
    while (sendlen > 0) {
        size_t chunk = sendlen > sizeof(IoContext::buffer) ? sizeof(IoContext::buffer) : sendlen;
        auto* ctx = new IoContext();
        memset(ctx, 0, sizeof(*ctx));
        ctx->op = IoOp::Write;
        memcpy(ctx->buffer, sendbuf, chunk);
        ctx->wsabuf.buf = ctx->buffer;
        ctx->wsabuf.len = (ULONG)chunk;

        DWORD sent = 0;
        int rc = WSASend(socket_, &ctx->wsabuf, 1, &sent, 0,
                         &ctx->overlapped, nullptr);
        if (rc == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
            delete ctx;
            break;
        }
        sendbuf += chunk;
        sendlen -= chunk;
    }
}

void Connection::send_telnet(uint8_t cmd, uint8_t opt) {
    uint8_t buf[3] = { TEL_IAC, cmd, opt };
    send_raw(buf, 3);
}

void Connection::send_subneg_ttype() {
    // IAC SB TTYPE IS "TinyMUX-Console" IAC SE
    const char* ttype = "TinyMUX-Console";
    size_t tlen = strlen(ttype);
    std::vector<uint8_t> buf;
    buf.push_back(TEL_IAC);
    buf.push_back(TEL_SB);
    buf.push_back(TELOPT_TTYPE);
    buf.push_back(TTYPE_IS);
    buf.insert(buf.end(), ttype, ttype + tlen);
    buf.push_back(TEL_IAC);
    buf.push_back(TEL_SE);
    send_raw(buf.data(), buf.size());
}

void Connection::send_subneg_charset(bool accepted, const std::string& charset) {
    std::vector<uint8_t> buf;
    buf.push_back(TEL_IAC);
    buf.push_back(TEL_SB);
    buf.push_back(TELOPT_CHARSET);
    if (accepted) {
        buf.push_back(CHARSET_ACCEPTED);
        buf.insert(buf.end(), charset.begin(), charset.end());
    } else {
        buf.push_back(CHARSET_REJECTED);
    }
    buf.push_back(TEL_IAC);
    buf.push_back(TEL_SE);
    send_raw(buf.data(), buf.size());
}

void Connection::send_naws(uint16_t width, uint16_t height) {
    naws_width_ = width;
    naws_height_ = height;
    if (!naws_agreed_ || !connected_) return;
    uint8_t buf[9];
    buf[0] = TEL_IAC; buf[1] = TEL_SB; buf[2] = TELOPT_NAWS;
    buf[3] = (uint8_t)(width >> 8); buf[4] = (uint8_t)(width & 0xFF);
    buf[5] = (uint8_t)(height >> 8); buf[6] = (uint8_t)(height & 0xFF);
    buf[7] = TEL_IAC; buf[8] = TEL_SE;
    send_raw(buf, 9);
}

void Connection::process_telnet(const unsigned char* data, size_t len,
                                std::vector<std::string>& lines_out) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = data[i];
        switch (tel_state_) {
        case TelState::DATA:
            if (c == TEL_IAC) {
                tel_state_ = TelState::IAC;
            } else {
                process_data(&c, 1, lines_out);
            }
            break;

        case TelState::IAC:
            switch (c) {
            case TEL_IAC:  // Escaped 0xFF
                process_data(&c, 1, lines_out);
                tel_state_ = TelState::DATA;
                break;
            case TEL_WILL: tel_state_ = TelState::WILL; break;
            case TEL_WONT: tel_state_ = TelState::WONT; break;
            case TEL_DO:   tel_state_ = TelState::DO;   break;
            case TEL_DONT: tel_state_ = TelState::DONT; break;
            case TEL_SB:   tel_state_ = TelState::SB;   break;
            case TEL_GA:
                // Treat GA as end-of-prompt
                if (!line_buf_.empty()) {
                    lines_out.push_back(line_buf_);
                    line_buf_.clear();
                }
                tel_state_ = TelState::DATA;
                break;
            default:
                tel_state_ = TelState::DATA;
                break;
            }
            break;

        case TelState::WILL:
            if (c == TELOPT_ECHO) {
                remote_echo_ = true;
                send_telnet(TEL_DO, c);
            } else if (c == TELOPT_SGA) {
                send_telnet(TEL_DO, c);
            } else if (c == TELOPT_GMCP) {
                send_telnet(TEL_DO, c);
            } else {
                send_telnet(TEL_DONT, c);
            }
            tel_state_ = TelState::DATA;
            break;

        case TelState::WONT:
            if (c == TELOPT_ECHO) remote_echo_ = false;
            tel_state_ = TelState::DATA;
            break;

        case TelState::DO:
            if (c == TELOPT_NAWS) {
                naws_agreed_ = true;
                send_naws(naws_width_, naws_height_);
            } else if (c == TELOPT_TTYPE) {
                send_subneg_ttype();
            } else if (c == TELOPT_CHARSET) {
                // We already sent WILL; wait for SB
            } else {
                send_telnet(TEL_WONT, c);
            }
            tel_state_ = TelState::DATA;
            break;

        case TelState::DONT:
            if (c == TELOPT_NAWS) naws_agreed_ = false;
            tel_state_ = TelState::DATA;
            break;

        case TelState::SB:
            sb_option_ = c;
            sb_buf_.clear();
            tel_state_ = TelState::SB_DATA;
            break;

        case TelState::SB_DATA:
            if (c == TEL_IAC) {
                tel_state_ = TelState::SB_IAC;
            } else {
                sb_buf_.push_back((char)c);
            }
            break;

        case TelState::SB_IAC:
            if (c == TEL_SE) {
                // Process subnegotiation
                if (sb_option_ == TELOPT_TTYPE && !sb_buf_.empty() &&
                    (uint8_t)sb_buf_[0] == TTYPE_SEND) {
                    send_subneg_ttype();
                } else if (sb_option_ == TELOPT_CHARSET && !sb_buf_.empty() &&
                           (uint8_t)sb_buf_[0] == CHARSET_REQUEST) {
                    // Look for UTF-8 in the offered charsets
                    bool found_utf8 = false;
                    std::string offered(sb_buf_.begin() + 1, sb_buf_.end());
                    // Charsets are separated by a delimiter (first byte after REQUEST)
                    if (!offered.empty()) {
                        char delim = offered[0];
                        size_t pos = 1;
                        while (pos < offered.size()) {
                            size_t next = offered.find(delim, pos);
                            if (next == std::string::npos) next = offered.size();
                            std::string cs = offered.substr(pos, next - pos);
                            if (cs == "UTF-8" || cs == "utf-8") {
                                found_utf8 = true;
                                break;
                            }
                            pos = next + 1;
                        }
                    }
                    send_subneg_charset(found_utf8, found_utf8 ? "UTF-8" : "");
                } else if (sb_option_ == TELOPT_GMCP) {
                    handle_gmcp_subneg(sb_buf_);
                } else if (sb_option_ == TELOPT_MSSP) {
                    handle_mssp_subneg(sb_buf_);
                }
                tel_state_ = TelState::DATA;
            } else if (c == TEL_IAC) {
                sb_buf_.push_back((char)0xFF);
                tel_state_ = TelState::SB_DATA;
            } else {
                tel_state_ = TelState::DATA;
            }
            break;
        }
    }
}

size_t Connection::process_data(const unsigned char* buf, size_t len,
                                std::vector<std::string>& lines_out) {
    for (size_t i = 0; i < len; i++) {
        char c = (char)buf[i];
        if (c == '\n') {
            // Strip trailing \r if present
            if (!line_buf_.empty() && line_buf_.back() == '\r') {
                line_buf_.pop_back();
            }
            lines_out.push_back(line_buf_);
            line_buf_.clear();
        } else {
            line_buf_.push_back(c);
        }
    }
    return len;
}

std::string Connection::check_prompt(int timeout_ms) {
    if (line_buf_.empty()) return "";
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - line_buf_time_).count();
    if (elapsed >= timeout_ms) {
        last_prompt_ = line_buf_;
        line_buf_.clear();
        return last_prompt_;
    }
    return "";
}

void Connection::add_to_scrollback(const std::string& line) {
    scrollback_.push_back(line);
    while (scrollback_.size() > MAX_SCROLLBACK) {
        scrollback_.pop_front();
    }
    log_line(line);
}

// -- GMCP --

const std::string& Connection::gmcp_get(const std::string& pkg) const {
    auto it = gmcp_.find(pkg);
    return it != gmcp_.end() ? it->second : empty_string_;
}

void Connection::handle_gmcp_subneg(const std::string& data) {
    // GMCP format: "Package.Name <JSON payload>"
    // Split at first space
    size_t sp = data.find(' ');
    if (sp == std::string::npos) {
        gmcp_[data] = "";
    } else {
        std::string pkg = data.substr(0, sp);
        std::string payload = data.substr(sp + 1);
        gmcp_[pkg] = payload;
    }
}

// -- MSSP --

void Connection::handle_mssp_subneg(const std::string& data) {
    // MSSP format: VAR <name> VAL <value> [VAR <name> VAL <value> ...]
    // VAR = 1, VAL = 2
    constexpr char MSSP_VAR = 1;
    constexpr char MSSP_VAL = 2;

    std::string key, val;
    enum { NONE, IN_VAR, IN_VAL } state = NONE;

    for (size_t i = 0; i < data.size(); i++) {
        char c = data[i];
        if (c == MSSP_VAR) {
            if (state == IN_VAL && !key.empty()) {
                mssp_[key] = val;
            }
            key.clear();
            val.clear();
            state = IN_VAR;
        } else if (c == MSSP_VAL) {
            state = IN_VAL;
        } else if (state == IN_VAR) {
            key.push_back(c);
        } else if (state == IN_VAL) {
            val.push_back(c);
        }
    }
    if (state == IN_VAL && !key.empty()) {
        mssp_[key] = val;
    }
}

// -- Idle tracking --

int Connection::idle_secs() const {
    auto now = std::chrono::steady_clock::now();
    return (int)std::chrono::duration_cast<std::chrono::seconds>(
        now - last_recv_time_).count();
}

int Connection::send_idle_secs() const {
    auto now = std::chrono::steady_clock::now();
    return (int)std::chrono::duration_cast<std::chrono::seconds>(
        now - last_send_time_).count();
}

// Logging is inherited from IConnection.
