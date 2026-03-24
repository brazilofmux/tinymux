#include "scrollback.h"
#include "crypto.h"
#include "hydra_log.h"
#include <sqlite3.h>
#include <cstring>

ScrollBack::ScrollBack(size_t capacity)
    : capacity_(capacity) {
    buffer_.resize(capacity);
}

void ScrollBack::append(const std::string& text, const std::string& source) {
    Line& line = buffer_[head_];

    // Track memory: subtract evicted line, add new line
    if (count_ == capacity_) {
        // Ring is full — head_ slot is being overwritten
        memoryBytes_ -= line.text.size() + line.source.size();
    }
    memoryBytes_ += text.size() + source.size();

    line.text = text;
    line.source = source;
    line.timestamp = time(nullptr);
    line.seq = nextSeq_++;

    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_) {
        count_++;
    }
    dirtyCount_++;
}

void ScrollBack::replay(size_t maxLines, ReplayCallback cb,
                        void* context) const {
    if (count_ == 0 || maxLines == 0) return;

    size_t n = (maxLines < count_) ? maxLines : count_;

    // Calculate start position: n lines back from head_
    size_t start;
    if (head_ >= n) {
        start = head_ - n;
    } else {
        start = capacity_ - (n - head_);
    }

    for (size_t i = 0; i < n; i++) {
        size_t idx = (start + i) % capacity_;
        const Line& line = buffer_[idx];
        cb(line.text, line.source, line.timestamp, context);
    }
}

int ScrollBack::flushToDb(sqlite3* db,
                          const std::string& sessionId,
                          uint32_t accountId,
                          const std::vector<uint8_t>& key) {
    if (dirtyCount_ == 0) return 0;
    if (!db || key.size() < AEAD_KEY_LEN) return -1;

    std::string keyId = computeKeyId(key);

    // Dirty lines are the most recent dirtyCount_ lines.
    size_t n = dirtyCount_;
    if (n > count_) n = count_;

    size_t start;
    if (head_ >= n) {
        start = head_ - n;
    } else {
        start = capacity_ - (n - head_);
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "INSERT OR REPLACE INTO scrollback"
        " (session_id, seq, source, timestamp, ciphertext, nonce, key_id)"
        " VALUES (?, ?, ?, datetime(?, 'unixepoch'), ?, ?, ?)";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("scrollback flush prepare: %s", sqlite3_errmsg(db));
        return -1;
    }

    int flushed = 0;

    sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);

    for (size_t i = 0; i < n; i++) {
        size_t idx = (start + i) % capacity_;
        const Line& line = buffer_[idx];

        // Build AAD
        std::vector<uint8_t> aad = buildScrollbackAAD(
            accountId, sessionId, line.seq);

        // Generate random nonce
        uint8_t nonce[AEAD_NONCE_LEN];
        randomBytes(nonce, AEAD_NONCE_LEN);

        // Encrypt
        std::vector<uint8_t> ciphertext;
        if (!aeadEncrypt(key.data(), key.size(),
                         nonce, AEAD_NONCE_LEN,
                         aad.data(), aad.size(),
                         reinterpret_cast<const uint8_t*>(line.text.data()),
                         line.text.size(),
                         ciphertext)) {
            LOG_ERROR("scrollback encrypt failed for seq %lu",
                      (unsigned long)line.seq);
            continue;
        }

        sqlite3_reset(stmt);
        sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(line.seq));
        sqlite3_bind_text(stmt, 3, line.source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(line.timestamp));
        sqlite3_bind_blob(stmt, 5, ciphertext.data(),
                          static_cast<int>(ciphertext.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(stmt, 6, nonce, AEAD_NONCE_LEN, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, keyId.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            LOG_ERROR("scrollback flush insert: %s", sqlite3_errmsg(db));
        } else {
            flushed++;
        }
    }

    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    sqlite3_finalize(stmt);

    dirtyCount_ = 0;
    LOG_DEBUG("Flushed %d scroll-back lines for session %s",
              flushed, sessionId.c_str());
    return flushed;
}

int ScrollBack::loadFromDb(sqlite3* db,
                           const std::string& sessionId,
                           uint32_t accountId,
                           const std::vector<uint8_t>& key) {
    if (!db || key.size() < AEAD_KEY_LEN) return -1;

    sqlite3_stmt* stmt = nullptr;
    const char* sql =
        "SELECT seq, source, strftime('%s', timestamp), ciphertext, nonce, key_id"
        " FROM scrollback WHERE session_id = ? ORDER BY seq ASC";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("scrollback load prepare: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);

    std::string keyId = computeKeyId(key);
    int loaded = 0;
    int skipped = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        uint64_t seq = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        const char* source = (const char*)sqlite3_column_text(stmt, 1);
        int64_t ts = sqlite3_column_int64(stmt, 2);
        const void* ctBlob = sqlite3_column_blob(stmt, 3);
        int ctLen = sqlite3_column_bytes(stmt, 3);
        const void* nonceBlob = sqlite3_column_blob(stmt, 4);
        int nonceLen = sqlite3_column_bytes(stmt, 4);
        const char* rowKeyId = (const char*)sqlite3_column_text(stmt, 5);

        if (!source || !ctBlob || !nonceBlob || !rowKeyId) continue;

        // Check key_id matches — stale rows from before password change
        if (keyId != rowKeyId) {
            skipped++;
            continue;
        }

        if (nonceLen != AEAD_NONCE_LEN) {
            LOG_WARN("scrollback load: bad nonce len %d for seq %lu",
                     nonceLen, (unsigned long)seq);
            continue;
        }

        // Build AAD
        std::vector<uint8_t> aad = buildScrollbackAAD(
            accountId, sessionId, seq);

        // Decrypt
        std::vector<uint8_t> plaintext;
        if (!aeadDecrypt(key.data(), key.size(),
                         static_cast<const uint8_t*>(nonceBlob),
                         AEAD_NONCE_LEN,
                         aad.data(), aad.size(),
                         static_cast<const uint8_t*>(ctBlob),
                         static_cast<size_t>(ctLen),
                         plaintext)) {
            LOG_WARN("scrollback decrypt failed for seq %lu (tampered?)",
                     (unsigned long)seq);
            continue;
        }

        // Append to ring buffer
        Line& line = buffer_[head_];
        line.text.assign(reinterpret_cast<char*>(plaintext.data()),
                         plaintext.size());
        line.source = source;
        line.timestamp = static_cast<time_t>(ts);
        line.seq = seq;

        head_ = (head_ + 1) % capacity_;
        if (count_ < capacity_) count_++;

        if (seq >= nextSeq_) nextSeq_ = seq + 1;
        loaded++;
    }

    sqlite3_finalize(stmt);

    if (skipped > 0) {
        LOG_INFO("scrollback load: skipped %d stale-key rows for session %s",
                 skipped, sessionId.c_str());
    }
    LOG_INFO("Loaded %d scroll-back lines for session %s",
             loaded, sessionId.c_str());
    return loaded;
}

bool ScrollBack::deleteFromDb(sqlite3* db, const std::string& sessionId) {
    if (!db) return false;

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "DELETE FROM scrollback WHERE session_id = ?";
    int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE;
}

int ScrollBack::reencryptInDb(sqlite3* db,
                              const std::string& sessionId,
                              uint32_t accountId,
                              const std::vector<uint8_t>& oldKey,
                              const std::vector<uint8_t>& newKey) {
    if (!db || oldKey.size() < AEAD_KEY_LEN || newKey.size() < AEAD_KEY_LEN) {
        return -1;
    }

    std::string oldKeyId = computeKeyId(oldKey);
    std::string newKeyId = computeKeyId(newKey);

    // Read all rows encrypted with the old key
    sqlite3_stmt* selStmt = nullptr;
    const char* selSql =
        "SELECT seq, source, strftime('%s', timestamp), ciphertext, nonce"
        " FROM scrollback WHERE session_id = ? AND key_id = ?"
        " ORDER BY seq ASC";
    int rc = sqlite3_prepare_v2(db, selSql, -1, &selStmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("reencrypt select prepare: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(selStmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(selStmt, 2, oldKeyId.c_str(), -1, SQLITE_TRANSIENT);

    struct Row {
        uint64_t seq;
        std::string source;
        int64_t timestamp;
        std::string plaintext;
    };
    std::vector<Row> rows;

    while (sqlite3_step(selStmt) == SQLITE_ROW) {
        uint64_t seq = static_cast<uint64_t>(sqlite3_column_int64(selStmt, 0));
        const char* source = (const char*)sqlite3_column_text(selStmt, 1);
        int64_t ts = sqlite3_column_int64(selStmt, 2);
        const void* ctBlob = sqlite3_column_blob(selStmt, 3);
        int ctLen = sqlite3_column_bytes(selStmt, 3);
        const void* nonceBlob = sqlite3_column_blob(selStmt, 4);
        int nonceLen = sqlite3_column_bytes(selStmt, 4);

        if (!source || !ctBlob || !nonceBlob || nonceLen != AEAD_NONCE_LEN) {
            continue;
        }

        std::vector<uint8_t> aad = buildScrollbackAAD(accountId, sessionId, seq);
        std::vector<uint8_t> plaintext;
        if (!aeadDecrypt(oldKey.data(), oldKey.size(),
                         static_cast<const uint8_t*>(nonceBlob),
                         AEAD_NONCE_LEN,
                         aad.data(), aad.size(),
                         static_cast<const uint8_t*>(ctBlob),
                         static_cast<size_t>(ctLen),
                         plaintext)) {
            LOG_WARN("reencrypt: decrypt failed for seq %lu", (unsigned long)seq);
            continue;
        }

        Row row;
        row.seq = seq;
        row.source = source;
        row.timestamp = ts;
        row.plaintext.assign(reinterpret_cast<char*>(plaintext.data()),
                             plaintext.size());
        rows.push_back(std::move(row));
    }
    sqlite3_finalize(selStmt);

    if (rows.empty()) return 0;

    // Re-encrypt and update each row with the new key
    sqlite3_stmt* updStmt = nullptr;
    const char* updSql =
        "INSERT OR REPLACE INTO scrollback"
        " (session_id, seq, source, timestamp, ciphertext, nonce, key_id)"
        " VALUES (?, ?, ?, datetime(?, 'unixepoch'), ?, ?, ?)";
    rc = sqlite3_prepare_v2(db, updSql, -1, &updStmt, nullptr);
    if (rc != SQLITE_OK) {
        LOG_ERROR("reencrypt update prepare: %s", sqlite3_errmsg(db));
        return -1;
    }

    int reencrypted = 0;
    sqlite3_exec(db, "BEGIN", nullptr, nullptr, nullptr);

    for (const Row& row : rows) {
        std::vector<uint8_t> aad = buildScrollbackAAD(
            accountId, sessionId, row.seq);

        uint8_t nonce[AEAD_NONCE_LEN];
        randomBytes(nonce, AEAD_NONCE_LEN);

        std::vector<uint8_t> ciphertext;
        if (!aeadEncrypt(newKey.data(), newKey.size(),
                         nonce, AEAD_NONCE_LEN,
                         aad.data(), aad.size(),
                         reinterpret_cast<const uint8_t*>(row.plaintext.data()),
                         row.plaintext.size(),
                         ciphertext)) {
            LOG_ERROR("reencrypt: encrypt failed for seq %lu",
                      (unsigned long)row.seq);
            continue;
        }

        sqlite3_reset(updStmt);
        sqlite3_bind_text(updStmt, 1, sessionId.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(updStmt, 2, static_cast<sqlite3_int64>(row.seq));
        sqlite3_bind_text(updStmt, 3, row.source.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(updStmt, 4, static_cast<sqlite3_int64>(row.timestamp));
        sqlite3_bind_blob(updStmt, 5, ciphertext.data(),
                          static_cast<int>(ciphertext.size()), SQLITE_TRANSIENT);
        sqlite3_bind_blob(updStmt, 6, nonce, AEAD_NONCE_LEN, SQLITE_TRANSIENT);
        sqlite3_bind_text(updStmt, 7, newKeyId.c_str(), -1, SQLITE_TRANSIENT);

        rc = sqlite3_step(updStmt);
        if (rc == SQLITE_DONE) {
            reencrypted++;
        } else {
            LOG_ERROR("reencrypt update: %s", sqlite3_errmsg(db));
        }
    }

    sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr);
    sqlite3_finalize(updStmt);

    LOG_INFO("Re-encrypted %d/%zu scroll-back rows for session %s",
             reencrypted, rows.size(), sessionId.c_str());
    return reencrypted;
}
