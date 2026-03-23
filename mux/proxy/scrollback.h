#ifndef HYDRA_SCROLLBACK_H
#define HYDRA_SCROLLBACK_H

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

struct sqlite3;

class ScrollBack {
public:
    explicit ScrollBack(size_t capacity = 10000);

    // Append a line (PUA-encoded UTF-8).
    void append(const std::string& text, const std::string& source);

    // Replay up to maxLines to a callback.
    // The callback receives (text, source, timestamp) for each line.
    using ReplayCallback = void(*)(const std::string& text,
                                   const std::string& source,
                                   time_t timestamp,
                                   void* context);
    void replay(size_t maxLines, ReplayCallback cb, void* context) const;

    // Number of lines currently buffered.
    size_t count() const { return count_; }

    // Approximate memory usage in bytes (text + source strings).
    size_t memoryBytes() const { return memoryBytes_; }

    // Number of unflushed lines.
    size_t dirtyCount() const { return dirtyCount_; }

    // Mark all lines as flushed.
    void clearDirty() { dirtyCount_ = 0; }

    // ---- Persistence ----

    // Flush dirty lines to SQLite, encrypted with the player-derived key.
    // Returns number of lines flushed, or -1 on error.
    int flushToDb(sqlite3* db,
                  const std::string& sessionId,
                  uint32_t accountId,
                  const std::vector<uint8_t>& key);

    // Load persisted scroll-back from SQLite, decrypt, populate ring buffer.
    // Returns number of lines loaded, or -1 on error.
    int loadFromDb(sqlite3* db,
                   const std::string& sessionId,
                   uint32_t accountId,
                   const std::vector<uint8_t>& key);

    // Delete all persisted scroll-back for a session.
    static bool deleteFromDb(sqlite3* db, const std::string& sessionId);

private:
    struct Line {
        std::string text;
        std::string source;
        time_t      timestamp;
        uint64_t    seq;
    };

    std::vector<Line> buffer_;
    size_t head_{0};
    size_t count_{0};
    size_t capacity_;
    size_t dirtyCount_{0};
    size_t memoryBytes_{0};
    uint64_t nextSeq_{1};
};

#endif // HYDRA_SCROLLBACK_H
