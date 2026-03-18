// keybind.h -- Key binding system for the console client.
#ifndef KEYBIND_H
#define KEYBIND_H

#include "terminal.h"
#include <string>
#include <unordered_map>
#include <vector>

// A bindable key: event type + optional codepoint for Char events.
struct BindKey {
    InputEvent::Type type = InputEvent::None;
    uint32_t         cp = 0;
    bool             ctrl = false;
    bool             alt = false;

    bool operator==(const BindKey& o) const {
        return type == o.type && cp == o.cp && ctrl == o.ctrl && alt == o.alt;
    }
};

struct BindKeyHash {
    size_t operator()(const BindKey& k) const {
        return std::hash<uint32_t>()(
            (static_cast<uint32_t>(k.type) << 16) |
            (k.cp & 0xFFFF) |
            (k.ctrl ? 0x80000000u : 0) |
            (k.alt ? 0x40000000u : 0));
    }
};

class KeyBindings {
public:
    void bind(const BindKey& key, const std::string& command);
    bool unbind(const BindKey& key);
    const std::string* find(const BindKey& key) const;

    const std::unordered_map<BindKey, std::string, BindKeyHash>& all() const {
        return bindings_;
    }

private:
    std::unordered_map<BindKey, std::string, BindKeyHash> bindings_;
};

// Convert an InputEvent to a BindKey for lookup.
BindKey event_to_bindkey(const InputEvent& ev);

// Parse a key name string (e.g. "F1", "Ctrl-A", "PgUp") into a BindKey.
BindKey parse_key_name(const std::string& name);

// Format a BindKey as a human-readable name.
std::string format_key_name(const BindKey& key);

#endif // KEYBIND_H
