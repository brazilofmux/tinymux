#ifndef ICONNECTION_H
#define ICONNECTION_H

#include <string>
#include <vector>
#include <deque>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include "charset.h"

// Abstract base class for MUD connections.
// Concrete implementations: Connection (telnet), HydraConnection (gRPC).
class IConnection {
public:
    virtual ~IConnection() = default;

    // Lifecycle
    virtual bool connect() = 0;
    virtual void disconnect() = 0;
    virtual bool is_connected() const = 0;

    // I/O
    virtual bool send_line(const std::string& line) = 0;
    virtual std::vector<std::string> read_lines() = 0;

    // Pollable file descriptor for the main event loop.
    // For telnet: the TCP socket fd.
    // For gRPC: an eventfd that signals when output is available.
    virtual int fd() const = 0;

    // Identity
    virtual const std::string& world_name() const = 0;
    virtual const std::string& host() const = 0;
    virtual const std::string& port() const = 0;

    // Prompt detection
    virtual std::string check_prompt(std::chrono::milliseconds timeout) = 0;
    virtual std::string current_prompt() const = 0;
    virtual bool has_partial_line() const = 0;

    // Scrollback
    virtual const std::deque<std::string>& scrollback() const = 0;
    virtual void add_to_scrollback(const std::string& line) = 0;

    // Telnet-specific (default no-ops/false for non-telnet transports)
    virtual bool remote_echo() const { return false; }
    virtual bool uses_ssl() const { return false; }
    virtual bool mccp_active() const { return false; }
    virtual void send_naws(uint16_t width, uint16_t height) { (void)width; (void)height; }

    // Idle tracking
    virtual int idle_secs() const = 0;
    virtual int sidle_secs() const = 0;

    // Per-world logging
    std::string log_file;
    FILE* log_fp = nullptr;

    virtual void start_log(const std::string& path) {
        stop_log();
        log_fp = fopen(path.c_str(), "a");
        if (log_fp) log_file = path;
    }
    virtual void stop_log() {
        if (log_fp) { fclose(log_fp); log_fp = nullptr; }
        log_file.clear();
    }
    virtual void log_line(const std::string& line) {
        if (log_fp) { fprintf(log_fp, "%s\n", line.c_str()); fflush(log_fp); }
    }

    // Restart serialization accessors (defaults for non-telnet transports)
    virtual Charset charset() const { return Charset::UTF8; }
    virtual int tel_state_int() const { return 0; }
    virtual bool naws_agreed() const { return false; }
    virtual uint16_t naws_width() const { return 80; }
    virtual uint16_t naws_height() const { return 24; }
    virtual const std::string& line_buf() const { static const std::string e; return e; }
    virtual const std::string& last_prompt() const { static const std::string e; return e; }

    // Transport type query
    virtual bool is_hydra() const { return false; }
};

#endif // ICONNECTION_H
