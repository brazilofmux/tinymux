// outputbuffer.cpp -- Line storage with co_render_attrs integration.
#include "outputbuffer.h"

void OutputBuffer::append(const std::string& pua_line) {
    OutputLine ol;
    unsigned char out_text[LBUF_SIZE];
    co_color_attr out_attrs[LBUF_SIZE];

    size_t n = co_render_attrs(out_attrs, out_text,
                               (const unsigned char*)pua_line.data(),
                               pua_line.size(), 0);

    ol.text.assign((const char*)out_text, n);
    ol.attrs.assign(out_attrs, out_attrs + n);
    ol.display_width = (int)co_visual_width(
        (const unsigned char*)pua_line.data(), pua_line.size());

    lines_.push_back(std::move(ol));
    while (lines_.size() > MAX_LINES) {
        lines_.pop_front();
    }
}

void OutputBuffer::clear() {
    lines_.clear();
    scroll_offset = 0;
}
