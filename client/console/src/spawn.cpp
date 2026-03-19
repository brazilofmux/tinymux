// spawn.cpp -- Output routing spawn implementation.
#include "spawn.h"
#include <algorithm>

void SpawnConfig::compile() {
    compiled.clear();
    compiled_exceptions.clear();
    for (auto& p : patterns) {
        try { compiled.push_back(std::regex(p, std::regex::ECMAScript | std::regex::icase)); }
        catch (...) {}
    }
    for (auto& p : exceptions) {
        try { compiled_exceptions.push_back(std::regex(p, std::regex::ECMAScript | std::regex::icase)); }
        catch (...) {}
    }
    is_compiled = true;
}

bool SpawnConfig::matches(const std::string& line) {
    if (!is_compiled) compile();
    if (compiled.empty()) return false;
    bool hit = false;
    for (auto& re : compiled) {
        try { if (std::regex_search(line, re)) { hit = true; break; } }
        catch (...) {}
    }
    if (!hit) return false;
    for (auto& re : compiled_exceptions) {
        try { if (std::regex_search(line, re)) return false; }
        catch (...) {}
    }
    return true;
}

void SpawnDB::add(SpawnConfig s) {
    s.compile();
    for (auto& existing : spawns_) {
        if (existing.path == s.path) {
            existing = std::move(s);
            return;
        }
    }
    spawns_.push_back(std::move(s));
    std::sort(spawns_.begin(), spawns_.end(),
              [](const SpawnConfig& a, const SpawnConfig& b) { return a.weight < b.weight; });
}

bool SpawnDB::remove(const std::string& path) {
    auto it = std::find_if(spawns_.begin(), spawns_.end(),
                           [&](const SpawnConfig& s) { return s.path == path; });
    if (it == spawns_.end()) return false;
    spawns_.erase(it);
    return true;
}

std::vector<std::string> SpawnDB::match(const std::string& line) {
    std::vector<std::string> result;
    for (auto& s : spawns_) {
        if (s.matches(line)) result.push_back(s.path);
    }
    return result;
}
