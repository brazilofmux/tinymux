#include "app.h"
#include "input.h"
#include "script.h"
#include "macro.h"
#include <sstream>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <vector>
#include <fnmatch.h>

// Debug-keys helper: format a BindKey for log output.
static std::string dbg_key_name(const BindKey& bk) {
    if (bk.key == Key::CHAR) {
        if (bk.cp >= 32 && bk.cp < 127)
            return std::string("CHAR('") + (char)bk.cp + "')";
        char buf[32];
        snprintf(buf, sizeof(buf), "CHAR(U+%04X)", bk.cp);
        return buf;
    }
    const char* n = key_to_name(bk.key);
    return n ? n : "UNKNOWN";
}

static volatile sig_atomic_t got_sigwinch = 0;
static volatile sig_atomic_t got_sigterm = 0;
static volatile sig_atomic_t got_sigint = 0;
static volatile sig_atomic_t got_sigtstp = 0;

static void sigwinch_handler(int) {
    got_sigwinch = 1;
}

static void sigterm_handler(int) {
    got_sigterm = 1;
}

static void sigint_handler(int) {
    got_sigint = 1;
}

static void sigtstp_handler(int) {
    got_sigtstp = 1;
}

static void handle_shell_line(App& app, const ShellProcess& proc, const std::string& line) {
    switch (proc.disposition) {
        case ShellDisposition::Echo:
            app.terminal.print_line(line);
            break;

        case ShellDisposition::Exec:
            exec_body(app, line);
            break;

        case ShellDisposition::Send: {
            Connection* target = app.fg;
            if (!proc.world_name.empty()) {
                auto it = app.connections.find(proc.world_name);
                if (it != app.connections.end()) target = it->second.get();
            }
            if (target && target->is_connected()) {
                app_send_line(app, target, line);
            }
            break;
        }
    }
}

static void drain_shell_buffer(App& app, ShellProcess& proc, bool flush_partial) {
    size_t start = 0;
    while (start < proc.buffer.size()) {
        size_t newline = proc.buffer.find('\n', start);
        if (newline == std::string::npos) break;

        std::string line = proc.buffer.substr(start, newline - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        handle_shell_line(app, proc, line);
        start = newline + 1;
    }

    proc.buffer.erase(0, start);
    if (flush_partial && !proc.buffer.empty()) {
        std::string line = proc.buffer;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        handle_shell_line(app, proc, line);
        proc.buffer.clear();
    }
}

static void close_shell_process(ShellProcess& proc) {
    if (proc.fd >= 0) {
        close(proc.fd);
        proc.fd = -1;
    }
    if (proc.pid > 0) {
        int status = 0;
        (void)waitpid(proc.pid, &status, 0);
        proc.pid = -1;
    }
}

bool app_send_line(App& app, Connection* conn, const std::string& line,
                   bool allow_local_echo) {
    if (!conn || !conn->is_connected()) return false;

    if (allow_local_echo) {
        auto it = app.vars.find("localecho");
        bool local_echo = (it != app.vars.end() && it->second == "on");
        if (local_echo) {
            if (conn == app.fg) app.terminal.print_line(line);
            else app.terminal.print_line_to(conn->world_name(), line);
        }
    }

    return conn->send_line(line);
}

void app_receive_line(App& app, Connection* conn, const std::string& world_name,
                      const std::string& line) {
    if (!conn) return;

    conn->add_to_scrollback(line);

    std::string display_line = line;
    TriggerResult tr = check_triggers(app, display_line);

    bool show = !tr.gagged;
    if (show) {
        auto lim = app.vars.find("_limit_pattern");
        if (lim != app.vars.end() && !lim->second.empty()) {
            if (fnmatch(lim->second.c_str(), display_line.c_str(), 0) != 0)
                show = false;
        }
    }

    if (show) {
        if (conn == app.fg) {
            app.terminal.clear_prompt();
            app.terminal.print_line(display_line);
        } else {
            app.terminal.print_line_to(world_name, display_line);
            // Mark background activity.
            app.active_worlds.insert(world_name);
        }
    }

    auto log_it = app.vars.find("_log_file");
    if (log_it != app.vars.end() && !log_it->second.empty()) {
        FILE* lf = fopen(log_it->second.c_str(), "a");
        if (lf) { fprintf(lf, "%s\n", line.c_str()); fclose(lf); }
    }
}

bool app_spawn_shell(App& app, const std::string& command, ShellDisposition disposition,
                     const std::string& world_name) {
    int pipefd[2];
    if (pipe(pipefd) != 0) return false;

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }
    if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(127);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", command.c_str(), nullptr);
        _exit(127);
    }

    close(pipefd[1]);
    int flags = fcntl(pipefd[0], F_GETFL, 0);
    if (flags >= 0) {
        fcntl(pipefd[0], F_SETFL, flags | O_NONBLOCK);
    }

    ShellProcess proc;
    proc.pid = pid;
    proc.fd = pipefd[0];
    proc.disposition = disposition;
    proc.world_name = world_name;
    app.shell_processes.push_back(std::move(proc));
    return true;
}

void app_rerender_foreground(App& app) {
    app.terminal.clear_output();

    if (!app.fg) return;

    auto lim = app.vars.find("_limit_pattern");
    const std::string* pattern =
        (lim != app.vars.end() && !lim->second.empty()) ? &lim->second : nullptr;

    for (const auto& line : app.fg->scrollback()) {
        if (pattern && fnmatch(pattern->c_str(), line.c_str(), 0) != 0) continue;
        app.terminal.print_line(line);
    }
}

// ---- Unix mailbox monitoring (classic TF @mail) ----
//
// Checks $MAIL (or /set MAIL=path) for unread mail by comparing
// mtime vs atime.  Multiple paths can be space-separated in TFMAILPATH.
// Default check interval: 60 seconds (classic TF maildelay).
//
static std::chrono::steady_clock::time_point next_mail_check{};

static void check_mail(App& app) {
    auto now = std::chrono::steady_clock::now();
    if (now < next_mail_check) return;

    // Default 60-second interval; configurable via /set maildelay=N.
    int delay = 60;
    auto delay_it = app.vars.find("maildelay");
    if (delay_it != app.vars.end()) {
        int d = std::atoi(delay_it->second.c_str());
        if (d > 0) delay = d;
    }
    next_mail_check = now + std::chrono::seconds(delay);

    // Collect mail file paths from TFMAILPATH or MAIL.
    //
    std::vector<std::string> paths;
    auto tfmp = app.vars.find("TFMAILPATH");
    if (tfmp != app.vars.end() && !tfmp->second.empty()) {
        std::istringstream ss(tfmp->second);
        std::string p;
        while (ss >> p) paths.push_back(p);
    } else {
        auto mp = app.vars.find("MAIL");
        if (mp != app.vars.end() && !mp->second.empty()) {
            paths.push_back(mp->second);
        } else {
            // Try environment.
            const char* env_mail = std::getenv("MAIL");
            if (env_mail && *env_mail) paths.push_back(env_mail);
        }
    }

    if (paths.empty()) {
        app.terminal.status_mail_count = 0;
        return;
    }

    int count = 0;
    for (const auto& path : paths) {
        struct stat st;
        if (stat(path.c_str(), &st) != 0) continue;
        if (st.st_size == 0) continue;
        // Unread mail: mtime > atime (modified more recently than read).
        if (st.st_mtime > st.st_atime) count++;
    }
    app.terminal.status_mail_count = count;
}

static void update_status(App& app) {
    std::string status;
    if (app.fg) {
        status = " [" + app.fg->world_name() + "]";
        if (!app.fg->is_connected()) status += " (disconnected)";
    } else {
        status = " [no connection]";
    }

    int other = 0;
    for (auto& [name, conn] : app.connections) {
        if (conn.get() != app.fg && conn->is_connected()) other++;
    }
    if (other > 0) {
        status += "  +" + std::to_string(other) + " bg";
    }

    // Show activity indicator for background worlds with unread lines.
    //
    int active = (int)app.active_worlds.size();
    if (active > 0) {
        status += "  (" + std::to_string(active) + " active)";
    }

    // Populate @active and @log backing state for status bar fields.
    //
    app.terminal.status_active_count = active;
    auto log_it = app.vars.find("_log_file");
    app.terminal.status_logging = (log_it != app.vars.end() && !log_it->second.empty());

    app.terminal.set_status(status);
}

static void import_world_vars(App& app) {
    for (const auto& [k, v] : app.worlddb.vars()) {
        app.vars[k] = v;
    }
}

static bool load_world_file(App& app, const std::string& path, bool quiet = false) {
    int before = static_cast<int>(app.worlddb.worlds().size());
    int nw = app.worlddb.load(path);
    if (nw < 0) {
        if (!quiet) app.terminal.print_system("Cannot open world file: " + path);
        return false;
    }

    import_world_vars(app);

    int added = static_cast<int>(app.worlddb.worlds().size()) - before;
    if (!quiet) {
        app.terminal.print_system("Loaded " + std::to_string(added) + " worlds from " + path);
    }
    return true;
}

static bool load_command_file(App& app, const std::string& path, bool quiet = false) {
    // Check that the file exists before dispatching /load — dispatch
    // returns true when the *command* is found, not when the file loads
    // successfully, which would short-circuit the search-path loop.
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        if (!quiet) app.terminal.print_system("Cannot load file: " + path);
        return false;
    }
    app.commands.dispatch(app, "/load " + path);
    return true;
}

static void handle_input_line(App& app, const std::string& line) {
    if (line.empty()) return;
    if (line[0] == '/') {
        // Try built-in command first, then user-defined macro
        if (!app.commands.dispatch(app, line)) {
            // Extract macro name
            size_t sp = line.find(' ');
            std::string name = (sp != std::string::npos) ? line.substr(1, sp - 1) : line.substr(1);
            std::string args = (sp != std::string::npos) ? line.substr(sp + 1) : "";

            Macro* m = app.macros.find(name);
            if (m) {
                // Copy before exec_body — it may invalidate m
                std::string macro_name = m->name;
                std::string body = m->body;

                std::vector<std::string> params;
                std::istringstream iss(args);
                std::string tok;
                while (iss >> tok) params.push_back(tok);
                exec_body(app, body, params);

                // Re-lookup to decrement shots safely
                Macro* m2 = app.macros.find(macro_name);
                if (m2 && m2->shots > 0) {
                    m2->shots--;
                    if (m2->shots == 0) app.macros.undef(macro_name);
                }
            } else {
                app.terminal.print_system("Unknown command: " + line);
            }
        }
    } else {
        // Expand substitutions in outbound text
        ScriptEnv fallback(app.vars, &app);
        ScriptEnv& env = app.current_env ? *app.current_env : fallback;
        std::string expanded = expand_subs(line, env);
        if (app.fg && app.fg->is_connected()) {
            app.terminal.clear_prompt();
            if (!app_send_line(app, app.fg, expanded)) {
                app.terminal.print_system("Send failed on " + app.fg->world_name());
            }
        } else {
            app.terminal.print_system("No active connection. Use /connect <world>");
        }
    }
}

static void run(App& app) {
    InputLexer lexer;

    // Set stdin non-blocking for read() in the event loop
    int stdin_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, stdin_flags | O_NONBLOCK);

    check_mail(app);
    update_status(app);
    app.terminal.refresh();

    while (app.running) {
        // Handle signals
        if (got_sigterm) {
            app.running = false;
            break;
        }
        if (got_sigint) {
            got_sigint = 0;
            app.terminal.print_system("Interrupt: /quit to exit");
        }
        if (got_sigwinch) {
            got_sigwinch = 0;
            app.terminal.handle_resize();
            int cols = app.terminal.get_cols();
            int rows = app.terminal.get_rows();
            for (auto& [name, conn] : app.connections) {
                conn->send_naws((uint16_t)cols, (uint16_t)rows);
            }
            fire_hook(app, Hook::RESIZE, std::to_string(cols) + "x" + std::to_string(rows));
        }
        if (got_sigtstp) {
            got_sigtstp = 0;
            // Suspend: restore terminal, send SIGTSTP, reinit on resume.
            app.terminal.shutdown();
            signal(SIGTSTP, SIG_DFL);
            raise(SIGTSTP);
            // Resumed — reinstall handler and reinitialize.
            struct sigaction sa_tstp{};
            sa_tstp.sa_handler = sigtstp_handler;
            sigemptyset(&sa_tstp.sa_mask);
            sa_tstp.sa_flags = 0;
            sigaction(SIGTSTP, &sa_tstp, nullptr);
            app.terminal.init();
            app.terminal.handle_resize();
        }

        // Build pollfd set: STDIN + all connected sockets + shell pipes
        std::vector<struct pollfd> pollfds;
        pollfds.push_back({STDIN_FILENO, POLLIN, 0});
        std::vector<Connection*> polled_conns;
        polled_conns.reserve(app.connections.size());
        std::vector<ShellProcess*> polled_shells;
        polled_shells.reserve(app.shell_processes.size());

        for (auto& [name, conn] : app.connections) {
            if (conn->is_connected()) {
                pollfds.push_back({conn->fd(), POLLIN, 0});
                polled_conns.push_back(conn.get());
            }
        }
        for (auto& proc : app.shell_processes) {
            if (proc.fd >= 0) {
                pollfds.push_back({proc.fd, POLLIN, 0});
                polled_shells.push_back(&proc);
            }
        }

        // Compute poll() timeout:
        //   - 25ms if pending ESC disambiguation
        //   - ms_until_next timer, if any timers active
        //   - otherwise block indefinitely (zero CPU)
        int timeout_ms = -1;

        if (lexer.has_pending_esc()) {
            timeout_ms = 25;
        }

        int timer_ms = app.timers.ms_until_next();
        if (timer_ms >= 0) {
            if (timeout_ms < 0 || timer_ms < timeout_ms)
                timeout_ms = timer_ms;
        }

        // If any connection has a partial line, cap timeout at 250ms
        // so we can flush it as a prompt.
        for (auto& [name, conn] : app.connections) {
            if (conn->has_partial_line()) {
                if (timeout_ms < 0 || timeout_ms > 250)
                    timeout_ms = 250;
                break;
            }
        }

        int ready = poll(pollfds.data(), pollfds.size(), timeout_ms);

        // --- stdin input ---
        bool stdin_had_data = false;
        if (ready > 0 && !pollfds.empty() && (pollfds[0].revents & POLLIN)) {
            unsigned char buf[4096];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n > 0) {
                if (app.debug_keys_fp) {
                    fprintf(app.debug_keys_fp, "READ %zd bytes:", n);
                    for (ssize_t j = 0; j < n; j++)
                        fprintf(app.debug_keys_fp, " %02X", buf[j]);
                    fprintf(app.debug_keys_fp, "\n");
                }
                lexer.feed(buf, (size_t)n);
                stdin_had_data = true;
            }
        } else if (ready == 0 && lexer.has_pending_esc()) {
            // Timeout with no new data — bare ESC
            if (app.debug_keys_fp)
                fprintf(app.debug_keys_fp, "TIMEOUT: flushing pending ESC\n");
            lexer.flush_pending_esc();
        }

        // --- Fire due timers ---
        {
            auto cmds = app.timers.fire_due();
            for (auto& cmd : cmds) {
                exec_body(app, cmd);
            }
        }

        // Process input events from the lexer.
        //
        // Multi-key sequence matching: when a keystroke is a prefix of
        // a bound sequence, buffer it and wait for the next key.  If
        // no continuation arrives within the poll timeout (handled
        // implicitly — the next iteration will see no new keys and
        // flush the buffer), replay the buffered keys as individual
        // keystrokes.
        //
        for (auto& ev : lexer.events()) {
            BindKey bk;
            bk.key = ev.key;
            bk.cp  = ev.cp;

            if (app.debug_keys_fp)
                fprintf(app.debug_keys_fp, "EVENT: %s\n", dbg_key_name(bk).c_str());

            // Try advancing the multi-key sequence trie.
            //
            std::string seq_cmd;
            auto sr = app.keybindings.seq_advance(bk, seq_cmd);

            if (sr == KeyBindings::SeqResult::MATCH) {
                if (app.debug_keys_fp)
                    fprintf(app.debug_keys_fp, "  SEQ MATCH -> %s\n", seq_cmd.c_str());
                exec_body(app, seq_cmd);
                continue;
            }
            if (sr == KeyBindings::SeqResult::PREFIX) {
                if (app.debug_keys_fp)
                    fprintf(app.debug_keys_fp, "  SEQ PREFIX (waiting for more keys)\n");
                // Partial match — hold this key and wait for more.
                continue;
            }

            // SeqResult::NONE — no sequence match.  If we had buffered
            // keys from a partial sequence, replay them now.
            //
            auto buffered = app.keybindings.seq_buffered();
            app.keybindings.seq_reset();
            if (app.debug_keys_fp)
                fprintf(app.debug_keys_fp, "  SEQ NONE (buffered=%zu)\n", buffered.size());
            if (!buffered.empty()) {
                // The buffered keys plus current key didn't form a
                // sequence.  Replay the buffered keys (they were already
                // consumed from the event stream), then process current.
                for (const auto& replay_bk : buffered) {
                    const std::string* cmd = app.keybindings.find(replay_bk);
                    if (cmd) {
                        exec_body(app, *cmd);
                    } else {
                        InputEvent replay_ev;
                        replay_ev.key = replay_bk.key;
                        replay_ev.cp = replay_bk.cp;
                        std::string line;
                        if (app.terminal.handle_key(replay_ev, line)) {
                            handle_input_line(app, line);
                        }
                    }
                }
            }

            // Now process the current key normally.
            const std::string* bound_cmd = app.keybindings.find(bk);
            if (bound_cmd) {
                if (app.debug_keys_fp)
                    fprintf(app.debug_keys_fp, "  SINGLE BIND -> %s\n", bound_cmd->c_str());
                exec_body(app, *bound_cmd);
                continue;
            }
            if (app.debug_keys_fp)
                fprintf(app.debug_keys_fp, "  DEFAULT (no binding)\n");

            // Default key handling.
            std::string line;
            if (app.terminal.handle_key(ev, line)) {
                handle_input_line(app, line);
            }
        }
        lexer.clear_events();

        // If a partial sequence is pending and no new stdin input arrived
        // in this poll iteration, flush it (the poll timeout acts as the
        // sequence timeout).
        //
        if (app.keybindings.seq_pending() && !stdin_had_data) {
            auto buffered = app.keybindings.seq_buffered();
            if (app.debug_keys_fp) {
                fprintf(app.debug_keys_fp, "SEQ TIMEOUT: flushing %zu buffered keys:", buffered.size());
                for (const auto& b : buffered)
                    fprintf(app.debug_keys_fp, " %s", dbg_key_name(b).c_str());
                fprintf(app.debug_keys_fp, "\n");
            }
            app.keybindings.seq_reset();
            for (const auto& replay_bk : buffered) {
                const std::string* cmd = app.keybindings.find(replay_bk);
                if (cmd) {
                    exec_body(app, *cmd);
                } else {
                    InputEvent replay_ev;
                    replay_ev.key = replay_bk.key;
                    replay_ev.cp = replay_bk.cp;
                    std::string line;
                    if (app.terminal.handle_key(replay_ev, line)) {
                        handle_input_line(app, line);
                    }
                }
            }
        }

        // --- Network sockets ---
        if (ready > 0) {
            std::vector<std::string> names;
            for (auto& [name, conn] : app.connections) names.push_back(name);

            for (auto& name : names) {
                auto it = app.connections.find(name);
                if (it == app.connections.end()) continue;
                auto& conn = it->second;

                if (!conn->is_connected()) continue;
                bool readable = false;
                for (size_t i = 0; i < polled_conns.size(); ++i) {
                    if (polled_conns[i] == conn.get()) {
                        short revents = pollfds[i + 1].revents;
                        readable = (revents & (POLLIN | POLLERR | POLLHUP)) != 0;
                        break;
                    }
                }
                if (!readable) continue;

                auto lines = conn->read_lines();

                bool was_connected = conn->is_connected();
                for (auto& line : lines) {
                    app_receive_line(app, conn.get(), name, line);
                }

                if (!conn->is_connected() && was_connected) {
                    app.terminal.print_system("Connection to " + name + " closed");
                    fire_hook(app, Hook::DISCONNECT, name);
                    if (app.fg == conn.get()) {
                        app.terminal.clear_prompt();
                        app.fg = nullptr;
                        app.terminal.set_history_context("");
                        app.terminal.set_output_context("");
                    }
                    app.connections.erase(name);
                    app.active_worlds.erase(name);
                }
            }
        }

        // --- Shell processes ---
        if (ready > 0) {
            size_t shell_base = 1 + polled_conns.size();
            for (size_t i = 0; i < polled_shells.size(); ++i) {
                ShellProcess& proc = *polled_shells[i];
                short revents = pollfds[shell_base + i].revents;
                if (proc.fd < 0 || (revents & (POLLIN | POLLERR | POLLHUP)) == 0) continue;

                char buf[4096];
                for (;;) {
                    ssize_t n = read(proc.fd, buf, sizeof(buf));
                    if (n > 0) {
                        proc.buffer.append(buf, static_cast<size_t>(n));
                        drain_shell_buffer(app, proc, false);
                        continue;
                    }
                    if (n == 0) {
                        drain_shell_buffer(app, proc, true);
                        close_shell_process(proc);
                    }
                    break;
                }
            }
            app.shell_processes.erase(
                std::remove_if(app.shell_processes.begin(), app.shell_processes.end(),
                    [](const ShellProcess& proc) { return proc.fd < 0; }),
                app.shell_processes.end());
        }

        // --- Prompt detection ---
        // Flush partial lines that have been sitting > 250ms as prompts.
        for (auto& [name, conn] : app.connections) {
            std::string prompt = conn->check_prompt(std::chrono::milliseconds(250));
            if (!prompt.empty()) {
                if (conn.get() == app.fg) {
                    app.terminal.set_prompt(prompt);
                }
                fire_hook(app, Hook::PROMPT, prompt);
            }
        }

        // Clean up dead connections
        std::vector<std::string> dead;
        for (auto& [name, conn] : app.connections) {
            if (!conn->is_connected()) dead.push_back(name);
        }
        for (auto& name : dead) {
            if (app.fg && app.fg->world_name() == name) {
                app.terminal.clear_prompt();
                app.fg = nullptr;
                app.terminal.set_history_context("");
                app.terminal.set_output_context("");
            }
            app.connections.erase(name);
        }

        check_mail(app);
        update_status(app);
        app.terminal.refresh();
    }

    for (auto& proc : app.shell_processes) {
        close_shell_process(proc);
    }

    // Restore stdin flags
    fcntl(STDIN_FILENO, F_SETFL, stdin_flags);
}

int main(int argc, char* argv[]) {
    struct sigaction sa{};
    sa.sa_handler = sigwinch_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, nullptr);

    struct sigaction sa2{};
    sa2.sa_handler = sigterm_handler;
    sigemptyset(&sa2.sa_mask);
    sa2.sa_flags = 0;
    sigaction(SIGTERM, &sa2, nullptr);

    struct sigaction sa3{};
    sa3.sa_handler = sigint_handler;
    sigemptyset(&sa3.sa_mask);
    sa3.sa_flags = 0;
    sigaction(SIGINT, &sa3, nullptr);

    struct sigaction sa4{};
    sa4.sa_handler = sigtstp_handler;
    sigemptyset(&sa4.sa_mask);
    sa4.sa_flags = 0;
    sigaction(SIGTSTP, &sa4, nullptr);

    signal(SIGPIPE, SIG_IGN);

    App app;
    std::vector<std::string> world_files;
    std::vector<std::string> command_files;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-w" || arg == "--world-file") {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing argument for %s\n", arg.c_str());
                return 1;
            }
            world_files.push_back(argv[++i]);
        } else if (arg == "-f" || arg == "--file") {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing argument for %s\n", arg.c_str());
                return 1;
            }
            command_files.push_back(argv[++i]);
        } else if (arg == "--debug-keys") {
            if (i + 1 >= argc) {
                fprintf(stderr, "Missing argument for %s\n", arg.c_str());
                return 1;
            }
            const char* path = argv[++i];
            app.debug_keys_fp = fopen(path, "w");
            if (!app.debug_keys_fp) {
                fprintf(stderr, "Cannot open debug-keys log: %s\n", path);
                return 1;
            }
            setlinebuf(app.debug_keys_fp);
        } else if (arg == "-?" || arg == "--help") {
            fprintf(stderr, "Usage: %s [-w worldfile] [-f cmdfile] [--debug-keys FILE]\n", argv[0]);
            fprintf(stderr, "  -w, --world-file PATH  load TinyFugue world definitions at startup\n");
            fprintf(stderr, "  -f, --file PATH        load a startup command file at startup\n");
            fprintf(stderr, "  --debug-keys FILE      log input events and key binding decisions\n");
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            fprintf(stderr, "Use %s --help for usage.\n", argv[0]);
            return 1;
        }
    }

    app.terminal.set_app(&app);

    // Import mail-related environment variables (classic TF behavior).
    //
    const char* env_mail = std::getenv("MAIL");
    if (env_mail && *env_mail) app.vars["MAIL"] = env_mail;
    const char* env_tfmailpath = std::getenv("TFMAILPATH");
    if (env_tfmailpath && *env_tfmailpath) app.vars["TFMAILPATH"] = env_tfmailpath;

    if (!app.terminal.init()) {
        fprintf(stderr, "Failed to initialize terminal\n");
        return 1;
    }

    app.terminal.print_system("TinyFugue (C++ rebuild) - Type /help for commands, /quit to exit");

    // Load stdlib.tf — search TFDIR, next to executable, ../tf-lib/, ~/.tf/.
    //
    {
        std::vector<std::string> search_paths;

        // TFDIR environment variable (explicit override).
        const char* tfdir = std::getenv("TFDIR");
        if (tfdir && *tfdir) {
            search_paths.push_back(std::string(tfdir) + "/stdlib.tf");
        }

        // Next to the executable (build tree or installed).
        // Try /proc/self/exe (Linux), /proc/curproc/file (FreeBSD).
        char exe_path[4096];
        ssize_t exe_len = -1;
#if defined(__linux__)
        exe_len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
#elif defined(__FreeBSD__)
        exe_len = readlink("/proc/curproc/file", exe_path, sizeof(exe_path) - 1);
#endif
        if (exe_len > 0) {
            exe_path[exe_len] = '\0';
            std::string dir(exe_path);
            auto slash = dir.rfind('/');
            if (slash != std::string::npos) dir.resize(slash);
            search_paths.push_back(dir + "/tf-lib/stdlib.tf");
            search_paths.push_back(dir + "/../tf-lib/stdlib.tf");
        }

        // Home directory.
        const char* home = std::getenv("HOME");
        if (home) {
            search_paths.push_back(std::string(home) + "/.tf/stdlib.tf");
        }

        for (const auto& sp : search_paths) {
            if (load_command_file(app, sp, true)) break;
        }
    }

    if (world_files.empty() && command_files.empty()) {
        app.terminal.print_system("Use /world to define worlds, /connect <world> to connect.");
        app.terminal.print_system("Use -w <file> to load world definitions at startup.");
    }

    for (const auto& path : world_files) {
        load_world_file(app, path);
    }
    for (const auto& path : command_files) {
        load_command_file(app, path);
    }

    app.terminal.refresh();

    run(app);

    app.terminal.shutdown();
    return 0;
}
