// world.h -- World (MUD server) definitions.
#ifndef WORLD_H
#define WORLD_H

#include <string>
#include <vector>
#include <unordered_map>

struct World {
    std::string name;
    std::string host;
    std::string port;
    bool        use_ssl = false;
    std::string character;
    std::string password;

    // Hydra proxy fields
    bool        use_hydra = false;
    std::string hydra_user;     // Hydra account username
    std::string hydra_pass;     // Hydra account password
    std::string hydra_game;     // game name to /connect on Hydra
    std::string hydra_session;  // last-known session_id for resume-on-restart
};

class WorldDB {
public:
    bool load(const std::string& path);
    bool save(const std::string& path) const;

    const World* find(const std::string& name) const;
    void add(const World& w);
    void remove(const std::string& name);
    std::vector<std::string> names() const;

private:
    std::unordered_map<std::string, World> worlds_;
};

#endif // WORLD_H
