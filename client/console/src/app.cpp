// app.cpp -- Application-level logic.
#include "app.h"

bool app_send_line(App& app, Connection* conn, const std::string& line) {
    if (!conn || !conn->is_connected()) return false;
    conn->send_line(line);
    return true;
}

void app_on_connect(App& app, Connection* conn, const std::string& world_name) {
    // Fire CONNECT hooks
    for (auto& cmd : app.hooks.fire_event("CONNECT")) {
        if (cmd[0] == '/') {
            app.commands.dispatch(app, cmd);
        } else {
            app_send_line(app, conn, cmd);
        }
    }
}

void app_on_disconnect(App& app, const std::string& world_name) {
    // Fire DISCONNECT hooks
    for (auto& cmd : app.hooks.fire_event("DISCONNECT")) {
        app.terminal.print_system("[hook] " + cmd);
    }
    // Cancel timers
    app.timers.cancel_all();
}

void app_receive_line(App& app, Connection* conn, const std::string& world_name,
                      const std::string& line) {
    conn->add_to_scrollback(line);

    // Check triggers
    std::string text = line;
    TriggerResult tr = check_triggers(app, text);

    // Display unless gagged
    if (!tr.gagged) {
        app.terminal.print_line_to(world_name, text);

        // Route to matching spawns
        auto matched = app.spawns.match(text);
        for (auto& path : matched) {
            auto& lines = app.spawn_lines[world_name][path];
            lines.push_back(text);
            while (lines.size() > 20000) lines.pop_front();
        }
    }

    // Mark background activity if not foreground
    if (app.fg != conn) {
        app.active_worlds.insert(world_name);
    }
}

void app_rerender_foreground(App& app) {
    app.terminal.refresh();
}
