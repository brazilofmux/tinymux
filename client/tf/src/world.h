#ifndef TF_WORLD_H
#define TF_WORLD_H

#include <string>
#include <vector>
#include <unordered_map>

struct World {
    std::string name;
    std::string type;      // "tiny" etc.
    std::string host;
    std::string port;
    std::string character;
    std::string password;
    std::string mfile;
    std::string flags;     // "x" = SSL

    bool ssl() const { return flags.find('x') != std::string::npos; }
};

class WorldDB {
public:
    // Parse a tiny.world file. Returns number of worlds loaded.
    int load(const std::string& path);

    const World* find(const std::string& name) const;
    const std::vector<World>& worlds() const { return worlds_; }

    // Add or replace a world programmatically.  Returns true if added (vs replaced).
    bool add(World w);

    // Remove a world by name.  Returns true if found.
    bool remove(const std::string& name);

    // Variables set via /set lines in the world file
    const std::unordered_map<std::string, std::string>& vars() const { return vars_; }

private:
    bool parse_addworld(const std::string& line);
    bool parse_set(const std::string& line);

    std::vector<World> worlds_;
    std::unordered_map<std::string, std::string> vars_;
};

#endif // TF_WORLD_H
