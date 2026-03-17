#ifndef APP_H
#define APP_H

#include "world.h"
#include "connection.h"
#include "terminal.h"
#include "command.h"
#include "macro.h"
#include "timer.h"
#include "keybind.h"
#include <string>
#include <unordered_map>
#include <memory>

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
    std::unordered_map<std::string, FILE*>         open_files;  // tfopen handles
    int                                            next_file_id = 1;
    bool                                           running = true;

    ~App() {
        for (auto& [h, fp] : open_files) if (fp) fclose(fp);
    }
};

bool app_send_line(App& app, Connection* conn, const std::string& line,
                   bool allow_local_echo = true);
void app_receive_line(App& app, Connection* conn, const std::string& world_name,
                      const std::string& line);
void app_rerender_foreground(App& app);

#endif // APP_H
