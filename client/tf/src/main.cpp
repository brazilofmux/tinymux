#include "app.h"
#include "input.h"
#include "script.h"
#include "macro.h"
#include <sstream>
#include <sys/select.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <vector>
#include <fnmatch.h>

static volatile sig_atomic_t got_sigwinch = 0;
static volatile sig_atomic_t got_sigterm = 0;

static void sigwinch_handler(int) {
    got_sigwinch = 1;
}

static void sigterm_handler(int) {
    got_sigterm = 1;
}

bool app_send_line(App& app, Connection* conn, const std::string& line,
                   bool allow_local_echo) {
    if (!conn || !conn->is_connected()) return false;

    if (allow_local_echo) {
        auto it = app.vars.find("localecho");
        bool local_echo = (it != app.vars.end() && it->second == "on");
        if (local_echo) {
            if (conn == app.fg) app.terminal.print_line(line);
            else app.terminal.print_line("[" + conn->world_name() + "] " + line);
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
            app.terminal.print_line("[" + world_name + "] " + display_line);
        }
    }

    auto log_it = app.vars.find("_log_file");
    if (log_it != app.vars.end() && !log_it->second.empty()) {
        FILE* lf = fopen(log_it->second.c_str(), "a");
        if (lf) { fprintf(lf, "%s\n", line.c_str()); fclose(lf); }
    }
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
    if (!app.commands.dispatch(app, "/load " + path)) {
        if (!quiet) app.terminal.print_system("Cannot load file: " + path);
        return false;
    }
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
        ScriptEnv env(app.vars, &app);
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

    update_status(app);
    app.terminal.refresh();

    while (app.running) {
        // Handle signals
        if (got_sigterm) {
            app.running = false;
            break;
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

        // Build fd_set: STDIN + all connected sockets
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        int maxfd = STDIN_FILENO;

        for (auto& [name, conn] : app.connections) {
            if (conn->is_connected()) {
                int fd = conn->fd();
                FD_SET(fd, &rfds);
                if (fd > maxfd) maxfd = fd;
            }
        }

        // Compute select() timeout:
        //   - 25ms if pending ESC disambiguation
        //   - ms_until_next timer, if any timers active
        //   - otherwise block indefinitely (zero CPU)
        struct timeval tv;
        struct timeval* tvp = nullptr;
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

        if (timeout_ms >= 0) {
            tv.tv_sec = timeout_ms / 1000;
            tv.tv_usec = (timeout_ms % 1000) * 1000;
            tvp = &tv;
        }

        int ready = select(maxfd + 1, &rfds, nullptr, nullptr, tvp);

        // --- stdin input ---
        if (ready > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
            unsigned char buf[4096];
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n > 0) {
                lexer.feed(buf, (size_t)n);
            }
        } else if (ready == 0 && lexer.has_pending_esc()) {
            // Timeout with no new data — bare ESC
            lexer.flush_pending_esc();
        }

        // --- Fire due timers ---
        {
            auto cmds = app.timers.fire_due();
            for (auto& cmd : cmds) {
                exec_body(app, cmd);
            }
        }

        // Process input events from the lexer
        for (auto& ev : lexer.events()) {
            // Check key bindings first
            BindKey bk;
            bk.key = ev.key;
            bk.cp  = ev.cp;
            const std::string* bound_cmd = app.keybindings.find(bk);
            if (bound_cmd) {
                exec_body(app, *bound_cmd);
                continue;
            }

            // Default key handling
            std::string line;
            if (app.terminal.handle_key(ev, line)) {
                handle_input_line(app, line);
            }
        }
        lexer.clear_events();

        // --- Network sockets ---
        if (ready > 0) {
            std::vector<std::string> names;
            for (auto& [name, conn] : app.connections) names.push_back(name);

            for (auto& name : names) {
                auto it = app.connections.find(name);
                if (it == app.connections.end()) continue;
                auto& conn = it->second;

                if (!conn->is_connected()) continue;
                if (!FD_ISSET(conn->fd(), &rfds)) continue;

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
                    }
                    app.connections.erase(name);
                }
            }
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
            }
            app.connections.erase(name);
        }

        update_status(app);
        app.terminal.refresh();
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
    sigaction(SIGINT, &sa2, nullptr);
    sigaction(SIGTERM, &sa2, nullptr);

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
        } else if (arg == "-?" || arg == "--help") {
            fprintf(stderr, "Usage: %s [-w worldfile] [-f cmdfile]\n", argv[0]);
            fprintf(stderr, "  -w, --world-file PATH  load TinyFugue world definitions at startup\n");
            fprintf(stderr, "  -f, --file PATH        load a startup command file at startup\n");
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", arg.c_str());
            fprintf(stderr, "Use %s --help for usage.\n", argv[0]);
            return 1;
        }
    }

    if (!app.terminal.init()) {
        fprintf(stderr, "Failed to initialize terminal\n");
        return 1;
    }

    app.terminal.print_system("TinyFugue (C++ rebuild) - Type /help for commands, /quit to exit");
    if (world_files.empty() && command_files.empty()) {
        app.terminal.print_system("Use /load tiny.world then /load tiny.connect to get started.");
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
