// command.cpp -- Built-in commands.
#include "command.h"
#include "app.h"
#ifdef HYDRA_GRPC
#include "hydra_connection.h"
#endif
#include <sstream>
#include <algorithm>
#include <cstdlib>

// Split input into tokens, respecting the first word as the command.
static std::vector<std::string> tokenize(const std::string& input) {
    std::vector<std::string> tokens;
    std::istringstream ss(input);
    std::string token;
    while (ss >> token) tokens.push_back(token);
    return tokens;
}

void CommandDispatcher::register_cmd(const std::string& name, CmdFunc func,
                                     const std::string& help) {
    cmds_[name] = { func, help };
}

bool CommandDispatcher::dispatch(App& app, const std::string& input) {
    if (input.empty() || input[0] != '/') return false;

    auto tokens = tokenize(input);
    if (tokens.empty()) return false;

    std::string cmd = tokens[0].substr(1); // strip leading /
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    auto it = cmds_.find(cmd);
    if (it == cmds_.end()) {
        app.terminal.print_system("Unknown command: /" + cmd);
        return true;
    }

    it->second.func(app, tokens);
    return true;
}

std::vector<std::pair<std::string, std::string>> CommandDispatcher::help_list() const {
    std::vector<std::pair<std::string, std::string>> result;
    for (auto& [name, entry] : cmds_) {
        result.push_back({ name, entry.help });
    }
    std::sort(result.begin(), result.end());
    return result;
}

// -- Built-in command implementations --

static void cmd_quit(App& app, const std::vector<std::string>&) {
    app.running = false;
}

static void cmd_connect(App& app, const std::vector<std::string>& args) {
    // /connect <world> or /connect <host> <port> [ssl]
    if (args.size() < 2) {
        app.terminal.print_system("Usage: /connect <world> | /connect <host> <port> [ssl]");
        return;
    }

    std::string name, host, port;
    bool use_ssl = false;

    if (args.size() == 2) {
        // Look up world by name
        const World* w = app.worlddb.find(args[1]);
        if (!w) {
            app.terminal.print_system("Unknown world: " + args[1]);
            return;
        }
        name = w->name;
        host = w->host;
        port = w->port;
        use_ssl = w->use_ssl;
    } else {
        host = args[1];
        port = args[2];
        name = host + ":" + port;
        if (args.size() > 3 && args[3] == "ssl") use_ssl = true;
    }

    if (app.connections.count(name)) {
        app.terminal.print_system("Already connected to " + name);
        return;
    }

    // Check if this is a Hydra world
    const World* world = app.worlddb.find(name);
    if (world && world->use_hydra) {
#ifdef HYDRA_GRPC
        auto hconn = std::make_unique<HydraConnection>(
            name, world->host, world->port,
            world->hydra_user, world->hydra_pass,
            world->hydra_game, app.iocp, world->use_ssl);
        app.terminal.print_system("Connecting via Hydra to " + name + " (" +
                                  world->host + ":" + world->port + ")...");
        IConnection* raw = hconn.get();
        app.connections[name] = std::move(hconn);
        // connect() is synchronous for auth, then spawns reader thread
        if (!static_cast<HydraConnection*>(raw)->connect()) {
            // Error already queued as output; drain it
            auto lines = static_cast<HydraConnection*>(raw)->drain_output();
            for (auto& line : lines) {
                app.terminal.print_system(line);
            }
            app.connections.erase(name);
            return;
        }
        if (!app.fg) {
            app.fg = raw;
            app.terminal.set_output_context(name);
            app.terminal.set_history_context(name);
        }
#else
        app.terminal.print_system("Hydra/gRPC support not compiled in.");
#endif
        return;
    }

    auto conn = std::make_unique<Connection>(name, host, port, use_ssl, app.iocp);
    if (!conn->begin_connect()) {
        app.terminal.print_system("Failed to connect to " + host + ":" + port);
        return;
    }

    app.terminal.print_system("Connecting to " + name + " (" + host + ":" + port +
                              (use_ssl ? " ssl" : "") + ")...");
    IConnection* raw = conn.get();
    app.connections[name] = std::move(conn);

    // Make foreground if first connection
    if (!app.fg) {
        app.fg = raw;
        app.terminal.set_output_context(name);
        app.terminal.set_history_context(name);
    }
}

static void cmd_disconnect(App& app, const std::vector<std::string>& args) {
    std::string name;
    if (args.size() >= 2) {
        name = args[1];
    } else if (app.fg) {
        name = app.fg->world_name();
    } else {
        app.terminal.print_system("Not connected.");
        return;
    }

    auto it = app.connections.find(name);
    if (it == app.connections.end()) {
        app.terminal.print_system("No connection: " + name);
        return;
    }

    it->second->disconnect();
    if (app.fg == it->second.get()) {
        app.fg = nullptr;
    }
    app.connections.erase(it);
    app.terminal.print_system("Disconnected from " + name);

    // Switch foreground to another connection if available
    if (!app.fg && !app.connections.empty()) {
        app.fg = app.connections.begin()->second.get();
        app.terminal.set_output_context(app.fg->world_name());
        app.terminal.set_status(app.fg->world_name());
    }
}

static void cmd_fg(App& app, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        if (app.fg) {
            app.terminal.print_system("Foreground: " + app.fg->world_name());
        } else {
            app.terminal.print_system("No foreground connection.");
        }
        return;
    }

    auto it = app.connections.find(args[1]);
    if (it == app.connections.end()) {
        app.terminal.print_system("No connection: " + args[1]);
        return;
    }

    app.fg = it->second.get();
    app.terminal.set_output_context(args[1]);
    app.terminal.set_history_context(args[1]);
    app.terminal.scroll_to_bottom();
    app_clear_fg_activity(app);
    app.terminal.print_system("Foreground: " + args[1]);
}

static void cmd_listsockets(App& app, const std::vector<std::string>&) {
    if (app.connections.empty()) {
        app.terminal.print_system("No connections.");
        return;
    }
    for (auto& [name, conn] : app.connections) {
        std::string status = conn->is_connected() ? "connected" : "disconnected";
        std::string fg_mark = (conn.get() == app.fg) ? " [fg]" : "";
        app.terminal.print_system("  " + name + " (" + conn->host() + ":" +
                                  conn->port() + ") " + status + fg_mark);
    }
}

static void cmd_listworlds(App& app, const std::vector<std::string>&) {
    auto names = app.worlddb.names();
    if (names.empty()) {
        app.terminal.print_system("No worlds defined.");
        return;
    }
    for (auto& name : names) {
        const World* w = app.worlddb.find(name);
        if (w) {
            app.terminal.print_system("  " + w->name + " " + w->host + " " + w->port +
                                      (w->use_ssl ? " ssl" : ""));
        }
    }
}

static void cmd_world(App& app, const std::vector<std::string>& args) {
    // /world <name> <host> <port> [ssl] -- define a world
    if (args.size() < 4) {
        app.terminal.print_system("Usage: /world <name> <host> <port> [ssl]");
        return;
    }
    World w;
    w.name = args[1];
    w.host = args[2];
    w.port = args[3];
    if (args.size() > 4 && args[4] == "ssl") w.use_ssl = true;
    app.worlddb.add(w);
    app.terminal.print_system("World defined: " + w.name);
}

static std::string expand_tilde(const std::string& path) {
    if (!path.empty() && path[0] == '~') {
        const char* home = std::getenv("USERPROFILE");
        if (!home) home = std::getenv("HOME");
        if (home) return std::string(home) + path.substr(1);
    }
    return path;
}

static void cmd_log(App& app, const std::vector<std::string>& args) {
    // /log -w <filename>       — log current fg world (persists across /fg)
    // /log -w<world> <filename> — log named world
    // /log <filename>          — global log (all worlds)
    // /log off                 — stop all logging
    // /log                     — show logging status
    if (args.size() < 2) {
        // Show status
        bool any = false;
        if (app.vars.count("_log_file") && !app.vars["_log_file"].empty()) {
            app.terminal.print_system("Global log: " + app.vars["_log_file"]);
            any = true;
        }
        for (auto& [name, conn] : app.connections) {
            if (conn->is_logging()) {
                app.terminal.print_system(name + ": " + conn->log_file());
                any = true;
            }
        }
        if (!any) {
            app.terminal.print_system("Not logging. Usage: /log [-w [world]] <filename>");
        }
        return;
    }

    // Parse -w flag
    bool per_world = false;
    std::string target_world;
    size_t file_arg = 1;

    if (args[1] == "-w" || args[1].substr(0, 2) == "-w") {
        per_world = true;
        if (args[1] == "-w") {
            // -w <file> — use foreground world
            if (!app.fg) {
                app.terminal.print_system("No foreground world.");
                return;
            }
            target_world = app.fg->world_name();
            file_arg = 2;
        } else {
            // -w<world> <file>
            target_world = args[1].substr(2);
            file_arg = 2;
        }
    }

    // /log off — stop logging
    if (file_arg < args.size() && args[file_arg] == "off") {
        if (per_world) {
            auto it = app.connections.find(target_world);
            if (it != app.connections.end() && it->second->is_logging()) {
                it->second->stop_log();
                app.terminal.print_system("Stopped logging " + target_world);
            } else {
                app.terminal.print_system(target_world + " is not being logged.");
            }
        } else {
            app.vars["_log_file"] = "";
            app.terminal.print_system("Logging stopped.");
        }
        return;
    }

    // /log -w (no file) — stop per-world logging
    if (per_world && file_arg >= args.size()) {
        auto it = app.connections.find(target_world);
        if (it != app.connections.end() && it->second->is_logging()) {
            it->second->stop_log();
            app.terminal.print_system("Stopped logging " + target_world);
        } else {
            app.terminal.print_system(target_world + " is not being logged.");
        }
        return;
    }

    if (file_arg >= args.size()) {
        app.terminal.print_system("Usage: /log [-w [world]] <filename>");
        return;
    }

    std::string path = expand_tilde(args[file_arg]);

    if (per_world) {
        auto it = app.connections.find(target_world);
        if (it == app.connections.end()) {
            app.terminal.print_system("No connection to " + target_world);
            return;
        }
        if (it->second->start_log(path)) {
            app.terminal.print_system("Logging " + target_world + " to " + path);
        } else {
            app.terminal.print_system("Failed to open log file: " + path);
        }
    } else {
        app.vars["_log_file"] = path;
        app.terminal.print_system("Logging to " + path);
    }
}

static void cmd_recall(App& app, const std::vector<std::string>& args) {
    std::string pattern;
    int max_lines = 20;
    if (args.size() >= 2) pattern = args[1];
    if (args.size() >= 3) {
        try { max_lines = std::stoi(args[2]); } catch (...) {}
    }
    auto results = app.terminal.recall(pattern, max_lines);
    if (results.empty()) {
        app.terminal.print_system("No matches.");
        return;
    }
    // Print in chronological order (results are newest-first)
    for (int i = (int)results.size() - 1; i >= 0; i--) {
        app.terminal.print_system(results[i]);
    }
}

static void cmd_gmcp(App& app, const std::vector<std::string>& args) {
    if (!app.fg) {
        app.terminal.print_system("Not connected.");
        return;
    }
    auto* telnet = dynamic_cast<Connection*>(app.fg);
    if (!telnet) {
        app.terminal.print_system("GMCP data not available via Hydra (use bidi stream).");
        return;
    }
    auto& data = telnet->gmcp_data();
    if (args.size() >= 2) {
        auto& val = telnet->gmcp_get(args[1]);
        if (val.empty()) {
            app.terminal.print_system("No GMCP data for: " + args[1]);
        } else {
            app.terminal.print_system(args[1] + " " + val);
        }
    } else {
        if (data.empty()) {
            app.terminal.print_system("No GMCP data received.");
        } else {
            for (auto& [pkg, payload] : data) {
                app.terminal.print_system("  " + pkg + " " + payload);
            }
        }
    }
}

static void cmd_mssp(App& app, const std::vector<std::string>&) {
    if (!app.fg) {
        app.terminal.print_system("Not connected.");
        return;
    }
    auto* telnet = dynamic_cast<Connection*>(app.fg);
    if (!telnet) {
        app.terminal.print_system("MSSP not available via Hydra.");
        return;
    }
    auto& data = telnet->mssp_data();
    if (data.empty()) {
        app.terminal.print_system("No MSSP data received.");
        return;
    }
    for (auto& [key, val] : data) {
        app.terminal.print_system("  " + key + ": " + val);
    }
}

static void cmd_def(App& app, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        app.terminal.print_system("Usage: /def [name] -t'pattern' [-p pri] [-n shots] [-g] body");
        return;
    }
    // Reconstruct the argument string after "/def "
    std::string rest;
    for (size_t i = 1; i < args.size(); i++) {
        if (i > 1) rest += " ";
        rest += args[i];
    }
    Macro m;
    std::string err;
    if (!parse_def(rest, m, err)) {
        app.terminal.print_system("Error: " + err);
        return;
    }
    app.macros.define(std::move(m));
    app.terminal.print_system("Defined: " + rest);
}

static void cmd_undef(App& app, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        app.terminal.print_system("Usage: /undef <name>");
        return;
    }
    if (app.macros.undef(args[1])) {
        app.terminal.print_system("Removed: " + args[1]);
    } else {
        app.terminal.print_system("No macro: " + args[1]);
    }
}

static void cmd_list(App& app, const std::vector<std::string>&) {
    auto& all = app.macros.all();
    if (all.empty()) {
        app.terminal.print_system("No macros defined.");
        return;
    }
    for (auto& m : all) {
        std::string desc = "  " + m.name;
        if (!m.trigger.empty()) desc += " -t'" + m.trigger + "'";
        if (m.gag) desc += " -g";
        if (m.priority != 0) desc += " -p" + std::to_string(m.priority);
        if (m.shots >= 0) desc += " -n" + std::to_string(m.shots);
        if (!m.body.empty()) desc += " = " + m.body;
        app.terminal.print_system(desc);
    }
}

static void cmd_bind(App& app, const std::vector<std::string>& args) {
    if (args.size() < 3) {
        // List all bindings
        auto& all = app.keybindings.all();
        if (all.empty()) {
            app.terminal.print_system("No key bindings.");
        } else {
            for (auto& [key, cmd] : all) {
                app.terminal.print_system("  " + format_key_name(key) + " = " + cmd);
            }
        }
        return;
    }
    BindKey bk = parse_key_name(args[1]);
    // Rest of args is the command
    std::string cmd;
    for (size_t i = 2; i < args.size(); i++) {
        if (i > 2) cmd += " ";
        cmd += args[i];
    }
    app.keybindings.bind(bk, cmd);
    app.terminal.print_system("Bound " + format_key_name(bk) + " = " + cmd);
}

static void cmd_unbind(App& app, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        app.terminal.print_system("Usage: /unbind <key>");
        return;
    }
    BindKey bk = parse_key_name(args[1]);
    if (app.keybindings.unbind(bk)) {
        app.terminal.print_system("Unbound " + format_key_name(bk));
    } else {
        app.terminal.print_system("Not bound: " + args[1]);
    }
}

static void cmd_repeat(App& app, const std::vector<std::string>& args) {
    // /repeat <name> <interval_sec> [-n shots] <command>
    if (args.size() < 4) {
        app.terminal.print_system("Usage: /repeat <name> <seconds> [-n shots] <command>");
        return;
    }
    std::string name = args[1];
    int interval_ms = 0;
    try { interval_ms = (int)(std::stod(args[2]) * 1000); } catch (...) {}
    if (interval_ms <= 0) {
        app.terminal.print_system("Invalid interval.");
        return;
    }
    int shots = -1;
    size_t cmd_start = 3;
    if (args.size() > 4 && args[3] == "-n") {
        try { shots = std::stoi(args[4]); } catch (...) {}
        cmd_start = 5;
    }
    std::string cmd;
    for (size_t i = cmd_start; i < args.size(); i++) {
        if (i > cmd_start) cmd += " ";
        cmd += args[i];
    }
    if (cmd.empty()) {
        app.terminal.print_system("No command specified.");
        return;
    }
    app.timers.add(name, cmd, interval_ms, shots);
    app.terminal.print_system("Timer " + name + ": every " + args[2] + "s");
}

static void cmd_killtimer(App& app, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        app.terminal.print_system("Usage: /killtimer <name>");
        return;
    }
    if (app.timers.remove(args[1])) {
        app.terminal.print_system("Removed timer: " + args[1]);
    } else {
        app.terminal.print_system("No timer: " + args[1]);
    }
}

static void cmd_listtimers(App& app, const std::vector<std::string>&) {
    auto& all = app.timers.all();
    if (all.empty()) {
        app.terminal.print_system("No timers.");
        return;
    }
    for (auto& t : all) {
        std::string desc = "  " + t.name + " every " +
                           std::to_string(t.interval_ms / 1000) + "s";
        if (t.shots >= 0) desc += " (" + std::to_string(t.shots) + " left)";
        desc += " = " + t.command;
        app.terminal.print_system(desc);
    }
}

static void cmd_save(App& app, const std::vector<std::string>& args) {
    std::string path = args.size() >= 2 ? args[1] : "worlds.txt";
    if (app.worlddb.save(path)) {
        app.terminal.print_system("Saved to " + path);
    } else {
        app.terminal.print_system("Failed to save: " + path);
    }
}

static void cmd_load(App& app, const std::vector<std::string>& args) {
    std::string path = args.size() >= 2 ? args[1] : "worlds.txt";
    if (app.worlddb.load(path)) {
        app.terminal.print_system("Loaded " + path);
    } else {
        app.terminal.print_system("Failed to load: " + path);
    }
}

// -- Hooks --

static void cmd_hook(App& app, const std::vector<std::string>& args) {
    // /hook <name> <event> = <command>
    if (args.size() < 2) {
        app.terminal.print_system("Usage: /hook <name> <event> = <command>");
        app.terminal.print_system("Events: CONNECT, DISCONNECT, ACTIVITY");
        return;
    }
    // Reconstruct args after /hook
    std::string rest;
    for (size_t i = 1; i < args.size(); i++) {
        if (i > 1) rest += " ";
        rest += args[i];
    }
    auto eq = rest.find('=');
    if (eq == std::string::npos) {
        app.terminal.print_system("Usage: /hook <name> <event> = <command>");
        return;
    }
    std::string before = rest.substr(0, eq);
    std::string body = rest.substr(eq + 1);
    // Trim
    while (!before.empty() && before.back() == ' ') before.pop_back();
    while (!body.empty() && body.front() == ' ') body = body.substr(1);

    std::istringstream ss(before);
    std::string hname, event;
    ss >> hname >> event;
    if (hname.empty() || event.empty()) {
        app.terminal.print_system("Usage: /hook <name> <event> = <command>");
        return;
    }
    std::transform(event.begin(), event.end(), event.begin(), ::toupper);
    if (event != "CONNECT" && event != "DISCONNECT" && event != "ACTIVITY") {
        app.terminal.print_system("Unknown event: " + event);
        return;
    }
    Hook h;
    h.name = hname;
    h.event = event;
    h.body = body;
    app.hooks.add(std::move(h));
    app.terminal.print_system("Hook '" + hname + "' on " + event + " -> " + body);
}

static void cmd_unhook(App& app, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        app.terminal.print_system("Usage: /unhook <name>");
        return;
    }
    if (app.hooks.remove(args[1])) {
        app.terminal.print_system("Hook '" + args[1] + "' removed.");
    } else {
        app.terminal.print_system("No hook: " + args[1]);
    }
}

static void cmd_hooks(App& app, const std::vector<std::string>&) {
    auto& all = app.hooks.all();
    if (all.empty()) {
        app.terminal.print_system("No hooks defined.");
        return;
    }
    for (auto& h : all) {
        app.terminal.print_system("  " + h.name + ": " + h.event +
                                  (h.enabled ? "" : " [off]") + " -> " + h.body);
    }
}

// -- Spawns --

static void cmd_spawn(App& app, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        // List spawns
        auto& all = app.spawns.all();
        if (all.empty()) {
            app.terminal.print_system("No spawns defined. Use /spawn add <name> <pattern>");
        } else {
            for (auto& s : all) {
                std::string desc = "  " + s.name + " (" + s.path + "):";
                for (auto& p : s.patterns) desc += " /" + p + "/";
                app.terminal.print_system(desc);
            }
        }
        return;
    }
    std::string sub = args[1];
    std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

    if (sub == "add" && args.size() >= 4) {
        try {
            std::regex(args[3], std::regex::ECMAScript);
        } catch (const std::exception& e) {
            app.terminal.print_system(std::string("Bad pattern: ") + e.what());
            return;
        }
        SpawnConfig s;
        s.name = args[2];
        s.path = args[2];
        std::transform(s.path.begin(), s.path.end(), s.path.begin(), ::tolower);
        s.patterns.push_back(args[3]);
        app.spawns.add(std::move(s));
        app.terminal.print_system("Spawn '" + args[2] + "' added: /" + args[3] + "/");
    } else if ((sub == "remove" || sub == "del") && args.size() >= 3) {
        std::string path = args[2];
        std::transform(path.begin(), path.end(), path.begin(), ::tolower);
        if (app.spawns.remove(path)) {
            app.terminal.print_system("Spawn '" + args[2] + "' removed.");
        } else {
            app.terminal.print_system("No spawn: " + args[2]);
        }
    } else if (sub == "list") {
        cmd_spawn(app, std::vector<std::string>{args[0]}); // recurse with no sub
    } else {
        app.terminal.print_system("Usage: /spawn [add|remove|list] ...");
    }
}

// -- Variables --

static void cmd_set(App& app, const std::vector<std::string>& args) {
    if (args.size() < 3) {
        app.terminal.print_system("Usage: /set <name> <value>");
        return;
    }
    std::string val;
    for (size_t i = 2; i < args.size(); i++) {
        if (i > 2) val += " ";
        val += args[i];
    }
    app.vars[args[1]] = val;
    app.terminal.print_system("Set " + args[1] + " = " + val);
}

static void cmd_unset(App& app, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        app.terminal.print_system("Usage: /unset <name>");
        return;
    }
    app.vars.erase(args[1]);
    app.terminal.print_system("Unset " + args[1]);
}

static void cmd_vars(App& app, const std::vector<std::string>&) {
    if (app.vars.empty()) {
        app.terminal.print_system("No variables set.");
        return;
    }
    for (auto& [k, v] : app.vars) {
        app.terminal.print_system("  " + k + " = " + v);
    }
}

// -- TTS --

static void cmd_speak(App& app, const std::vector<std::string>& args) {
    if (args.size() < 2) {
        app.terminal.print_system("Usage: /speak <text>");
        return;
    }
    std::string text;
    for (size_t i = 1; i < args.size(); i++) {
        if (i > 1) text += " ";
        text += args[i];
    }
    // Windows SAPI text-to-speech
    // Note: Requires COM initialization and ISpVoice.
    // For now, use simple Beep as placeholder — full SAPI needs sapi.h.
    app.terminal.print_system("[speak] " + text);
    // TODO: Integrate ISpVoice for actual TTS
}

#ifdef HYDRA_GRPC
static void cmd_hcreate(App& app, const std::vector<std::string>& args) {
    if (!app.fg || !app.fg->is_hydra()) {
        app.terminal.print_system("Not connected via Hydra.");
        return;
    }
    if (args.size() < 3) {
        app.terminal.print_system("Usage: /hcreate <username> <password>");
        return;
    }
    auto* h = static_cast<HydraConnection*>(app.fg);
    app.terminal.print_system(h->rpc_create_account(args[1], args[2]));
}

static void cmd_hconnect(App& app, const std::vector<std::string>& args) {
    if (!app.fg || !app.fg->is_hydra()) {
        app.terminal.print_system("Not connected via Hydra.");
        return;
    }
    if (args.size() < 2) {
        app.terminal.print_system("Usage: /hconnect <game>");
        return;
    }
    auto* h = static_cast<HydraConnection*>(app.fg);
    app.terminal.print_system(h->rpc_connect_game(args[1]));
}

static void cmd_hswitch(App& app, const std::vector<std::string>& args) {
    if (!app.fg || !app.fg->is_hydra()) {
        app.terminal.print_system("Not connected via Hydra.");
        return;
    }
    if (args.size() < 2) {
        app.terminal.print_system("Usage: /hswitch <link#>");
        return;
    }
    int link = 0;
    try { link = std::stoi(args[1]); } catch (...) {}
    if (link <= 0) {
        app.terminal.print_system("Invalid link number.");
        return;
    }
    auto* h = static_cast<HydraConnection*>(app.fg);
    app.terminal.print_system(h->rpc_switch_link(link));
}

static void cmd_hlinks(App& app, const std::vector<std::string>&) {
    if (!app.fg || !app.fg->is_hydra()) {
        app.terminal.print_system("Not connected via Hydra.");
        return;
    }
    auto* h = static_cast<HydraConnection*>(app.fg);
    for (auto& line : h->rpc_list_links()) {
        app.terminal.print_system(line);
    }
}

static void cmd_hdisconnect(App& app, const std::vector<std::string>& args) {
    if (!app.fg || !app.fg->is_hydra()) {
        app.terminal.print_system("Not connected via Hydra.");
        return;
    }
    if (args.size() < 2) {
        app.terminal.print_system("Usage: /hdisconnect <link#>");
        return;
    }
    int link = 0;
    try { link = std::stoi(args[1]); } catch (...) {}
    if (link <= 0) {
        app.terminal.print_system("Invalid link number.");
        return;
    }
    auto* h = static_cast<HydraConnection*>(app.fg);
    app.terminal.print_system(h->rpc_disconnect_link(link));
}

static void cmd_hsession(App& app, const std::vector<std::string>&) {
    if (!app.fg || !app.fg->is_hydra()) {
        app.terminal.print_system("Not connected via Hydra.");
        return;
    }
    auto* h = static_cast<HydraConnection*>(app.fg);
    for (auto& line : h->rpc_get_session()) {
        app.terminal.print_system(line);
    }
}

static void cmd_hdetach(App& app, const std::vector<std::string>&) {
    if (!app.fg || !app.fg->is_hydra()) {
        app.terminal.print_system("Not connected via Hydra.");
        return;
    }
    auto* h = static_cast<HydraConnection*>(app.fg);
    app.terminal.print_system(h->rpc_detach_session());
}
#endif // HYDRA_GRPC

static void cmd_help(App& app, const std::vector<std::string>&) {
    app.terminal.print_system("Commands:");
    auto list = app.commands.help_list();
    for (auto& [name, help] : list) {
        app.terminal.print_system("  /" + name + " - " + help);
    }
}

void register_builtin_commands(App& app) {
    app.commands.register_cmd("quit",        cmd_quit,        "Exit the client");
    app.commands.register_cmd("connect",     cmd_connect,     "Connect to a world or host:port");
    app.commands.register_cmd("dc",          cmd_disconnect,  "Disconnect from a world");
    app.commands.register_cmd("disconnect",  cmd_disconnect,  "Disconnect from a world");
    app.commands.register_cmd("fg",          cmd_fg,          "Switch foreground world");
    app.commands.register_cmd("listsockets", cmd_listsockets, "List active connections");
    app.commands.register_cmd("listworlds",  cmd_listworlds,  "List defined worlds");
    app.commands.register_cmd("world",       cmd_world,       "Define a world");
    app.commands.register_cmd("log",         cmd_log,         "Start/stop logging to file");
    app.commands.register_cmd("recall",      cmd_recall,      "Search scrollback [pattern] [count]");
    app.commands.register_cmd("gmcp",        cmd_gmcp,        "Show GMCP data [package]");
    app.commands.register_cmd("mssp",        cmd_mssp,        "Show MSSP server data");
    app.commands.register_cmd("def",         cmd_def,         "Define a trigger/macro");
    app.commands.register_cmd("undef",       cmd_undef,       "Remove a macro by name");
    app.commands.register_cmd("list",        cmd_list,        "List all macros/triggers");
    app.commands.register_cmd("bind",        cmd_bind,        "Bind a key [key command]");
    app.commands.register_cmd("unbind",      cmd_unbind,      "Unbind a key");
    app.commands.register_cmd("repeat",      cmd_repeat,      "Set a timer: name secs [-n N] cmd");
    app.commands.register_cmd("killtimer",   cmd_killtimer,   "Remove a timer");
    app.commands.register_cmd("listtimers",  cmd_listtimers,  "List all timers");
    app.commands.register_cmd("save",        cmd_save,        "Save worlds [file]");
    app.commands.register_cmd("load",        cmd_load,        "Load worlds [file]");
    app.commands.register_cmd("hook",        cmd_hook,        "Define event hook: name event = cmd");
    app.commands.register_cmd("unhook",      cmd_unhook,      "Remove a hook by name");
    app.commands.register_cmd("hooks",       cmd_hooks,       "List all hooks");
    app.commands.register_cmd("spawn",       cmd_spawn,       "Manage output spawns");
    app.commands.register_cmd("set",         cmd_set,         "Set a variable");
    app.commands.register_cmd("unset",       cmd_unset,       "Remove a variable");
    app.commands.register_cmd("vars",        cmd_vars,        "List variables");
    app.commands.register_cmd("speak",       cmd_speak,       "Text-to-speech");
    app.commands.register_cmd("help",        cmd_help,        "Show this help");
#ifdef HYDRA_GRPC
    app.commands.register_cmd("hcreate",     cmd_hcreate,     "Create a Hydra account");
    app.commands.register_cmd("hconnect",    cmd_hconnect,    "Connect to a game on Hydra");
    app.commands.register_cmd("hswitch",     cmd_hswitch,     "Switch active Hydra link");
    app.commands.register_cmd("hlinks",      cmd_hlinks,      "List active Hydra links");
    app.commands.register_cmd("hdisconnect", cmd_hdisconnect, "Disconnect a Hydra link");
    app.commands.register_cmd("hsession",    cmd_hsession,    "Show Hydra session info");
    app.commands.register_cmd("hdetach",     cmd_hdetach,     "Detach from Hydra session");
#endif
}
