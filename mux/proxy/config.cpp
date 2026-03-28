#include "config.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

// Trim whitespace from both ends.
static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Case-insensitive comparison.
static std::string toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return r;
}

// Parse a duration string into seconds.
// Accepts: bare integer (seconds), or integer with suffix s/m/h/d.
// Returns -1 on parse failure.
static int parseDuration(const std::string& s) {
    if (s.empty()) return -1;

    // Check for suffix
    char suffix = s.back();
    int multiplier = 1;
    std::string digits = s;

    if (std::isalpha(static_cast<unsigned char>(suffix))) {
        digits = s.substr(0, s.size() - 1);
        switch (std::tolower(static_cast<unsigned char>(suffix))) {
            case 's': multiplier = 1;     break;
            case 'm': multiplier = 60;    break;
            case 'h': multiplier = 3600;  break;
            case 'd': multiplier = 86400; break;
            default: return -1;
        }
    }

    try {
        int val = std::stoi(digits);
        if (val < 0) return -1;
        return val * multiplier;
    } catch (...) {
        return -1;
    }
}

// Split a line into key and value at first whitespace.
static bool splitKeyValue(const std::string& line, std::string& key,
                          std::string& value) {
    size_t pos = line.find_first_of(" \t");
    if (pos == std::string::npos) {
        key = line;
        value.clear();
        return true;
    }
    key = line.substr(0, pos);
    value = trim(line.substr(pos + 1));
    return true;
}

// Parse "key=value" from a whitespace-separated token list.
static std::string extractParam(const std::string& params,
                                const std::string& name) {
    size_t pos = params.find(name + "=");
    if (pos == std::string::npos) return "";
    size_t start = pos + name.size() + 1;
    size_t end = params.find_first_of(" \t", start);
    if (end == std::string::npos) end = params.size();
    return params.substr(start, end - start);
}

// Parse a listen directive: "listen <type> <host:port> [cert=... key=...]"
static bool parseListenLine(const std::string& value, ListenConfig& lc,
                            std::string& errorMsg) {
    std::istringstream ss(value);
    std::string type, hostport, rest;
    ss >> type >> hostport;
    std::getline(ss, rest);
    rest = trim(rest);

    std::string lt = toLower(type);
    lc.tls = (lt == "telnet+tls");
    lc.websocket = (lt == "websocket" || lt == "websocket+tls");
    lc.grpcWeb = (lt == "grpc-web" || lt == "grpcweb");
    if (lt == "websocket+tls") lc.tls = true;

    // Parse host:port
    size_t colon = hostport.rfind(':');
    if (colon == std::string::npos) {
        errorMsg = "listen: expected host:port, got '" + hostport + "'";
        return false;
    }
    lc.host = hostport.substr(0, colon);
    try {
        lc.port = static_cast<uint16_t>(std::stoi(hostport.substr(colon + 1)));
    } catch (...) {
        errorMsg = "listen: invalid port in '" + hostport + "'";
        return false;
    }

    if (lc.tls) {
        lc.certFile = extractParam(rest, "cert");
        lc.keyFile = extractParam(rest, "key");
        if (lc.certFile.empty() || lc.keyFile.empty()) {
            errorMsg = "listen: TLS listener requires cert= and key=";
            return false;
        }
    }
    // Non-TLS websocket/grpc-web may have optional params in rest
    // (currently none, but future-proof)
    return true;
}

// Parse retry schedule: "5s 10s 30s 60s"
static std::vector<int> parseRetrySchedule(const std::string& s) {
    std::vector<int> schedule;
    std::istringstream ss(s);
    std::string token;
    while (ss >> token) {
        int val = 0;
        try {
            // Strip trailing 's' if present
            if (!token.empty() && token.back() == 's') {
                val = std::stoi(token.substr(0, token.size() - 1));
            } else {
                val = std::stoi(token);
            }
        } catch (...) {
            continue;
        }
        if (val > 0) schedule.push_back(val);
    }
    return schedule;
}

static ganl::EncodingType parseCharset(const std::string& s) {
    std::string l = toLower(s);
    if (l == "utf-8" || l == "utf8") return ganl::EncodingType::Utf8;
    if (l == "latin-1" || l == "latin1" || l == "iso-8859-1")
        return ganl::EncodingType::Latin1;
    if (l == "cp437") return ganl::EncodingType::Cp437;
    if (l == "cp1252") return ganl::EncodingType::Cp1252;
    if (l == "ascii" || l == "us-ascii") return ganl::EncodingType::Ascii;
    return ganl::EncodingType::Utf8;
}

// Parse a game block.  Called after seeing "game "name" {"
static bool parseGameBlock(std::ifstream& file, GameConfig& game,
                           int& lineNum, std::string& errorMsg) {
    std::string line;
    while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "}") return true;

        std::string key, value;
        splitKeyValue(line, key, value);
        key = toLower(key);

        if (key == "host") {
            game.host = value;
        } else if (key == "port") {
            try { game.port = static_cast<uint16_t>(std::stoi(value)); }
            catch (...) {
                errorMsg = "game: invalid port '" + value + "'";
                return false;
            }
        } else if (key == "socket") {
            game.socketPath = value;
            game.transport = GameTransport::Unix;
        } else if (key == "transport") {
            game.transport = (toLower(value) == "unix")
                ? GameTransport::Unix : GameTransport::Tcp;
        } else if (key == "type") {
            game.type = (toLower(value) == "local")
                ? GameType::Local : GameType::Remote;
        } else if (key == "binary") {
            game.binary = value;
        } else if (key == "workdir") {
            game.workdir = value;
        } else if (key == "autostart") {
            game.autostart = (toLower(value) == "yes");
        } else if (key == "reconnect") {
            game.reconnect = (toLower(value) == "yes");
        } else if (key == "retry") {
            game.retrySchedule = parseRetrySchedule(value);
        } else if (key == "charset") {
            game.charset = parseCharset(value);
        } else if (key == "tls_required") {
            game.tlsRequired = (toLower(value) != "no");
        } else if (key == "tls") {
            game.tls = (toLower(value) == "yes");
        } else if (key == "tls_verify") {
            game.tlsVerify = (toLower(value) == "yes");
        } else if (key == "tls_ca") {
            game.tlsCaFile = value;
        }
        // Ignore unknown keys for forward compatibility
    }
    errorMsg = "game block: unexpected end of file (missing '}')";
    return false;
}

bool loadConfig(const std::string& path, HydraConfig& config,
                std::string& errorMsg) {
    std::ifstream file(path);
    if (!file.is_open()) {
        errorMsg = "cannot open config file: " + path;
        return false;
    }

    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        lineNum++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        std::string key, value;
        splitKeyValue(line, key, value);
        key = toLower(key);

        if (key == "listen") {
            ListenConfig lc;
            if (!parseListenLine(value, lc, errorMsg)) {
                errorMsg = "line " + std::to_string(lineNum) + ": " + errorMsg;
                return false;
            }
            config.listeners.push_back(lc);
        } else if (key == "database") {
            config.databasePath = value;
        } else if (key == "master_key") {
            config.masterKeyPath = value;
        } else if (key == "scrollback_lines") {
            try { config.scrollbackLines = std::stoul(value); }
            catch (...) {}
        } else if (key == "session_idle_timeout") {
            int d = parseDuration(value);
            if (d >= 0) config.sessionIdleTimeout = d;
        } else if (key == "detached_session_timeout") {
            int d = parseDuration(value);
            if (d >= 0) config.detachedSessionTimeout = d;
        } else if (key == "link_reconnect_timeout") {
            int d = parseDuration(value);
            if (d >= 0) config.linkReconnectTimeout = d;
        } else if (key == "session_token_ttl") {
            int d = parseDuration(value);
            if (d >= 0) config.sessionTokenTtl = d;
        } else if (key == "allow_plaintext") {
            config.allowPlaintext = (toLower(value) == "yes");
        } else if (key == "cors_origin") {
            config.corsOrigins.push_back(value);
        } else if (key == "grpc_listen") {
            config.grpcListenAddr = value;
        } else if (key == "grpc_tls_cert") {
            config.grpcTlsCert = value;
        } else if (key == "grpc_tls_key") {
            config.grpcTlsKey = value;
        } else if (key == "max_sessions_per_account") {
            try { config.maxSessionsPerAccount = std::stoi(value); }
            catch (...) {}
        } else if (key == "max_frontdoors_per_session") {
            try { config.maxFrontDoorsPerSession = std::stoi(value); }
            catch (...) {}
        } else if (key == "max_links_per_session") {
            try { config.maxLinksPerSession = std::stoi(value); }
            catch (...) {}
        } else if (key == "max_scrollback_memory_mb") {
            try { config.maxScrollbackMemoryMb = std::stoul(value); }
            catch (...) {}
        } else if (key == "max_connections_per_ip") {
            try { config.maxConnectionsPerIp = std::stoi(value); }
            catch (...) {}
        } else if (key == "connect_rate_limit") {
            try { config.connectRateLimit = std::stoi(value); }
            catch (...) {}
        } else if (key == "failed_login_lockout") {
            try { config.failedLoginLockout = std::stoi(value); }
            catch (...) {}
        } else if (key == "failed_login_lockout_minutes") {
            int d = parseDuration(value);
            if (d >= 0) config.failedLoginLockoutMinutes = d;
        } else if (key == "log_file") {
            config.logFile = value;
        } else if (key == "log_level") {
            config.logLevel = toLower(value);
        } else if (key == "game") {
            // Parse: game "Name" {
            GameConfig game;
            // Extract quoted name
            size_t q1 = value.find('"');
            size_t q2 = value.find('"', q1 + 1);
            if (q1 == std::string::npos || q2 == std::string::npos) {
                errorMsg = "line " + std::to_string(lineNum)
                    + ": game name must be quoted";
                return false;
            }
            game.name = value.substr(q1 + 1, q2 - q1 - 1);

            // Expect '{' after the name
            std::string rest = trim(value.substr(q2 + 1));
            if (rest != "{") {
                errorMsg = "line " + std::to_string(lineNum)
                    + ": expected '{' after game name";
                return false;
            }

            if (!parseGameBlock(file, game, lineNum, errorMsg)) {
                errorMsg = "line " + std::to_string(lineNum) + ": " + errorMsg;
                return false;
            }

            // Default retry schedule if not specified
            if (game.retrySchedule.empty()) {
                game.retrySchedule = {5, 10, 30, 60};
            }

            config.games.push_back(game);
        }
        // Ignore unknown top-level keys for forward compatibility
    }

    if (config.listeners.empty()) {
        errorMsg = "no listeners configured";
        return false;
    }

    // Validate gRPC TLS config: both cert and key must be provided together
    if (!config.grpcTlsCert.empty() != !config.grpcTlsKey.empty()) {
        errorMsg = "grpc_tls_cert and grpc_tls_key must both be set (or both omitted)";
        return false;
    }

    return true;
}
