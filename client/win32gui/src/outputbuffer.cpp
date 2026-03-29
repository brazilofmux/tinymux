// outputbuffer.cpp -- Line storage with co_render_attrs integration.
#include "outputbuffer.h"

void OutputBuffer::append(const std::string& pua_line) {
    OutputLine ol;
    ol.raw_pua = pua_line;
    render_line(ol);
    lines_.push_back(std::move(ol));
    open_line_ = false;
    trim_to_limit();
}

void OutputBuffer::append_text(const std::string& pua_text) {
    size_t pos = 0;
    while (pos <= pua_text.size()) {
        size_t nl = pua_text.find('\n', pos);
        bool has_newline = nl != std::string::npos;
        size_t end = has_newline ? nl : pua_text.size();
        std::string chunk = pua_text.substr(pos, end - pos);
        if (!chunk.empty() && chunk.back() == '\r') {
            chunk.pop_back();
        }

        if (!chunk.empty() || has_newline) {
            if (open_line_ && !lines_.empty()) {
                lines_.back().raw_pua += chunk;
                render_line(lines_.back());
            } else {
                OutputLine ol;
                ol.raw_pua = std::move(chunk);
                render_line(ol);
                lines_.push_back(std::move(ol));
            }
        }

        if (!has_newline) {
            open_line_ = !pua_text.empty();
            trim_to_limit();
            return;
        }

        open_line_ = false;
        pos = nl + 1;
        if (pos == pua_text.size()) {
            trim_to_limit();
            return;
        }
    }
}

void OutputBuffer::render_line(OutputLine& line) {
    unsigned char out_text[LBUF_SIZE];
    co_color_attr out_attrs[LBUF_SIZE];

    size_t n = co_render_attrs(out_attrs, out_text,
                               (const unsigned char*)line.raw_pua.data(),
                               line.raw_pua.size(), 0);

    line.text.assign((const char*)out_text, n);
    line.attrs.assign(out_attrs, out_attrs + n);
    line.display_width = (int)co_visual_width(
        (const unsigned char*)line.raw_pua.data(), line.raw_pua.size());
}

void OutputBuffer::trim_to_limit() {
    while (lines_.size() > MAX_LINES) {
        lines_.pop_front();
    }
}

void OutputBuffer::clear() {
    lines_.clear();
    scroll_offset = 0;
    open_line_ = false;
}
