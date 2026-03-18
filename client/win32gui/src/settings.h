// settings.h -- JSON-based configuration for Titan.
#ifndef SETTINGS_H
#define SETTINGS_H

#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

struct WorldDef {
    std::string name;
    std::string host;
    std::string port = "4201";
    bool        ssl = false;
    std::string character;
    std::string font_name;      // empty = use global default
    int         font_size = 0;  // 0 = use global default
    bool        auto_connect = false;
};

struct TriggerDef {
    std::string name;
    std::string pattern;
    std::string body;
    int         priority = 0;
    int         shots = -1;
    bool        gag = false;
};

struct Settings {
    // Window position (0 = default / unset)
    int         win_x = 0;
    int         win_y = 0;
    int         win_cx = 0;
    int         win_cy = 0;
    bool        win_maximized = false;

    // Global
    std::string font_name = "Consolas";
    int         font_size = 12;
    uint32_t    default_fg = 0x00C0C0C0;   // RGB
    uint32_t    default_bg = 0x00000000;
    int         scrollback_lines = 20000;
    int         prompt_timeout_ms = 500;
    bool        show_status_bar = true;

    // Worlds
    std::vector<WorldDef> worlds;

    // Triggers (global, not per-world for now)
    std::vector<TriggerDef> triggers;

    // Key bindings: key_name -> command
    std::unordered_map<std::string, std::string> keybindings;

    // Load/save
    bool Load(const std::string& dir);
    bool Save(const std::string& dir) const;

    // Get the settings directory (%APPDATA%\Titan)
    static std::string GetSettingsDir();
};

#endif // SETTINGS_H
