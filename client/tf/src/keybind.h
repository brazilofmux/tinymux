#ifndef KEYBIND_H
#define KEYBIND_H

#include "input.h"
#include <string>
#include <unordered_map>

// Bidirectional mapping between Key enum and human-readable names.
// TF-compatible names: ^A, ^B, Up, Down, PgUp, F1, etc.
const char* key_to_name(Key k);
Key         name_to_key(const std::string& name);

// A bindable key: either a Key enum value, or a specific character.
struct BindKey {
    Key      key = Key::UNKNOWN;
    uint32_t cp  = 0;   // for Key::CHAR bindings (specific character)

    bool operator==(const BindKey& o) const { return key == o.key && cp == o.cp; }
};

struct BindKeyHash {
    size_t operator()(const BindKey& k) const {
        return std::hash<uint32_t>()(static_cast<uint32_t>(k.key) << 16 | (k.cp & 0xFFFF));
    }
};

class KeyBindings {
public:
    // Bind a key to a command.
    void bind(const BindKey& key, const std::string& command);

    // Unbind a key.
    bool unbind(const BindKey& key);

    // Look up a binding.  Returns nullptr if not bound.
    const std::string* find(const BindKey& key) const;

    // List all bindings.
    const std::unordered_map<BindKey, std::string, BindKeyHash>& all() const {
        return bindings_;
    }

private:
    std::unordered_map<BindKey, std::string, BindKeyHash> bindings_;
};

// Parse a TF key name string into a BindKey.
// Supports: ^A-^Z, Up, Down, Left, Right, Home, End, PgUp, PgDn,
//           Insert, Delete, Tab, Return, Enter, Esc, F1-F12,
//           single characters.
BindKey parse_key_name(const std::string& name);

// Format a BindKey as a human-readable name.
std::string format_key_name(const BindKey& key);

#endif // KEYBIND_H
