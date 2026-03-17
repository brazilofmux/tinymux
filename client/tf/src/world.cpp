#include "world.h"
#include <fstream>
#include <sstream>
#include <algorithm>

int WorldDB::load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return -1;

    int count = 0;
    std::string line;
    while (std::getline(f, line)) {
        // Strip trailing \r
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty()) continue;

        if (line.find("/test addworld(") != std::string::npos) {
            if (parse_addworld(line)) count++;
        } else if (line.find("/set ") == 0) {
            parse_set(line);
        }
        // Ignore /def and other lines
    }
    return count;
}

const World* WorldDB::find(const std::string& name) const {
    for (auto& w : worlds_) {
        // Case-insensitive match
        if (w.name.size() == name.size() &&
            std::equal(w.name.begin(), w.name.end(), name.begin(),
                       [](char a, char b) { return tolower(a) == tolower(b); })) {
            return &w;
        }
    }
    return nullptr;
}

bool WorldDB::add(World w) {
    // Replace if exists
    for (auto& existing : worlds_) {
        if (existing.name.size() == w.name.size() &&
            std::equal(existing.name.begin(), existing.name.end(), w.name.begin(),
                       [](char a, char b) { return tolower(a) == tolower(b); })) {
            existing = std::move(w);
            return false;  // replaced
        }
    }
    worlds_.push_back(std::move(w));
    return true;  // added
}

bool WorldDB::remove(const std::string& name) {
    for (auto it = worlds_.begin(); it != worlds_.end(); ++it) {
        if (it->name.size() == name.size() &&
            std::equal(it->name.begin(), it->name.end(), name.begin(),
                       [](char a, char b) { return tolower(a) == tolower(b); })) {
            worlds_.erase(it);
            return true;
        }
    }
    return false;
}

// Parse: /test addworld("name","type","host","port","char","pass",mfile,"flags")
// mfile can be quoted or bare (e.g., foo without quotes)
bool WorldDB::parse_addworld(const std::string& line) {
    // Find the opening paren
    auto paren = line.find('(');
    if (paren == std::string::npos) return false;

    std::string args = line.substr(paren + 1);
    // Remove trailing )
    auto rparen = args.rfind(')');
    if (rparen != std::string::npos) args.resize(rparen);

    // Extract positional arguments — mix of quoted and unquoted
    std::vector<std::string> fields;
    size_t pos = 0;
    while (pos < args.size() && fields.size() < 8) {
        // Skip whitespace and commas
        while (pos < args.size() && (args[pos] == ' ' || args[pos] == ','))
            pos++;
        if (pos >= args.size()) break;

        if (args[pos] == '"') {
            // Quoted string
            pos++; // skip opening quote
            std::string val;
            while (pos < args.size() && args[pos] != '"') {
                val += args[pos++];
            }
            if (pos < args.size()) pos++; // skip closing quote
            fields.push_back(val);
        } else {
            // Bare word
            std::string val;
            while (pos < args.size() && args[pos] != ',' && args[pos] != ')') {
                val += args[pos++];
            }
            // Trim trailing whitespace
            while (!val.empty() && val.back() == ' ') val.pop_back();
            fields.push_back(val);
        }
    }

    if (fields.size() < 6) return false;

    World w;
    w.name      = fields[0];
    w.type      = fields[1];
    w.host      = fields[2];
    w.port      = fields[3];
    w.character = fields[4];
    w.password  = fields[5];
    if (fields.size() > 6) w.mfile = fields[6];
    if (fields.size() > 7) w.flags = fields[7];

    worlds_.push_back(std::move(w));
    return true;
}

// Parse: /set NAME=VALUE
bool WorldDB::parse_set(const std::string& line) {
    auto eq = line.find('=');
    if (eq == std::string::npos) return false;
    std::string name = line.substr(5, eq - 5); // skip "/set "
    std::string val  = line.substr(eq + 1);
    // Trim
    while (!name.empty() && name.back() == ' ') name.pop_back();
    while (!val.empty() && val.back() == ' ') val.pop_back();
    vars_[name] = val;
    return true;
}
