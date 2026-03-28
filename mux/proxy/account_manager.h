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
                        const std::vector<uint8_t>& oldScrollbackKey,
                        std::vector<uint8_t>& newScrollbackKeyOut,
                        std::string& errorMsg);

    // Returns true if the accounts table is empty (bootstrap mode).
    bool isEmpty();

    // Returns true if the account has admin privileges (flags & 1).
    bool isAdmin(uint32_t accountId);

    // ---- Session persistence ----

    struct SavedSession {
        std::string persistId;   // random hex, matches HydraSession::persistId
        uint32_t    accountId;
        std::string created;
        std::string lastActive;
        std::string linksJson;
    };

    // Save or update a session record.
    bool saveSession(const std::string& persistId, uint32_t accountId,
                     const std::string& created, const std::string& lastActive,
                     const std::string& linksJson, std::string& errorMsg);

    // Load a saved session for an account. Returns true if found.
    bool loadSession(uint32_t accountId, SavedSession& out);

    // Delete a session and its scroll-back.
    bool deleteSession(const std::string& persistId);

    // Load all saved sessions (for eager restore on startup).
    std::vector<SavedSession> loadAllSessions();

    // Access the database handle (for scroll-back flush/load).
    sqlite3* db() { return db_; }

    // ---- Master key and game credentials ----

    // Load the master key from a raw 32-byte binary file.
    // Also checks file permissions and warns if world-readable.
    bool loadMasterKey(const std::string& path, std::string& errorMsg);

    // Load the master key from a hex-encoded environment variable.
    bool loadMasterKeyFromEnv(const std::string& envVar, std::string& errorMsg);

    // Generate a new master key and save it to the given path.
    // Used for first-run auto-generation.
    bool generateMasterKey(const std::string& path, std::string& errorMsg);

    bool hasMasterKey() const { return masterKey_.size() >= 32; }

    // Store a game credential (encrypts secret with master key).
    bool storeCredential(uint32_t accountId, const std::string& game,
                         const std::string& character, const std::string& verb,
                         const std::string& name, const std::string& secret,
                         std::string& errorMsg);

    // Delete a credential.
    bool deleteCredential(uint32_t accountId, const std::string& game,
                          const std::string& character);

    struct GameCredential {
        std::string game, character, verb, name;
        bool autoLogin;
    };

    // List all credentials for an account (never returns secrets).
    std::vector<GameCredential> listCredentials(uint32_t accountId);

    // Decrypt and return the login secret for auto-login.
    // Returns true if a credential exists and decryption succeeds.
    bool getLoginSecret(uint32_t accountId, const std::string& game,
                        std::string& verb, std::string& name,
                        std::string& secret);

private:
    bool createSchema(std::string& errorMsg);

    sqlite3* db_{nullptr};
    std::vector<uint8_t> masterKey_;
    std::string masterKeyId_;
};

#endif // HYDRA_ACCOUNT_MANAGER_H
