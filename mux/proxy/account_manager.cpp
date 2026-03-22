#include "account_manager.h"
#include "hydra_log.h"
#include <sqlite3.h>

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

    // WAL mode for concurrent reads
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
    // TODO: Argon2id hash, generate salts, INSERT
    (void)username;
    (void)password;
    (void)admin;
    (void)accountIdOut;
    errorMsg = "not yet implemented";
    return false;
}

uint32_t AccountManager::authenticate(const std::string& username,
                                      const std::string& password,
                                      std::vector<uint8_t>& scrollbackKeyOut) {
    // TODO: SELECT pw_hash, pw_salt, sb_key_salt; verify Argon2id;
    //       derive scroll-back key
    (void)username;
    (void)password;
    (void)scrollbackKeyOut;
    return 0;
}

bool AccountManager::changePassword(uint32_t accountId,
                                    const std::string& newPassword,
                                    std::string& errorMsg) {
    // TODO: re-hash, new sb_key_salt, re-encrypt scroll-back
    (void)accountId;
    (void)newPassword;
    errorMsg = "not yet implemented";
    return false;
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
