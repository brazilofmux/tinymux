// iconnection.h -- Abstract interface for MUD connections.
// Concrete implementations: Connection (telnet), HydraConnection (gRPC).
#ifndef ICONNECTION_H
#define ICONNECTION_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <cstdint>
#include <cstdio>

class IConnection {
public:
    virtual ~IConnection() = default;

    // Lifecycle
    virtual bool is_connected() const = 0;
    virtual void disconnect() = 0;

    // I/O
    virtual bool send_line(const std::string& line) = 0;

    // Identity
    virtual const std::string& world_name() const = 0;
    virtual const std::string& host() const = 0;
    virtual const std::string& port() const = 0;

    // Prompt detection
    virtual std::string check_prompt(int timeout_ms) = 0;
    virtual bool has_partial_line() const = 0;

    // Scrollback
    virtual const std::deque<std::string>& scrollback() const = 0;
    virtual void add_to_scrollback(const std::string& line) = 0;

    // Telnet-specific (default no-ops for non-telnet transports)
    virtual bool remote_echo() const { return false; }
    virtual bool uses_ssl() const { return false; }
    virtual void send_naws(uint16_t width, uint16_t height) {
        (void)width; (void)height;
    }

    // Idle tracking
    virtual int idle_secs() const = 0;
    virtual int send_idle_secs() const = 0;

    // Per-world logging
    virtual bool start_log(const std::string& path);
    virtual void stop_log();
    virtual bool is_logging() const { return log_fp_ != nullptr; }
    virtual void log_line(const std::string& line);
    virtual const std::string& log_file() const { return log_file_; }

    // Transport type query
    virtual bool is_hydra() const { return false; }

protected:
    std::string log_file_;
    FILE* log_fp_ = nullptr;
};

// Inline default implementations
inline bool IConnection::start_log(const std::string& path) {
    stop_log();
    log_fp_ = fopen(path.c_str(), "a");
    if (log_fp_) { log_file_ = path; return true; }
    return false;
}

inline void IConnection::stop_log() {
    if (log_fp_) { fclose(log_fp_); log_fp_ = nullptr; }
    log_file_.clear();
}

inline void IConnection::log_line(const std::string& line) {
    if (log_fp_) { fprintf(log_fp_, "%s\n", line.c_str()); fflush(log_fp_); }
}

#endif // ICONNECTION_H
