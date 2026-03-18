// terminal.cpp -- Win32 Console terminal implementation.
#include "terminal.h"
#include <cstring>
#include <algorithm>

// Render a PUA-colored string to ANSI escape sequences for console output.
std::string Terminal::render_line(const std::string& line) {
    if (line.empty()) return line;
    unsigned char out[LBUF_SIZE * 4];
    const unsigned char* data = (const unsigned char*)line.data();
    size_t len = line.size();
    size_t n;
    if (vt_enabled_) {
        n = co_render_truecolor(out, data, len, 0);
    } else {
        n = co_render_ansi16(out, data, len, 0);
    }
    return std::string((const char*)out, n);
}

// Display column of cursor within input_buf_ (up to cursor_pos_ bytes).
int Terminal::cursor_display_col() const {
    if (cursor_pos_ == 0) return 0;
    return (int)co_visual_width((const unsigned char*)input_buf_.data(), cursor_pos_);
}

// Advance past one grapheme cluster starting at byte position pos.
size_t Terminal::cluster_next(size_t pos) const {
    if (pos >= input_buf_.size()) return input_buf_.size();
    const unsigned char* data = (const unsigned char*)input_buf_.data();
    const unsigned char* pe = data + input_buf_.size();
    const unsigned char* p = data + pos;
    const unsigned char* next = co_cluster_advance(p, pe, 1, nullptr);
    return (size_t)(next - data);
}

// Move backward one grapheme cluster ending at byte position pos.
size_t Terminal::cluster_prev(size_t pos) const {
    if (pos == 0) return 0;
    // Walk forward cluster by cluster until we find the one that ends at or past pos
    const unsigned char* data = (const unsigned char*)input_buf_.data();
    const unsigned char* pe = data + input_buf_.size();
    const unsigned char* p = data;
    const unsigned char* prev = data;
    while (p < data + pos) {
        prev = p;
        p = co_cluster_advance(p, pe, 1, nullptr);
    }
    return (size_t)(prev - data);
}

Terminal::Terminal() {}
Terminal::~Terminal() { shutdown(); }

bool Terminal::init() {
    if (initialized_) return true;

    hOut_ = GetStdHandle(STD_OUTPUT_HANDLE);
    hIn_  = GetStdHandle(STD_INPUT_HANDLE);
    if (hOut_ == INVALID_HANDLE_VALUE || hIn_ == INVALID_HANDLE_VALUE) return false;

    // Save original modes
    GetConsoleMode(hOut_, &orig_out_mode_);
    GetConsoleMode(hIn_, &orig_in_mode_);

    // Enable VT processing for ANSI color output
    DWORD out_mode = orig_out_mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING |
                     ENABLE_PROCESSED_OUTPUT;
    vt_enabled_ = SetConsoleMode(hOut_, out_mode) != 0;
    if (!vt_enabled_) {
        // Fall back without VT
        SetConsoleMode(hOut_, orig_out_mode_ | ENABLE_PROCESSED_OUTPUT);
    }

    // Set input mode: we want raw key events, window resize events, no line editing
    SetConsoleMode(hIn_, ENABLE_WINDOW_INPUT | ENABLE_EXTENDED_FLAGS);

    // Set console to UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    update_size();
    initialized_ = true;

    // Initial clear and draw
    DWORD written;
    COORD origin = {0, 0};
    FillConsoleOutputCharacterW(hOut_, L' ', (DWORD)(rows_ * cols_), origin, &written);
    FillConsoleOutputAttribute(hOut_, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
                               (DWORD)(rows_ * cols_), origin, &written);

    redraw_status();
    redraw_input();
    return true;
}

void Terminal::shutdown() {
    if (!initialized_) return;
    SetConsoleMode(hOut_, orig_out_mode_);
    SetConsoleMode(hIn_, orig_in_mode_);
    initialized_ = false;
}

void Terminal::update_size() {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(hOut_, &csbi)) {
        cols_ = csbi.srWindow.Right - csbi.srWindow.Left + 1;
        rows_ = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
    }
    if (rows_ < 4) rows_ = 4;
    if (cols_ < 20) cols_ = 20;

    // Set buffer size to match window to avoid scrollbars
    COORD buf_size = { (SHORT)cols_, (SHORT)rows_ };
    SetConsoleScreenBufferSize(hOut_, buf_size);
}

InputEvent Terminal::translate(const INPUT_RECORD& rec) {
    InputEvent ev;
    if (rec.EventType == KEY_EVENT && rec.Event.KeyEvent.bKeyDown) {
        auto& ke = rec.Event.KeyEvent;
        ev.ctrl  = (ke.dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
        ev.alt   = (ke.dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
        ev.shift = (ke.dwControlKeyState & SHIFT_PRESSED) != 0;

        switch (ke.wVirtualKeyCode) {
        case VK_UP:     ev.type = InputEvent::Key_Up; break;
        case VK_DOWN:   ev.type = InputEvent::Key_Down; break;
        case VK_LEFT:   ev.type = InputEvent::Key_Left; break;
        case VK_RIGHT:  ev.type = InputEvent::Key_Right; break;
        case VK_HOME:   ev.type = InputEvent::Key_Home; break;
        case VK_END:    ev.type = InputEvent::Key_End; break;
        case VK_PRIOR:  ev.type = InputEvent::Key_PageUp; break;
        case VK_NEXT:   ev.type = InputEvent::Key_PageDown; break;
        case VK_BACK:   ev.type = InputEvent::Key_Backspace; break;
        case VK_DELETE: ev.type = InputEvent::Key_Delete; break;
        case VK_RETURN: ev.type = InputEvent::Key_Enter; break;
        case VK_TAB:    ev.type = InputEvent::Key_Tab; break;
        case VK_ESCAPE: ev.type = InputEvent::Key_Escape; break;
        case VK_F1:     ev.type = InputEvent::Key_F1; break;
        case VK_F2:     ev.type = InputEvent::Key_F2; break;
        case VK_F3:     ev.type = InputEvent::Key_F3; break;
        case VK_F4:     ev.type = InputEvent::Key_F4; break;
        case VK_F5:     ev.type = InputEvent::Key_F5; break;
        case VK_F6:     ev.type = InputEvent::Key_F6; break;
        case VK_F7:     ev.type = InputEvent::Key_F7; break;
        case VK_F8:     ev.type = InputEvent::Key_F8; break;
        case VK_F9:     ev.type = InputEvent::Key_F9; break;
        case VK_F10:    ev.type = InputEvent::Key_F10; break;
        case VK_F11:    ev.type = InputEvent::Key_F11; break;
        case VK_F12:    ev.type = InputEvent::Key_F12; break;
        default:
            // Check for printable Unicode character
            if (ke.uChar.UnicodeChar >= 32) {
                ev.type = InputEvent::Char;
                ev.codepoint = ke.uChar.UnicodeChar;
            } else if (ev.ctrl && ke.wVirtualKeyCode >= 'A' && ke.wVirtualKeyCode <= 'Z') {
                // Ctrl+letter: encode as codepoint 1-26 for command dispatch
                ev.type = InputEvent::Char;
                ev.codepoint = ke.wVirtualKeyCode - 'A' + 1;
            }
            break;
        }
    } else if (rec.EventType == WINDOW_BUFFER_SIZE_EVENT) {
        ev.type = InputEvent::Resize;
    }
    return ev;
}

bool Terminal::handle_key(const InputEvent& ev, std::string& out_line) {
    switch (ev.type) {
    case InputEvent::Key_Enter:
        out_line = input_buf_;
        // Save to per-world history
        if (!input_buf_.empty()) {
            auto& hist = current_history();
            hist.push_front(input_buf_);
            if (hist.size() > MAX_HISTORY) hist.pop_back();
        }
        input_buf_.clear();
        cursor_pos_ = 0;
        history_pos_ = -1;
        redraw_input();
        return true;

    case InputEvent::Char:
        if (ev.ctrl) {
            // Ctrl+A = home, Ctrl+E = end, Ctrl+K = kill to end, Ctrl+U = kill line
            switch (ev.codepoint) {
            case 1: move_cursor_home(); break;  // Ctrl+A
            case 5: move_cursor_end(); break;   // Ctrl+E
            case 11: kill_to_end(); break;      // Ctrl+K
            case 21: kill_line(); break;        // Ctrl+U
            }
        } else {
            insert_char(ev.codepoint);
        }
        break;

    case InputEvent::Key_Backspace: delete_backward(); break;
    case InputEvent::Key_Delete:    delete_forward(); break;
    case InputEvent::Key_Left:
        if (ev.ctrl) move_word_left();
        else move_cursor_left();
        break;
    case InputEvent::Key_Right:
        if (ev.ctrl) move_word_right();
        else move_cursor_right();
        break;
    case InputEvent::Key_Home:   move_cursor_home(); break;
    case InputEvent::Key_End:    move_cursor_end(); break;
    case InputEvent::Key_Up:     history_up(); break;
    case InputEvent::Key_Down:   history_down(); break;
    case InputEvent::Key_PageUp: scroll_page_up(); break;
    case InputEvent::Key_PageDown: scroll_page_down(); break;
    case InputEvent::Resize:     handle_resize(); break;
    default: break;
    }
    return false;
}

// -- Output --

Terminal::OutputScreen& Terminal::current_output() {
    return output_screens_[output_key_];
}

void Terminal::set_output_context(const std::string& key) {
    output_key_ = key;
    redraw_output();
}

void Terminal::print_line(const std::string& line) {
    print_line_to(output_key_, line);
}

void Terminal::print_line_to(const std::string& context, const std::string& line) {
    auto& screen = output_screens_[context];
    screen.lines.push_back(line);
    while (screen.lines.size() > MAX_SCROLLBACK) {
        screen.lines.pop_front();
    }
    if (context == output_key_ && screen.scroll_offset == 0) {
        // Fast path: just scroll the output region up one line and write the new line
        redraw_output();
    }
}

void Terminal::print_system(const std::string& msg) {
    print_line("% " + msg);
}

void Terminal::set_status(const std::string& text) {
    status_text_ = text;
    redraw_status();
}

void Terminal::refresh() {
    // No-op for Win32 console — writes are immediate
}

void Terminal::handle_resize() {
    update_size();
    redraw_output();
    redraw_status();
    redraw_input();
}

// -- Scrollback --

void Terminal::scroll_up(int lines) {
    auto& screen = current_output();
    int max_offset = (int)screen.lines.size() - output_rows();
    if (max_offset < 0) max_offset = 0;
    screen.scroll_offset = std::min(screen.scroll_offset + lines, max_offset);
    redraw_output();
}

void Terminal::scroll_down(int lines) {
    auto& screen = current_output();
    screen.scroll_offset = std::max(screen.scroll_offset - lines, 0);
    redraw_output();
}

void Terminal::scroll_page_up() { scroll_up(output_rows() - 1); }
void Terminal::scroll_page_down() { scroll_down(output_rows() - 1); }
void Terminal::scroll_to_bottom() {
    current_output().scroll_offset = 0;
    redraw_output();
}

// -- History (per-world) --

std::deque<std::string>& Terminal::current_history() {
    return histories_[history_key_];
}

void Terminal::set_history_context(const std::string& key) {
    history_key_ = key;
    history_pos_ = -1;
}

void Terminal::history_up() {
    auto& hist = current_history();
    if (hist.empty()) return;
    if (history_pos_ < 0) {
        saved_input_ = input_buf_;
        history_pos_ = 0;
    } else if (history_pos_ < (int)hist.size() - 1) {
        history_pos_++;
    } else {
        return;
    }
    input_buf_ = hist[history_pos_];
    cursor_pos_ = input_buf_.size();
    redraw_input();
}

void Terminal::history_down() {
    if (history_pos_ < 0) return;
    history_pos_--;
    if (history_pos_ < 0) {
        input_buf_ = saved_input_;
    } else {
        input_buf_ = current_history()[history_pos_];
    }
    cursor_pos_ = input_buf_.size();
    redraw_input();
}

// -- Scrollback search --

std::vector<std::string> Terminal::recall(const std::string& pattern, int max_lines) const {
    std::vector<std::string> results;
    auto it = output_screens_.find(output_key_);
    if (it == output_screens_.end()) return results;
    auto& lines = it->second.lines;
    // Search backward
    for (int i = (int)lines.size() - 1; i >= 0 && (int)results.size() < max_lines; i--) {
        if (pattern.empty() || lines[i].find(pattern) != std::string::npos) {
            results.push_back(lines[i]);
        }
    }
    return results;
}

// -- Drawing --

void Terminal::clear_row(int row, WORD attr) {
    COORD pos = { 0, (SHORT)row };
    DWORD written;
    FillConsoleOutputCharacterW(hOut_, L' ', (DWORD)cols_, pos, &written);
    if (attr) {
        FillConsoleOutputAttribute(hOut_, attr, (DWORD)cols_, pos, &written);
    }
}

// Strip ANSI escape sequences from a string for non-VT consoles.
static std::string strip_ansi(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
            // Skip ESC [ ... (final byte 0x40-0x7E)
            i += 2;
            while (i < text.size() && (unsigned char)text[i] < 0x40) i++;
            if (i < text.size()) i++;  // skip final byte
        } else {
            out.push_back(text[i]);
            i++;
        }
    }
    return out;
}

void Terminal::write_at(int row, int col, const std::string& text, WORD attr) {
    COORD pos = { (SHORT)col, (SHORT)row };
    SetConsoleCursorPosition(hOut_, pos);
    DWORD written;
    if (vt_enabled_) {
        WriteConsoleA(hOut_, text.c_str(), (DWORD)text.size(), &written, nullptr);
    } else {
        std::string clean = strip_ansi(text);
        WriteConsoleA(hOut_, clean.c_str(), (DWORD)clean.size(), &written, nullptr);
    }
}

void Terminal::redraw_output() {
    auto& screen = current_output();
    int nlines = output_rows();
    int total = (int)screen.lines.size();
    int start = total - nlines - screen.scroll_offset;
    if (start < 0) start = 0;

    for (int i = 0; i < nlines; i++) {
        clear_row(i);
        int idx = start + i;
        if (idx >= 0 && idx < total) {
            write_at(i, 0, render_line(screen.lines[idx]));
        }
    }

    // Restore cursor to input line
    COORD pos = { (SHORT)cursor_display_col(), (SHORT)input_row() };
    SetConsoleCursorPosition(hOut_, pos);
}

void Terminal::redraw_status() {
    WORD attr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
                FOREGROUND_INTENSITY;
    clear_row(status_row(), attr);
    if (!status_text_.empty()) {
        COORD pos = { 0, (SHORT)status_row() };
        SetConsoleCursorPosition(hOut_, pos);
        // Write with the status bar attribute
        DWORD written;
        WriteConsoleA(hOut_, status_text_.c_str(),
                      (DWORD)std::min(status_text_.size(), (size_t)cols_),
                      &written, nullptr);
    }
    // Restore cursor
    COORD pos = { (SHORT)cursor_display_col(), (SHORT)input_row() };
    SetConsoleCursorPosition(hOut_, pos);
}

void Terminal::redraw_input() {
    clear_row(input_row());
    if (!input_buf_.empty()) {
        write_at(input_row(), 0, input_buf_);
    }
    COORD pos = { (SHORT)cursor_display_col(), (SHORT)input_row() };
    SetConsoleCursorPosition(hOut_, pos);
}

// -- Input line editing --

void Terminal::insert_char(uint32_t cp) {
    // Encode as UTF-8
    char buf[4];
    int len = 0;
    if (cp < 0x80) {
        buf[0] = (char)cp;
        len = 1;
    } else if (cp < 0x800) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        len = 2;
    } else if (cp < 0x10000) {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        len = 3;
    } else {
        buf[0] = (char)(0xF0 | (cp >> 18));
        buf[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        buf[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[3] = (char)(0x80 | (cp & 0x3F));
        len = 4;
    }
    input_buf_.insert(cursor_pos_, buf, len);
    cursor_pos_ += len;
    redraw_input();
}

void Terminal::delete_backward() {
    if (cursor_pos_ == 0) return;
    size_t prev = cluster_prev(cursor_pos_);
    input_buf_.erase(prev, cursor_pos_ - prev);
    cursor_pos_ = prev;
    redraw_input();
}

void Terminal::delete_forward() {
    if (cursor_pos_ >= input_buf_.size()) return;
    size_t next = cluster_next(cursor_pos_);
    input_buf_.erase(cursor_pos_, next - cursor_pos_);
    redraw_input();
}

void Terminal::move_cursor_left() {
    if (cursor_pos_ == 0) return;
    cursor_pos_ = cluster_prev(cursor_pos_);
    redraw_input();
}

void Terminal::move_cursor_right() {
    if (cursor_pos_ >= input_buf_.size()) return;
    cursor_pos_ = cluster_next(cursor_pos_);
    redraw_input();
}

void Terminal::move_cursor_home() {
    cursor_pos_ = 0;
    redraw_input();
}

void Terminal::move_cursor_end() {
    cursor_pos_ = input_buf_.size();
    redraw_input();
}

void Terminal::move_word_left() {
    if (cursor_pos_ == 0) return;
    cursor_pos_--;
    while (cursor_pos_ > 0 && input_buf_[cursor_pos_] == ' ') cursor_pos_--;
    while (cursor_pos_ > 0 && input_buf_[cursor_pos_ - 1] != ' ') cursor_pos_--;
    redraw_input();
}

void Terminal::move_word_right() {
    size_t len = input_buf_.size();
    while (cursor_pos_ < len && input_buf_[cursor_pos_] != ' ') cursor_pos_++;
    while (cursor_pos_ < len && input_buf_[cursor_pos_] == ' ') cursor_pos_++;
    redraw_input();
}

void Terminal::kill_line() {
    input_buf_.clear();
    cursor_pos_ = 0;
    redraw_input();
}

void Terminal::kill_to_end() {
    input_buf_.erase(cursor_pos_);
    redraw_input();
}
