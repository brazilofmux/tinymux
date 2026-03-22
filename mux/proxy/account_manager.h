#ifndef HYDRA_ACCOUNT_MANAGER_H
#define HYDRA_ACCOUNT_MANAGER_H

#include <cstdint>
#include <string>
#include <vector>

struct sqlite3;

class AccountManager {
public:
    AccountManager();
    ~AccountManager();

    // Open (or create) the database at the given path.
    bool initialize(const std::string& dbPath, std::string& errorMsg);

    // Close the database.
    void shutdown();

    // Account operations.
    bool createAccount(const std::string& username,
                       const std::string& password,
                       bool admin, uint32_t& accountIdOut,
                       std::string& errorMsg);

    // Returns accountId on success, 0 on failure.
    // Also derives and returns the scroll-back key.
    uint32_t authenticate(const std::string& username,
                          const std::string& password,
                          std::vector<uint8_t>& scrollbackKeyOut);

    bool changePassword(uint32_t accountId,
                        const std::string& newPassword,
                        std::string& errorMsg);

    // Returns true if the accounts table is empty (bootstrap mode).
    bool isEmpty();

private:
    bool createSchema(std::string& errorMsg);

    sqlite3* db_{nullptr};
};

#endif // HYDRA_ACCOUNT_MANAGER_H
