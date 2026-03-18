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

// Parse a key specification that may contain multiple keys.
//
// Supported formats:
//   ^X^F         — Ctrl-X followed by Ctrl-F
//   Esc-a        — Escape followed by 'a'
//   Esc a        — same
//   Meta-a, M-a  — shorthand for Esc followed by 'a'
//   ^X a         — Ctrl-X followed by 'a'
//   F1           — single key (returns length-1 vector)
//
std::vector<BindKey> parse_key_sequence(const std::string& spec) {
    std::vector<BindKey> seq;
    std::string s = spec;

    // Trim leading/trailing whitespace.
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();

    size_t i = 0;
    while (i < s.size()) {
        // Skip whitespace and hyphens between keys (but not inside key names).
        while (i < s.size() && s[i] == ' ') i++;
        if (i >= s.size()) break;

        // Meta-X / M-X: expand to Esc + X
        if (i + 2 <= s.size()) {
            std::string prefix2 = str_lower(s.substr(i, 2));
            if (prefix2 == "m-" || (i + 5 <= s.size() && str_lower(s.substr(i, 5)) == "meta-")) {
                BindKey esc;
                esc.key = Key::ESCAPE;
                seq.push_back(esc);
                i += (prefix2 == "m-") ? 2 : 5;
                // The character after Meta- is the second key.
                if (i < s.size()) {
                    BindKey ch;
                    ch.key = Key::CHAR;
                    ch.cp = (uint32_t)(unsigned char)s[i];
                    seq.push_back(ch);
                    i++;
                }
                continue;
            }
        }

        // ^X notation: always exactly 2 characters
        if (s[i] == '^' && i + 1 < s.size() && std::isalpha((unsigned char)s[i + 1])) {
            BindKey bk = parse_key_name(s.substr(i, 2));
            if (bk.key != Key::UNKNOWN) {
                seq.push_back(bk);
                i += 2;
                // Skip optional hyphen/space separator
                if (i < s.size() && (s[i] == '-' || s[i] == ' ')) i++;
                continue;
            }
        }

        // Esc followed by separator and next key
        if (i + 3 <= s.size()) {
            std::string esc3 = str_lower(s.substr(i, 3));
            if (esc3 == "esc") {
                BindKey bk;
                bk.key = Key::ESCAPE;
                seq.push_back(bk);
                i += 3;
                // Skip separator (hyphen or space)
                if (i < s.size() && (s[i] == '-' || s[i] == ' ')) i++;
                continue;
            }
        }

        // Try named multi-character key (F1-F12, Up, Down, etc.)
        // Find the longest match from current position.
        bool found_named = false;
        for (int len = std::min((int)(s.size() - i), 10); len >= 2; len--) {
            std::string candidate = s.substr(i, len);
            Key k = name_to_key(candidate);
            if (k != Key::UNKNOWN) {
                BindKey bk;
                bk.key = k;
                seq.push_back(bk);
                i += len;
                if (i < s.size() && (s[i] == '-' || s[i] == ' ')) i++;
                found_named = true;
                break;
            }
        }
        if (found_named) continue;

        // Single character
        BindKey ch;
        ch.key = Key::CHAR;
        ch.cp = (uint32_t)(unsigned char)s[i];
        seq.push_back(ch);
        i++;
        if (i < s.size() && (s[i] == '-' || s[i] == ' ')) i++;
    }

    return seq;
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

std::string format_key_sequence(const std::vector<BindKey>& seq) {
    std::string result;
    for (size_t i = 0; i < seq.size(); i++) {
        if (i > 0) result += ' ';
        result += format_key_name(seq[i]);
    }
    return result;
}

// ---- KeyBindings ----

void KeyBindings::bind(const BindKey& key, const std::string& command) {
    bindings_[key] = command;
}

void KeyBindings::bind_seq(const std::vector<BindKey>& seq, const std::string& command) {
    if (seq.empty()) return;
    if (seq.size() == 1) {
        bind(seq[0], command);
        return;
    }

    SeqTrieNode* node = &seq_root_;
    for (const auto& bk : seq) {
        uint64_t k = bindkey_to_u64(bk);
        auto& child = node->children[k];
        if (!child) child = std::make_unique<SeqTrieNode>();
        node = child.get();
    }
    node->command = command;
}

bool KeyBindings::unbind(const BindKey& key) {
    return bindings_.erase(key) > 0;
}

bool KeyBindings::unbind_seq(const std::vector<BindKey>& seq) {
    if (seq.empty()) return false;
    if (seq.size() == 1) return unbind(seq[0]);

    SeqTrieNode* node = &seq_root_;
    for (const auto& bk : seq) {
        uint64_t k = bindkey_to_u64(bk);
        auto it = node->children.find(k);
        if (it == node->children.end()) return false;
        node = it->second.get();
    }
    if (node->command.empty()) return false;
    node->command.clear();
    return true;
}

const std::string* KeyBindings::find(const BindKey& key) const {
    auto it = bindings_.find(key);
    if (it != bindings_.end()) return &it->second;
    return nullptr;
}

KeyBindings::SeqResult KeyBindings::seq_advance(const BindKey& key, std::string& out_command) {
    uint64_t k = bindkey_to_u64(key);
    auto it = seq_node_->children.find(k);
    if (it == seq_node_->children.end()) {
        // No continuation from current node.  Do NOT reset here —
        // caller needs to read seq_buffered() first, then call seq_reset().
        seq_node_ = &seq_root_;
        return SeqResult::NONE;
    }

    const SeqTrieNode* next = it->second.get();
    seq_buf_.push_back(key);

    if (next->is_terminal() && !next->is_prefix()) {
        // Unambiguous terminal: full match.
        out_command = next->command;
        seq_reset();
        return SeqResult::MATCH;
    }

    if (next->is_terminal()) {
        // Terminal but also a prefix of longer sequences.
        // Prefer the match (classic TF behavior: shortest match wins).
        out_command = next->command;
        seq_reset();
        return SeqResult::MATCH;
    }

    // Prefix only — wait for more keys.
    seq_node_ = next;
    return SeqResult::PREFIX;
}

void KeyBindings::seq_reset() {
    seq_node_ = &seq_root_;
    seq_buf_.clear();
}

void KeyBindings::collect_sequences(const SeqTrieNode* node, std::vector<BindKey>& prefix,
                                    std::vector<SeqBinding>& out) const {
    if (node->is_terminal()) {
        out.push_back({prefix, node->command});
    }
    for (const auto& [k, child] : node->children) {
        BindKey bk;
        bk.key = static_cast<Key>(k >> 32);
        bk.cp = static_cast<uint32_t>(k & 0xFFFFFFFF);
        prefix.push_back(bk);
        collect_sequences(child.get(), prefix, out);
        prefix.pop_back();
    }
}

std::vector<KeyBindings::SeqBinding> KeyBindings::all_sequences() const {
    std::vector<SeqBinding> result;
    std::vector<BindKey> prefix;
    collect_sequences(&seq_root_, prefix, result);
    return result;
}
