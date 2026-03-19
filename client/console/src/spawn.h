// spawn.h -- Output routing spawns for the console client.
#ifndef SPAWN_H
#define SPAWN_H

#include <string>
#include <vector>
#include <deque>
#include <regex>
#include <unordered_map>

struct SpawnConfig {
    std::string              name;
    std::string              path;
    std::vector<std::string> patterns;
    std::vector<std::string> exceptions;
    std::string              prefix;
    int                      max_lines = 20000;
    int                      weight = 0;

    // Compiled patterns (lazily built)
    std::vector<std::regex>  compiled;
    std::vector<std::regex>  compiled_exceptions;
    bool                     is_compiled = false;

    void compile();
    bool matches(const std::string& line);
};

class SpawnDB {
public:
    void add(SpawnConfig s);
    bool remove(const std::string& path);
    const std::vector<SpawnConfig>& all() const { return spawns_; }

    // Match a line against all spawns. Returns matching paths.
    std::vector<std::string> match(const std::string& line);

private:
    std::vector<SpawnConfig> spawns_;
};

// Per-connection spawn line storage.
using SpawnLines = std::unordered_map<std::string, std::deque<std::string>>;

#endif // SPAWN_H
