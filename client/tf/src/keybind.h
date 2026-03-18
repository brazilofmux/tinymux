#ifndef KEYBIND_H
#define KEYBIND_H

#include "input.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <memory>

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

// Trie node for multi-key sequence matching.
//
struct SeqTrieNode {
    std::unordered_map<uint64_t, std::unique_ptr<SeqTrieNode>> children;
    std::string command;     // non-empty at terminal nodes
    bool is_terminal() const { return !command.empty(); }
    bool is_prefix() const { return !children.empty(); }
};

// Pack a BindKey into a 64-bit trie key.
inline uint64_t bindkey_to_u64(const BindKey& bk) {
    return (static_cast<uint64_t>(bk.key) << 32) | bk.cp;
}

class KeyBindings {
public:
    // Bind a single key to a command.
    void bind(const BindKey& key, const std::string& command);

    // Bind a multi-key sequence to a command.
    void bind_seq(const std::vector<BindKey>& seq, const std::string& command);

    // Unbind a key.
    bool unbind(const BindKey& key);

    // Unbind a sequence.
    bool unbind_seq(const std::vector<BindKey>& seq);

    // Look up a single-key binding.  Returns nullptr if not bound.
    const std::string* find(const BindKey& key) const;

    // Sequence matching state machine.
    // Returns:
    //   "match"   — full match, command is in out_command
    //   "prefix"  — partial match, waiting for more keys
    //   "none"    — no match (sequence should be replayed as individual keys)
    enum class SeqResult { MATCH, PREFIX, NONE };
    SeqResult seq_advance(const BindKey& key, std::string& out_command);
    void seq_reset();
    bool seq_pending() const { return seq_node_ != &seq_root_; }
    const std::vector<BindKey>& seq_buffered() const { return seq_buf_; }

    // List all bindings (single + sequences).
    const std::unordered_map<BindKey, std::string, BindKeyHash>& all() const {
        return bindings_;
    }
    struct SeqBinding {
        std::vector<BindKey> keys;
        std::string command;
    };
    std::vector<SeqBinding> all_sequences() const;

private:
    std::unordered_map<BindKey, std::string, BindKeyHash> bindings_;
    SeqTrieNode seq_root_;
    const SeqTrieNode* seq_node_ = &seq_root_;
    std::vector<BindKey> seq_buf_;

    void collect_sequences(const SeqTrieNode* node, std::vector<BindKey>& prefix,
                          std::vector<SeqBinding>& out) const;
};

// Parse a single TF key name into a BindKey.
BindKey parse_key_name(const std::string& name);

// Parse a key specification that may be a multi-key sequence.
// Returns a vector of BindKeys (length 1 for single keys).
// Supports: ^X^F, Esc-a, Esc a, Meta-a, M-a
std::vector<BindKey> parse_key_sequence(const std::string& spec);

// Format a BindKey as a human-readable name.
std::string format_key_name(const BindKey& key);

// Format a sequence of BindKeys.
std::string format_key_sequence(const std::vector<BindKey>& seq);

#endif // KEYBIND_H
