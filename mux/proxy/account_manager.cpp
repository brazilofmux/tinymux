#include "account_manager.h"
#include "crypto.h"
#include "hydra_log.h"
#include <sqlite3.h>
#include <crypt.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

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

// ---- Session persistence ----

bool AccountManager::saveSession(const std::string& persistId,
                                 uint32_t accountId,
                                 const std::string& created,
                                 const std::string& lastActive,
                                 const std::string& linksJson,
                                 std::string& errorMsg) {
    if (!db_) { errorMsg = "no database"; return false; }

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT OR REPLACE INTO saved_sessions"
        " (id, account_id, created, last_active, links_json)"
        " VALUES (?, ?, ?, ?, ?)";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        errorMsg = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_text(stmt, 1, persistId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, static_cast<int>(accountId));
    sqlite3_bind_text(stmt, 3, created.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, lastActive.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, linksJson.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        errorMsg = sqlite3_errmsg(db_);
        return false;
    }
    return true;
}

bool AccountManager::loadSession(uint32_t accountId, SavedSession& out) {
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, account_id, created, last_active, links_json"
        " FROM saved_sessions WHERE account_id = ? LIMIT 1";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, static_cast<int>(accountId));

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }

    out.persistId = (const char*)sqlite3_column_text(stmt, 0);
    out.accountId = static_cast<uint32_t>(sqlite3_column_int(stmt, 1));
    const char* created = (const char*)sqlite3_column_text(stmt, 2);
    const char* lastActive = (const char*)sqlite3_column_text(stmt, 3);
    const char* links = (const char*)sqlite3_column_text(stmt, 4);
    out.created = created ? created : "";
    out.lastActive = lastActive ? lastActive : "";
    out.linksJson = links ? links : "";

    sqlite3_finalize(stmt);
    return true;
}

bool AccountManager::deleteSession(const std::string& persistId) {
    if (!db_) return false;

    // Cascade deletes scrollback rows via FK constraint.
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM saved_sessions WHERE id = ?";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, persistId.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<AccountManager::SavedSession>
AccountManager::loadAllSessions() {
    std::vector<SavedSession> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT id, account_id, created, last_active, links_json"
        " FROM saved_sessions";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        SavedSession s;
        const char* id = (const char*)sqlite3_column_text(stmt, 0);
        s.persistId = id ? id : "";
        s.accountId = static_cast<uint32_t>(sqlite3_column_int(stmt, 1));
        const char* created = (const char*)sqlite3_column_text(stmt, 2);
        const char* lastActive = (const char*)sqlite3_column_text(stmt, 3);
        const char* links = (const char*)sqlite3_column_text(stmt, 4);
        s.created = created ? created : "";
        s.lastActive = lastActive ? lastActive : "";
        s.linksJson = links ? links : "";
        result.push_back(std::move(s));
    }

    sqlite3_finalize(stmt);
    return result;
}

// ---- Master key and game credentials ----

// Build AAD for credential encryption:
// account_id (4 bytes LE) || game_name || character
static std::vector<uint8_t> buildCredentialAAD(uint32_t accountId,
                                               const std::string& game,
                                               const std::string& character) {
    std::vector<uint8_t> aad;
    aad.reserve(4 + game.size() + character.size());
    aad.push_back(static_cast<uint8_t>(accountId));
    aad.push_back(static_cast<uint8_t>(accountId >> 8));
    aad.push_back(static_cast<uint8_t>(accountId >> 16));
    aad.push_back(static_cast<uint8_t>(accountId >> 24));
    aad.insert(aad.end(), game.begin(), game.end());
    aad.insert(aad.end(), character.begin(), character.end());
    return aad;
}

bool AccountManager::loadMasterKey(const std::string& path,
                                   std::string& errorMsg) {
    // Check file permissions — warn if world-readable
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (st.st_mode & S_IROTH) {
            LOG_WARN("Master key file %s is world-readable! "
                     "Run: chmod 600 %s", path.c_str(), path.c_str());
        }
        if (st.st_mode & S_IWOTH) {
            LOG_WARN("Master key file %s is world-writable! "
                     "Run: chmod 600 %s", path.c_str(), path.c_str());
        }
    }

    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        errorMsg = "cannot open master key file: " + path;
        return false;
    }

    masterKey_.resize(AEAD_KEY_LEN);
    size_t n = fread(masterKey_.data(), 1, AEAD_KEY_LEN, f);
    fclose(f);

    if (n != AEAD_KEY_LEN) {
        errorMsg = "master key file too short (need 32 bytes, got "
                   + std::to_string(n) + ")";
        masterKey_.clear();
        return false;
    }

    masterKeyId_ = computeKeyId(masterKey_);
    LOG_INFO("Master key loaded from %s (key_id=%s)",
             path.c_str(), masterKeyId_.c_str());
    return true;
}

bool AccountManager::loadMasterKeyFromEnv(const std::string& envVar,
                                          std::string& errorMsg) {
    const char* hex = getenv(envVar.c_str());
    if (!hex || strlen(hex) == 0) {
        errorMsg = "environment variable " + envVar + " not set";
        return false;
    }

    std::string hexStr(hex);
    if (hexStr.size() != AEAD_KEY_LEN * 2) {
        errorMsg = envVar + " must be " + std::to_string(AEAD_KEY_LEN * 2)
                 + " hex characters (got " + std::to_string(hexStr.size()) + ")";
        return false;
    }

    masterKey_.resize(AEAD_KEY_LEN);
    for (size_t i = 0; i < AEAD_KEY_LEN; i++) {
        unsigned int byte = 0;
        if (sscanf(hexStr.c_str() + i * 2, "%02x", &byte) != 1) {
            errorMsg = envVar + " contains invalid hex at position "
                     + std::to_string(i * 2);
            masterKey_.clear();
            return false;
        }
        masterKey_[i] = static_cast<uint8_t>(byte);
    }

    masterKeyId_ = computeKeyId(masterKey_);
    LOG_INFO("Master key loaded from env %s (key_id=%s)",
             envVar.c_str(), masterKeyId_.c_str());
    return true;
}

bool AccountManager::generateMasterKey(const std::string& path,
                                       std::string& errorMsg) {
    masterKey_.resize(AEAD_KEY_LEN);
    randomBytes(masterKey_.data(), AEAD_KEY_LEN);

    // Write with restrictive permissions (owner-only)
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) {
        if (errno == EEXIST) {
            errorMsg = "master key file already exists: " + path;
        } else {
            errorMsg = "cannot create master key file: " + std::string(strerror(errno));
        }
        masterKey_.clear();
        return false;
    }

    ssize_t written = write(fd, masterKey_.data(), AEAD_KEY_LEN);
    close(fd);

    if (written != static_cast<ssize_t>(AEAD_KEY_LEN)) {
        errorMsg = "failed to write master key file";
        unlink(path.c_str());
        masterKey_.clear();
        return false;
    }

    masterKeyId_ = computeKeyId(masterKey_);
    LOG_INFO("Generated new master key at %s (key_id=%s, mode 0600)",
             path.c_str(), masterKeyId_.c_str());
    return true;
}

bool AccountManager::storeCredential(uint32_t accountId,
                                     const std::string& game,
                                     const std::string& character,
                                     const std::string& verb,
                                     const std::string& name,
                                     const std::string& secret,
                                     std::string& errorMsg) {
    if (!hasMasterKey()) {
        errorMsg = "no master key configured";
        return false;
    }
    if (!db_) { errorMsg = "no database"; return false; }

    // Encrypt the secret
    uint8_t nonce[AEAD_NONCE_LEN];
    randomBytes(nonce, AEAD_NONCE_LEN);

    std::vector<uint8_t> aad = buildCredentialAAD(accountId, game, character);
    std::vector<uint8_t> ciphertext;

    if (!aeadEncrypt(masterKey_.data(), masterKey_.size(),
                     nonce, AEAD_NONCE_LEN,
                     aad.data(), aad.size(),
                     reinterpret_cast<const uint8_t*>(secret.data()),
                     secret.size(),
                     ciphertext)) {
        errorMsg = "encryption failed";
        return false;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT OR REPLACE INTO game_credentials"
        " (account_id, game_name, character, login_verb, login_name,"
        "  secret_enc, secret_nonce, secret_key_id)"
        " VALUES (?, ?, ?, ?, ?, ?, ?, ?)";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        errorMsg = sqlite3_errmsg(db_);
        return false;
    }

    sqlite3_bind_int(stmt, 1, static_cast<int>(accountId));
    sqlite3_bind_text(stmt, 2, game.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, character.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, verb.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 5, name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 6, ciphertext.data(),
                      static_cast<int>(ciphertext.size()), SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 7, nonce, AEAD_NONCE_LEN, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 8, masterKeyId_.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        errorMsg = sqlite3_errmsg(db_);
        return false;
    }

    LOG_INFO("Credential stored for account %u game '%s' character '%s'",
             accountId, game.c_str(), character.c_str());
    return true;
}

bool AccountManager::deleteCredential(uint32_t accountId,
                                      const std::string& game,
                                      const std::string& character) {
    if (!db_) return false;

    sqlite3_stmt* stmt = nullptr;
    std::string sql;
    if (character.empty()) {
        sql = "DELETE FROM game_credentials"
              " WHERE account_id = ? AND game_name = ?";
    } else {
        sql = "DELETE FROM game_credentials"
              " WHERE account_id = ? AND game_name = ? AND character = ?";
    }

    int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, static_cast<int>(accountId));
    sqlite3_bind_text(stmt, 2, game.c_str(), -1, SQLITE_TRANSIENT);
    if (!character.empty()) {
        sqlite3_bind_text(stmt, 3, character.c_str(), -1, SQLITE_TRANSIENT);
    }

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

std::vector<AccountManager::GameCredential>
AccountManager::listCredentials(uint32_t accountId) {
    std::vector<GameCredential> result;
    if (!db_) return result;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT game_name, character, login_verb, login_name, auto_login"
        " FROM game_credentials WHERE account_id = ? ORDER BY game_name, character";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return result;

    sqlite3_bind_int(stmt, 1, static_cast<int>(accountId));

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        GameCredential cred;
        const char* g = (const char*)sqlite3_column_text(stmt, 0);
        const char* c = (const char*)sqlite3_column_text(stmt, 1);
        const char* v = (const char*)sqlite3_column_text(stmt, 2);
        const char* n = (const char*)sqlite3_column_text(stmt, 3);
        cred.game = g ? g : "";
        cred.character = c ? c : "";
        cred.verb = v ? v : "";
        cred.name = n ? n : "";
        cred.autoLogin = sqlite3_column_int(stmt, 4) != 0;
        result.push_back(std::move(cred));
    }

    sqlite3_finalize(stmt);
    return result;
}

bool AccountManager::getLoginSecret(uint32_t accountId,
                                    const std::string& game,
                                    std::string& verb,
                                    std::string& name,
                                    std::string& secret) {
    if (!hasMasterKey() || !db_) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT character, login_verb, login_name, secret_enc, secret_nonce,"
        "       secret_key_id"
        " FROM game_credentials"
        " WHERE account_id = ? AND game_name = ? AND auto_login = 1"
        " LIMIT 1";
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_int(stmt, 1, static_cast<int>(accountId));
    sqlite3_bind_text(stmt, 2, game.c_str(), -1, SQLITE_TRANSIENT);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return false;
    }

    const char* character = (const char*)sqlite3_column_text(stmt, 0);
    const char* dbVerb = (const char*)sqlite3_column_text(stmt, 1);
    const char* dbName = (const char*)sqlite3_column_text(stmt, 2);
    const void* ctBlob = sqlite3_column_blob(stmt, 3);
    int ctLen = sqlite3_column_bytes(stmt, 3);
    const void* nonceBlob = sqlite3_column_blob(stmt, 4);
    int nonceLen = sqlite3_column_bytes(stmt, 4);
    const char* keyId = (const char*)sqlite3_column_text(stmt, 5);

    if (!character || !dbVerb || !dbName || !ctBlob || !nonceBlob || !keyId) {
        sqlite3_finalize(stmt);
        return false;
    }

    // Check key_id matches current master key
    if (masterKeyId_ != keyId) {
        LOG_WARN("Credential for game '%s' encrypted with stale key %s"
                 " (current=%s)", game.c_str(), keyId, masterKeyId_.c_str());
        sqlite3_finalize(stmt);
        return false;
    }

    if (nonceLen != AEAD_NONCE_LEN) {
        sqlite3_finalize(stmt);
        return false;
    }

    // Build AAD and decrypt
    std::string charStr(character);
    std::vector<uint8_t> aad = buildCredentialAAD(accountId, game, charStr);

    std::vector<uint8_t> plaintext;
    bool ok = aeadDecrypt(masterKey_.data(), masterKey_.size(),
                          static_cast<const uint8_t*>(nonceBlob),
                          AEAD_NONCE_LEN,
                          aad.data(), aad.size(),
                          static_cast<const uint8_t*>(ctBlob),
                          static_cast<size_t>(ctLen),
                          plaintext);

    sqlite3_finalize(stmt);

    if (!ok) {
        LOG_ERROR("Credential decryption failed for game '%s' (tampered?)",
                  game.c_str());
        return false;
    }

    verb = dbVerb;
    name = dbName;
    secret.assign(reinterpret_cast<char*>(plaintext.data()), plaintext.size());
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
