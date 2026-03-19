#ifndef APP_H
#define APP_H

#include "world.h"
#include "connection.h"
#include "terminal.h"
#include "command.h"
#include "macro.h"
#include "timer.h"
#include "keybind.h"
#include "spawn.h"
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <vector>
#include <sys/types.h>

class ScriptEnv;

enum class ShellDisposition {
    Echo,
    Send,
    Exec,
};

struct ShellProcess {
    pid_t       pid = -1;
    int         fd = -1;
    ShellDisposition disposition = ShellDisposition::Echo;
    std::string world_name;
    std::string buffer;
};

struct App {
    WorldDB                                        worlddb;
    std::unordered_map<std::string, std::unique_ptr<Connection>> connections;
    Connection*                                    fg = nullptr;
    Terminal                                       terminal;
    CommandDispatcher                              commands;
    MacroDB                                        macros;
    TimerDB                                        timers;
    KeyBindings                                    keybindings;
    std::unordered_map<std::string, std::string>   vars;
    SpawnDB                                        spawns;
    std::unordered_map<std::string, SpawnLines>    spawn_lines;
    std::unordered_map<std::string, FILE*>         open_files;  // tfopen handles
    std::vector<ShellProcess>                      shell_processes;
    int                                            next_file_id = 1;
    bool                                           running = true;
    ScriptEnv*                                     current_env = nullptr;
    std::unordered_set<std::string>                active_worlds;  // worlds with unread bg activity
    FILE*                                          debug_keys_fp = nullptr;  // --debug-keys log

    ~App() {
        for (auto& [h, fp] : open_files) if (fp) fclose(fp);
        if (debug_keys_fp) fclose(debug_keys_fp);
    }
};

bool app_send_line(App& app, Connection* conn, const std::string& line,
                   bool allow_local_echo = true);
void app_receive_line(App& app, Connection* conn, const std::string& world_name,
                      const std::string& line);
void app_rerender_foreground(App& app);

// Clear background activity flag for the foreground world.
inline void app_clear_fg_activity(App& app) {
    if (app.fg) app.active_worlds.erase(app.fg->world_name());
}
bool app_spawn_shell(App& app, const std::string& command, ShellDisposition disposition,
                     const std::string& world_name = "");

// Access the stored original argv (for restart support).
const std::vector<std::string>& app_original_argv();

#endif // APP_H
