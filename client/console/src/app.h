// app.h -- Top-level application state for the Win32 console MU* client.
#ifndef APP_H
#define APP_H

#include "world.h"
#include "iconnection.h"
#include "connection.h"
#include "terminal.h"
#include "command.h"
#include "macro.h"
#include "keybind.h"
#include "timer.h"
#include "hook.h"
#include "spawn.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>

struct App {
    WorldDB                                        worlddb;
    std::unordered_map<std::string, std::unique_ptr<IConnection>> connections;
    IConnection*                                   fg = nullptr;
    Terminal                                       terminal;
    CommandDispatcher                              commands;
    MacroDB                                        macros;
    KeyBindings                                    keybindings;
    TimerDB                                        timers;
    HookDB                                         hooks;
    SpawnDB                                        spawns;
    std::unordered_map<std::string, SpawnLines>    spawn_lines;
    std::unordered_map<std::string, std::string>   hydra_line_buffers;
    std::unordered_map<std::string, std::string>   vars;
    std::unordered_set<std::string>                active_worlds;
    HANDLE                                         iocp = INVALID_HANDLE_VALUE;
    bool                                           running = true;
};

bool app_send_line(App& app, IConnection* conn, const std::string& line);
void app_receive_line(App& app, IConnection* conn, const std::string& world_name,
                      const std::string& line);
void app_receive_hydra_chunk(App& app, IConnection* conn, const std::string& world_name,
                             const std::string& text, bool is_stream_text,
                             bool end_of_record);
void app_receive_partial_line(App& app, IConnection* conn, const std::string& world_name,
                              const std::string& line);
void app_clear_partial_line(App& app, const std::string& world_name);
void app_on_connect(App& app, IConnection* conn, const std::string& world_name);
void app_on_disconnect(App& app, const std::string& world_name);
void app_rerender_foreground(App& app);

inline void app_clear_fg_activity(App& app) {
    if (app.fg) app.active_worlds.erase(app.fg->world_name());
}

#endif // APP_H
