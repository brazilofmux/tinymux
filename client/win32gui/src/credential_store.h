// credential_store.h -- Windows Credential Manager wrapper for Hydra passwords.
#ifndef CREDENTIAL_STORE_H
#define CREDENTIAL_STORE_H

#include <string>

namespace CredStore {
    // Store credentials under "Titan:<world_name>".
    bool Save(const std::string& world_name, const std::string& username,
              const std::string& password);

    // Retrieve stored credentials. Returns empty string if not found.
    std::string LoadPassword(const std::string& world_name);
    std::string LoadUsername(const std::string& world_name);

    // Remove stored credentials for a world.
    void Remove(const std::string& world_name);
}

#endif // CREDENTIAL_STORE_H
