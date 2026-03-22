#include "account_manager.h"
#include "hydra_log.h"
#include <sqlite3.h>
#include <crypt.h>
#include <cstring>
#include <random>

// Generate a random salt string for crypt(3) SHA-512.
static std::string generateSalt() {
    static const char charset[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789./";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);

    std::string salt = "$6$";  // SHA-512
    for (int i = 0; i < 16; i++) {
        salt += charset[dist(gen)];
    }
    salt += '$';
    return salt;
}

static std::string hashPassword(const std::string& password,
                                const std::string& salt) {
    struct crypt_data cd;
    memset(&cd, 0, sizeof(cd));
    char* result = crypt_r(password.c_str(), salt.c_str(), &cd);
    if (!result) return "";
    return result;
}

// Derive a scroll-back key from the password and a salt.
// For Phase 1, this is a SHA-512 crypt with a different salt.
// The key material is the hash bytes — not cryptographically ideal
// but functional until we add proper Argon2id + AEAD.
static std::vector<uint8_t> deriveScrollbackKey(
    const std::string& password, const std::string& sbKeySalt) {
    struct crypt_data cd;
    memset(&cd, 0, sizeof(cd));
    std::string salt = "$6$" + sbKeySalt + "$";
    char* result = crypt_r(password.c_str(), salt.c_str(), &cd);
    if (!result) return {};

    std::string hash(result);
    // Use last 32 bytes of the hash as key material
    std::vector<uint8_t> key(32, 0);
    size_t hlen = hash.size();
    for (size_t i = 0; i < 32 && i < hlen; i++) {
        key[i] = static_cast<uint8_t>(hash[hlen - 32 + i]);
    }
    return key;
}

AccountManager::AccountManager() {
}

AccountManager::~AccountManager() {
    shutdown();
}

bool AccountManager::initialize(const std::string& dbPath,
                                std::string& errorMsg) {
    int rc = sqlite3_open(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        errorMsg = "cannot open database: " + std::string(sqlite3_errmsg(db_));
        return false;
    }

    sqlite3_exec(db_, "PRAGMA journal_mode=WAL", nullptr, nullptr, nullptr);
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON", nullptr, nullptr, nullptr);

    return createSchema(errorMsg);
}

void AccountManager::shutdown() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool AccountManager::createSchema(std::string& errorMsg) {
    const char* sql = R"SQL(
        CREATE TABLE IF NOT EXISTS schema_version (
            version INTEGER NOT NULL
        );

        CREATE TABLE IF NOT EXISTS accounts (
            id          INTEGER PRIMARY KEY,
            username    TEXT UNIQUE NOT NULL COLLATE NOCASE,
            pw_hash     TEXT NOT NULL,
            pw_salt     TEXT NOT NULL,
            sb_key_salt TEXT NOT NULL,
            created     TEXT NOT NULL DEFAULT (datetime('now')),
            last_login  TEXT,
            flags       INTEGER DEFAULT 0
        );

        CREATE TABLE IF NOT EXISTS game_credentials (
            id          INTEGER PRIMARY KEY,
            account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
            game_name   TEXT NOT NULL,
            character   TEXT NOT NULL,
            login_verb  TEXT NOT NULL,
            login_name  TEXT NOT NULL,
            secret_enc  BLOB,
            secret_nonce BLOB NOT NULL,
            secret_key_id TEXT NOT NULL,
            auto_login  INTEGER DEFAULT 1,
            UNIQUE(account_id, game_name, character)
        );

        CREATE TABLE IF NOT EXISTS saved_sessions (
            id          TEXT PRIMARY KEY,
            account_id  INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
            created     TEXT NOT NULL,
            last_active TEXT NOT NULL,
            links_json  TEXT
        );

        CREATE TABLE IF NOT EXISTS scrollback (
            id          INTEGER PRIMARY KEY,
            session_id  TEXT NOT NULL REFERENCES saved_sessions(id) ON DELETE CASCADE,
            seq         INTEGER NOT NULL,
            source      TEXT NOT NULL,
            timestamp   TEXT NOT NULL,
            ciphertext  BLOB NOT NULL,
            nonce       BLOB NOT NULL,
            key_id      TEXT NOT NULL,
            UNIQUE(session_id, seq)
        );

        CREATE INDEX IF NOT EXISTS idx_scrollback_session
            ON scrollback(session_id, seq);
    )SQL";

    char* err = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        errorMsg = "schema creation failed: " + std::string(err ? err : "unknown");
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool AccountManager::createAccount(const std::string& username,
                                   const std::string& password,
                                   bool admin, uint32_t& accountIdOut,
                                   std::string& errorMsg) {
    if (username.empty() || password.empty()) {
        errorMsg = "username and password required";
        return false;
    }

    std::string salt = generateSalt();
    std::string hash = hashPassword(password, salt);
    if (hash.empty()) {
        errorMsg = "password hashing failed";
        return false;
    }

    // Generate scroll-back key salt (random string)
    std::string sbSalt;
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, 61);
        static const char c[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        for (int i = 0; i < 16; i++) sbSalt += c[dist(gen)];
    }

    int flags = admin ? 1 : 0;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT INTO accounts (username, pw_hash, pw_salt, sb_key_salt, flags)"
        " VALUES (?, ?, ?, ?, ?)";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        errorMsg = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, salt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, sbSalt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, flags);

    rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        errorMsg = sqlite3_errmsg(db_);
        sqlite3_finalize(stmt);
        return false;
    }

    accountIdOut = static_cast<uint32_t>(sqlite3_last_insert_rowid(db_));
    sqlite3_finalize(stmt);

    LOG_INFO("Account created: '%s' (id=%u, admin=%d)",
             username.c_str(), accountIdOut, admin);
    return true;
}

uint32_t AccountManager::authenticate(const std::string& username,
                                      const std::string& password,
                                      std::vector<uint8_t>& scrollbackKeyOut) {
    if (!db_) return 0;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, pw_hash, pw_salt, sb_key_salt FROM accounts"
        " WHERE username = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return 0;
    }

    uint32_t accountId = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
    const char* storedHash = (const char*)sqlite3_column_text(stmt, 1);
    const char* storedSalt = (const char*)sqlite3_column_text(stmt, 2);
    const char* sbKeySalt  = (const char*)sqlite3_column_text(stmt, 3);

    if (!storedHash || !storedSalt || !sbKeySalt) {
        sqlite3_finalize(stmt);
        return 0;
    }

    // Verify password
    std::string computed = hashPassword(password, storedSalt);
    if (computed.empty() || computed != storedHash) {
        sqlite3_finalize(stmt);
        return 0;  // Wrong password
    }

    // Derive scroll-back key
    scrollbackKeyOut = deriveScrollbackKey(password, sbKeySalt);

    sqlite3_finalize(stmt);

    // Update last_login
    sqlite3_exec(db_,
        ("UPDATE accounts SET last_login = datetime('now') WHERE id = "
         + std::to_string(accountId)).c_str(),
        nullptr, nullptr, nullptr);

    return accountId;
}

bool AccountManager::changePassword(uint32_t accountId,
                                    const std::string& newPassword,
                                    std::string& errorMsg) {
    std::string salt = generateSalt();
    std::string hash = hashPassword(newPassword, salt);
    if (hash.empty()) {
        errorMsg = "password hashing failed";
        return false;
    }

    // Generate new scroll-back key salt
    std::string sbSalt;
    {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dist(0, 61);
        static const char c[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
        for (int i = 0; i < 16; i++) sbSalt += c[dist(gen)];
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "UPDATE accounts SET pw_hash = ?, pw_salt = ?, sb_key_salt = ?"
        " WHERE id = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        errorMsg = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, salt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, sbSalt.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, static_cast<int>(accountId));

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        errorMsg = sqlite3_errmsg(db_);
        return false;
    }

    // TODO: re-encrypt scroll-back with new key
    return true;
}

bool AccountManager::isEmpty() {
    if (!db_) return true;

    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM accounts",
                                -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return true;

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count == 0;
}
