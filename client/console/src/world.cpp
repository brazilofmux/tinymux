// world.cpp -- World database.
#include "world.h"
#include "credential_store.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>
#include <cstdlib>
#include <vector>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace {

// worlds.txt → worlds.cred (or path.cred if no .txt suffix).
std::string cred_path_for(const std::string& worlds_path) {
    const std::string suffix = ".txt";
    if (worlds_path.size() >= suffix.size()
        && worlds_path.compare(worlds_path.size() - suffix.size(),
                               suffix.size(), suffix) == 0) {
        return worlds_path.substr(0, worlds_path.size() - suffix.size()) + ".cred";
    }
    return worlds_path + ".cred";
}

void bind_cred_path(const std::string& worlds_path) {
    CredStore::SetFilePath(cred_path_for(worlds_path));
}

bool env_allow_insecure() {
    const char* v = std::getenv("HYDRA_ALLOW_INSECURE_WORLDS");
    return v && v[0] != '\0' && v[0] != '0';
}

// Write body to path, creating the file with restrictive permissions first.
bool write_private_file(const std::string& path, const std::string& body) {
#ifndef _WIN32
    int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) return false;
    const char* p = body.data();
    size_t left = body.size();
    while (left > 0) {
        ssize_t n = ::write(fd, p, left);
        if (n < 0) {
            ::close(fd);
            return false;
        }
        p += n;
        left -= static_cast<size_t>(n);
    }
    ::close(fd);
    ::chmod(path.c_str(), 0600);
    return true;
#else
    // Create/truncate before writing; avoid a long window as a world-readable file.
    HANDLE h = CreateFileA(
        path.c_str(),
        GENERIC_WRITE,
        0,  // no sharing while writing
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = WriteFile(h, body.data(), (DWORD)body.size(), &written, nullptr);
    CloseHandle(h);
    return ok && written == body.size();
#endif
}

bool is_option_token(const std::string& tok) {
    return tok == "notls" || tok.compare(0, 8, "session=") == 0;
}

} // namespace

const World* WorldDB::find(const std::string& name) const {
    auto it = worlds_.find(name);
    return it != worlds_.end() ? &it->second : nullptr;
}

void WorldDB::add(const World& w) {
    worlds_[w.name] = w;
    if (w.use_hydra && !w.hydra_pass.empty()) {
        CredStore::Save(w.name, w.hydra_user, w.hydra_pass);
    }
}

void WorldDB::remove(const std::string& name) {
    worlds_.erase(name);
    CredStore::Remove(name);
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
//   hydra <name> <host> <port> <user> <game> [notls] [session=...]
//     Password is NOT stored here — see CredStore / worlds.cred.
//   Legacy (migrated on load):
//   hydra <name> <host> <port> <user> <pass> <game> [notls] [session=...]
bool WorldDB::load(const std::string& path) {
    bind_cred_path(path);

#ifndef _WIN32
    struct stat st;
    if (stat(path.c_str(), &st) == 0 && (st.st_mode & 077) != 0) {
        if (!env_allow_insecure()) {
            std::cerr << "ERROR: " << path
                      << " has insecure permissions (group/other access)."
                      << " Run: chmod 600 " << path
                      << "  (or set HYDRA_ALLOW_INSECURE_WORLDS=1 to override)"
                      << std::endl;
            return false;
        }
        std::cerr << "WARNING: " << path
                  << " has insecure permissions; continuing because"
                  << " HYDRA_ALLOW_INSECURE_WORLDS is set." << std::endl;
    }
#endif

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
            if (!(ss >> w.name >> w.host >> w.port)) {
                std::cerr << "worlds: malformed world line: " << line << std::endl;
                continue;
            }
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
            if (!(ss >> w.name >> w.host >> w.port >> w.hydra_user)) {
                std::cerr << "worlds: malformed hydra line: " << line << std::endl;
                continue;
            }

            // Collect remaining tokens; options may appear at the end.
            std::vector<std::string> tokens;
            std::string token;
            while (ss >> token) tokens.push_back(token);

            for (const auto& t : tokens) {
                if (t == "notls") w.use_ssl = false;
                else if (t.compare(0, 8, "session=") == 0) {
                    w.hydra_session = t.substr(8);
                }
            }

            std::vector<std::string> middle;
            for (const auto& t : tokens) {
                if (!is_option_token(t)) middle.push_back(t);
            }

            // New: <game> only. Legacy: <pass> <game>.
            if (middle.size() == 1) {
                w.hydra_game = middle[0];
            } else if (middle.size() >= 2) {
                w.hydra_pass = middle[0];
                w.hydra_game = middle[1];
            } else {
                std::cerr << "worlds: hydra line missing game: " << line << std::endl;
                continue;
            }

            // Prefer Credential Manager / worlds.cred; migrate legacy plaintext.
            std::string stored_pass = CredStore::LoadPassword(w.name);
            std::string stored_user = CredStore::LoadUsername(w.name);
            if (!stored_pass.empty()) {
                w.hydra_pass = stored_pass;
                if (!stored_user.empty()) w.hydra_user = stored_user;
            } else if (!w.hydra_pass.empty()) {
                // #1891: migrate off disk into the platform store.
                if (!CredStore::Save(w.name, w.hydra_user, w.hydra_pass)) {
                    std::cerr << "worlds: WARNING: could not migrate password for '"
                              << w.name << "' into the credential store" << std::endl;
                }
            }

            if (!w.name.empty() && !w.host.empty() && !w.port.empty()
                && !w.hydra_user.empty() && !w.hydra_pass.empty()
                && !w.hydra_game.empty()) {
                worlds_[w.name] = w;
            } else if (w.hydra_pass.empty() && !w.name.empty()) {
                std::cerr << "worlds: hydra world '" << w.name
                          << "' has no password in credential store;"
                          << " re-add credentials or restore the secrets file"
                          << std::endl;
            }
        }
    }
    return true;
}

bool WorldDB::save(const std::string& path) const {
    bind_cred_path(path);

    std::ostringstream out;
    for (auto& name : names()) {
        auto& w = worlds_.at(name);
        if (w.use_hydra) {
            // #1891: never write hydra_pass to worlds.txt.
            if (!w.hydra_pass.empty()) {
                CredStore::Save(w.name, w.hydra_user, w.hydra_pass);
            }
            out << "hydra " << w.name << " " << w.host << " " << w.port
                << " " << w.hydra_user
                << " " << w.hydra_game;
            if (!w.use_ssl) out << " notls";
            if (!w.hydra_session.empty()) out << " session=" << w.hydra_session;
            out << "\n";
        } else {
            out << "world " << w.name << " " << w.host << " " << w.port;
            if (w.use_ssl) out << " ssl";
            out << "\n";
        }
    }

    return write_private_file(path, out.str());
}
