// settings.cpp -- JSON config load/save.
#include "settings.h"
#include "json_mini.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>
#include <sys/stat.h>

std::string Settings::GetSettingsDir() {
    wchar_t path[MAX_PATH];
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path))) {
        char utf8[MAX_PATH * 3];
        WideCharToMultiByte(CP_UTF8, 0, path, -1, utf8, sizeof(utf8), nullptr, nullptr);
        std::string dir = std::string(utf8) + "\\Titan";
        CreateDirectoryA(dir.c_str(), nullptr);
        return dir;
    }
    return ".";
}

static uint32_t parse_color(const std::string& s) {
    // "#RRGGBB" or "RRGGBB"
    const char* p = s.c_str();
    if (*p == '#') p++;
    return (uint32_t)strtoul(p, nullptr, 16);
}

static std::string format_color(uint32_t rgb) {
    char buf[8];
    snprintf(buf, sizeof(buf), "#%06X", rgb & 0xFFFFFF);
    return buf;
}

bool Settings::Load(const std::string& dir) {
    // Load client.json
    JObject cfg;
    if (json_parse_file(dir + "\\client.json", cfg)) {
        font_name = jstr(cfg, "font_name", font_name);
        font_size = (int)jint(cfg, "font_size", font_size);
        std::string fg_str = jstr(cfg, "default_fg", "");
        std::string bg_str = jstr(cfg, "default_bg", "");
        if (!fg_str.empty()) default_fg = parse_color(fg_str);
        if (!bg_str.empty()) default_bg = parse_color(bg_str);
        scrollback_lines = (int)jint(cfg, "scrollback_lines", scrollback_lines);
        prompt_timeout_ms = (int)jint(cfg, "prompt_timeout_ms", prompt_timeout_ms);
        show_status_bar = jbool(cfg, "show_status_bar", show_status_bar);
    }

    // Load worlds.json
    JArray warr;
    if (json_parse_array_file(dir + "\\worlds.json", warr)) {
        worlds.clear();
        for (auto& wobj : warr) {
            WorldDef w;
            w.name = jstr(wobj, "name");
            w.host = jstr(wobj, "host");
            w.port = jstr(wobj, "port", "4201");
            w.ssl = jbool(wobj, "ssl", false);
            w.character = jstr(wobj, "character");
            w.font_name = jstr(wobj, "font_name");
            w.font_size = (int)jint(wobj, "font_size", 0);
            w.auto_connect = jbool(wobj, "auto_connect", false);
            if (!w.name.empty() && !w.host.empty()) {
                worlds.push_back(std::move(w));
            }
        }
    }

    // Load triggers.json
    JArray tarr;
    if (json_parse_array_file(dir + "\\triggers.json", tarr)) {
        triggers.clear();
        for (auto& tobj : tarr) {
            TriggerDef t;
            t.name = jstr(tobj, "name");
            t.pattern = jstr(tobj, "pattern");
            t.body = jstr(tobj, "body");
            t.priority = (int)jint(tobj, "priority", 0);
            t.shots = (int)jint(tobj, "shots", -1);
            t.gag = jbool(tobj, "gag", false);
            if (!t.pattern.empty()) {
                triggers.push_back(std::move(t));
            }
        }
    }

    // Load keybindings.json
    JObject kobj;
    if (json_parse_file(dir + "\\keybindings.json", kobj)) {
        keybindings.clear();
        for (auto& [key, val] : kobj) {
            if (val.type == JValue::T_STRING) {
                keybindings[key] = val.sval;
            }
        }
    }

    return true;
}

bool Settings::Save(const std::string& dir) const {
    // Save client.json
    JObject cfg;
    cfg.push_back({"font_name", JValue(font_name)});
    cfg.push_back({"font_size", JValue((int64_t)font_size)});
    cfg.push_back({"default_fg", JValue(format_color(default_fg))});
    cfg.push_back({"default_bg", JValue(format_color(default_bg))});
    cfg.push_back({"scrollback_lines", JValue((int64_t)scrollback_lines)});
    cfg.push_back({"prompt_timeout_ms", JValue((int64_t)prompt_timeout_ms)});
    cfg.push_back({"show_status_bar", JValue(show_status_bar)});
    json_write_file(dir + "\\client.json", cfg);

    // Save worlds.json
    JArray warr;
    for (auto& w : worlds) {
        JObject wobj;
        wobj.push_back({"name", JValue(w.name)});
        wobj.push_back({"host", JValue(w.host)});
        wobj.push_back({"port", JValue(w.port)});
        wobj.push_back({"ssl", JValue(w.ssl)});
        if (!w.character.empty())
            wobj.push_back({"character", JValue(w.character)});
        if (!w.font_name.empty())
            wobj.push_back({"font_name", JValue(w.font_name)});
        if (w.font_size > 0)
            wobj.push_back({"font_size", JValue((int64_t)w.font_size)});
        wobj.push_back({"auto_connect", JValue(w.auto_connect)});
        warr.push_back(std::move(wobj));
    }
    json_write_array_file(dir + "\\worlds.json", warr);

    // Save triggers.json
    JArray tarr;
    for (auto& t : triggers) {
        JObject tobj;
        tobj.push_back({"name", JValue(t.name)});
        tobj.push_back({"pattern", JValue(t.pattern)});
        tobj.push_back({"body", JValue(t.body)});
        tobj.push_back({"priority", JValue((int64_t)t.priority)});
        tobj.push_back({"shots", JValue((int64_t)t.shots)});
        tobj.push_back({"gag", JValue(t.gag)});
        tarr.push_back(std::move(tobj));
    }
    json_write_array_file(dir + "\\triggers.json", tarr);

    // Save keybindings.json
    JObject kobj;
    for (auto& [key, cmd] : keybindings) {
        kobj.push_back({key, JValue(cmd)});
    }
    json_write_file(dir + "\\keybindings.json", kobj);

    return true;
}
