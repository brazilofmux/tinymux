// spawn.h -- Output routing spawns for TitanFugue.
#ifndef TF_SPAWN_H
#define TF_SPAWN_H

#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include "regex_utils.h"

struct SpawnConfig {
    std::string              name;
    std::string              path;
    std::vector<std::string> patterns;
    std::vector<std::string> exceptions;
    std::string              prefix;
    int                      max_lines = 20000;
    int                      weight = 0;

    // Compiled patterns
    std::vector<RegexPattern> compiled;
    std::vector<RegexPattern> compiled_exceptions;
    bool                      is_compiled = false;

    void compile();
    bool matches(const std::string& line);
};

class SpawnDB {
public:
    void add(SpawnConfig s);
    bool remove(const std::string& path);
    const std::vector<SpawnConfig>& all() const { return spawns_; }
    std::vector<std::string> match(const std::string& line);

private:
    std::vector<SpawnConfig> spawns_;
};

using SpawnLines = std::unordered_map<std::string, std::deque<std::string>>;

#endif // TF_SPAWN_H
