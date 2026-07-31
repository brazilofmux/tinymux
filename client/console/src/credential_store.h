// credential_store.h -- Platform secret store for Hydra account passwords.
// Windows: Credential Manager (target "HydraConsole:<world>").
// Unix: adjacent 0600 file (default worlds.cred) when SetFilePath is used.
#ifndef CREDENTIAL_STORE_H
#define CREDENTIAL_STORE_H

#include <string>

namespace CredStore {
    // Optional: Unix file-backend path (e.g. "worlds.cred"). No-op on Windows.
    void SetFilePath(const std::string& path);

    // Store credentials under a per-world key.
    bool Save(const std::string& world_name, const std::string& username,
              const std::string& password);

    // Retrieve stored credentials. Empty string if not found.
    std::string LoadPassword(const std::string& world_name);
    std::string LoadUsername(const std::string& world_name);

    // Remove stored credentials for a world.
    void Remove(const std::string& world_name);
}

#endif // CREDENTIAL_STORE_H
