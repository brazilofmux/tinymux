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

    // ---- Session persistence ----

    struct SavedSession {
        std::string id;
        uint32_t    accountId;
        std::string created;
        std::string lastActive;
        std::string linksJson;
    };

    // Save or update a session record.
    bool saveSession(const std::string& sessionId, uint32_t accountId,
                     const std::string& created, const std::string& lastActive,
                     const std::string& linksJson, std::string& errorMsg);

    // Load a saved session for an account. Returns true if found.
    bool loadSession(uint32_t accountId, SavedSession& out);

    // Delete a session and its scroll-back.
    bool deleteSession(const std::string& sessionId);

    // Access the database handle (for scroll-back flush/load).
    sqlite3* db() { return db_; }

private:
    bool createSchema(std::string& errorMsg);

    sqlite3* db_{nullptr};
};

#endif // HYDRA_ACCOUNT_MANAGER_H
