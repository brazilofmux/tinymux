#define _XOPEN_SOURCE_EXTENDED
#include "terminal.h"
#include <cstring>
#include <algorithm>
#include <locale.h>
#include <cmath>

extern "C" {
#include <color_ops.h>
}

Terminal::Terminal() {}

Terminal::~Terminal() {
    shutdown();
}

bool Terminal::init() {
    if (initialized_) return true;

    setlocale(LC_ALL, "");
    initscr();
    if (!has_colors()) {
        endwin();
        return false;
    }
    start_color();
    use_default_colors();
    raw();       // raw mode — we handle all input via read(STDIN_FILENO)
    noecho();
    nonl();
    leaveok(stdscr, FALSE);
    curs_set(1);

    color_pairs_.clear();
    next_pair_ = 1;

    getmaxyx(stdscr, rows_, cols_);
    create_windows();

    initialized_ = true;
    return true;
}

void Terminal::shutdown() {
    if (!initialized_) return;
    destroy_windows();
    endwin();
    initialized_ = false;
}

void Terminal::create_windows() {
    // Layout: output (top, rows-2 lines), status (1 line), input (1 line)
    int out_h = rows_ - 2;
    if (out_h < 1) out_h = 1;

    win_output_ = newwin(out_h, cols_, 0, 0);
    win_status_ = newwin(1, cols_, out_h, 0);
    win_input_  = newwin(1, cols_, out_h + 1, 0);

    scrollok(win_output_, FALSE);
    leaveok(win_output_, TRUE);   // don't position cursor here
    leaveok(win_status_, TRUE);   // don't position cursor here
    leaveok(win_input_, FALSE);   // cursor lives in input window
    // No keypad/nodelay — we don't use ncurses for input

    // Status bar appearance
    wbkgd(win_status_, A_REVERSE);

    redraw_output();
    redraw_status();
    redraw_input();
}

void Terminal::destroy_windows() {
    if (win_output_) { delwin(win_output_); win_output_ = nullptr; }
    if (win_status_) { delwin(win_status_); win_status_ = nullptr; }
    if (win_input_)  { delwin(win_input_);  win_input_  = nullptr; }
}

int Terminal::max_output_lines() const {
    return rows_ - 2;
}

void Terminal::handle_resize() {
    endwin();
    refresh();
    getmaxyx(stdscr, rows_, cols_);
    destroy_windows();
    create_windows();
}

int Terminal::normalize_color(int color) const {
    if (color < 0) return -1;
    if (COLORS <= 0) return -1;
    if (color < COLORS) return color;

    if (COLORS >= 16 && color < 16) return color;

    // Fallback for low-color terminals.
    static const int basic_map[16] = {
        COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW,
        COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE,
        COLOR_BLACK, COLOR_RED, COLOR_GREEN, COLOR_YELLOW,
        COLOR_BLUE, COLOR_MAGENTA, COLOR_CYAN, COLOR_WHITE
    };
    return basic_map[color % 16];
}

int Terminal::rgb_to_xterm(int r, int g, int b) {
    auto to_cube = [](int value) {
        if (value < 48) return 0;
        if (value < 115) return 1;
        return (value - 35) / 40;
    };

    int ir = to_cube(r);
    int ig = to_cube(g);
    int ib = to_cube(b);
    int cube_index = 16 + (36 * ir) + (6 * ig) + ib;

    static const int cube_steps[6] = {0, 95, 135, 175, 215, 255};
    int cr = cube_steps[ir];
    int cg = cube_steps[ig];
    int cb = cube_steps[ib];
    int cube_dist = (r - cr) * (r - cr) + (g - cg) * (g - cg) + (b - cb) * (b - cb);

    int gray_avg = (r + g + b) / 3;
    int gray_slot = (gray_avg > 238) ? 23 : std::max(0, (gray_avg - 3) / 10);
    int gray_level = 8 + gray_slot * 10;
    int gray_dist = (r - gray_level) * (r - gray_level) +
                    (g - gray_level) * (g - gray_level) +
                    (b - gray_level) * (b - gray_level);
    int gray_index = 232 + gray_slot;

    return (gray_dist < cube_dist) ? gray_index : cube_index;
}

// ---- UTF-8 / grapheme cluster helpers for input editing ----

// Length of UTF-8 sequence given lead byte.
size_t Terminal::utf8_char_len(unsigned char lead) {
    if (lead < 0x80) return 1;
    if (lead < 0xC0) return 1;  // continuation — shouldn't be lead, treat as 1
    if (lead < 0xE0) return 2;
    if (lead < 0xF0) return 3;
    return 4;
}

// Find the start byte of the code point preceding pos.
size_t Terminal::utf8_prev_start(const std::string& s, size_t pos) {
    if (pos == 0) return 0;
    size_t p = pos - 1;
    // Walk back over continuation bytes (10xxxxxx)
    while (p > 0 && (static_cast<unsigned char>(s[p]) & 0xC0) == 0x80)
        p--;
    return p;
}

// Advance past the grapheme cluster starting at pos.
// Uses co_cluster_advance for correct UAX #29 segmentation.
size_t Terminal::cluster_end(size_t pos) const {
    if (pos >= input_buf_.size()) return input_buf_.size();
    const auto* p  = reinterpret_cast<const unsigned char*>(input_buf_.data() + pos);
    const auto* pe = reinterpret_cast<const unsigned char*>(input_buf_.data() + input_buf_.size());
    size_t count = 0;
    const auto* after = co_cluster_advance(p, pe, 1, &count);
    return pos + (after - p);
}

// Find the start of the grapheme cluster ending at/before pos.
// Walk back one code point at a time, then verify with co_cluster_advance.
size_t Terminal::cluster_start(size_t pos) const {
    if (pos == 0) return 0;
    // Step back one code point as a first guess
    size_t candidate = utf8_prev_start(input_buf_, pos);
    // Verify: cluster_end(candidate) should reach pos.
    // If not, we stepped into the middle of a cluster — keep going back.
    while (candidate > 0 && cluster_end(candidate) < pos) {
        candidate = utf8_prev_start(input_buf_, candidate);
    }
    // Could also have combining marks before this — keep going back while
    // cluster_end(prev) still reaches the same position.
    while (candidate > 0) {
        size_t prev = utf8_prev_start(input_buf_, candidate);
        if (cluster_end(prev) >= pos) {
            candidate = prev;
        } else {
            break;
        }
    }
    return candidate;
}

// Display width of input_buf_[from..to) in columns.
int Terminal::display_width_of(size_t from, size_t to) const {
    if (from >= to) return 0;
    return (int)co_visual_width(
        reinterpret_cast<const unsigned char*>(input_buf_.data() + from),
        to - from);
}

// Display column where the cursor should be drawn.
int Terminal::cursor_display_col() const {
    return display_width_of(0, cursor_pos_);
}

size_t Terminal::normalize_cursor_pos(size_t pos) const {
    if (pos >= input_buf_.size()) return input_buf_.size();
    size_t cur = 0;
    while (cur < input_buf_.size()) {
        size_t next = cluster_end(cur);
        if (pos <= cur) return cur;
        if (pos < next) return cur;
        cur = next;
    }
    return input_buf_.size();
}

std::string Terminal::input_head() const {
    return input_buf_.substr(0, cursor_pos_);
}

std::string Terminal::input_tail() const {
    return input_buf_.substr(cursor_pos_);
}

void Terminal::set_cursor_pos(size_t pos) {
    cursor_pos_ = normalize_cursor_pos(pos);
    redraw_input();
}

void Terminal::delete_at_cursor(size_t count) {
    while (count-- > 0 && cursor_pos_ < input_buf_.size()) {
        size_t end = cluster_end(cursor_pos_);
        input_buf_.erase(cursor_pos_, end - cursor_pos_);
    }
    redraw_input();
}

size_t Terminal::word_left_pos(size_t pos) const {
    size_t cur = normalize_cursor_pos(pos);
    while (cur > 0) {
        size_t prev = cluster_start(cur);
        if (prev < cur && input_buf_[prev] == ' ') cur = prev;
        else break;
    }
    while (cur > 0) {
        size_t prev = cluster_start(cur);
        if (prev < cur && input_buf_[prev] != ' ') cur = prev;
        else break;
    }
    return cur;
}

size_t Terminal::word_right_pos(size_t pos) const {
    size_t cur = normalize_cursor_pos(pos);
    while (cur < input_buf_.size() && input_buf_[cur] != ' ')
        cur = cluster_end(cur);
    while (cur < input_buf_.size() && input_buf_[cur] == ' ')
        cur = cluster_end(cur);
    return cur;
}

int Terminal::match_bracket(int start) const {
    int pos = (start >= 0) ? std::max(0, start) : (int)cursor_pos_;
    if (pos > (int)input_buf_.size()) pos = (int)input_buf_.size();

    auto match_for = [](char c) -> char {
        switch (c) {
            case '(': return ')';
            case '[': return ']';
            case '{': return '}';
            case ')': return '(';
            case ']': return '[';
            case '}': return '{';
            default: return '\0';
        }
    };

    int idx = pos;
    if (idx == (int)input_buf_.size() && idx > 0) idx--;
    if (idx < 0 || idx >= (int)input_buf_.size()) return -1;

    char c = input_buf_[idx];
    char target = match_for(c);
    if (!target && idx > 0) {
        idx--;
        c = input_buf_[idx];
        target = match_for(c);
    }
    if (!target) return -1;

    int dir = (c == '(' || c == '[' || c == '{') ? 1 : -1;
    int depth = 0;
    for (int i = idx; i >= 0 && i < (int)input_buf_.size(); i += dir) {
        char cur = input_buf_[i];
        if (cur == c) depth++;
        else if (cur == target) {
            depth--;
            if (depth == 0) return i;
        }
    }
    return -1;
}

int Terminal::get_color_pair(int fg, int bg) {
    fg = normalize_color(fg);
    bg = normalize_color(bg);

    if (fg < 0 && bg < 0) return 0;

    uint32_t key = (static_cast<uint32_t>(fg + 2) << 16) |
                   static_cast<uint32_t>(bg + 2);
    auto it = color_pairs_.find(key);
    if (it != color_pairs_.end()) return it->second;

    if (next_pair_ >= COLOR_PAIRS) return 0;

#if NCURSES_EXT_COLORS
    if (init_extended_pair(next_pair_, fg, bg) == ERR) return 0;
#else
    if (init_pair(static_cast<short>(next_pair_),
                  static_cast<short>(fg),
                  static_cast<short>(bg)) == ERR) {
        return 0;
    }
#endif

    color_pairs_[key] = next_pair_;
    return next_pair_++;
}

// ---- Input editing driven by InputEvent from Ragel lexer ----

void Terminal::insert_codepoint(uint32_t cp) {
    // Encode code point as UTF-8 and insert at cursor
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
    input_buf_.insert(cursor_pos_, mb, len);
    cursor_pos_ += len;
    redraw_input();
}

void Terminal::delete_cluster_before() {
    if (cursor_pos_ > 0) {
        size_t prev = cluster_start(cursor_pos_);
        input_buf_.erase(prev, cursor_pos_ - prev);
        cursor_pos_ = prev;
        redraw_input();
    }
}

void Terminal::delete_cluster_at() {
    if (cursor_pos_ < input_buf_.size()) {
        size_t end = cluster_end(cursor_pos_);
        input_buf_.erase(cursor_pos_, end - cursor_pos_);
        redraw_input();
    }
}

bool Terminal::handle_key(const InputEvent& ev, std::string& out_line) {
    switch (ev.key) {
        case Key::CHAR:
            insert_codepoint(ev.cp);
            return false;

        case Key::ENTER:
            out_line = input_buf_;
            if (!input_buf_.empty()) {
                input_history_.push_front(input_buf_);
                if (input_history_.size() > MAX_HISTORY)
                    input_history_.pop_back();
            }
            input_buf_.clear();
            cursor_pos_ = 0;
            history_pos_ = -1;
            redraw_input();
            return true;

        case Key::BACKSPACE:    delete_cluster_before();   return false;
        case Key::DELETE_KEY:   delete_cluster_at();       return false;

        case Key::LEFT:
            if (cursor_pos_ > 0) { cursor_pos_ = cluster_start(cursor_pos_); redraw_input(); }
            return false;
        case Key::RIGHT:
            if (cursor_pos_ < input_buf_.size()) { cursor_pos_ = cluster_end(cursor_pos_); redraw_input(); }
            return false;

        case Key::CTRL_LEFT: {
            // Word left: skip whitespace, then skip non-whitespace
            while (cursor_pos_ > 0 && cursor_pos_ <= input_buf_.size()) {
                size_t prev = cluster_start(cursor_pos_);
                if (prev < cursor_pos_ && input_buf_[prev] == ' ') { cursor_pos_ = prev; } else break;
            }
            while (cursor_pos_ > 0) {
                size_t prev = cluster_start(cursor_pos_);
                if (prev < cursor_pos_ && input_buf_[prev] != ' ') { cursor_pos_ = prev; } else break;
            }
            redraw_input();
            return false;
        }
        case Key::CTRL_RIGHT: {
            // Word right: skip non-whitespace, then skip whitespace
            while (cursor_pos_ < input_buf_.size() && input_buf_[cursor_pos_] != ' ')
                cursor_pos_ = cluster_end(cursor_pos_);
            while (cursor_pos_ < input_buf_.size() && input_buf_[cursor_pos_] == ' ')
                cursor_pos_ = cluster_end(cursor_pos_);
            redraw_input();
            return false;
        }

        case Key::HOME:
        case Key::CTRL_A:
            cursor_pos_ = 0; redraw_input(); return false;

        case Key::END:
        case Key::CTRL_E:
            cursor_pos_ = input_buf_.size(); redraw_input(); return false;

        case Key::UP:       history_up();       return false;
        case Key::DOWN:     history_down();     return false;
        case Key::PAGE_UP:  scroll_page_up();   return false;
        case Key::PAGE_DOWN: scroll_page_down(); return false;

        case Key::CTRL_U:   // Kill whole line
            input_buf_.clear(); cursor_pos_ = 0; redraw_input(); return false;

        case Key::CTRL_K:   // Kill to end of line
            input_buf_.erase(cursor_pos_); redraw_input(); return false;

        case Key::CTRL_W: { // Kill word backward
            size_t end = cursor_pos_;
            while (cursor_pos_ > 0 && input_buf_[cursor_pos_ - 1] == ' ')
                cursor_pos_ = cluster_start(cursor_pos_);
            while (cursor_pos_ > 0 && input_buf_[cursor_pos_ - 1] != ' ')
                cursor_pos_ = cluster_start(cursor_pos_);
            input_buf_.erase(cursor_pos_, end - cursor_pos_);
            redraw_input();
            return false;
        }

        case Key::CTRL_L:   // Redraw screen
            clearok(curscr, TRUE);
            handle_resize();
            return false;

        case Key::TAB:       // Could be tab completion later
            return false;

        case Key::CTRL_D:    // EOF / quit if empty line
            if (input_buf_.empty()) {
                out_line = "/quit";
                return true;
            }
            delete_cluster_at();
            return false;

        case Key::CTRL_B:   // Back one char (emacs)
            if (cursor_pos_ > 0) { cursor_pos_ = cluster_start(cursor_pos_); redraw_input(); }
            return false;
        case Key::CTRL_F:   // Forward one char (emacs)
            if (cursor_pos_ < input_buf_.size()) { cursor_pos_ = cluster_end(cursor_pos_); redraw_input(); }
            return false;

        case Key::CTRL_P:   history_up();   return false;  // emacs prev
        case Key::CTRL_N:   history_down(); return false;  // emacs next

        default:
            return false;
    }
}

// Wrap a single logical line into display-width-limited visual lines.
// Splits at column boundaries, not byte boundaries. ANSI escapes are
// carried across to continuation lines so color doesn't reset at wrap.
static std::vector<std::string> wrap_line(const std::string& line, int cols) {
    std::vector<std::string> result;
    if (cols <= 0) { result.push_back(line); return result; }

    std::string cur;
    int col = 0;
    // Track current SGR state so we can re-emit it on continuation lines
    std::string sgr_state;

    size_t i = 0;
    while (i < line.size()) {
        // ANSI escape — copy to current segment, track state
        if (line[i] == '\033' && i + 1 < line.size() && line[i + 1] == '[') {
            size_t esc_start = i;
            i += 2;
            while (i < line.size() && line[i] != 'm' && !(line[i] >= 0x40 && line[i] <= 0x7E))
                i++;
            if (i < line.size()) i++; // skip final byte
            std::string esc = line.substr(esc_start, i - esc_start);
            cur += esc;
            if (esc.back() == 'm') sgr_state = esc; // remember last SGR
            continue;
        }

        unsigned char c = static_cast<unsigned char>(line[i]);
        size_t clen = 1;
        if (c >= 0xC0 && c < 0xE0) clen = 2;
        else if (c >= 0xE0 && c < 0xF0) clen = 3;
        else if (c >= 0xF0 && c < 0xF8) clen = 4;
        if (i + clen > line.size()) break;

        int w = co_console_width(reinterpret_cast<const unsigned char*>(&line[i]));
        if (w < 0) w = 1;

        // Would this character exceed the line?
        if (w > 0 && col + w > cols) {
            result.push_back(cur);
            cur.clear();
            col = 0;
            // Re-emit SGR state on new visual line
            if (!sgr_state.empty()) cur += sgr_state;
        }

        cur.append(line, i, clen);
        col += w;
        i += clen;
    }
    result.push_back(cur);
    return result;
}

int Terminal::display_width_ansi(const std::string& line) {
    int col = 0;
    size_t i = 0;
    while (i < line.size()) {
        if (line[i] == '\033' && i + 1 < line.size() && line[i + 1] == '[') {
            i += 2;
            while (i < line.size() && line[i] != 'm' && !(line[i] >= 0x40 && line[i] <= 0x7E))
                i++;
            if (i < line.size()) i++;
            continue;
        }

        unsigned char c = static_cast<unsigned char>(line[i]);
        size_t clen = utf8_char_len(c);
        if (i + clen > line.size()) break;

        int w = co_console_width(reinterpret_cast<const unsigned char*>(&line[i]));
        if (w < 0) w = 1;
        col += w;
        i += clen;
    }
    return col;
}

void Terminal::print_line(const std::string& line) {
    // Wrap long lines into multiple visual lines
    auto wrapped = wrap_line(line, cols_);
    for (auto& wl : wrapped) {
        output_lines_.push_back(std::move(wl));
        if (output_lines_.size() > MAX_SCROLLBACK)
            output_lines_.pop_front();
    }

    // If at bottom, stay at bottom
    if (scroll_offset_ == 0) {
        redraw_output();
    } else {
        scroll_offset_ += (int)wrapped.size();
    }
}

void Terminal::print_system(const std::string& msg) {
    print_line("% " + msg);
}

void Terminal::replace_last_output_line(const std::string& line) {
    if (!output_lines_.empty()) output_lines_.pop_back();
    auto wrapped = wrap_line(line, cols_);
    for (auto& wl : wrapped) {
        output_lines_.push_back(std::move(wl));
        if (output_lines_.size() > MAX_SCROLLBACK)
            output_lines_.pop_front();
    }
    redraw_output();
}

void Terminal::clear_output() {
    output_lines_.clear();
    scroll_offset_ = 0;
    redraw_output();
}

void Terminal::set_prompt(const std::string& prompt) {
    prompt_text_ = prompt;
    redraw_input();
}

void Terminal::clear_prompt() {
    if (prompt_text_.empty()) return;
    prompt_text_.clear();
    redraw_input();
}

void Terminal::set_input_text(const std::string& text) {
    input_buf_ = text;
    cursor_pos_ = input_buf_.size();
    history_pos_ = -1;
    redraw_input();
}

void Terminal::set_status(const std::string& text) {
    status_text_ = text;
    redraw_status();
}

std::string Terminal::status_field_name(const std::string& field) {
    size_t end = field.find(':');
    return field.substr(0, end);
}

void Terminal::status_add_field(const std::string& field) {
    if (field.empty()) return;
    status_fields_.push_back(field);
}

bool Terminal::status_edit_field(const std::string& field) {
    std::string name = status_field_name(field);
    if (name.empty()) return false;
    for (auto& existing : status_fields_) {
        if (status_field_name(existing) == name) {
            existing = field;
            return true;
        }
    }
    return false;
}

bool Terminal::status_remove_field(const std::string& name) {
    auto it = std::find_if(status_fields_.begin(), status_fields_.end(),
        [&](const std::string& field) { return status_field_name(field) == name; });
    if (it == status_fields_.end()) return false;
    status_fields_.erase(it);
    return true;
}

std::string Terminal::status_fields() const {
    std::string out;
    for (size_t i = 0; i < status_fields_.size(); i++) {
        if (i) out += ' ';
        out += status_fields_[i];
    }
    return out;
}

void Terminal::refresh() {
    if (!initialized_) return;
    wnoutrefresh(win_output_);
    wnoutrefresh(win_status_);
    wnoutrefresh(win_input_);
    doupdate();
}

void Terminal::scroll_up(int lines) {
    int max_off = (int)output_lines_.size() - max_output_lines();
    if (max_off < 0) max_off = 0;
    scroll_offset_ = std::min(scroll_offset_ + lines, max_off);
    redraw_output();
}

void Terminal::scroll_down(int lines) {
    scroll_offset_ = std::max(scroll_offset_ - lines, 0);
    redraw_output();
}

void Terminal::scroll_page_up() {
    scroll_up(max_output_lines() - 1);
}

void Terminal::scroll_page_down() {
    scroll_down(max_output_lines() - 1);
}

void Terminal::scroll_to_bottom() {
    scroll_offset_ = 0;
    redraw_output();
}

void Terminal::history_up() {
    if (input_history_.empty()) return;
    if (history_pos_ == -1) {
        saved_input_ = input_buf_;
        history_pos_ = 0;
    } else if (history_pos_ < (int)input_history_.size() - 1) {
        history_pos_++;
    } else {
        return;
    }
    input_buf_ = input_history_[history_pos_];
    cursor_pos_ = input_buf_.size();
    redraw_input();
}

void Terminal::history_down() {
    if (history_pos_ < 0) return;
    history_pos_--;
    if (history_pos_ < 0) {
        input_buf_ = saved_input_;
    } else {
        input_buf_ = input_history_[history_pos_];
    }
    cursor_pos_ = input_buf_.size();
    redraw_input();
}

// ANSI SGR parser — renders ANSI color escape sequences using ncurses attributes
void Terminal::render_ansi_line(WINDOW* win, const std::string& line, int row) {
    wmove(win, row, 0);
    wclrtoeol(win);

    int fg = -1, bg = -1;
    bool bold = false, underline = false, reverse_vid = false;

    auto apply_attrs = [&]() {
        attr_t a = A_NORMAL;
        if (bold) a |= A_BOLD;
        if (underline) a |= A_UNDERLINE;
        if (reverse_vid) a |= A_REVERSE;
        int pair = get_color_pair(fg, bg);
        wattr_set(win, a, static_cast<NCURSES_PAIRS_T>(pair), nullptr);
    };

    size_t i = 0;
    int col = 0;
    while (i < line.size() && col < cols_) {
        if (line[i] == '\033' && i + 1 < line.size() && line[i + 1] == '[') {
            // Parse CSI sequence
            i += 2;
            std::vector<int> params;
            int cur_param = 0;
            bool has_param = false;
            while (i < line.size()) {
                char c = line[i];
                if (c >= '0' && c <= '9') {
                    cur_param = cur_param * 10 + (c - '0');
                    has_param = true;
                    i++;
                } else if (c == ';') {
                    params.push_back(has_param ? cur_param : 0);
                    cur_param = 0;
                    has_param = false;
                    i++;
                } else {
                    params.push_back(has_param ? cur_param : 0);
                    if (c == 'm') {
                        i++;
                        // Process SGR params
                        for (size_t p = 0; p < params.size(); p++) {
                            int v = params[p];
                            if (v == 0) { fg = -1; bg = -1; bold = false; underline = false; reverse_vid = false; }
                            else if (v == 1) bold = true;
                            else if (v == 4) underline = true;
                            else if (v == 7) reverse_vid = true;
                            else if (v == 22) bold = false;
                            else if (v == 24) underline = false;
                            else if (v == 27) reverse_vid = false;
                            else if (v >= 30 && v <= 37) fg = v - 30;
                            else if (v >= 40 && v <= 47) bg = v - 40;
                            else if (v >= 90 && v <= 97) fg = v - 90 + 8;
                            else if (v >= 100 && v <= 107) bg = v - 100 + 8;
                            else if (v == 39) fg = -1;
                            else if (v == 49) bg = -1;
                            else if (v == 38 && p + 1 < params.size()) {
                                if (params[p + 1] == 5 && p + 2 < params.size()) {
                                    fg = params[p + 2]; p += 2;
                                } else if (params[p + 1] == 2 && p + 4 < params.size()) {
                                    fg = rgb_to_xterm(params[p + 2], params[p + 3], params[p + 4]);
                                    p += 4;
                                }
                            }
                            else if (v == 48 && p + 1 < params.size()) {
                                if (params[p + 1] == 5 && p + 2 < params.size()) {
                                    bg = params[p + 2]; p += 2;
                                } else if (params[p + 1] == 2 && p + 4 < params.size()) {
                                    bg = rgb_to_xterm(params[p + 2], params[p + 3], params[p + 4]);
                                    p += 4;
                                }
                            }
                        }
                        apply_attrs();
                    } else {
                        i++; // skip unknown final byte
                    }
                    break;
                }
            }
            continue;
        }

        // Regular character — handle UTF-8
        unsigned char c = (unsigned char)line[i];
        int char_len = (int)utf8_char_len(c);

        if (i + char_len <= line.size()) {
            // Get display width
            int w = co_console_width((const unsigned char*)&line[i]);
            if (w < 0) w = 1;
            // w == 0 for combining marks — emit them (ncurses attaches to prev char)
            if (w > 0 && col + w > cols_) break;

            waddnstr(win, &line[i], char_len);
            col += w;
        }
        i += char_len;
    }

    // Reset attributes at end of line
    wattr_set(win, A_NORMAL, 0, nullptr);
}

void Terminal::redraw_output() {
    if (!win_output_) return;
    int out_h = max_output_lines();

    werase(win_output_);

    int total = (int)output_lines_.size();
    int start = total - out_h - scroll_offset_;
    if (start < 0) start = 0;
    int end = start + out_h;
    if (end > total) end = total;

    for (int i = start; i < end; i++) {
        render_ansi_line(win_output_, output_lines_[i], i - start);
    }
}

void Terminal::redraw_input() {
    if (!win_input_) return;
    werase(win_input_);
    render_ansi_line(win_input_, prompt_text_ + input_buf_, 0);

    // Position cursor at correct display column
    int cur_col = display_width_ansi(prompt_text_) + cursor_display_col();
    if (cur_col >= cols_) cur_col = cols_ - 1;
    wmove(win_input_, 0, cur_col);
}

void Terminal::redraw_status() {
    if (!win_status_) return;
    werase(win_status_);
    std::string text = status_text_;
    std::string fields = status_fields();
    if (!fields.empty()) {
        if (!text.empty()) text += "  ";
        text += fields;
    }
    mvwaddnstr(win_status_, 0, 0, text.c_str(), cols_ - 1);
}
