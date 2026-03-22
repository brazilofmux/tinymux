#include "scrollback.h"

ScrollBack::ScrollBack(size_t capacity)
    : capacity_(capacity) {
    buffer_.resize(capacity);
}

void ScrollBack::append(const std::string& text, const std::string& source) {
    Line& line = buffer_[head_];
    line.text = text;
    line.source = source;
    line.timestamp = time(nullptr);

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
