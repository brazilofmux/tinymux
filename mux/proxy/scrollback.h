#ifndef HYDRA_SCROLLBACK_H
#define HYDRA_SCROLLBACK_H

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <string>
#include <vector>

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

    // Number of unflushed lines.
    size_t dirtyCount() const { return dirtyCount_; }

    // Mark all lines as flushed.
    void clearDirty() { dirtyCount_ = 0; }

private:
    struct Line {
        std::string text;
        std::string source;
        time_t      timestamp;
    };

    std::vector<Line> buffer_;
    size_t head_{0};
    size_t count_{0};
    size_t capacity_;
    size_t dirtyCount_{0};
};

#endif // HYDRA_SCROLLBACK_H
