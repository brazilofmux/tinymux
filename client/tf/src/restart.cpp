#include "restart.h"
#include "app.h"
#include "connection.h"
#include "macro.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <sstream>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

// ---- Minimal JSON writer ----

static void json_write_string(FILE* fp, const std::string& s) {
    fputc('"', fp);
    for (unsigned char ch : s) {
        switch (ch) {
        case '"':  fputs("\\\"", fp); break;
        case '\\': fputs("\\\\", fp); break;
        case '\b': fputs("\\b", fp); break;
        case '\f': fputs("\\f", fp); break;
        case '\n': fputs("\\n", fp); break;
        case '\r': fputs("\\r", fp); break;
        case '\t': fputs("\\t", fp); break;
        default:
            if (ch < 0x20) {
                fprintf(fp, "\\u%04x", ch);
            } else {
                fputc(ch, fp);
            }
            break;
        }
    }
    fputc('"', fp);
}

// ---- Minimal JSON reader (recursive descent) ----

enum class JType { NUL, BOOL, INT, FLOAT, STRING, ARRAY, OBJECT };

struct JValue {
    JType type = JType::NUL;
    bool bval = false;
    int64_t ival = 0;
    double fval = 0.0;
    std::string sval;
    std::vector<JValue> arr;
    std::vector<std::pair<std::string, JValue>> obj;

    const JValue* get(const std::string& key) const {
        for (auto& [k, v] : obj) {
            if (k == key) return &v;
        }
        return nullptr;
    }
    std::string str(const std::string& key, const std::string& def = "") const {
        auto* v = get(key);
        return (v && v->type == JType::STRING) ? v->sval : def;
    }
    int64_t num(const std::string& key, int64_t def = 0) const {
        auto* v = get(key);
        if (!v) return def;
        if (v->type == JType::INT) return v->ival;
        if (v->type == JType::FLOAT) return (int64_t)v->fval;
        return def;
    }
    bool boolean(const std::string& key, bool def = false) const {
        auto* v = get(key);
        return (v && v->type == JType::BOOL) ? v->bval : def;
    }
};

class JParser {
public:
    JParser(const char* data, size_t len) : p_(data), end_(data + len) {}

    bool parse(JValue& out) {
        skip_ws();
        if (!parse_value(out)) return false;
        skip_ws();
        return true;
    }

private:
    const char* p_;
    const char* end_;

    bool at_end() const { return p_ >= end_; }
    char peek() const { return at_end() ? '\0' : *p_; }
    char next() { return at_end() ? '\0' : *p_++; }

    void skip_ws() {
        while (!at_end() && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r'))
            p_++;
    }

    bool expect(char ch) {
        skip_ws();
        if (peek() != ch) return false;
        p_++;
        return true;
    }

    bool parse_value(JValue& out) {
        skip_ws();
        if (at_end()) return false;
        char ch = peek();
        if (ch == '"') return parse_string(out);
        if (ch == '{') return parse_object(out);
        if (ch == '[') return parse_array(out);
        if (ch == 't' || ch == 'f') return parse_bool(out);
        if (ch == 'n') return parse_null(out);
        if (ch == '-' || (ch >= '0' && ch <= '9')) return parse_number(out);
        return false;
    }

    bool parse_string(JValue& out) {
        out.type = JType::STRING;
        return parse_string_raw(out.sval);
    }

    bool parse_string_raw(std::string& s) {
        if (next() != '"') return false;
        s.clear();
        while (!at_end()) {
            char ch = next();
            if (ch == '"') return true;
            if (ch == '\\') {
                if (at_end()) return false;
                char esc = next();
                switch (esc) {
                case '"': s += '"'; break;
                case '\\': s += '\\'; break;
                case '/': s += '/'; break;
                case 'b': s += '\b'; break;
                case 'f': s += '\f'; break;
                case 'n': s += '\n'; break;
                case 'r': s += '\r'; break;
                case 't': s += '\t'; break;
                case 'u': {
                    uint32_t cp = 0;
                    for (int i = 0; i < 4; i++) {
                        if (at_end()) return false;
                        char h = next();
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= h - '0';
                        else if (h >= 'a' && h <= 'f') cp |= 10 + h - 'a';
                        else if (h >= 'A' && h <= 'F') cp |= 10 + h - 'A';
                        else return false;
                    }
                    // Encode as UTF-8
                    if (cp < 0x80) {
                        s += (char)cp;
                    } else if (cp < 0x800) {
                        s += (char)(0xC0 | (cp >> 6));
                        s += (char)(0x80 | (cp & 0x3F));
                    } else {
                        s += (char)(0xE0 | (cp >> 12));
                        s += (char)(0x80 | ((cp >> 6) & 0x3F));
                        s += (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: s += esc; break;
                }
            } else {
                s += ch;
            }
        }
        return false;
    }

    bool parse_number(JValue& out) {
        const char* start = p_;
        bool is_float = false;
        if (peek() == '-') p_++;
        while (!at_end() && *p_ >= '0' && *p_ <= '9') p_++;
        if (!at_end() && *p_ == '.') { is_float = true; p_++; while (!at_end() && *p_ >= '0' && *p_ <= '9') p_++; }
        if (!at_end() && (*p_ == 'e' || *p_ == 'E')) { is_float = true; p_++; if (!at_end() && (*p_ == '+' || *p_ == '-')) p_++; while (!at_end() && *p_ >= '0' && *p_ <= '9') p_++; }
        std::string num(start, p_);
        if (is_float) {
            out.type = JType::FLOAT;
            out.fval = std::strtod(num.c_str(), nullptr);
        } else {
            out.type = JType::INT;
            out.ival = std::strtoll(num.c_str(), nullptr, 10);
        }
        return true;
    }

    bool parse_bool(JValue& out) {
        if (end_ - p_ >= 4 && strncmp(p_, "true", 4) == 0) {
            p_ += 4; out.type = JType::BOOL; out.bval = true; return true;
        }
        if (end_ - p_ >= 5 && strncmp(p_, "false", 5) == 0) {
            p_ += 5; out.type = JType::BOOL; out.bval = false; return true;
        }
        return false;
    }

    bool parse_null(JValue& out) {
        if (end_ - p_ >= 4 && strncmp(p_, "null", 4) == 0) {
            p_ += 4; out.type = JType::NUL; return true;
        }
        return false;
    }

    bool parse_array(JValue& out) {
        if (next() != '[') return false;
        out.type = JType::ARRAY;
        skip_ws();
        if (peek() == ']') { p_++; return true; }
        for (;;) {
            JValue elem;
            if (!parse_value(elem)) return false;
            out.arr.push_back(std::move(elem));
            skip_ws();
            if (peek() == ']') { p_++; return true; }
            if (!expect(',')) return false;
        }
    }

    bool parse_object(JValue& out) {
        if (next() != '{') return false;
        out.type = JType::OBJECT;
        skip_ws();
        if (peek() == '}') { p_++; return true; }
        for (;;) {
            skip_ws();
            std::string key;
            if (!parse_string_raw(key)) return false;
            if (!expect(':')) return false;
            JValue val;
            if (!parse_value(val)) return false;
            out.obj.push_back({std::move(key), std::move(val)});
            skip_ws();
            if (peek() == '}') { p_++; return true; }
            if (!expect(',')) return false;
        }
    }
};

// ---- Helpers ----

std::string restart_file_path() {
    const char* home = std::getenv("HOME");
    if (!home) home = "/tmp";
    return std::string(home) + "/.titanfugue/restart.dat";
}

static std::string resolve_exe_path() {
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return {};
    buf[len] = '\0';
    return std::string(buf, (size_t)len);
}

// charset_name() and charset_from_name() are provided by charset.h

// ---- Serialization ----

bool serialize_restart(App& app, const std::string& path,
                       const std::vector<std::string>& original_argv) {
    // Ensure directory exists
    std::string dir = path;
    auto slash = dir.rfind('/');
    if (slash != std::string::npos) {
        dir.resize(slash);
        mkdir(dir.c_str(), 0700);
    }

    // Write to temp file, then rename atomically
    std::string tmp_path = path + ".tmp";
    FILE* fp = fopen(tmp_path.c_str(), "w");
    if (!fp) return false;
    fchmod(fileno(fp), 0600);

    fprintf(fp, "{\n");
    fprintf(fp, "  \"version\": 1,\n");

    // argv
    fprintf(fp, "  \"argv\": [");
    for (size_t i = 0; i < original_argv.size(); i++) {
        if (i > 0) fputc(',', fp);
        json_write_string(fp, original_argv[i]);
    }
    fprintf(fp, "],\n");

    // connections
    fprintf(fp, "  \"connections\": [\n");
    bool first_conn = true;
    std::vector<std::string> reconnect_list;

    for (auto& [name, conn] : app.connections) {
        if (!conn->is_connected()) continue;

        // SSL/MCCP/Hydra connections can't survive execv — reconnect after
        if (conn->uses_ssl() || conn->mccp_active() || conn->is_hydra()) {
            reconnect_list.push_back(name);
            continue;
        }

        if (!first_conn) fprintf(fp, ",\n");
        first_conn = false;

        fprintf(fp, "    {");
        fprintf(fp, "\"world_name\":"); json_write_string(fp, conn->world_name());
        fprintf(fp, ",\"host\":"); json_write_string(fp, conn->host());
        fprintf(fp, ",\"port\":"); json_write_string(fp, conn->port());
        fprintf(fp, ",\"fd\":%d", conn->fd());
        fprintf(fp, ",\"use_ssl\":false");
        fprintf(fp, ",\"tel_state\":%d", conn->tel_state_int());
        fprintf(fp, ",\"remote_echo\":%s", conn->remote_echo() ? "true" : "false");
        fprintf(fp, ",\"charset\":"); json_write_string(fp, charset_name(conn->charset()));
        fprintf(fp, ",\"naws_agreed\":%s", conn->naws_agreed() ? "true" : "false");
        fprintf(fp, ",\"naws_width\":%d", conn->naws_width());
        fprintf(fp, ",\"naws_height\":%d", conn->naws_height());
        fprintf(fp, ",\"line_buf\":"); json_write_string(fp, conn->line_buf());
        fprintf(fp, ",\"last_prompt\":"); json_write_string(fp, conn->last_prompt());

        // Scrollback
        fprintf(fp, ",\"scrollback\":[");
        const auto& sb = conn->scrollback();
        for (size_t i = 0; i < sb.size(); i++) {
            if (i > 0) fputc(',', fp);
            json_write_string(fp, sb[i]);
        }
        fprintf(fp, "]}");
    }
    fprintf(fp, "\n  ],\n");

    // foreground
    fprintf(fp, "  \"foreground\":");
    if (app.fg) json_write_string(fp, app.fg->world_name());
    else fprintf(fp, "\"\"");
    fprintf(fp, ",\n");

    // reconnect list
    fprintf(fp, "  \"reconnect\":[");
    for (size_t i = 0; i < reconnect_list.size(); i++) {
        if (i > 0) fputc(',', fp);
        json_write_string(fp, reconnect_list[i]);
    }
    fprintf(fp, "],\n");

    // timers
    fprintf(fp, "  \"timers\":[");
    auto now = std::chrono::steady_clock::now();
    const auto& timers = app.timers.all();
    for (size_t i = 0; i < timers.size(); i++) {
        if (i > 0) fputc(',', fp);
        const auto& t = timers[i];
        auto remaining_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            t.next_fire - now).count();
        if (remaining_ms < 0) remaining_ms = 0;
        fprintf(fp, "{\"id\":%d,\"command\":", t.id);
        json_write_string(fp, t.command);
        fprintf(fp, ",\"remaining\":%d,\"interval_ms\":%lld,\"remaining_ms\":%lld}",
                t.remaining, (long long)t.interval.count(), (long long)remaining_ms);
    }
    fprintf(fp, "],\n");
    fprintf(fp, "  \"timer_next_id\":%d,\n", app.timers.next_id());

    // vars
    fprintf(fp, "  \"vars\":{");
    {
        bool first = true;
        for (auto& [k, v] : app.vars) {
            if (!first) fputc(',', fp);
            first = false;
            json_write_string(fp, k);
            fputc(':', fp);
            json_write_string(fp, v);
        }
    }
    fprintf(fp, "},\n");

    // active_worlds
    fprintf(fp, "  \"active_worlds\":[");
    {
        bool first = true;
        for (auto& w : app.active_worlds) {
            if (!first) fputc(',', fp);
            first = false;
            json_write_string(fp, w);
        }
    }
    fprintf(fp, "],\n");

    // input_histories
    fprintf(fp, "  \"input_histories\":{");
    {
        bool first = true;
        for (auto& [key, hist] : app.terminal.input_histories()) {
            if (hist.empty()) continue;
            if (!first) fputc(',', fp);
            first = false;
            json_write_string(fp, key);
            fprintf(fp, ":[");
            for (size_t i = 0; i < hist.size(); i++) {
                if (i > 0) fputc(',', fp);
                json_write_string(fp, hist[i]);
            }
            fputc(']', fp);
        }
    }
    fprintf(fp, "},\n");

    // output_screens
    fprintf(fp, "  \"output_screens\":{");
    {
        bool first = true;
        for (auto& [key, screen] : app.terminal.output_screens()) {
            if (screen.lines.empty()) continue;
            if (!first) fputc(',', fp);
            first = false;
            json_write_string(fp, key);
            fprintf(fp, ":[");
            for (size_t i = 0; i < screen.lines.size(); i++) {
                if (i > 0) fputc(',', fp);
                json_write_string(fp, screen.lines[i]);
            }
            fputc(']', fp);
        }
    }
    fprintf(fp, "},\n");

    // input buffer and cursor
    fprintf(fp, "  \"input_buf\":");
    json_write_string(fp, app.terminal.input_text());
    fprintf(fp, ",\n  \"cursor_pos\":%zu\n", app.terminal.cursor_pos());

    fprintf(fp, "}\n");
    fclose(fp);

    // Atomic rename
    if (rename(tmp_path.c_str(), path.c_str()) != 0) {
        unlink(tmp_path.c_str());
        return false;
    }
    return true;
}

// ---- Restoration ----

static bool validate_fd(int fd) {
    if (fcntl(fd, F_GETFD) < 0) return false;
    int so_error = 0;
    socklen_t len = sizeof(so_error);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_error, &len) != 0) return false;
    return so_error == 0;
}

bool restore_restart(App& app, const std::string& path) {
    // Read the entire file
    FILE* fp = fopen(path.c_str(), "r");
    if (!fp) return false;

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (file_size <= 0 || file_size > 10 * 1024 * 1024) {
        fclose(fp);
        unlink(path.c_str());
        return false;
    }

    std::string data((size_t)file_size, '\0');
    size_t nread = fread(&data[0], 1, (size_t)file_size, fp);
    fclose(fp);
    if (nread != (size_t)file_size) {
        unlink(path.c_str());
        return false;
    }

    // Parse JSON
    JParser parser(data.data(), data.size());
    JValue root;
    if (!parser.parse(root) || root.type != JType::OBJECT) {
        app.terminal.print_system("% Restart file corrupt, starting fresh");
        unlink(path.c_str());
        return false;
    }

    int version = (int)root.num("version", 0);
    if (version != 1) {
        app.terminal.print_system("% Restart file version mismatch, starting fresh");
        unlink(path.c_str());
        return false;
    }

    std::vector<std::string> reconnect_names;

    // Restore connections
    auto* conns_val = root.get("connections");
    if (conns_val && conns_val->type == JType::ARRAY) {
        for (auto& cv : conns_val->arr) {
            if (cv.type != JType::OBJECT) continue;

            std::string world_name = cv.str("world_name");
            std::string host = cv.str("host");
            std::string port = cv.str("port");
            int fd = (int)cv.num("fd", -1);
            bool use_ssl = cv.boolean("use_ssl");
            int tel_state = (int)cv.num("tel_state", 0);
            bool remote_echo = cv.boolean("remote_echo");
            Charset charset = charset_from_name(cv.str("charset", "UTF8"));
            bool naws_agreed = cv.boolean("naws_agreed");
            uint16_t naws_w = (uint16_t)cv.num("naws_width", 80);
            uint16_t naws_h = (uint16_t)cv.num("naws_height", 24);
            std::string line_buf = cv.str("line_buf");
            std::string last_prompt = cv.str("last_prompt");

            if (fd < 0 || !validate_fd(fd)) {
                // FD is stale — add to reconnect list
                reconnect_names.push_back(world_name);
                continue;
            }

            auto conn = Connection::adopt_fd(
                world_name, host, port, fd, use_ssl,
                tel_state, remote_echo, charset,
                naws_agreed, naws_w, naws_h,
                line_buf, last_prompt);

            // Restore scrollback
            auto* sb = cv.get("scrollback");
            if (sb && sb->type == JType::ARRAY) {
                for (auto& line : sb->arr) {
                    if (line.type == JType::STRING) {
                        conn->add_to_scrollback(line.sval);
                    }
                }
            }

            app.connections[world_name] = std::move(conn);
        }
    }

    // Reconnect list from file
    auto* recon_val = root.get("reconnect");
    if (recon_val && recon_val->type == JType::ARRAY) {
        for (auto& rv : recon_val->arr) {
            if (rv.type == JType::STRING) {
                reconnect_names.push_back(rv.sval);
            }
        }
    }

    // Set foreground
    std::string fg_name = root.str("foreground");
    if (!fg_name.empty()) {
        auto it = app.connections.find(fg_name);
        if (it != app.connections.end()) {
            app.fg = it->second.get();
            app.terminal.set_history_context(fg_name);
            app.terminal.set_output_context(fg_name);
        }
    }

    // Restore timers
    auto* timers_val = root.get("timers");
    if (timers_val && timers_val->type == JType::ARRAY) {
        auto now = std::chrono::steady_clock::now();
        for (auto& tv : timers_val->arr) {
            if (tv.type != JType::OBJECT) continue;
            Timer t;
            t.id = (int)tv.num("id");
            t.command = tv.str("command");
            t.remaining = (int)tv.num("remaining", -1);
            auto interval_ms = tv.num("interval_ms", 1000);
            auto remaining_ms = tv.num("remaining_ms", interval_ms);
            t.interval = std::chrono::milliseconds(interval_ms);
            t.next_fire = now + std::chrono::milliseconds(remaining_ms);
            app.timers.add_raw(std::move(t));
        }
    }
    app.timers.set_next_id((int)root.num("timer_next_id", 1));

    // Restore vars
    auto* vars_val = root.get("vars");
    if (vars_val && vars_val->type == JType::OBJECT) {
        for (auto& [k, v] : vars_val->obj) {
            if (v.type == JType::STRING) {
                app.vars[k] = v.sval;
            }
        }
    }

    // Restore active_worlds
    auto* aw_val = root.get("active_worlds");
    if (aw_val && aw_val->type == JType::ARRAY) {
        for (auto& w : aw_val->arr) {
            if (w.type == JType::STRING) {
                app.active_worlds.insert(w.sval);
            }
        }
    }

    // Restore input histories
    auto* ih_val = root.get("input_histories");
    if (ih_val && ih_val->type == JType::OBJECT) {
        for (auto& [key, val] : ih_val->obj) {
            if (val.type != JType::ARRAY) continue;
            std::deque<std::string> hist;
            for (auto& entry : val.arr) {
                if (entry.type == JType::STRING) {
                    hist.push_back(entry.sval);
                }
            }
            app.terminal.set_input_history(key, std::move(hist));
        }
    }

    // Restore output screens
    auto* os_val = root.get("output_screens");
    if (os_val && os_val->type == JType::OBJECT) {
        for (auto& [key, val] : os_val->obj) {
            if (val.type != JType::ARRAY) continue;
            std::deque<std::string> lines;
            for (auto& entry : val.arr) {
                if (entry.type == JType::STRING) {
                    lines.push_back(entry.sval);
                }
            }
            app.terminal.set_output_lines(key, std::move(lines));
        }
    }

    // Restore input buffer and cursor
    std::string input_buf = root.str("input_buf");
    size_t cursor_pos = (size_t)root.num("cursor_pos", 0);
    if (!input_buf.empty()) {
        app.terminal.set_input_text(input_buf);
        app.terminal.set_cursor_pos(cursor_pos);
    }

    // Auto-reconnect SSL/MCCP and stale connections
    for (auto& name : reconnect_names) {
        app.terminal.print_system("% Reconnecting to " + name + "...");
        app.commands.dispatch(app, "/connect " + name);
    }

    // Fire RESTART hook
    fire_hook(app, Hook::RESTART);

    // Delete restart file
    unlink(path.c_str());

    app.terminal.print_system("% Restart complete");
    return true;
}

// ---- perform_restart ----

void perform_restart(App& app, const std::vector<std::string>& original_argv) {
    std::string exe = resolve_exe_path();
    if (exe.empty()) {
        app.terminal.print_system("% Cannot resolve executable path for restart");
        return;
    }

    std::string path = restart_file_path();

    // Warn and disconnect SSL/MCCP/Hydra connections (can't survive execv)
    for (auto& [name, conn] : app.connections) {
        if (conn->is_connected() && (conn->uses_ssl() || conn->mccp_active() || conn->is_hydra())) {
            app.terminal.print_system("% " + name + ": SSL/MCCP connection will be disconnected and auto-reconnected");
        }
    }

    // Kill shell subprocesses
    for (auto& proc : app.shell_processes) {
        if (proc.pid > 0) {
            kill(proc.pid, SIGTERM);
            int status = 0;
            waitpid(proc.pid, &status, 0);
            proc.pid = -1;
        }
        if (proc.fd >= 0) {
            close(proc.fd);
            proc.fd = -1;
        }
    }
    app.shell_processes.clear();

    // Close open file handles
    for (auto& [h, fp] : app.open_files) {
        if (fp) fclose(fp);
    }
    app.open_files.clear();

    if (app.debug_keys_fp) {
        fclose(app.debug_keys_fp);
        app.debug_keys_fp = nullptr;
    }

    // Serialize state
    if (!serialize_restart(app, path, original_argv)) {
        app.terminal.print_system("% Failed to write restart file");
        return;
    }

    // Disconnect SSL/MCCP connections (their FDs can't survive execv)
    for (auto& [name, conn] : app.connections) {
        if (conn->is_connected() && (conn->uses_ssl() || conn->mccp_active())) {
            conn->disconnect();
        }
    }

    // Shutdown terminal
    app.terminal.shutdown();

    // Build argv for execv: original args + --restart
    std::vector<std::string> new_argv = original_argv;
    new_argv.push_back("--restart");

    std::vector<char*> argv_ptrs;
    for (auto& a : new_argv) argv_ptrs.push_back(const_cast<char*>(a.c_str()));
    argv_ptrs.push_back(nullptr);

    // Replace process
    execv(exe.c_str(), argv_ptrs.data());

    // execv failed — re-init terminal and report
    app.terminal.init();
    app.terminal.print_system("% Restart failed: " + std::string(strerror(errno)));
}
