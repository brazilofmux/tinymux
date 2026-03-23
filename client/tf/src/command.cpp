#include "command.h"
#include "world.h"
#include "connection.h"
#ifdef HYDRA_GRPC
#include "hydra_connection.h"
#endif
#include "terminal.h"
#include "script.h"
#include "macro.h"
#include "timer.h"
#include "keybind.h"
#include "spawn.h"
#include "restart.h"
#include <sstream>
#include <fstream>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <unistd.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>

// Forward declaration of App (defined in main.cpp)
struct App;

// Forward declaration for commands defined after register_commands().
static void cmd_spawn(App& app, const std::string& args);

// We need the App definition from main.cpp. Since App is defined there
// and we're compiled separately, we include a minimal definition here
// that must match main.cpp exactly.
#include "app.h"

static std::string trim_copy(std::string s) {
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

static std::string quote_world_arg(const std::string& s) {
    std::string out = "\"";
    out.reserve(s.size() + 2);
    for (char ch : s) {
        if (ch == '"' || ch == '\\') out += '\\';
        out += ch;
    }
    out += '"';
    return out;
}

static ScriptEnv& active_script_env(App& app, ScriptEnv& fallback) {
    return app.current_env ? *app.current_env : fallback;
}

CommandDispatcher::CommandDispatcher() {
    register_commands();
}

void CommandDispatcher::register_commands() {
    commands_["/quit"]    = cmd_quit;
    commands_["/connect"] = cmd_connect;
    commands_["/dc"]      = cmd_dc;
    commands_["/fg"]      = cmd_fg;
    commands_["/fg_next"] = cmd_fg_next;
    commands_["/fg_prev"] = cmd_fg_prev;
    commands_["/world"]   = cmd_world;
    commands_["/set"]     = cmd_set;
    commands_["/load"]    = cmd_load;
    commands_["/recall"]  = cmd_recall;
    commands_["/help"]    = cmd_help;
    commands_["/test"]    = cmd_test;
    commands_["/expr"]    = cmd_expr;
    commands_["/echo"]    = cmd_echo;
    commands_["/let"]     = cmd_let;
    commands_["/def"]     = cmd_def;
    commands_["/undef"]   = cmd_undef;
    commands_["/list"]    = cmd_list;
    commands_["/if"]      = cmd_if;
    commands_["/while"]   = cmd_while_cmd;
    commands_["/repeat"]  = cmd_repeat;
    commands_["/kill"]    = cmd_kill;
    commands_["/ps"]      = cmd_ps;
    commands_["/quote"]   = cmd_quote;
    commands_["/sh"]      = cmd_sh;
    commands_["/sys"]     = cmd_sys;
    commands_["/listworlds"]  = cmd_listworlds;
    commands_["/listsockets"] = cmd_listsockets;
    commands_["/gag"]     = cmd_gag;
    commands_["/hilite"]  = cmd_hilite;
    commands_["/trigger"] = cmd_trigger;
    commands_["/purge"]   = cmd_purge;
    commands_["/save"]    = cmd_save;
    commands_["/log"]     = cmd_log;
    commands_["/version"] = cmd_version;
    commands_["/eval"]    = cmd_eval;
    commands_["/unworld"] = cmd_unworld;
    commands_["/bind"]    = cmd_bind;
    commands_["/unbind"]  = cmd_unbind;
    commands_["/dokey"]   = cmd_dokey;
    commands_["/listvar"] = cmd_listvar;
    commands_["/export"]  = cmd_export;
    commands_["/setenv"]  = cmd_setenv;
    commands_["/shift"]   = cmd_shift;
    commands_["/hook"]    = cmd_hook;
    commands_["/beep"]    = cmd_beep;
    commands_["/suspend"] = cmd_suspend;
    commands_["/features"]= cmd_features;
    commands_["/saveworld"] = cmd_saveworld;
    commands_["/recordline"] = cmd_recordline;
    commands_["/lcd"]     = cmd_lcd;
    commands_["/limit"]   = cmd_limit;
    commands_["/unlimit"] = cmd_unlimit;
    commands_["/input"]   = cmd_input;
    commands_["/relimit"] = cmd_relimit;
    commands_["/core"]    = cmd_core;
    commands_["/edit"]    = cmd_edit;
    commands_["/histsize"]= cmd_histsize;
    commands_["/liststreams"] = cmd_liststreams;
    commands_["/localecho"] = cmd_localecho;
    commands_["/restrict"]= cmd_restrict;
    commands_["/status_add"] = cmd_status_add;
    commands_["/status_edit"] = cmd_status_edit;
    commands_["/status_rm"] = cmd_status_rm;
    commands_["/trigpc"]  = cmd_trigpc;
    commands_["/undefn"]  = cmd_undefn;
    commands_["/unset"]   = cmd_unset;
    commands_["/watchdog"]= cmd_watchdog;
    commands_["/watchname"] = cmd_watchname;
    commands_["/restart"] = cmd_restart;
    commands_["/update"]  = cmd_update;
    commands_["/exit"]    = cmd_quit;  // alias
    commands_["/spawn"]   = cmd_spawn;
}

bool CommandDispatcher::dispatch(App& app, const std::string& line) {
    // Extract command name (first word)
    size_t sp = line.find(' ');
    std::string cmd = (sp != std::string::npos) ? line.substr(0, sp) : line;
    std::string args = (sp != std::string::npos) ? line.substr(sp + 1) : "";

    // Convert command to lowercase for matching
    std::string cmd_lower = cmd;
    std::transform(cmd_lower.begin(), cmd_lower.end(), cmd_lower.begin(), ::tolower);

    auto it = commands_.find(cmd_lower);
    if (it != commands_.end()) {
        it->second(app, args);
        return true;
    }

    // Classic TF compatibility: /dokey_FOO → /dokey FOO
    //
    if (cmd_lower.substr(0, 7) == "/dokey_") {
        std::string dokey_arg = cmd_lower.substr(7);
        if (!args.empty()) dokey_arg += " " + args;
        cmd_dokey(app, dokey_arg);
        return true;
    }

    return false;
}

void cmd_quit(App& app, const std::string& /*args*/) {
    // Disconnect all
    app.connections.clear();
    app.fg = nullptr;
    app.running = false;
}

void cmd_connect(App& app, const std::string& args) {
    std::string name = trim_copy(args);

    if (name.empty()) {
        app.terminal.print_system("Usage: /connect <world>");
        return;
    }

    // Check if already connected
    auto it = app.connections.find(name);
    if (it != app.connections.end() && it->second->is_connected()) {
        app.fg = it->second.get();
        app_clear_fg_activity(app);
        app.terminal.set_history_context(app.fg->world_name());
        if (std::string prompt = app.fg->current_prompt(); !prompt.empty()) {
            app.terminal.set_prompt(prompt);
        } else {
            app.terminal.clear_prompt();
        }
        app.terminal.print_system("Switched to " + name);
        return;
    }

    const World* w = app.worlddb.find(name);
    if (!w) {
        app.terminal.print_system("Unknown world: " + name);
        return;
    }

    std::unique_ptr<IConnection> conn;

#ifdef HYDRA_GRPC
    if (w->type == "hydra") {
        // gRPC connection to Hydra proxy
        // For hydra worlds: host:port = gRPC endpoint, character = username,
        // password = password, name of first game to connect comes from mfile field
        std::string game = w->mfile;  // repurpose mfile as game name
        conn = std::make_unique<HydraConnection>(
            w->name, w->host, w->port, w->character, w->password, game, w->ssl());
        app.terminal.print_system("Connecting to Hydra at " + w->host + ":" + w->port
            + (w->ssl() ? " (TLS)" : "") + "...");
    } else
#endif
    {
        auto telnet = std::make_unique<Connection>(w->name, w->host, w->port, w->ssl());
        telnet->send_naws((uint16_t)app.terminal.get_cols(), (uint16_t)app.terminal.get_rows());
        app.terminal.print_system("Connecting to " + w->name + " (" + w->host + ":" + w->port +
                                  (w->ssl() ? " SSL" : "") + ")...");
        conn = std::move(telnet);
    }

    if (!conn->connect()) {
        app.terminal.print_system("Failed to connect to " + w->name);
        return;
    }

    IConnection* raw = conn.get();
    app.connections[w->name] = std::move(conn);
    app.fg = raw;
    app_clear_fg_activity(app);
    app.terminal.set_history_context(app.fg->world_name());
    app.terminal.set_output_context(app.fg->world_name());
    app.terminal.print_system("Connected to " + w->name);

    // Auto-login for telnet worlds (Hydra handles auth via gRPC)
    if (!raw->is_hydra() && !w->character.empty()) {
        if (app_send_line(app, app.fg, "connect " + w->character + " " + w->password, false)) {
            fire_hook(app, Hook::LOGIN, w->name);
        }
    }

    fire_hook(app, Hook::CONNECT, w->name);
}

void cmd_dc(App& app, const std::string& args) {
    std::string name = trim_copy(args);

    IConnection* target = nullptr;
    if (name.empty()) {
        target = app.fg;
    } else {
        auto it = app.connections.find(name);
        if (it != app.connections.end()) target = it->second.get();
    }

    if (!target) {
        app.terminal.print_system("No connection to disconnect");
        return;
    }

    std::string wname = target->world_name();
    target->disconnect();
    app.terminal.print_system("Disconnected from " + wname);

    // Remove from connections and clear any stale activity.
    app.connections.erase(wname);
    app.active_worlds.erase(wname);

    // Update foreground
    if (app.fg == target || app.fg == nullptr) {
        app.terminal.clear_prompt();
        app.fg = nullptr;
        if (!app.connections.empty()) {
            app.fg = app.connections.begin()->second.get();
            app_clear_fg_activity(app);
            app.terminal.set_history_context(app.fg->world_name());
            app.terminal.set_output_context(app.fg->world_name());
            if (std::string prompt = app.fg->current_prompt(); !prompt.empty()) {
                app.terminal.set_prompt(prompt);
            } else {
                app.terminal.clear_prompt();
            }
            app.terminal.print_system("Foreground: " + app.fg->world_name());
        } else {
            app.terminal.set_history_context("");
            app.terminal.set_output_context("");
        }
    }
}

void cmd_fg(App& app, const std::string& args) {
    std::string name = trim_copy(args);

    if (name.empty()) {
        if (app.fg) {
            app.terminal.print_system("Foreground: " + app.fg->world_name());
        } else {
            app.terminal.print_system("No foreground connection");
        }
        return;
    }

    // Find by name (case insensitive)
    for (auto& [wname, conn] : app.connections) {
        if (wname.size() == name.size() &&
            std::equal(wname.begin(), wname.end(), name.begin(),
                       [](char a, char b) { return tolower(a) == tolower(b); })) {
            app.fg = conn.get();
            app_clear_fg_activity(app);
            app.terminal.set_history_context(app.fg->world_name());
            app.terminal.set_output_context(app.fg->world_name());
            if (std::string prompt = app.fg->current_prompt(); !prompt.empty()) {
                app.terminal.set_prompt(prompt);
            } else {
                app.terminal.clear_prompt();
            }
            app.terminal.print_system("Foreground: " + wname);

            // Replay scrollback to output
            app.terminal.scroll_to_bottom();
            return;
        }
    }

    app.terminal.print_system("No connection to: " + name);
}

// Cycle to the next/previous connection (Esc-Right / Esc-Left in classic TF).
//
static void fg_cycle(App& app, int direction) {
    if (app.connections.size() < 2) return;

    // Build ordered name list.
    std::vector<std::string> names;
    for (auto& [name, conn] : app.connections) names.push_back(name);
    std::sort(names.begin(), names.end());

    // Find current position.
    std::string cur = app.fg ? app.fg->world_name() : "";
    int pos = 0;
    for (int i = 0; i < (int)names.size(); i++) {
        if (names[i] == cur) { pos = i; break; }
    }

    pos = (pos + direction + (int)names.size()) % (int)names.size();
    std::string target = names[pos];

    auto it = app.connections.find(target);
    if (it == app.connections.end()) return;

    app.fg = it->second.get();
    app_clear_fg_activity(app);
    app.terminal.set_history_context(app.fg->world_name());
    app.terminal.set_output_context(app.fg->world_name());
    if (std::string prompt = app.fg->current_prompt(); !prompt.empty()) {
        app.terminal.set_prompt(prompt);
    } else {
        app.terminal.clear_prompt();
    }
    app.terminal.print_system("Foreground: " + target);
    app.terminal.scroll_to_bottom();
}

void cmd_fg_next(App& app, const std::string& /*args*/) {
    fg_cycle(app, 1);
}

void cmd_fg_prev(App& app, const std::string& /*args*/) {
    fg_cycle(app, -1);
}

void cmd_world(App& app, const std::string& /*args*/) {
    if (app.worlddb.worlds().empty()) {
        app.terminal.print_system("No worlds loaded");
        return;
    }
    app.terminal.print_system("Known worlds:");
    for (auto& w : app.worlddb.worlds()) {
        std::string info = "  " + w.name + " - " + w.host + ":" + w.port;
        if (w.ssl()) info += " [SSL]";
        // Mark if connected
        auto it = app.connections.find(w.name);
        if (it != app.connections.end() && it->second->is_connected()) {
            info += " [connected]";
            if (app.fg == it->second.get()) info += " [fg]";
        }
        app.terminal.print_system(info);
    }
}

void cmd_set(App& app, const std::string& args) {
    if (args.empty()) {
        // Display all vars
        for (auto& [k, v] : app.vars) {
            app.terminal.print_system(k + "=" + v);
        }
        return;
    }
    auto eq = args.find('=');
    if (eq == std::string::npos) {
        // Display one var
        auto it = app.vars.find(args);
        if (it != app.vars.end()) {
            app.terminal.print_system(it->first + "=" + it->second);
        } else {
            app.terminal.print_system(args + " is not set");
        }
        return;
    }
    std::string name = args.substr(0, eq);
    std::string val  = args.substr(eq + 1);
    app.vars[name] = val;
    app.terminal.print_system(name + "=" + val);

    // Reactive status bar update — redraw if this variable could
    // affect any status field (direct display, format, or attribute).
    //
    app.terminal.update_status();
}

void cmd_load(App& app, const std::string& args) {
    std::string path = trim_copy(args);

    if (path.empty()) {
        app.terminal.print_system("Usage: /load <file>");
        return;
    }

    bool looks_like_world = path.size() >= 6 && path.substr(path.size() - 6) == ".world";
    if (looks_like_world) {
        int before = static_cast<int>(app.worlddb.worlds().size());
        int count = app.worlddb.load(path);
        if (count < 0) {
            app.terminal.print_system("Cannot open: " + path);
            return;
        }

        for (const auto& [k, v] : app.worlddb.vars()) {
            app.vars[k] = v;
        }

        int added = static_cast<int>(app.worlddb.worlds().size()) - before;
        app.terminal.print_system("Loaded " + std::to_string(added) + " worlds from " + path);
        return;
    }

    std::ifstream f(path);
    if (!f.is_open()) {
        app.terminal.print_system("Cannot open: " + path);
        return;
    }

    int count = 0;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == ';') continue;
        if (line[0] == '/') {
            app.commands.dispatch(app, line);
        }
        count++;
    }
    app.terminal.print_system("Loaded " + std::to_string(count) + " lines from " + path);
}

void cmd_recall(App& app, const std::string& args) {
    int n = 20; // default
    if (!args.empty()) {
        try { n = std::stoi(args); } catch (...) {}
    }

    if (!app.fg) {
        app.terminal.print_system("No foreground connection");
        return;
    }

    auto& sb = app.fg->scrollback();
    int start = (int)sb.size() - n;
    if (start < 0) start = 0;

    app.terminal.print_system("--- Recall " + std::to_string(sb.size() - start) + " lines ---");
    for (int i = start; i < (int)sb.size(); i++) {
        app.terminal.print_line(sb[i]);
    }
}

void cmd_help(App& app, const std::string& /*args*/) {
    app.terminal.print_system("Commands: /connect <world>, /dc [world], /fg [world], "
                              "/world, /set [name=value], /load <file>, /recall [n], /quit");
    app.terminal.print_system("Scripting: /test <expr>, /expr <expr>, /echo <text>, /let <name>=<expr>");
    app.terminal.print_system("Substitution: %{var}, $[expr] in /echo and outbound text");
    app.terminal.print_system("PgUp/PgDn to scroll, Up/Down for input history");
}

void cmd_test(App& app, const std::string& args) {
    // /test expression — evaluate and print result (TF: sets user_result)
    ScriptEnv fallback(app.vars, &app);
    ScriptEnv& env = active_script_env(app, fallback);
    Value v = eval_expr(args, env);
    app.terminal.print_system(v.as_str());
}

void cmd_expr(App& app, const std::string& args) {
    // /expr — same as /test, evaluate expression
    ScriptEnv fallback(app.vars, &app);
    ScriptEnv& env = active_script_env(app, fallback);
    Value v = eval_expr(args, env);
    app.terminal.print_system(v.as_str());
}

void cmd_echo(App& app, const std::string& args) {
    // /echo text — print with substitution expansion
    ScriptEnv fallback(app.vars, &app);
    ScriptEnv& env = active_script_env(app, fallback);
    std::string expanded = expand_subs(args, env);
    app.terminal.print_line(expanded);
}

void cmd_let(App& app, const std::string& args) {
    auto eq = args.find('=');
    if (eq == std::string::npos) {
        app.terminal.print_system("Usage: /let name=expression");
        return;
    }
    std::string name = trim_copy(args.substr(0, eq));
    std::string expr = args.substr(eq + 1);

    ScriptEnv fallback(app.vars, &app);
    ScriptEnv& env = active_script_env(app, fallback);
    Value v = eval_expr(expr, env);
    env.set(name, v);
    app.terminal.print_system(name + "=" + v.as_str());
}

void cmd_def(App& app, const std::string& args) {
    Macro m;
    std::string error;
    if (!parse_def(args, m, error)) {
        app.terminal.print_system("% /def: " + error);
        return;
    }
    std::string info = "% Macro defined: " + m.name;
    if (!m.trigger.empty()) info += " -t'" + m.trigger + "'";
    if (!m.hook.empty()) info += " -h'" + m.hook + "'";
    if (m.priority) info += " -p" + std::to_string(m.priority);
    if (m.shots >= 0) info += " -n" + std::to_string(m.shots);
    app.terminal.print_system(info);
    app.macros.define(std::move(m));
}

void cmd_undef(App& app, const std::string& args) {
    std::string name = trim_copy(args);
    if (app.macros.undef(name)) {
        app.terminal.print_system("% Undefined: " + name);
    } else {
        app.terminal.print_system("% Not defined: " + name);
    }
}

void cmd_list(App& app, const std::string& /*args*/) {
    auto& all = app.macros.all();
    if (all.empty()) {
        app.terminal.print_system("No macros defined");
        return;
    }
    for (auto& m : all) {
        std::string line = "/def ";
        if (!m.trigger.empty()) line += "-t'" + m.trigger + "' ";
        if (!m.hook.empty()) line += "-h'" + m.hook + "' ";
        if (m.priority) line += "-p" + std::to_string(m.priority) + " ";
        if (m.shots >= 0) line += "-n" + std::to_string(m.shots) + " ";
        if (m.fallthrough) line += "-F ";
        if (m.match_type != "glob") line += "-m" + m.match_type + " ";
        line += m.name + " = " + m.body;
        app.terminal.print_system(line);
    }
}

void cmd_if(App& app, const std::string& args) {
    // Inline /if: evaluate condition and run the rest as a single command
    // For multi-statement /if, use exec_body which handles /if.../endif
    ScriptEnv fallback(app.vars, &app);
    ScriptEnv& env = active_script_env(app, fallback);

    // Parse: /if (condition) command
    std::string s = args;
    s = trim_copy(s);
    std::string cond, cmd;

    if (!s.empty() && s[0] == '(') {
        // Find matching paren
        int depth = 1;
        size_t i = 1;
        while (i < s.size() && depth > 0) {
            if (s[i] == '(') depth++;
            else if (s[i] == ')') depth--;
            i++;
        }
        cond = s.substr(1, i - 2);
        cmd = trim_copy(s.substr(i));
    } else {
        app.terminal.print_system("Usage: /if (expression) command");
        return;
    }

    Value v = eval_expr(cond, env);
    if (v.as_bool() && !cmd.empty()) {
        // Route through exec_body so user macros are found too
        exec_body_in_env(app, cmd, env);
    }
}

void cmd_while_cmd(App& app, const std::string& args) {
    // Inline /while: /while (condition) command
    ScriptEnv fallback(app.vars, &app);
    ScriptEnv& env = active_script_env(app, fallback);
    std::string s = trim_copy(args);
    std::string cond, cmd;

    if (!s.empty() && s[0] == '(') {
        int depth = 1;
        size_t i = 1;
        while (i < s.size() && depth > 0) {
            if (s[i] == '(') depth++;
            else if (s[i] == ')') depth--;
            i++;
        }
        cond = s.substr(1, i - 2);
        cmd = trim_copy(s.substr(i));
    } else {
        app.terminal.print_system("Usage: /while (expression) command");
        return;
    }

    int limit = 10000;  // safety limit
    while (limit-- > 0) {
        Value v = eval_expr(cond, env);
        if (!v.as_bool()) break;
        if (!cmd.empty()) {
            exec_body_in_env(app, cmd, env);
        }
    }
}

void cmd_repeat(App& app, const std::string& args) {
    int count;
    std::chrono::milliseconds interval;
    std::string command, error;

    if (!parse_repeat(args, count, interval, command, error)) {
        app.terminal.print_system("% /repeat: " + error);
        return;
    }

    int id = app.timers.add(command, count, interval);
    double secs = interval.count() / 1000.0;
    std::string info = "% Process " + std::to_string(id) + ": ";
    if (count < 0) {
        info += "every " + std::to_string(secs) + "s: " + command;
    } else {
        info += std::to_string(count) + "x every " + std::to_string(secs) + "s: " + command;
    }
    app.terminal.print_system(info);
}

void cmd_kill(App& app, const std::string& args) {
    std::string s = args;
    while (!s.empty() && s.front() == ' ') s.erase(0, 1);
    while (!s.empty() && s.back() == ' ') s.pop_back();

    if (s.empty()) {
        app.terminal.print_system("Usage: /kill <pid> or /kill all");
        return;
    }

    if (s == "all") {
        int n = (int)app.timers.all().size();
        app.timers.kill_all();
        app.terminal.print_system("% Killed " + std::to_string(n) + " processes");
        return;
    }

    int id = std::atoi(s.c_str());
    if (app.timers.kill(id)) {
        app.terminal.print_system("% Killed process " + std::to_string(id));
    } else {
        app.terminal.print_system("% No such process: " + std::to_string(id));
    }
}

void cmd_ps(App& app, const std::string& /*args*/) {
    auto& all = app.timers.all();
    if (all.empty()) {
        app.terminal.print_system("No active processes");
        return;
    }
    for (auto& t : all) {
        double secs = t.interval.count() / 1000.0;
        std::string line = "  " + std::to_string(t.id) + ": ";
        if (t.remaining < 0) {
            line += "(infinite)";
        } else {
            line += std::to_string(t.remaining) + " remaining";
        }
        line += " every " + std::to_string(secs) + "s: " + t.command;
        app.terminal.print_system(line);
    }
}

void cmd_quote(App& app, const std::string& args) {
    // /quote [-0] [-dchar] [-w<world>] !<shell command>
    // /quote [-0] [-dchar] [-w<world>] '<tf command>
    // -0 = prefix each line with /
    // -d = use delimiter char to split lines before sending
    // Default: send each line from the external command to the MUD
    std::string s = trim_copy(args);
    bool as_cmd = false;  // -0 flag: execute as TF commands
    std::string world;

    // Parse flags
    while (!s.empty() && s[0] == '-') {
        if (s.size() >= 2 && s[1] == '0') {
            as_cmd = true;
            s = trim_copy(s.substr(2));
        } else if (s.size() >= 2 && s[1] == 'w') {
            s = s.substr(2);
            size_t sp = s.find(' ');
            world = (sp != std::string::npos) ? s.substr(0, sp) : s;
            s = (sp != std::string::npos) ? trim_copy(s.substr(sp)) : "";
        } else if (s.size() >= 2 && s[1] == 'd') {
            // -d flag: skip for now (delimiter mode)
            s = trim_copy(s.substr(3));
        } else {
            break;
        }
    }

    if (s.empty()) {
        app.terminal.print_system("Usage: /quote [-0] !<command> or /quote '<tf command>");
        return;
    }

    // Determine mode
    if (s[0] == '!' || s[0] == '`') {
        // Shell command: pipe output
        std::string shell_cmd = s.substr(1);
        if (!app_spawn_shell(app, shell_cmd,
                as_cmd ? ShellDisposition::Exec : ShellDisposition::Send, world)) {
            app.terminal.print_system("% /quote: failed to run: " + shell_cmd);
        }
    } else if (s[0] == '\'') {
        // TF command as prefix: run it
        if (app.current_env) exec_body_in_env(app, s.substr(1), *app.current_env);
        else exec_body(app, s.substr(1));
    } else {
        // Default: send lines to MUD
        IConnection* target = app.fg;
        if (target && target->is_connected()) {
            app_send_line(app, target, s);
        }
    }
}

void cmd_sh(App& app, const std::string& args) {
    // /sh <command> — run shell command, display output
    std::string cmd = trim_copy(args);
    if (cmd.empty()) {
        app.terminal.print_system("Usage: /sh <command>");
        return;
    }
    if (!app_spawn_shell(app, cmd, ShellDisposition::Echo)) {
        app.terminal.print_system("% /sh: failed to run: " + cmd);
    }
}

void cmd_sys(App& app, const std::string& args) {
    // /sys — alias for /sh
    cmd_sh(app, args);
}

void cmd_listworlds(App& app, const std::string& /*args*/) {
    cmd_world(app, "");
}

void cmd_listsockets(App& app, const std::string& /*args*/) {
    if (app.connections.empty()) {
        app.terminal.print_system("No active connections");
        return;
    }
    for (auto& [name, conn] : app.connections) {
        std::string info = "  " + name + " - " + conn->host() + ":" + conn->port();
        if (conn->uses_ssl()) info += " [SSL]";
        if (conn->is_connected()) info += " [connected]";
        if (conn.get() == app.fg) info += " [fg]";
        app.terminal.print_system(info);
    }
}

void cmd_gag(App& app, const std::string& args) {
    // /gag <pattern> — suppress lines matching pattern from display
    std::string pattern = trim_copy(args);
    if (pattern.empty()) {
        // List active gags
        bool any = false;
        for (auto& m : app.macros.all()) {
            if (m.gag) {
                app.terminal.print_system("  /gag " + m.trigger + "  [" + m.name + "]");
                any = true;
            }
        }
        if (!any) app.terminal.print_system("No gags defined");
        return;
    }
    static int gag_id = 0;
    Macro m;
    m.name = "_gag_" + std::to_string(++gag_id);
    m.trigger = pattern;
    m.match_type = "glob";
    m.body = "";
    m.gag = true;
    m.quiet = true;
    app.macros.define(std::move(m));
    app.terminal.print_system("% Gagged: " + pattern);
}

void cmd_hilite(App& app, const std::string& args) {
    // /hilite <pattern> — bold-highlight matching text in output
    std::string pattern = trim_copy(args);
    if (pattern.empty()) {
        // List active hilites
        bool any = false;
        for (auto& m : app.macros.all()) {
            if (m.hilite) {
                app.terminal.print_system("  /hilite " + m.trigger + "  [" + m.name + "]");
                any = true;
            }
        }
        if (!any) app.terminal.print_system("No hilites defined");
        return;
    }
    static int hilite_id = 0;
    Macro m;
    m.name = "_hilite_" + std::to_string(++hilite_id);
    m.trigger = pattern;
    m.match_type = "glob";
    m.body = "";
    m.hilite = true;
    m.fallthrough = true;  // don't consume the line — let other triggers see it
    m.quiet = true;
    app.macros.define(std::move(m));
    app.terminal.print_system("% Hilited: " + pattern);
}

void cmd_trigger(App& app, const std::string& args) {
    // /trigger — alias for /def -t
    // Rewrite as /def -t and dispatch
    if (args.empty()) {
        // List triggers
        for (auto& m : app.macros.all()) {
            if (!m.trigger.empty()) {
                std::string line = "  " + m.name + " -t'" + m.trigger + "' = " + m.body;
                app.terminal.print_system(line);
            }
        }
        return;
    }
    auto eq = args.find('=');
    if (eq == std::string::npos) {
        app.terminal.print_system("Usage: /trigger <pattern> = <body>");
        return;
    }

    std::string pattern = trim_copy(args.substr(0, eq));
    std::string body = trim_copy(args.substr(eq + 1));
    if (pattern.empty() || body.empty()) {
        app.terminal.print_system("Usage: /trigger <pattern> = <body>");
        return;
    }

    static int trigger_id = 0;
    std::string name = "_trigger_" + std::to_string(++trigger_id);
    cmd_def(app, "-t'" + pattern + "' " + name + " = " + body);
}

void cmd_purge(App& app, const std::string& args) {
    // /purge [pattern] — remove macros matching pattern, or all
    std::string pattern = trim_copy(args);
    if (pattern.empty()) {
        int n = (int)app.macros.all().size();
        while (!app.macros.all().empty()) {
            app.macros.undef(app.macros.all().front().name);
        }
        app.terminal.print_system("% Purged " + std::to_string(n) + " macros");
        return;
    }
    // Remove macros whose name matches the glob pattern
    std::vector<std::string> to_remove;
    for (auto& m : app.macros.all()) {
        if (fnmatch(pattern.c_str(), m.name.c_str(), 0) == 0) {
            to_remove.push_back(m.name);
        }
    }
    for (auto& name : to_remove) {
        app.macros.undef(name);
    }
    app.terminal.print_system("% Purged " + std::to_string(to_remove.size()) + " macros");
}

void cmd_save(App& app, const std::string& args) {
    // /save <filename> — save macros to file
    std::string path = trim_copy(args);
    if (path.empty()) path = "macros.tf";

    std::ofstream f(path);
    if (!f.is_open()) {
        app.terminal.print_system("% Cannot write: " + path);
        return;
    }

    int count = 0;
    for (auto& m : app.macros.all()) {
        f << "/def ";
        if (!m.trigger.empty()) f << "-t'" << m.trigger << "' ";
        if (!m.hook.empty()) f << "-h'" << m.hook << "' ";
        if (m.priority) f << "-p" << m.priority << " ";
        if (m.shots >= 0) f << "-n" << m.shots << " ";
        if (m.fallthrough) f << "-F ";
        if (m.match_type != "glob") f << "-m" << m.match_type << " ";
        f << m.name << " = " << m.body << "\n";
        count++;
    }
    f.close();
    app.terminal.print_system("% Saved " + std::to_string(count) + " macros to " + path);
}

void cmd_log(App& app, const std::string& args) {
    // /log -w <filename>   — log current fg world (persists across /fg)
    // /log -w<world> <filename> — log named world
    // /log <filename>      — global log (all worlds)
    // /log off              — stop all logging
    // /log                  — show logging status
    std::string rest = trim_copy(args);

    // Determine target: per-world (-w) or global.
    bool per_world = false;
    std::string target_world;
    if (rest.substr(0, 2) == "-w") {
        per_world = true;
        std::string after_w = rest.substr(2);
        // -w<world> <file>  or  -w <file> (meaning fg world)
        if (after_w.empty() || after_w[0] == ' ') {
            // -w <file> — use foreground world
            if (!app.fg) {
                app.terminal.print_system("% No foreground world");
                return;
            }
            target_world = app.fg->world_name();
            rest = trim_copy(after_w);
        } else {
            // -w<world> <file>
            auto sp = after_w.find(' ');
            if (sp == std::string::npos) {
                target_world = after_w;
                rest.clear();
            } else {
                target_world = after_w.substr(0, sp);
                rest = trim_copy(after_w.substr(sp + 1));
            }
        }
    }

    // /log off — stop logging
    if (rest == "off" || (rest.empty() && per_world)) {
        if (per_world) {
            // Stop logging for the target world
            auto it = app.connections.find(target_world);
            if (it != app.connections.end() && !it->second->log_file.empty()) {
                app.terminal.print_system("% Stopped logging " + target_world);
                it->second->stop_log();
            } else {
                app.terminal.print_system("% " + target_world + " is not being logged");
            }
        } else if (rest == "off") {
            // Stop global logging
            if (app.vars.count("_log_file") && !app.vars["_log_file"].empty()) {
                app.terminal.print_system("% Logging stopped");
                app.vars["_log_file"] = "";
            } else {
                app.terminal.print_system("% Not logging");
            }
        } else {
            // /log with no args — show status
            bool any = false;
            if (app.vars.count("_log_file") && !app.vars["_log_file"].empty()) {
                app.terminal.print_system("% Global log: " + app.vars["_log_file"]);
                any = true;
            }
            for (auto& [name, conn] : app.connections) {
                if (!conn->log_file.empty()) {
                    app.terminal.print_system("% " + name + ": " + conn->log_file);
                    any = true;
                }
            }
            if (!any) {
                app.terminal.print_system("% Not logging");
            }
        }
        return;
    }

    if (rest.empty()) {
        // Shouldn't reach here, but just in case
        app.terminal.print_system("% Usage: /log [-w [world]] <filename>");
        return;
    }

    // Expand ~ to $HOME
    if (!rest.empty() && rest[0] == '~') {
        const char* home = std::getenv("HOME");
        if (home) rest = std::string(home) + rest.substr(1);
    }

    if (per_world) {
        auto it = app.connections.find(target_world);
        if (it == app.connections.end()) {
            app.terminal.print_system("% No connection to " + target_world);
            return;
        }
        it->second->start_log(rest);
        if (it->second->log_fp) {
            app.terminal.print_system("% Logging " + target_world + " to " + rest);
        } else {
            app.terminal.print_system("% Cannot open " + rest);
        }
    } else {
        // Global logging (original behavior)
        app.vars["_log_file"] = rest;
        app.terminal.print_system("% Logging to " + rest);
    }
}

void cmd_eval(App& app, const std::string& args) {
    // /eval text — expand substitutions and execute result as a command
    ScriptEnv fallback(app.vars, &app);
    ScriptEnv& env = active_script_env(app, fallback);
    std::string expanded = expand_subs(args, env);
    exec_body_in_env(app, expanded, env);
}

void cmd_unworld(App& app, const std::string& args) {
    std::string name = trim_copy(args);
    if (name.empty()) {
        app.terminal.print_system("Usage: /unworld <name>");
        return;
    }
    // Don't allow removing a connected world
    auto it = app.connections.find(name);
    if (it != app.connections.end() && it->second->is_connected()) {
        app.terminal.print_system("% Cannot remove connected world: " + name);
        return;
    }
    if (app.worlddb.remove(name)) {
        app.terminal.print_system("% Removed world: " + name);
    } else {
        app.terminal.print_system("% No such world: " + name);
    }
}

void cmd_version(App& app, const std::string& /*args*/) {
    app.terminal.print_system("TinyFugue C++ rebuild");
    app.terminal.print_system("  UTF-8 native, ncursesw output, Ragel -G2 input/script lexers");
    app.terminal.print_system("  libmux.so for Unicode width tables");
    app.terminal.print_system("  OpenSSL for TLS connections");
}

void cmd_bind(App& app, const std::string& args) {
    // /bind <keyspec> = <command>
    // /bind                — list all bindings
    // Supports multi-key sequences: ^X^F, Esc-a, Meta-a, M-a
    std::string s = trim_copy(args);

    if (s.empty()) {
        auto& all = app.keybindings.all();
        auto seqs = app.keybindings.all_sequences();
        if (all.empty() && seqs.empty()) {
            app.terminal.print_system("No key bindings");
            return;
        }
        for (auto& [bk, cmd] : all) {
            app.terminal.print_system("  /bind " + format_key_name(bk) + " = " + cmd);
        }
        for (auto& sb : seqs) {
            app.terminal.print_system("  /bind " + format_key_sequence(sb.keys) + " = " + sb.command);
        }
        return;
    }

    // Find '='
    auto eq = s.find('=');
    if (eq == std::string::npos) {
        // Show binding for this key
        std::string keyname = trim_copy(s);
        auto seq = parse_key_sequence(keyname);
        if (seq.size() == 1) {
            const std::string* cmd = app.keybindings.find(seq[0]);
            if (cmd) {
                app.terminal.print_system("  " + keyname + " = " + *cmd);
            } else {
                app.terminal.print_system("  " + keyname + " is not bound");
            }
        } else {
            app.terminal.print_system("  " + keyname + " (sequence lookup not supported in query mode)");
        }
        return;
    }

    std::string keyname = trim_copy(s.substr(0, eq));
    std::string command = trim_copy(s.substr(eq + 1));

    auto seq = parse_key_sequence(keyname);
    if (seq.empty() || (seq.size() == 1 && seq[0].key == Key::UNKNOWN)) {
        app.terminal.print_system("% Unknown key: " + keyname);
        return;
    }

    if (seq.size() == 1) {
        app.keybindings.bind(seq[0], command);
    } else {
        app.keybindings.bind_seq(seq, command);
    }
    if (app.debug_keys_fp) {
        fprintf(app.debug_keys_fp, "BIND: \"%s\" (%zu keys) -> %s\n",
                format_key_sequence(seq).c_str(), seq.size(), command.c_str());
    }
    app.terminal.print_system("% Bound " + format_key_sequence(seq) + " = " + command);
}

void cmd_unbind(App& app, const std::string& args) {
    std::string keyname = trim_copy(args);
    if (keyname.empty()) {
        app.terminal.print_system("Usage: /unbind <key>");
        return;
    }
    auto seq = parse_key_sequence(keyname);
    bool ok;
    if (seq.size() == 1) {
        ok = app.keybindings.unbind(seq[0]);
    } else {
        ok = app.keybindings.unbind_seq(seq);
    }
    if (ok) {
        app.terminal.print_system("% Unbound " + format_key_sequence(seq));
    } else {
        app.terminal.print_system("% Not bound: " + keyname);
    }
}

void cmd_dokey(App& app, const std::string& args) {
    // /dokey <function> — execute a named key function.
    // Maps classic TF dokey names to InputEvents or direct actions.
    //
    std::string func = trim_copy(args);
    std::transform(func.begin(), func.end(), func.begin(), ::tolower);

    // Helper: synthesize an InputEvent and pass through handle_key.
    auto synth = [&](Key k) {
        InputEvent ev;
        ev.key = k;
        ev.cp = 0;
        std::string line;
        if (app.terminal.handle_key(ev, line)) {
            // Submitted a line — but dokey shouldn't really trigger this.
        }
    };

    // Editing operations — dispatch through handle_key.
    if (func == "home")      { synth(Key::HOME); }
    else if (func == "end")  { synth(Key::END); }
    else if (func == "left") { synth(Key::LEFT); }
    else if (func == "right"){ synth(Key::RIGHT); }
    else if (func == "dch")  { synth(Key::DELETE_KEY); }
    else if (func == "bspc") { synth(Key::BACKSPACE); }
    else if (func == "up")   { synth(Key::UP); }
    else if (func == "down") { synth(Key::DOWN); }
    else if (func == "wleft")  { synth(Key::CTRL_LEFT); }
    else if (func == "wright") { synth(Key::CTRL_RIGHT); }
    else if (func == "deol") { synth(Key::CTRL_K); }

    // History
    else if (func == "recallb") { app.terminal.history_up(); }
    else if (func == "recallf") { app.terminal.history_down(); }

    // Scrollback / paging
    else if (func == "page" || func == "scrollforward") { app.terminal.scroll_page_down(); }
    else if (func == "pageback" || func == "scrollback") { app.terminal.scroll_page_up(); }
    else if (func == "flush")  { app.terminal.scroll_to_bottom(); }

    // Socket cycling
    else if (func == "socketf") { cmd_fg_next(app, ""); }
    else if (func == "socketb") { cmd_fg_prev(app, ""); }

    // Display
    else if (func == "redraw" || func == "refresh") { app.terminal.handle_resize(); }
    else if (func == "clear")  { app.terminal.clear_output(); app.terminal.handle_resize(); }
    else if (func == "newline") {
        InputEvent ev;
        ev.key = Key::ENTER;
        ev.cp = 0;
        std::string line;
        app.terminal.handle_key(ev, line);
    }

    else {
        app.terminal.print_system("% Unknown dokey function: " + func);
    }
}

void cmd_listvar(App& app, const std::string& args) {
    // /listvar [pattern] — list variables
    std::string pattern = trim_copy(args);
    std::vector<std::pair<std::string, std::string>> sorted(app.vars.begin(), app.vars.end());
    std::sort(sorted.begin(), sorted.end());
    int count = 0;
    for (auto& [k, v] : sorted) {
        // Skip internal vars
        if (!k.empty() && k[0] == '_') continue;
        if (!pattern.empty() && fnmatch(pattern.c_str(), k.c_str(), 0) != 0) continue;
        app.terminal.print_system("  " + k + "=" + v);
        count++;
    }
    if (count == 0) app.terminal.print_system("No variables" + (pattern.empty() ? std::string("") : " matching " + pattern));
}

void cmd_export(App& app, const std::string& args) {
    // /export name — copy TF variable to environment
    std::string name = trim_copy(args);
    if (name.empty()) {
        app.terminal.print_system("Usage: /export <varname>");
        return;
    }
    auto it = app.vars.find(name);
    if (it != app.vars.end()) {
        setenv(name.c_str(), it->second.c_str(), 1);
        app.terminal.print_system("% Exported: " + name + "=" + it->second);
    } else {
        app.terminal.print_system("% Variable not set: " + name);
    }
}

void cmd_setenv(App& app, const std::string& args) {
    // /setenv NAME=VALUE or /setenv NAME VALUE
    std::string s = trim_copy(args);
    auto eq = s.find('=');
    std::string name, val;
    if (eq != std::string::npos) {
        name = s.substr(0, eq);
        val  = s.substr(eq + 1);
    } else {
        auto sp = s.find(' ');
        if (sp != std::string::npos) {
            name = s.substr(0, sp);
            val  = trim_copy(s.substr(sp + 1));
        } else {
            name = s;
        }
    }
    if (name.empty()) {
        app.terminal.print_system("Usage: /setenv NAME=VALUE");
        return;
    }
    setenv(name.c_str(), val.c_str(), 1);
    app.vars[name] = val;
    app.terminal.print_system("% " + name + "=" + val);
}

void cmd_shift(App& app, const std::string& /*args*/) {
    // /shift — shift positional parameters (remove %{1}, renumber)
    // In our implementation, positional params are in app.vars as "1", "2", etc.
    int n = 0;
    while (app.vars.count(std::to_string(n + 1))) n++;
    if (n <= 0) return;

    // Shift: remove "1", move "2"->"1", "3"->"2", etc.
    for (int i = 1; i < n; i++) {
        app.vars[std::to_string(i)] = app.vars[std::to_string(i + 1)];
    }
    app.vars.erase(std::to_string(n));

    // Update #
    if (app.vars.count("#")) {
        int count = std::atoi(app.vars["#"].c_str());
        if (count > 0) app.vars["#"] = std::to_string(count - 1);
    }
}

void cmd_hook(App& app, const std::string& args) {
    // /hook — list hook macros, or /hook HOOKTYPE to list specific
    std::string type = trim_copy(args);
    bool any = false;
    for (auto& m : app.macros.all()) {
        if (m.hook.empty()) continue;
        if (!type.empty() && m.hook != type) continue;
        std::string line = "  " + m.name + " -h'" + m.hook + "'";
        if (m.priority) line += " -p" + std::to_string(m.priority);
        line += " = " + m.body;
        app.terminal.print_system(line);
        any = true;
    }
    if (!any) {
        if (type.empty()) app.terminal.print_system("No hook macros defined");
        else app.terminal.print_system("No hooks for " + type);
    }
}

void cmd_beep(App& app, const std::string& /*args*/) {
    beep();  // ncurses beep
}

void cmd_suspend(App& app, const std::string& /*args*/) {
    // Suspend the process — restore terminal, send SIGTSTP
    app.terminal.shutdown();
    raise(SIGTSTP);
    // When resumed, reinitialize
    app.terminal.init();
    app.terminal.handle_resize();
}

void cmd_features(App& app, const std::string& /*args*/) {
    app.terminal.print_system("Features:");
    app.terminal.print_system("  SSL/TLS: yes (OpenSSL)");
    app.terminal.print_system("  IPv6: yes");
    app.terminal.print_system("  UTF-8: yes (libmux co_* + ncursesw)");
    app.terminal.print_system("  256-color: yes (xterm)");
    app.terminal.print_system("  Ragel lexers: input + script");
    app.terminal.print_system("  Triggers: glob, regexp, substr");
    app.terminal.print_system("  File I/O: tfopen/tfread/tfwrite/tfclose");
    app.terminal.print_system("  Timers: /repeat with select() integration");
}

void cmd_saveworld(App& app, const std::string& args) {
    // /saveworld [filename] — save world definitions
    std::string path = trim_copy(args);
    if (path.empty()) path = "saved.world";

    std::ofstream f(path);
    if (!f.is_open()) {
        app.terminal.print_system("% Cannot write: " + path);
        return;
    }
    int count = 0;
    for (auto& w : app.worlddb.worlds()) {
        f << "/test addworld("
          << quote_world_arg(w.name) << ", "
          << quote_world_arg(w.type) << ", "
          << quote_world_arg(w.host) << ", "
          << quote_world_arg(w.port) << ", "
          << quote_world_arg(w.character) << ", "
          << quote_world_arg(w.password) << ", "
          << quote_world_arg(w.mfile) << ", "
          << quote_world_arg(w.flags) << ")\n";
        count++;
    }
    f.close();
    app.terminal.print_system("% Saved " + std::to_string(count) + " worlds to " + path);
}

void cmd_recordline(App& app, const std::string& args) {
    // /recordline [-w<world>] text — add a line to the scrollback
    std::string text = trim_copy(args);
    if (app.fg) {
        app.fg->add_to_scrollback(text);
    }
    app.terminal.print_line(text);
}

void cmd_lcd(App& app, const std::string& args) {
    // /lcd <directory> — change working directory
    std::string dir = trim_copy(args);
    if (dir.empty()) {
        char buf[4096];
        if (getcwd(buf, sizeof(buf))) {
            app.terminal.print_system(buf);
        }
        return;
    }
    if (chdir(dir.c_str()) == 0) {
        char buf[4096];
        if (getcwd(buf, sizeof(buf))) {
            app.terminal.print_system("% " + std::string(buf));
        }
    } else {
        app.terminal.print_system("% Cannot cd to: " + dir);
    }
}

void cmd_limit(App& app, const std::string& args) {
    // /limit <pattern> — filter output to show only matching lines
    // Store the filter pattern; the output path will check it.
    std::string pattern = trim_copy(args);
    if (pattern.empty()) {
        app.vars.erase("_limit_pattern");
        app_rerender_foreground(app);
        app.terminal.print_system("% Output filter cleared");
    } else {
        app.vars["_limit_pattern"] = pattern;
        app_rerender_foreground(app);
        app.terminal.print_system("% Output filtered to: " + pattern);
    }
}

void cmd_unlimit(App& app, const std::string& /*args*/) {
    app.vars.erase("_limit_pattern");
    app_rerender_foreground(app);
    app.terminal.print_system("% Output filter cleared");
}

void cmd_input(App& app, const std::string& args) {
    app.terminal.set_input_text(args);
}

void cmd_relimit(App& app, const std::string& args) {
    // /relimit — reapply current limit filter (refresh display)
    auto lim = app.vars.find("_limit_pattern");
    if (lim != app.vars.end() && !lim->second.empty()) {
        app_rerender_foreground(app);
        app.terminal.print_system("% Output filter active: " + lim->second);
    } else {
        app_rerender_foreground(app);
        app.terminal.print_system("% No output filter active");
    }
}

void cmd_core(App& app, const std::string& /*args*/) {
    // /core — enable core dumps for debugging
    struct rlimit rl;
    rl.rlim_cur = RLIM_INFINITY;
    rl.rlim_max = RLIM_INFINITY;
    setrlimit(RLIMIT_CORE, &rl);
    app.terminal.print_system("% Core dumps enabled");
}

void cmd_edit(App& app, const std::string& args) {
    // /edit <macro_name> — edit a macro definition
    // Show the current definition so the user can /def it again
    std::string name = trim_copy(args);
    if (name.empty()) {
        app.terminal.print_system("Usage: /edit <macro_name>");
        return;
    }
    Macro* m = app.macros.find(name);
    if (!m) {
        app.terminal.print_system("% Not defined: " + name);
        return;
    }
    std::string line = "/def ";
    if (!m->trigger.empty()) line += "-t'" + m->trigger + "' ";
    if (!m->hook.empty()) line += "-h'" + m->hook + "' ";
    if (m->priority) line += "-p" + std::to_string(m->priority) + " ";
    if (m->shots >= 0) line += "-n" + std::to_string(m->shots) + " ";
    if (m->fallthrough) line += "-F ";
    if (m->gag) line += "-ag ";
    if (m->hilite) line += "-ah ";
    if (m->match_type != "glob") line += "-m" + m->match_type + " ";
    line += m->name + " = " + m->body;
    app.terminal.print_system(line);
}

void cmd_histsize(App& app, const std::string& args) {
    // /histsize [n] — show or set history/scrollback size
    std::string s = trim_copy(args);
    if (s.empty()) {
        app.terminal.print_system("% History size: 500 (input), 10000 (scrollback)");
    } else {
        app.terminal.print_system("% History size is fixed at compile time");
    }
}

void cmd_liststreams(App& app, const std::string& /*args*/) {
    // /liststreams — list open file streams (tfopen handles)
    if (app.open_files.empty()) {
        app.terminal.print_system("No open streams");
        return;
    }
    for (auto& [handle, fp] : app.open_files) {
        app.terminal.print_system("  " + handle + (fp ? " (open)" : " (closed)"));
    }
}

void cmd_localecho(App& app, const std::string& args) {
    // /localecho [on|off] — toggle local echo
    std::string s = trim_copy(args);
    if (s.empty() || s == "on") {
        app.vars["localecho"] = "on";
        app.terminal.print_system("% Local echo: on");
    } else {
        app.vars["localecho"] = "off";
        app.terminal.print_system("% Local echo: off");
    }
}

void cmd_restrict(App& app, const std::string& args) {
    // /restrict [level] — set restriction level (security)
    std::string s = trim_copy(args);
    if (s.empty()) {
        auto it = app.vars.find("restrict");
        app.terminal.print_system("% Restriction level: " + (it != app.vars.end() ? it->second : "0"));
    } else {
        app.vars["restrict"] = s;
        app.terminal.print_system("% Restriction level set to " + s);
    }
}

void cmd_status_add(App& app, const std::string& args) {
    std::string s = trim_copy(args);
    if (s.empty()) {
        app.terminal.print_system("Usage: /status_add [-r row] [-A field|-B field] [-s n] [-x] [-c] <field> [field...]");
        return;
    }

    std::string before_name;
    std::string after_name;
    int spacer = 0;
    int row = 0;
    bool nodup = false;
    bool reset = false;

    while (!s.empty() && s[0] == '-') {
        if (s.rfind("-x", 0) == 0) {
            nodup = true;
            s = trim_copy(s.substr(2));
        } else if (s.rfind("-c", 0) == 0) {
            reset = true;
            s = trim_copy(s.substr(2));
        } else if (s.rfind("-s", 0) == 0) {
            size_t consumed = 2;
            while (consumed < s.size() && s[consumed] == ' ') consumed++;
            size_t end = consumed;
            while (end < s.size() && s[end] != ' ') end++;
            spacer = std::atoi(s.substr(consumed, end - consumed).c_str());
            s = trim_copy(s.substr(end));
        } else if (s.rfind("-r", 0) == 0) {
            size_t consumed = 2;
            while (consumed < s.size() && s[consumed] == ' ') consumed++;
            size_t end = consumed;
            while (end < s.size() && s[end] != ' ') end++;
            row = std::atoi(s.substr(consumed, end - consumed).c_str());
            s = trim_copy(s.substr(end));
        } else if (s.rfind("-A", 0) == 0 || s.rfind("-B", 0) == 0) {
            bool before = (s[1] == 'B');
            size_t consumed = 2;
            while (consumed < s.size() && s[consumed] == ' ') consumed++;
            size_t end = consumed;
            while (end < s.size() && s[end] != ' ') end++;
            std::string target = s.substr(consumed, end - consumed);
            if (before) before_name = target;
            else after_name = target;
            s = trim_copy(s.substr(end));
        } else {
            break;
        }
    }

    std::istringstream iss(s);
    std::string field;
    std::vector<std::string> fields;
    while (iss >> field) {
        fields.push_back(field);
    }
    if (fields.empty()) {
        app.terminal.print_system("% /status_add: no fields specified");
        return;
    }
    if (!app.terminal.status_insert_fields(fields, before_name, after_name, spacer, row, reset, nodup)) {
        app.terminal.print_system("% /status_add: invalid placement or duplicate variable-width field");
        return;
    }
    app.terminal.set_status(app.fg ? " [" + app.fg->world_name() + "]" : " [no connection]");
}

void cmd_status_edit(App& app, const std::string& args) {
    std::string s = trim_copy(args);
    int row = -1;
    while (!s.empty() && s.rfind("-r", 0) == 0) {
        size_t consumed = 2;
        while (consumed < s.size() && s[consumed] == ' ') consumed++;
        size_t end = consumed;
        while (end < s.size() && s[end] != ' ') end++;
        row = std::atoi(s.substr(consumed, end - consumed).c_str());
        s = trim_copy(s.substr(end));
    }
    std::string field = s;
    if (field.empty()) {
        app.terminal.print_system("Usage: /status_edit [-r row] <field>");
        return;
    }
    if (app.terminal.status_edit_field(field, row)) {
        app.terminal.set_status(app.fg ? " [" + app.fg->world_name() + "]" : " [no connection]");
    } else {
        app.terminal.print_system("% No such status field");
    }
}

void cmd_status_rm(App& app, const std::string& args) {
    std::string s = trim_copy(args);
    if (s.empty()) {
        app.terminal.print_system("Usage: /status_rm [-r row] <name> [name...]");
        return;
    }

    int row = -1;
    while (!s.empty() && s.rfind("-r", 0) == 0) {
        size_t consumed = 2;
        while (consumed < s.size() && s[consumed] == ' ') consumed++;
        size_t end = consumed;
        while (end < s.size() && s[end] != ' ') end++;
        row = std::atoi(s.substr(consumed, end - consumed).c_str());
        s = trim_copy(s.substr(end));
    }

    bool removed_any = false;
    std::istringstream iss(s);
    std::string name;
    while (iss >> name) {
        if (app.terminal.status_remove_field(name, row)) removed_any = true;
    }
    if (removed_any) {
        app.terminal.set_status(app.fg ? " [" + app.fg->world_name() + "]" : " [no connection]");
    }
}

void cmd_trigpc(App& app, const std::string& args) {
    // /trigpc <pattern> — shorthand for /def -t with probability
    // Parse: /trigpc <probability> <pattern> = <body>
    // For now, just delegate to /def -t
    app.terminal.print_system("% Use: /def -t'pattern' name = body");
    if (!args.empty()) cmd_def(app, "-t'" + args + "'");
}

void cmd_undefn(App& app, const std::string& args) {
    // /undefn <number> — undefine macro by number
    // We don't use numeric IDs, so treat as name
    cmd_undef(app, args);
}

void cmd_unset(App& app, const std::string& args) {
    // /unset <variable> — remove a variable
    std::string name = trim_copy(args);
    if (name.empty()) {
        app.terminal.print_system("Usage: /unset <variable>");
        return;
    }
    if (app.vars.erase(name)) {
        app.terminal.print_system("% Unset: " + name);
    } else {
        app.terminal.print_system("% Not set: " + name);
    }
}

void cmd_watchdog(App& app, const std::string& args) {
    // Simplified: /watchdog <pattern> = <command>
    std::string s = trim_copy(args);
    if (s.empty()) {
        // List watchdogs
        for (auto& m : app.macros.all()) {
            if (m.name.substr(0, 9) == "_watchdog") {
                app.terminal.print_system("  " + m.name + " -t'" + m.trigger + "' = " + m.body);
            }
        }
        return;
    }

    auto eq = s.find('=');
    if (eq == std::string::npos) {
        app.terminal.print_system("Usage: /watchdog <pattern> = <command>");
        return;
    }

    std::string pattern = trim_copy(s.substr(0, eq));
    std::string body = trim_copy(s.substr(eq + 1));
    if (pattern.empty() || body.empty()) {
        app.terminal.print_system("Usage: /watchdog <pattern> = <command>");
        return;
    }

    static int wd_id = 0;
    Macro m;
    m.name = "_watchdog_" + std::to_string(++wd_id);
    m.trigger = pattern;
    m.match_type = "glob";
    m.body = body;
    m.quiet = true;
    app.macros.define(std::move(m));
    app.terminal.print_system("% Watchdog set: " + pattern);
}

void cmd_watchname(App& app, const std::string& args) {
    // /watchname <pattern> — watch for a name in output
    std::string s = trim_copy(args);
    if (s.empty()) {
        for (auto& m : app.macros.all()) {
            if (m.name.substr(0, 10) == "_watchname") {
                app.terminal.print_system("  " + m.name + " -t'" + m.trigger + "' = " + m.body);
            }
        }
        return;
    }
    static int wn_id = 0;
    Macro m;
    m.name = "_watchname_" + std::to_string(++wn_id);
    m.trigger = "*" + s + "*";
    m.match_type = "glob";
    m.body = "/beep";
    m.fallthrough = true;
    m.quiet = true;
    app.macros.define(std::move(m));
    app.terminal.print_system("% Watching for: " + s);
}

void cmd_restart(App& app, const std::string& args) {
    (void)args;
    perform_restart(app, app_original_argv());
}

void cmd_update(App& app, const std::string& args) {
    // /update [branch] — git pull, rebuild, then restart with new binary
    std::string branch = trim_copy(args);

    // Resolve git repo root by walking up from the executable
    char exe_buf[4096];
    ssize_t exe_len = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
    if (exe_len <= 0) {
        app.terminal.print_system("% Cannot resolve executable path");
        return;
    }
    exe_buf[exe_len] = '\0';
    std::string exe_path(exe_buf, (size_t)exe_len);

    // Walk up looking for .git/
    std::string dir = exe_path;
    std::string repo_root;
    for (;;) {
        auto slash = dir.rfind('/');
        if (slash == std::string::npos || slash == 0) break;
        dir.resize(slash);
        struct stat st;
        std::string git_dir = dir + "/.git";
        if (stat(git_dir.c_str(), &st) == 0) {
            repo_root = dir;
            break;
        }
    }
    if (repo_root.empty()) {
        app.terminal.print_system("% Cannot find git repository root");
        return;
    }

    // Determine the build directory (client/tf/build relative to repo root)
    std::string build_dir = repo_root + "/client/tf/build";
    std::string src_dir = repo_root + "/client/tf";

    // Build the shell command
    std::string pull_cmd = "cd " + repo_root + " && git pull";
    if (!branch.empty()) pull_cmd += " origin " + branch;
    std::string build_cmd = pull_cmd + " && cd " + src_dir +
        " && cmake --build build --target tf 2>&1";

    app.terminal.print_system("% Updating: " + build_cmd);

    // Fork and exec the build
    int pipefd[2];
    if (pipe(pipefd) != 0) {
        app.terminal.print_system("% pipe() failed");
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        app.terminal.print_system("% fork() failed");
        return;
    }
    if (pid == 0) {
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", build_cmd.c_str(), nullptr);
        _exit(127);
    }

    close(pipefd[1]);

    // Read build output
    char buf[4096];
    std::string output;
    for (;;) {
        ssize_t n = read(pipefd[0], buf, sizeof(buf));
        if (n <= 0) break;
        output.append(buf, (size_t)n);
    }
    close(pipefd[0]);

    int status = 0;
    waitpid(pid, &status, 0);

    // Show build output
    std::istringstream iss(output);
    std::string line;
    while (std::getline(iss, line)) {
        app.terminal.print_system("  " + line);
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        app.terminal.print_system("% Update failed (exit " +
            std::to_string(WIFEXITED(status) ? WEXITSTATUS(status) : -1) + ")");
        return;
    }

    // Replace running binary: rename build output over current exe
    std::string new_binary = build_dir + "/tf";
    struct stat st;
    if (stat(new_binary.c_str(), &st) != 0) {
        app.terminal.print_system("% Built binary not found: " + new_binary);
        return;
    }

    if (rename(new_binary.c_str(), exe_path.c_str()) != 0) {
        app.terminal.print_system("% Cannot replace binary: " + std::string(strerror(errno)));
        return;
    }

    app.terminal.print_system("% Binary updated, restarting...");
    perform_restart(app, app_original_argv());
}

// /spawn [add|remove|list] ...
static void cmd_spawn(App& app, const std::string& args) {
    std::istringstream ss(args);
    std::string sub;
    ss >> sub;
    std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);

    if (sub == "add") {
        std::string name, pattern;
        ss >> name >> pattern;
        if (name.empty() || pattern.empty()) {
            app.terminal.print_system("% Usage: /spawn add <name> <pattern>");
            return;
        }
        SpawnConfig sc;
        sc.name = name;
        sc.path = name;
        std::transform(sc.path.begin(), sc.path.end(), sc.path.begin(), ::tolower);
        sc.patterns.push_back(pattern);
        app.spawns.add(std::move(sc));
        app.terminal.print_system("% Spawn '" + name + "' added: /" + pattern + "/");
    } else if (sub == "remove" || sub == "del") {
        std::string name;
        ss >> name;
        if (name.empty()) {
            app.terminal.print_system("% Usage: /spawn remove <name>");
            return;
        }
        std::string path = name;
        std::transform(path.begin(), path.end(), path.begin(), ::tolower);
        if (app.spawns.remove(path))
            app.terminal.print_system("% Spawn '" + name + "' removed.");
        else
            app.terminal.print_system("% No spawn: " + name);
    } else {
        // Default: list
        auto& all = app.spawns.all();
        if (all.empty()) {
            app.terminal.print_system("% No spawns defined. Use /spawn add <name> <pattern>");
            return;
        }
        for (auto& s : all) {
            std::string d = "  " + s.name + " (" + s.path + "):";
            for (auto& p : s.patterns) d += " /" + p + "/";
            app.terminal.print_system(d);
        }
    }
}
