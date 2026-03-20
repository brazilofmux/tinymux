// input_editor.cpp -- Platform-independent multi-line input editor.
//
// All editing logic, reflow, cursor movement, and history.
// Uses libmux color_ops for grapheme cluster segmentation and
// display-width computation.

#include "input_editor.h"
#include <algorithm>
#include <cstring>

extern "C" {
#include <color_ops.h>
}

// ---- UTF-8 helpers ----

size_t InputEditor::utf8_char_len(unsigned char lead) {
    if (lead < 0x80) return 1;
    if (lead < 0xC0) return 1;  // continuation byte — treat as 1
    if (lead < 0xE0) return 2;
    if (lead < 0xF0) return 3;
    return 4;
}

size_t InputEditor::utf8_prev_start(const std::string& s, size_t pos) {
    if (pos == 0) return 0;
    size_t p = pos - 1;
    while (p > 0 && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80)
        p--;
    return p;
}

// ---- Grapheme cluster operations ----

size_t InputEditor::cluster_end(size_t pos) const {
    if (pos >= buf_.size()) return buf_.size();
    const auto* p  = reinterpret_cast<const unsigned char*>(buf_.data() + pos);
    const auto* pe = reinterpret_cast<const unsigned char*>(buf_.data() + buf_.size());
    size_t count = 0;
    const auto* after = co_cluster_advance(p, pe, 1, &count);
    return pos + (size_t)(after - p);
}

size_t InputEditor::cluster_start(size_t pos) const {
    if (pos == 0) return 0;
    size_t candidate = utf8_prev_start(buf_, pos);
    // Verify: cluster_end(candidate) should reach pos.
    while (candidate > 0 && cluster_end(candidate) < pos) {
        candidate = utf8_prev_start(buf_, candidate);
    }
    // Walk back further in case of combining marks.
    while (candidate > 0) {
        size_t prev = utf8_prev_start(buf_, candidate);
        if (cluster_end(prev) >= pos) {
            candidate = prev;
        } else {
            break;
        }
    }
    return candidate;
}

int InputEditor::display_width_of(size_t from, size_t to) const {
    if (from >= to) return 0;
    return (int)co_visual_width(
        reinterpret_cast<const unsigned char*>(buf_.data() + from),
        to - from);
}

size_t InputEditor::normalize_cursor_pos(size_t pos) const {
    if (pos >= buf_.size()) return buf_.size();
    size_t cur = 0;
    while (cur < buf_.size()) {
        size_t next = cluster_end(cur);
        if (pos <= cur) return cur;
        if (pos < next) return cur;
        cur = next;
    }
    return buf_.size();
}

size_t InputEditor::word_left_pos(size_t pos) const {
    size_t cur = normalize_cursor_pos(pos);
    // Skip whitespace backward
    while (cur > 0) {
        size_t prev = cluster_start(cur);
        if (prev < cur && buf_[prev] == ' ') cur = prev;
        else break;
    }
    // Skip non-whitespace backward
    while (cur > 0) {
        size_t prev = cluster_start(cur);
        if (prev < cur && buf_[prev] != ' ') cur = prev;
        else break;
    }
    return cur;
}

size_t InputEditor::word_right_pos(size_t pos) const {
    size_t cur = normalize_cursor_pos(pos);
    // Skip non-whitespace forward
    while (cur < buf_.size() && buf_[cur] != ' ')
        cur = cluster_end(cur);
    // Skip whitespace forward
    while (cur < buf_.size() && buf_[cur] == ' ')
        cur = cluster_end(cur);
    return cur;
}

// ---- Reflow: break buffer into visual lines ----

void InputEditor::reflow() {
    vlines_.clear();

    if (buf_.empty()) {
        vlines_.push_back({0, 0, 0});
        cursor_vrow_ = 0;
        cursor_vcol_ = prompt_width_;
        return;
    }

    const auto* data = reinterpret_cast<const unsigned char*>(buf_.data());
    const auto* pe = data + buf_.size();

    size_t byte_pos = 0;
    int col = prompt_width_;  // first line starts after prompt
    size_t line_start = 0;
    int line_width = 0;

    while (byte_pos < buf_.size()) {
        const auto* p = data + byte_pos;
        size_t count = 0;
        const auto* after = co_cluster_advance(p, pe, 1, &count);
        if (count == 0) break;  // safety

        size_t cluster_bytes = (size_t)(after - p);
        int w = (int)co_visual_width(p, cluster_bytes);
        if (w < 0) w = 1;

        int line_cols = (vlines_.empty()) ? (cols_ - prompt_width_) : cols_;
        if (line_cols < 1) line_cols = 1;

        // Would this cluster exceed the current visual line?
        if (w > 0 && col + w > ((vlines_.empty()) ? cols_ : cols_)) {
            // End current visual line
            vlines_.push_back({line_start, byte_pos, line_width});
            line_start = byte_pos;
            line_width = 0;
            col = 0;
        }

        col += w;
        line_width += w;
        byte_pos += cluster_bytes;
    }

    // Final visual line
    vlines_.push_back({line_start, byte_pos, line_width});

    // Determine cursor visual position
    cursor_vrow_ = 0;
    cursor_vcol_ = 0;
    for (int i = 0; i < (int)vlines_.size(); ++i) {
        if (cursor_ >= vlines_[i].byte_start && cursor_ <= vlines_[i].byte_end) {
            // Prefer the line where cursor is at the start (not past end of prev line)
            // unless cursor == byte_end and this is not the last line
            if (cursor_ == vlines_[i].byte_end && i + 1 < (int)vlines_.size()
                && cursor_ == vlines_[i + 1].byte_start) {
                // Cursor is at the boundary — place it at start of next line
                continue;
            }
            cursor_vrow_ = i;
            int base_col = (i == 0) ? prompt_width_ : 0;
            cursor_vcol_ = base_col + display_width_of(vlines_[i].byte_start, cursor_);
            break;
        }
    }

    // If cursor is past the end of all lines (cursor_ == buf_.size())
    if (cursor_ >= buf_.size()) {
        int last = (int)vlines_.size() - 1;
        cursor_vrow_ = last;
        int base_col = (last == 0) ? prompt_width_ : 0;
        cursor_vcol_ = base_col + display_width_of(vlines_[last].byte_start, cursor_);

        // If the cursor would be past cols_, it wraps to next line
        if (cursor_vcol_ >= cols_ && cols_ > 0) {
            vlines_.push_back({buf_.size(), buf_.size(), 0});
            cursor_vrow_ = (int)vlines_.size() - 1;
            cursor_vcol_ = 0;
        }
    }
}

size_t InputEditor::byte_at_column(int vline_idx, int target_col) const {
    if (vline_idx < 0 || vline_idx >= (int)vlines_.size())
        return buf_.size();

    const auto& vl = vlines_[vline_idx];
    int base_col = (vline_idx == 0) ? prompt_width_ : 0;
    int adjusted_target = target_col - base_col;
    if (adjusted_target <= 0) return vl.byte_start;

    const auto* data = reinterpret_cast<const unsigned char*>(buf_.data());
    const auto* pe = data + buf_.size();

    size_t pos = vl.byte_start;
    int col = 0;

    while (pos < vl.byte_end) {
        const auto* p = data + pos;
        size_t count = 0;
        const auto* after = co_cluster_advance(p, pe, 1, &count);
        if (count == 0) break;

        size_t cluster_bytes = (size_t)(after - p);
        int w = (int)co_visual_width(p, cluster_bytes);
        if (w < 0) w = 1;

        // Snap: if we'd overshoot, stop before this cluster
        if (col + w > adjusted_target) {
            // If we're closer to this cluster than the previous position, snap forward
            if (adjusted_target - col >= w / 2 + 1) {
                pos += cluster_bytes;
            }
            break;
        }

        col += w;
        pos += cluster_bytes;

        if (col >= adjusted_target) break;
    }

    return pos;
}

// ---- Constructor ----

InputEditor::InputEditor() {
    reflow();
}

// ---- Configuration ----

void InputEditor::set_cols(int cols) {
    if (cols < 1) cols = 1;
    cols_ = cols;
    reflow();
}

void InputEditor::set_prompt_width(int w) {
    if (w < 0) w = 0;
    prompt_width_ = w;
    reflow();
}

void InputEditor::set_max_rows(int max) {
    if (max < 1) max = 1;
    max_rows_ = max;
}

int InputEditor::desired_rows() const {
    int n = (int)vlines_.size();
    if (n < 1) n = 1;
    if (n > max_rows_) n = max_rows_;
    return n;
}

// ---- Editing primitives ----

void InputEditor::insert_codepoint(uint32_t cp) {
    char mb[4];
    int len;
    if (cp < 0x80) {
        mb[0] = (char)cp; len = 1;
    } else if (cp < 0x800) {
        mb[0] = (char)(0xC0 | (cp >> 6));
        mb[1] = (char)(0x80 | (cp & 0x3F));
        len = 2;
    } else if (cp < 0x10000) {
        mb[0] = (char)(0xE0 | (cp >> 12));
        mb[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        mb[2] = (char)(0x80 | (cp & 0x3F));
        len = 3;
    } else {
        mb[0] = (char)(0xF0 | (cp >> 18));
        mb[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        mb[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        mb[3] = (char)(0x80 | (cp & 0x3F));
        len = 4;
    }
    buf_.insert(cursor_, mb, len);
    cursor_ += len;
    reflow();
}

void InputEditor::delete_cluster_before() {
    if (cursor_ > 0) {
        size_t prev = cluster_start(cursor_);
        buf_.erase(prev, cursor_ - prev);
        cursor_ = prev;
        reflow();
    }
}

void InputEditor::delete_cluster_at() {
    if (cursor_ < buf_.size()) {
        size_t end = cluster_end(cursor_);
        buf_.erase(cursor_, end - cursor_);
        reflow();
    }
}

// ---- External control ----

void InputEditor::set_text(const std::string& text) {
    buf_ = text;
    cursor_ = buf_.size();
    history_pos_ = -1;
    goal_col_ = -1;
    reflow();
}

void InputEditor::set_cursor(size_t byte_pos) {
    cursor_ = normalize_cursor_pos(byte_pos);
    goal_col_ = -1;
    reflow();
}

std::string InputEditor::take_line() {
    std::string line = buf_;
    buf_.clear();
    cursor_ = 0;
    history_pos_ = -1;
    goal_col_ = -1;
    reflow();
    return line;
}

// ---- History ----

void InputEditor::set_history_context(const std::string& key) {
    history_key_ = key;
    history_pos_ = -1;
    saved_input_.clear();
}

void InputEditor::history_up() {
    auto& hist = histories_[history_key_];
    if (hist.empty()) return;
    if (history_pos_ == -1) {
        saved_input_ = buf_;
        history_pos_ = 0;
    } else if (history_pos_ < (int)hist.size() - 1) {
        history_pos_++;
    } else {
        return;
    }
    buf_ = hist[history_pos_];
    cursor_ = buf_.size();
    goal_col_ = -1;
    reflow();
}

void InputEditor::history_down() {
    if (history_pos_ < 0) return;
    history_pos_--;
    if (history_pos_ < 0) {
        buf_ = saved_input_;
    } else {
        buf_ = histories_[history_key_][history_pos_];
    }
    cursor_ = buf_.size();
    goal_col_ = -1;
    reflow();
}

// ---- Key dispatch ----

EditResult InputEditor::handle_key(int key, uint32_t cp) {
    int old_desired = desired_rows();

    auto finish = [&]() -> EditResult {
        int new_desired = desired_rows();
        if (new_desired != old_desired) return EditResult::RESIZE;
        return EditResult::REDRAW;
    };

    switch (key) {
    case K_CHAR:
        insert_codepoint(cp);
        update_goal_col();
        return finish();

    case K_ENTER: {
        // Save to history before clearing.
        if (!buf_.empty()) {
            auto& hist = histories_[history_key_];
            hist.push_front(buf_);
            if (hist.size() > MAX_HISTORY) hist.pop_back();
        }
        history_pos_ = -1;
        goal_col_ = -1;
        // Don't clear buf_ yet — caller will call take_line().
        return EditResult::SUBMIT;
    }

    case K_BACKSPACE:
        delete_cluster_before();
        update_goal_col();
        return finish();

    case K_DELETE:
        delete_cluster_at();
        update_goal_col();
        return finish();

    case K_LEFT:
    case K_CTRL_B:
        if (cursor_ > 0) {
            cursor_ = cluster_start(cursor_);
            reflow();
            update_goal_col();
        }
        return finish();

    case K_RIGHT:
    case K_CTRL_F:
        if (cursor_ < buf_.size()) {
            cursor_ = cluster_end(cursor_);
            reflow();
            update_goal_col();
        }
        return finish();

    case K_CTRL_LEFT: {
        // Word left
        size_t cur = cursor_;
        while (cur > 0) {
            size_t prev = cluster_start(cur);
            if (prev < cur && buf_[prev] == ' ') cur = prev;
            else break;
        }
        while (cur > 0) {
            size_t prev = cluster_start(cur);
            if (prev < cur && buf_[prev] != ' ') cur = prev;
            else break;
        }
        cursor_ = cur;
        reflow();
        update_goal_col();
        return finish();
    }

    case K_CTRL_RIGHT: {
        // Word right
        while (cursor_ < buf_.size() && buf_[cursor_] != ' ')
            cursor_ = cluster_end(cursor_);
        while (cursor_ < buf_.size() && buf_[cursor_] == ' ')
            cursor_ = cluster_end(cursor_);
        reflow();
        update_goal_col();
        return finish();
    }

    case K_UP:
    case K_CTRL_P:
        // If multiple visual lines and not on top row, move up within buffer
        if (cursor_vrow_ > 0) {
            int target = (goal_col_ >= 0) ? goal_col_ : cursor_vcol_;
            cursor_ = byte_at_column(cursor_vrow_ - 1, target);
            reflow();
            // Don't update goal_col_ — preserve it
        } else {
            history_up();
        }
        return finish();

    case K_DOWN:
    case K_CTRL_N:
        if (cursor_vrow_ < (int)vlines_.size() - 1) {
            int target = (goal_col_ >= 0) ? goal_col_ : cursor_vcol_;
            cursor_ = byte_at_column(cursor_vrow_ + 1, target);
            reflow();
            // Don't update goal_col_ — preserve it
        } else {
            history_down();
        }
        return finish();

    case K_HOME:
    case K_CTRL_A:
        // Move to start of current visual line
        if (cursor_vrow_ >= 0 && cursor_vrow_ < (int)vlines_.size()) {
            cursor_ = vlines_[cursor_vrow_].byte_start;
        } else {
            cursor_ = 0;
        }
        reflow();
        update_goal_col();
        return finish();

    case K_END:
    case K_CTRL_E:
        // Move to end of current visual line
        if (cursor_vrow_ >= 0 && cursor_vrow_ < (int)vlines_.size()) {
            size_t end = vlines_[cursor_vrow_].byte_end;
            // If not the last visual line, step back one cluster from the
            // wrap point so cursor stays on this line rather than wrapping
            // to the start of the next.
            if (cursor_vrow_ < (int)vlines_.size() - 1 && end > vlines_[cursor_vrow_].byte_start) {
                // Actually, byte_end is the wrap point.  We want the cursor
                // at byte_end — reflow() will place it at the start of the
                // next line visually, but that's wrong.  Instead, use the
                // last grapheme's start on this line.
                size_t last = cluster_start(end);
                if (last >= vlines_[cursor_vrow_].byte_start) {
                    // Position after the last cluster that fits (which is byte_end)
                    // but since byte_end == next line's byte_start, we need the
                    // last byte before that.
                    cursor_ = end;
                    // Reflow will place us on the next line — override below.
                } else {
                    cursor_ = end;
                }
            } else {
                cursor_ = end;
            }
        } else {
            cursor_ = buf_.size();
        }
        reflow();
        update_goal_col();
        return finish();

    case K_CTRL_HOME:
        cursor_ = 0;
        reflow();
        update_goal_col();
        return finish();

    case K_CTRL_END:
        cursor_ = buf_.size();
        reflow();
        update_goal_col();
        return finish();

    case K_CTRL_U:
        // Kill whole line
        buf_.clear();
        cursor_ = 0;
        goal_col_ = -1;
        reflow();
        return finish();

    case K_CTRL_K:
        // Kill to end of buffer
        buf_.erase(cursor_);
        reflow();
        update_goal_col();
        return finish();

    case K_CTRL_W: {
        // Kill word backward
        size_t end = cursor_;
        while (cursor_ > 0 && buf_[cursor_ - 1] == ' ')
            cursor_ = cluster_start(cursor_);
        while (cursor_ > 0 && buf_[cursor_ - 1] != ' ')
            cursor_ = cluster_start(cursor_);
        buf_.erase(cursor_, end - cursor_);
        reflow();
        update_goal_col();
        return finish();
    }

    case K_CTRL_D:
        // Delete at cursor (same as DELETE)
        if (buf_.empty()) {
            // Signal quit — caller handles this
            return EditResult::NONE;
        }
        delete_cluster_at();
        update_goal_col();
        return finish();

    case K_PAGE_UP:
    case K_PAGE_DOWN:
    case K_CTRL_L:
    case K_CTRL_UP:
    case K_CTRL_DOWN:
    case K_TAB:
    case K_ESCAPE:
    case K_INSERT:
        // These are handled by the terminal backend, not the editor.
        return EditResult::NONE;

    default:
        return EditResult::NONE;
    }
}
