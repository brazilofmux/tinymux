// outputbuffer.h -- Line storage with parallel color attributes.
#ifndef OUTPUTBUFFER_H
#define OUTPUTBUFFER_H

#include <string>
#include <vector>
#include <deque>

extern "C" {
#include "color_ops.h"
}

struct OutputLine {
    std::string text;                       // Visible UTF-8 (PUA stripped)
    std::vector<co_color_attr> attrs;       // Parallel color, same length as text
    int display_width = 0;                  // Column count
};

class OutputBuffer {
public:
    void append(const std::string& pua_line);
    void clear();

    const std::deque<OutputLine>& lines() const { return lines_; }
    size_t size() const { return lines_.size(); }

    int scroll_offset = 0;

    static constexpr size_t MAX_LINES = 20000;

private:
    std::deque<OutputLine> lines_;
};

#endif // OUTPUTBUFFER_H
