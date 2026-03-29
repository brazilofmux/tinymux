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

// Map console InputEvent to InputEditor key code.
int Terminal::event_to_editor_key(const InputEvent& ev) {
    switch (ev.type) {
    case InputEvent::Char:
        if (ev.ctrl) {
            switch (ev.codepoint) {
            case 1:  return InputEditor::K_CTRL_A;
            case 2:  return InputEditor::K_CTRL_B;
            case 3:  return InputEditor::K_CTRL_C;
            case 4:  return InputEditor::K_CTRL_D;
            case 5:  return InputEditor::K_CTRL_E;
            case 6:  return InputEditor::K_CTRL_F;
            case 7:  return InputEditor::K_CTRL_G;
            case 11: return InputEditor::K_CTRL_K;
            case 12: return InputEditor::K_CTRL_L;
            case 14: return InputEditor::K_CTRL_N;
            case 15: return InputEditor::K_CTRL_O;
            case 16: return InputEditor::K_CTRL_P;
            case 17: return InputEditor::K_CTRL_Q;
            case 18: return InputEditor::K_CTRL_R;
            case 19: return InputEditor::K_CTRL_S;
            case 20: return InputEditor::K_CTRL_T;
            case 21: return InputEditor::K_CTRL_U;
            case 22: return InputEditor::K_CTRL_V;
            case 23: return InputEditor::K_CTRL_W;
            case 24: return InputEditor::K_CTRL_X;
            case 25: return InputEditor::K_CTRL_Y;
            case 26: return InputEditor::K_CTRL_Z;
            default: return InputEditor::K_UNKNOWN;
            }
        }
        return InputEditor::K_CHAR;
    case InputEvent::Key_Enter:     return InputEditor::K_ENTER;
    case InputEvent::Key_Backspace: return InputEditor::K_BACKSPACE;
    case InputEvent::Key_Delete:    return InputEditor::K_DELETE;
    case InputEvent::Key_Left:
        return ev.ctrl ? InputEditor::K_CTRL_LEFT : InputEditor::K_LEFT;
    case InputEvent::Key_Right:
        return ev.ctrl ? InputEditor::K_CTRL_RIGHT : InputEditor::K_RIGHT;
    case InputEvent::Key_Up:
        return ev.ctrl ? InputEditor::K_CTRL_UP : InputEditor::K_UP;
    case InputEvent::Key_Down:
        return ev.ctrl ? InputEditor::K_CTRL_DOWN : InputEditor::K_DOWN;
    case InputEvent::Key_Home:
        return ev.ctrl ? InputEditor::K_CTRL_HOME : InputEditor::K_HOME;
    case InputEvent::Key_End:
        return ev.ctrl ? InputEditor::K_CTRL_END : InputEditor::K_END;
    case InputEvent::Key_PageUp:    return InputEditor::K_PAGE_UP;
    case InputEvent::Key_PageDown:  return InputEditor::K_PAGE_DOWN;
    case InputEvent::Key_Tab:       return InputEditor::K_TAB;
    case InputEvent::Key_Escape:    return InputEditor::K_ESCAPE;
    case InputEvent::Key_F1:        return InputEditor::K_F1;
    case InputEvent::Key_F2:        return InputEditor::K_F2;
    case InputEvent::Key_F3:        return InputEditor::K_F3;
    case InputEvent::Key_F4:        return InputEditor::K_F4;
    case InputEvent::Key_F5:        return InputEditor::K_F5;
    case InputEvent::Key_F6:        return InputEditor::K_F6;
    case InputEvent::Key_F7:        return InputEditor::K_F7;
    case InputEvent::Key_F8:        return InputEditor::K_F8;
    case InputEvent::Key_F9:        return InputEditor::K_F9;
    case InputEvent::Key_F10:       return InputEditor::K_F10;
    case InputEvent::Key_F11:       return InputEditor::K_F11;
    case InputEvent::Key_F12:       return InputEditor::K_F12;
    default:                        return InputEditor::K_UNKNOWN;
    }
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
    editor_.set_cols(cols_);
    editor_.set_max_rows(1);  // console client: single-row input for now
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
    // Handle keys that the terminal manages directly.
    switch (ev.type) {
    case InputEvent::Key_PageUp:   scroll_page_up();  return false;
    case InputEvent::Key_PageDown: scroll_page_down(); return false;
    case InputEvent::Resize:       handle_resize();    return false;
    default: break;
    }

    int editor_key = event_to_editor_key(ev);
    uint32_t cp = (ev.type == InputEvent::Char && !ev.ctrl) ? ev.codepoint : 0;

    auto result = editor_.handle_key(editor_key, cp);
    switch (result) {
    case EditResult::SUBMIT:
        out_line = editor_.take_line();
        redraw_input();
        return true;
    case EditResult::RESIZE:
        // Console client currently uses single-row input, so just redraw.
        redraw_input();
        return false;
    case EditResult::REDRAW:
        redraw_input();
        return false;
    case EditResult::NONE:
        return false;
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
    screen.partial_line.clear();
    while (screen.lines.size() > MAX_SCROLLBACK) {
        screen.lines.pop_front();
    }
    if (context == output_key_ && screen.scroll_offset == 0) {
        redraw_output();
    }
}

void Terminal::set_partial_line(const std::string& context, const std::string& line) {
    auto& screen = output_screens_[context];
    screen.partial_line = line;
    if (context == output_key_ && screen.scroll_offset == 0) {
        redraw_output();
    }
}

void Terminal::clear_partial_line(const std::string& context) {
    auto it = output_screens_.find(context);
    if (it == output_screens_.end()) {
        return;
    }
    if (it->second.partial_line.empty()) {
        return;
    }
    it->second.partial_line.clear();
    if (context == output_key_ && it->second.scroll_offset == 0) {
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
    editor_.set_cols(cols_);
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

void Terminal::set_history_context(const std::string& key) {
    editor_.set_history_context(key);
}

void Terminal::history_up() {
    editor_.history_up();
    redraw_input();
}

void Terminal::history_down() {
    editor_.history_down();
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
    bool show_partial = screen.scroll_offset == 0 && !screen.partial_line.empty();
    int committed_rows = nlines - (show_partial ? 1 : 0);
    if (committed_rows < 0) committed_rows = 0;
    int total = (int)screen.lines.size();
    int start = total - committed_rows - screen.scroll_offset;
    if (start < 0) start = 0;

    for (int i = 0; i < committed_rows; i++) {
        clear_row(i);
        int idx = start + i;
        if (idx >= 0 && idx < total) {
            write_at(i, 0, render_line(screen.lines[idx]));
        }
    }
    for (int i = committed_rows; i < nlines; i++) {
        clear_row(i);
    }

    if (show_partial && committed_rows < nlines) {
        write_at(committed_rows, 0, render_line(screen.partial_line));
    }

    // Restore cursor to input line
    int cursor_col = editor_.cursor_vcol();
    COORD pos = { (SHORT)cursor_col, (SHORT)input_row() };
    SetConsoleCursorPosition(hOut_, pos);
}

void Terminal::redraw_status() {
    WORD attr = BACKGROUND_BLUE | FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE |
                FOREGROUND_INTENSITY;
    clear_row(status_row(), attr);
    if (!status_text_.empty()) {
        COORD pos = { 0, (SHORT)status_row() };
        SetConsoleCursorPosition(hOut_, pos);
        DWORD written;
        WriteConsoleA(hOut_, status_text_.c_str(),
                      (DWORD)std::min(status_text_.size(), (size_t)cols_),
                      &written, nullptr);
    }
    // Restore cursor
    int cursor_col = editor_.cursor_vcol();
    COORD pos = { (SHORT)cursor_col, (SHORT)input_row() };
    SetConsoleCursorPosition(hOut_, pos);
}

void Terminal::redraw_input() {
    clear_row(input_row());
    const auto& buf = editor_.text();
    if (!buf.empty()) {
        write_at(input_row(), 0, buf);
    }
    int cursor_col = editor_.cursor_vcol();
    COORD pos = { (SHORT)cursor_col, (SHORT)input_row() };
    SetConsoleCursorPosition(hOut_, pos);
}
