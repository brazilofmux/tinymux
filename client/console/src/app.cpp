// app.cpp -- Application-level logic.
#include "app.h"

bool app_send_line(App& app, Connection* conn, const std::string& line) {
    if (!conn || !conn->is_connected()) return false;
    conn->send_line(line);
    return true;
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
    }

    // Mark background activity if not foreground
    if (app.fg != conn) {
        app.active_worlds.insert(world_name);
    }
}

void app_rerender_foreground(App& app) {
    app.terminal.refresh();
}
