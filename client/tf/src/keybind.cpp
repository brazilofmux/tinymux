#include "keybind.h"
#include <algorithm>
#include <cctype>

// ---- Key name tables ----

struct KeyNameEntry {
    Key         key;
    const char* name;
};

static const KeyNameEntry key_names[] = {
    { Key::UP,         "Up" },
    { Key::DOWN,       "Down" },
    { Key::LEFT,       "Left" },
    { Key::RIGHT,      "Right" },
    { Key::HOME,       "Home" },
    { Key::END,        "End" },
    { Key::PAGE_UP,    "PgUp" },
    { Key::PAGE_DOWN,  "PgDn" },
    { Key::CTRL_LEFT,  "Ctrl-Left" },
    { Key::CTRL_RIGHT, "Ctrl-Right" },
    { Key::ENTER,      "Return" },
    { Key::BACKSPACE,  "Backspace" },
    { Key::DELETE_KEY,  "Delete" },
    { Key::TAB,        "Tab" },
    { Key::ESCAPE,     "Esc" },
    { Key::INSERT,     "Insert" },
    { Key::CTRL_A,     "^A" },
    { Key::CTRL_B,     "^B" },
    { Key::CTRL_C,     "^C" },
    { Key::CTRL_D,     "^D" },
    { Key::CTRL_E,     "^E" },
    { Key::CTRL_F,     "^F" },
    { Key::CTRL_G,     "^G" },
    { Key::CTRL_K,     "^K" },
    { Key::CTRL_L,     "^L" },
    { Key::CTRL_N,     "^N" },
    { Key::CTRL_O,     "^O" },
    { Key::CTRL_P,     "^P" },
    { Key::CTRL_Q,     "^Q" },
    { Key::CTRL_R,     "^R" },
    { Key::CTRL_S,     "^S" },
    { Key::CTRL_T,     "^T" },
    { Key::CTRL_U,     "^U" },
    { Key::CTRL_V,     "^V" },
    { Key::CTRL_W,     "^W" },
    { Key::CTRL_X,     "^X" },
    { Key::CTRL_Y,     "^Y" },
    { Key::CTRL_Z,     "^Z" },
    { Key::F1,         "F1" },
    { Key::F2,         "F2" },
    { Key::F3,         "F3" },
    { Key::F4,         "F4" },
    { Key::F5,         "F5" },
    { Key::F6,         "F6" },
    { Key::F7,         "F7" },
    { Key::F8,         "F8" },
    { Key::F9,         "F9" },
    { Key::F10,        "F10" },
    { Key::F11,        "F11" },
    { Key::F12,        "F12" },
};

static constexpr int NUM_KEY_NAMES = sizeof(key_names) / sizeof(key_names[0]);

const char* key_to_name(Key k) {
    for (int i = 0; i < NUM_KEY_NAMES; i++) {
        if (key_names[i].key == k) return key_names[i].name;
    }
    return nullptr;
}

static std::string str_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

Key name_to_key(const std::string& name) {
    std::string lower = str_lower(name);

    // Check table (case-insensitive)
    for (int i = 0; i < NUM_KEY_NAMES; i++) {
        if (str_lower(key_names[i].name) == lower) return key_names[i].key;
    }

    // TF aliases
    if (lower == "enter") return Key::ENTER;
    if (lower == "ret" || lower == "cr") return Key::ENTER;
    if (lower == "bs" || lower == "bksp") return Key::BACKSPACE;
    if (lower == "del") return Key::DELETE_KEY;
    if (lower == "escape") return Key::ESCAPE;
    if (lower == "pageup" || lower == "pgup") return Key::PAGE_UP;
    if (lower == "pagedown" || lower == "pgdn" || lower == "pagedn") return Key::PAGE_DOWN;
    if (lower == "ins") return Key::INSERT;

    return Key::UNKNOWN;
}

// ---- BindKey parsing ----

BindKey parse_key_name(const std::string& name) {
    BindKey bk;

    // ^X notation
    if (name.size() == 2 && name[0] == '^') {
        char c = toupper(name[1]);
        if (c >= 'A' && c <= 'Z') {
            Key k = name_to_key(name);
            if (k != Key::UNKNOWN) {
                bk.key = k;
                return bk;
            }
        }
    }

    // Named key
    Key k = name_to_key(name);
    if (k != Key::UNKNOWN) {
        bk.key = k;
        return bk;
    }

    // Single character binding
    if (name.size() == 1) {
        bk.key = Key::CHAR;
        bk.cp = (uint32_t)(unsigned char)name[0];
        return bk;
    }

    // Unknown
    bk.key = Key::UNKNOWN;
    return bk;
}

std::string format_key_name(const BindKey& key) {
    if (key.key == Key::CHAR) {
        if (key.cp >= 32 && key.cp < 127) {
            return std::string(1, (char)key.cp);
        }
        return "U+" + std::to_string(key.cp);
    }
    const char* name = key_to_name(key.key);
    if (name) return name;
    return "?";
}

// ---- KeyBindings ----

void KeyBindings::bind(const BindKey& key, const std::string& command) {
    bindings_[key] = command;
}

bool KeyBindings::unbind(const BindKey& key) {
    return bindings_.erase(key) > 0;
}

const std::string* KeyBindings::find(const BindKey& key) const {
    auto it = bindings_.find(key);
    if (it != bindings_.end()) return &it->second;
    return nullptr;
}
