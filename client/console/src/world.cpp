// world.cpp -- World database.
#include "world.h"
#include <fstream>
#include <sstream>
#include <algorithm>

const World* WorldDB::find(const std::string& name) const {
    auto it = worlds_.find(name);
    return it != worlds_.end() ? &it->second : nullptr;
}

void WorldDB::add(const World& w) {
    worlds_[w.name] = w;
}

void WorldDB::remove(const std::string& name) {
    worlds_.erase(name);
}

std::vector<std::string> WorldDB::names() const {
    std::vector<std::string> result;
    result.reserve(worlds_.size());
    for (auto& [k, v] : worlds_) result.push_back(k);
    std::sort(result.begin(), result.end());
    return result;
}

// World file format:
//   world <name> <host> <port> [ssl]
//   hydra <name> <host> <port> <user> <pass> <game>
bool WorldDB::load(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        std::string keyword;
        ss >> keyword;
        if (keyword == "world") {
            World w;
            ss >> w.name >> w.host >> w.port;
            std::string token;
            while (ss >> token) {
                if (token == "ssl") w.use_ssl = true;
            }
            if (!w.name.empty() && !w.host.empty() && !w.port.empty()) {
                worlds_[w.name] = w;
            }
        } else if (keyword == "hydra") {
            World w;
            w.use_hydra = true;
            w.use_ssl = true;  // TLS by default for Hydra
            ss >> w.name >> w.host >> w.port >> w.hydra_user >> w.hydra_pass >> w.hydra_game;
            std::string token;
            while (ss >> token) {
                if (token == "notls") w.use_ssl = false;
            }
            if (!w.name.empty() && !w.host.empty() && !w.port.empty()
                && !w.hydra_user.empty() && !w.hydra_pass.empty()) {
                worlds_[w.name] = w;
            }
        }
    }
    return true;
}

bool WorldDB::save(const std::string& path) const {
    std::ofstream f(path);
    if (!f) return false;
    for (auto& name : names()) {
        auto& w = worlds_.at(name);
        if (w.use_hydra) {
            f << "hydra " << w.name << " " << w.host << " " << w.port
              << " " << w.hydra_user << " " << w.hydra_pass
              << " " << w.hydra_game << "\n";
        } else {
            f << "world " << w.name << " " << w.host << " " << w.port;
            if (w.use_ssl) f << " ssl";
            f << "\n";
        }
    }
    return true;
}
