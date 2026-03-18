// command.cpp -- Built-in commands.
#include "command.h"
#include "app.h"
#include <sstream>
#include <algorithm>

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

    auto conn = std::make_unique<Connection>(name, host, port, use_ssl, app.iocp);
    if (!conn->begin_connect()) {
        app.terminal.print_system("Failed to connect to " + host + ":" + port);
        return;
    }

    app.terminal.print_system("Connecting to " + name + " (" + host + ":" + port +
                              (use_ssl ? " ssl" : "") + ")...");
    Connection* raw = conn.get();
    app.connections[name] = std::move(conn);

    // Make foreground if first connection
    if (!app.fg) {
        app.fg = raw;
        app.terminal.set_output_context(name);
        app.terminal.set_status(name);
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
    app.terminal.set_status(args[1]);
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
    app.commands.register_cmd("help",        cmd_help,        "Show this help");
}
