#define _XOPEN_SOURCE_EXTENDED
#include "terminal.h"
#include "app.h"
#include "script.h"
#include <cstring>
#include <algorithm>
#include <locale.h>
#include <cmath>
#include <cstdio>

extern "C" {
#include <color_ops.h>
}

// Map tf Key enum to InputEditor::KeyCode.
static int key_to_editor(Key k) {
    switch (k) {
    case Key::CHAR:       return InputEditor::K_CHAR;
    case Key::UP:         return InputEditor::K_UP;
    case Key::DOWN:       return InputEditor::K_DOWN;
    case Key::LEFT:       return InputEditor::K_LEFT;
    case Key::RIGHT:      return InputEditor::K_RIGHT;
    case Key::HOME:       return InputEditor::K_HOME;
    case Key::END:        return InputEditor::K_END;
    case Key::PAGE_UP:    return InputEditor::K_PAGE_UP;
    case Key::PAGE_DOWN:  return InputEditor::K_PAGE_DOWN;
    case Key::CTRL_LEFT:  return InputEditor::K_CTRL_LEFT;
    case Key::CTRL_RIGHT: return InputEditor::K_CTRL_RIGHT;
    case Key::ENTER:      return InputEditor::K_ENTER;
    case Key::BACKSPACE:  return InputEditor::K_BACKSPACE;
    case Key::DELETE_KEY: return InputEditor::K_DELETE;
    case Key::TAB:        return InputEditor::K_TAB;
    case Key::ESCAPE:     return InputEditor::K_ESCAPE;
    case Key::INSERT:     return InputEditor::K_INSERT;
    case Key::CTRL_A:     return InputEditor::K_CTRL_A;
    case Key::CTRL_B:     return InputEditor::K_CTRL_B;
    case Key::CTRL_C:     return InputEditor::K_CTRL_C;
    case Key::CTRL_D:     return InputEditor::K_CTRL_D;
    case Key::CTRL_E:     return InputEditor::K_CTRL_E;
    case Key::CTRL_F:     return InputEditor::K_CTRL_F;
    case Key::CTRL_G:     return InputEditor::K_CTRL_G;
    case Key::CTRL_K:     return InputEditor::K_CTRL_K;
    case Key::CTRL_L:     return InputEditor::K_CTRL_L;
    case Key::CTRL_N:     return InputEditor::K_CTRL_N;
    case Key::CTRL_O:     return InputEditor::K_CTRL_O;
    case Key::CTRL_P:     return InputEditor::K_CTRL_P;
    case Key::CTRL_Q:     return InputEditor::K_CTRL_Q;
    case Key::CTRL_R:     return InputEditor::K_CTRL_R;
    case Key::CTRL_S:     return InputEditor::K_CTRL_S;
    case Key::CTRL_T:     return InputEditor::K_CTRL_T;
    case Key::CTRL_U:     return InputEditor::K_CTRL_U;
    case Key::CTRL_V:     return InputEditor::K_CTRL_V;
    case Key::CTRL_W:     return InputEditor::K_CTRL_W;
    case Key::CTRL_X:     return InputEditor::K_CTRL_X;
    case Key::CTRL_Y:     return InputEditor::K_CTRL_Y;
    case Key::CTRL_Z:     return InputEditor::K_CTRL_Z;
    case Key::F1:         return InputEditor::K_F1;
    case Key::F2:         return InputEditor::K_F2;
    case Key::F3:         return InputEditor::K_F3;
    case Key::F4:         return InputEditor::K_F4;
    case Key::F5:         return InputEditor::K_F5;
    case Key::F6:         return InputEditor::K_F6;
    case Key::F7:         return InputEditor::K_F7;
    case Key::F8:         return InputEditor::K_F8;
    case Key::F9:         return InputEditor::K_F9;
    case Key::F10:        return InputEditor::K_F10;
    case Key::F11:        return InputEditor::K_F11;
    case Key::F12:        return InputEditor::K_F12;
    case Key::CTRL_HOME:  return InputEditor::K_CTRL_HOME;
    case Key::CTRL_END:   return InputEditor::K_CTRL_END;
    case Key::CTRL_UP:    return InputEditor::K_CTRL_UP;
    case Key::CTRL_DOWN:  return InputEditor::K_CTRL_DOWN;
    case Key::UNKNOWN:    return InputEditor::K_UNKNOWN;
    }
    return InputEditor::K_UNKNOWN;
}

Terminal::Terminal() {}

void Terminal::set_app(struct App* app) {
    app_ = app;
    if (app) vars_ = &app->vars;
}

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
    rgb_colors_.clear();
    next_color_ = 256;

    // Detect TrueColor support: if the terminal advertises enough
    // colors and can_change_color(), we can define custom RGB colors.
    //
    truecolor_ = (COLORS > 256 && can_change_color());
    if (!truecolor_) {
        // Also check COLORTERM environment variable — many modern
        // terminals set this to "truecolor" or "24bit".
        //
        const char* ct = getenv("COLORTERM");
        if (ct && (strcmp(ct, "truecolor") == 0 || strcmp(ct, "24bit") == 0)) {
            if (can_change_color()) {
                truecolor_ = true;
            }
        }
    }

    getmaxyx(stdscr, rows_, cols_);
    editor_.set_cols(cols_);
    editor_.set_max_rows(MAX_INPUT_ROWS);
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
    // Layout: output, status rows, input (1..MAX_INPUT_ROWS).
    int status_h = status_rows();
    if (status_h < 1) status_h = 1;
    int input_h = input_rows_;
    int out_h = rows_ - (status_h + input_h);
    if (out_h < 1) out_h = 1;

    win_output_ = newwin(out_h, cols_, 0, 0);
    win_status_.clear();
    for (int row = 0; row < status_h; ++row) {
        win_status_.push_back(newwin(1, cols_, out_h + row, 0));
    }
    win_input_  = newwin(input_h, cols_, out_h + status_h, 0);

    scrollok(win_output_, FALSE);
    leaveok(win_output_, TRUE);   // don't position cursor here
    for (auto* w : win_status_) leaveok(w, TRUE);   // don't position cursor here
    leaveok(win_input_, FALSE);   // cursor lives in input window
    // No keypad/nodelay — we don't use ncurses for input

    // Status bar appearance
    for (auto* w : win_status_) wbkgd(w, A_REVERSE);

    redraw_output();
    redraw_status();
    redraw_input();
}

void Terminal::destroy_windows() {
    if (win_output_) { delwin(win_output_); win_output_ = nullptr; }
    for (auto* w : win_status_) if (w) delwin(w);
    win_status_.clear();
    if (win_input_)  { delwin(win_input_);  win_input_  = nullptr; }
}

int Terminal::max_output_lines() const {
    return rows_ - (status_rows() + input_rows_);
}

void Terminal::handle_resize() {
    endwin();
    refresh();
    getmaxyx(stdscr, rows_, cols_);
    editor_.set_cols(cols_);
    input_rows_ = editor_.desired_rows();
    destroy_windows();
    create_windows();
}

// (calc_input_rows removed — now use editor_.desired_rows())

void Terminal::resize_input_area(int new_rows) {
    if (new_rows == input_rows_) return;
    if (new_rows < 1) new_rows = 1;
    if (new_rows > MAX_INPUT_ROWS) new_rows = MAX_INPUT_ROWS;

    int status_h = status_rows();
    if (status_h < 1) status_h = 1;
    int new_out_h = rows_ - (status_h + new_rows);
    if (new_out_h < 1) new_out_h = 1;

    input_rows_ = new_rows;

    // Resize output window (stays at row 0).
    wresize(win_output_, new_out_h, cols_);

    // Reposition status windows.
    for (int i = 0; i < (int)win_status_.size(); ++i) {
        mvwin(win_status_[i], new_out_h + i, 0);
    }

    // Resize and reposition input window.
    wresize(win_input_, new_rows, cols_);
    mvwin(win_input_, new_out_h + status_h, 0);

    redraw_output();
    redraw_status();
    // Caller handles redraw_input().
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

// Allocate an ncurses color slot with exact RGB values.  Returns a
// color index suitable for get_color_pair().  Falls back to the
// nearest xterm-256 index if TrueColor is not available.
//
static int nearest_xterm256(int r, int g, int b) {
    unsigned char rgb[3] = {
        static_cast<unsigned char>(r),
        static_cast<unsigned char>(g),
        static_cast<unsigned char>(b)
    };
    return co_nearest_xterm256(rgb);
}

int Terminal::alloc_rgb_color(int r, int g, int b) {
    if (!truecolor_) {
        return nearest_xterm256(r, g, b);
    }

    // Pack RGB into a 24-bit key for caching.
    //
    uint32_t key = (static_cast<uint32_t>(r & 0xFF) << 16) |
                   (static_cast<uint32_t>(g & 0xFF) << 8) |
                   static_cast<uint32_t>(b & 0xFF);
    auto it = rgb_colors_.find(key);
    if (it != rgb_colors_.end()) return it->second;

    if (next_color_ >= COLORS) {
        return nearest_xterm256(r, g, b);
    }

    // ncurses uses 0-1000 range for RGB components.
    //
    int nr = r * 1000 / 255;
    int ng = g * 1000 / 255;
    int nb = b * 1000 / 255;

    if (init_extended_color(next_color_, nr, ng, nb) == ERR) {
        return nearest_xterm256(r, g, b);
    }

    int idx = next_color_++;
    rgb_colors_[key] = idx;
    return idx;
}

// ---- UTF-8 helper (still needed for ANSI rendering) ----

size_t Terminal::utf8_char_len(unsigned char lead) {
    if (lead < 0x80) return 1;
    if (lead < 0xC0) return 1;
    if (lead < 0xE0) return 2;
    if (lead < 0xF0) return 3;
    return 4;
}

// ---- Editor delegation for public API ----

std::string Terminal::input_head() const {
    return editor_.head();
}

std::string Terminal::input_tail() const {
    return editor_.tail();
}

void Terminal::set_cursor_pos(size_t pos) {
    editor_.set_cursor(pos);
    redraw_input();
}

void Terminal::delete_at_cursor(size_t count) {
    // Delete count grapheme clusters at cursor via the editor.
    while (count-- > 0 && editor_.cursor() < editor_.text().size()) {
        editor_.handle_key(InputEditor::K_DELETE, 0);
    }
    redraw_input();
}

size_t Terminal::word_left_pos(size_t pos) const {
    return editor_.word_left_pos(pos);
}

size_t Terminal::word_right_pos(size_t pos) const {
    return editor_.word_right_pos(pos);
}

int Terminal::match_bracket(int start) const {
    const auto& buf = editor_.text();
    int pos = (start >= 0) ? std::max(0, start) : (int)editor_.cursor();
    if (pos > (int)buf.size()) pos = (int)buf.size();

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
    if (idx == (int)buf.size() && idx > 0) idx--;
    if (idx < 0 || idx >= (int)buf.size()) return -1;

    char c = buf[idx];
    char target = match_for(c);
    if (!target && idx > 0) {
        idx--;
        c = buf[idx];
        target = match_for(c);
    }
    if (!target) return -1;

    int dir = (c == '(' || c == '[' || c == '{') ? 1 : -1;
    int depth = 0;
    for (int i = idx; i >= 0 && i < (int)buf.size(); i += dir) {
        char cur = buf[i];
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
// Delegates to InputEditor for all editing, reflow, and history.

bool Terminal::handle_key(const InputEvent& ev, std::string& out_line) {
    int editor_key = key_to_editor(ev.key);

    // Handle keys that the terminal manages directly (not the editor).
    switch (ev.key) {
        case Key::PAGE_UP:  scroll_page_up();  return false;
        case Key::PAGE_DOWN: scroll_page_down(); return false;
        case Key::CTRL_L:
            clearok(curscr, TRUE);
            handle_resize();
            return false;
        case Key::CTRL_D:
            if (editor_.text().empty()) {
                out_line = "/quit";
                return true;
            }
            break;  // fall through to editor
        default:
            break;
    }

    auto result = editor_.handle_key(editor_key, ev.cp);
    switch (result) {
        case EditResult::SUBMIT:
            out_line = editor_.take_line();
            redraw_input();
            return true;
        case EditResult::RESIZE:
            resize_input_area(editor_.desired_rows());
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

Terminal::OutputScreen& Terminal::current_output() {
    return output_screens_[output_key_];
}

const Terminal::OutputScreen& Terminal::current_output() const {
    auto it = output_screens_.find(output_key_);
    static const OutputScreen empty{};
    if (it == output_screens_.end()) return empty;
    return it->second;
}

void Terminal::print_line(const std::string& line) {
    print_line_to(output_key_, line);
}

void Terminal::print_line_to(const std::string& context, const std::string& line) {
    // Wrap long lines into multiple visual lines
    auto& screen = output_screens_[context];
    auto wrapped = wrap_line(line, cols_);
    for (auto& wl : wrapped) {
        screen.lines.push_back(std::move(wl));
        if (screen.lines.size() > MAX_SCROLLBACK)
            screen.lines.pop_front();
    }
    screen.last_logical_line_count = (int)wrapped.size();

    // If at bottom, stay at bottom
    if (context == output_key_) {
        if (screen.scroll_offset == 0) {
            redraw_output();
        } else {
            screen.scroll_offset += (int)wrapped.size();
        }
    } else {
        if (screen.scroll_offset > 0) screen.scroll_offset += (int)wrapped.size();
    }
}

void Terminal::print_system(const std::string& msg) {
    print_line("% " + msg);
}

void Terminal::replace_last_output_line(const std::string& line) {
    auto& screen = current_output();
    // Pop all visual lines that the last logical line produced.
    int to_pop = std::max(screen.last_logical_line_count, 1);
    while (to_pop > 0 && !screen.lines.empty()) {
        screen.lines.pop_back();
        --to_pop;
    }
    auto wrapped = wrap_line(line, cols_);
    for (auto& wl : wrapped) {
        screen.lines.push_back(std::move(wl));
        if (screen.lines.size() > MAX_SCROLLBACK)
            screen.lines.pop_front();
    }
    screen.last_logical_line_count = (int)wrapped.size();
    redraw_output();
}

void Terminal::clear_output() {
    auto& screen = current_output();
    screen.lines.clear();
    screen.scroll_offset = 0;
    redraw_output();
}

void Terminal::set_output_context(const std::string& key) {
    output_key_ = key;
    redraw_output();
}

void Terminal::set_prompt(const std::string& prompt) {
    prompt_text_ = prompt;
    editor_.set_prompt_width(display_width_ansi(prompt_text_));
    redraw_input();
}

void Terminal::clear_prompt() {
    if (prompt_text_.empty()) return;
    prompt_text_.clear();
    editor_.set_prompt_width(0);
    redraw_input();
}

void Terminal::set_input_text(const std::string& text) {
    editor_.set_text(text);
    redraw_input();
}

void Terminal::set_history_context(const std::string& key) {
    editor_.set_history_context(key);
}

void Terminal::set_status(const std::string& text) {
    status_text_ = text;
    redraw_status();
}

std::string Terminal::status_field_name(const std::string& field) {
    size_t end = field.find(':');
    return field.substr(0, end);
}

int Terminal::status_field_width(const std::string& field, bool* explicit_width) {
    size_t first = field.find(':');
    if (explicit_width) *explicit_width = false;
    if (first == std::string::npos) return 0;

    size_t second = field.find(':', first + 1);
    std::string width_part = field.substr(first + 1,
        second == std::string::npos ? std::string::npos : second - first - 1);
    if (width_part.empty()) return 0;
    if (explicit_width) *explicit_width = true;
    return std::atoi(width_part.c_str());
}

std::string Terminal::status_field_attrs(const std::string& field) {
    size_t first = field.find(':');
    if (first == std::string::npos) return {};
    size_t second = field.find(':', first + 1);
    if (second == std::string::npos || second + 1 >= field.size()) return {};
    return field.substr(second + 1);
}

// Look up a variable by name, returning empty string if not found.
//
std::string Terminal::lookup_var(const std::string& name) const {
    if (!vars_) return {};
    auto it = vars_->find(name);
    return (it != vars_->end()) ? it->second : std::string{};
}

std::string Terminal::expand_status_field(const std::string& field) const {
    size_t first = field.find(':');
    std::string name = (first == std::string::npos) ? field : field.substr(0, first);
    bool explicit_width = false;
    int width = status_field_width(field, &explicit_width);

    std::string text;
    if (name.empty()) {
        int abs_w = width > 0 ? width : (width < 0 ? -width : 1);
        text.assign((size_t)abs_w, ' ');
        return text;
    }

    // Internal fields (prefixed with @).
    //
    bool is_internal = (!name.empty() && name[0] == '@');
    std::string fmt_var;

    if (is_internal) {
        std::string iname = name.substr(1);

        if (iname == "world") {
            text = output_key_.empty() ? "no connection" : output_key_;
        } else if (iname == "more") {
            if (more_paused()) text = "--More--";
        } else if (iname == "clock") {
            std::time_t now = std::time(nullptr);
            struct tm* tp = std::localtime(&now);
            if (tp) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), "%02d:%02d", tp->tm_hour, tp->tm_min);
                text = buf;
            }
        } else if (iname == "active") {
            if (status_active_count > 0) {
                text = std::to_string(status_active_count) + " Active";
            }
        } else if (iname == "log") {
            if (status_logging) text = "Log";
        } else if (iname == "read") {
            if (status_read_depth > 0) {
                text = std::to_string(status_read_depth);
            }
        } else if (iname == "mail") {
            if (status_mail_count > 0) {
                text = std::to_string(status_mail_count);
            }
        }

        // Check for format variable: status_int_<iname>
        //
        fmt_var = lookup_var("status_int_" + iname);
    } else {
        // Variable field — display the variable's live value.
        // If no variable exists with this name, treat as literal text
        // (classic TF also supports quoted literals, but bare names
        // that don't match a variable render as-is).
        //
        if (vars_) {
            auto it = vars_->find(name);
            if (it != vars_->end()) {
                text = it->second;
            } else {
                text = name;
            }
        } else {
            text = name;
        }

        // Check for format variable: status_var_<name>
        //
        fmt_var = lookup_var("status_var_" + name);
    }

    // If a format variable is set, evaluate it as a TF expression
    // (expanding %{var} and $[expr] substitutions) and use the result.
    //
    if (!fmt_var.empty() && app_) {
        ScriptEnv env(app_->vars, app_);
        text = expand_subs(fmt_var, env);
    }

    // Apply width: positive = left-justify, negative = right-justify.
    //
    if (explicit_width && width != 0) {
        int abs_w = width > 0 ? width : -width;
        if ((int)text.size() > abs_w) {
            text.resize((size_t)abs_w);
        } else if ((int)text.size() < abs_w) {
            size_t pad = (size_t)(abs_w - (int)text.size());
            if (width < 0) {
                // Right-justify: pad on the left.
                text.insert(0, pad, ' ');
            } else {
                // Left-justify: pad on the right.
                text.append(pad, ' ');
            }
        }
    }
    return text;
}

// Map a color name to an SGR color index (0-7), or -1 if not recognized.
//
static int color_name_to_index(const std::string& name) {
    if (name == "black")   return 0;
    if (name == "red")     return 1;
    if (name == "green")   return 2;
    if (name == "yellow")  return 3;
    if (name == "blue")    return 4;
    if (name == "magenta") return 5;
    if (name == "cyan")    return 6;
    if (name == "white")   return 7;
    return -1;
}

// Classic TF single-char attribute parser.
//
// Format: concatenated single-char codes with C<colorname> for colors.
//   B=bold, u=underline, r=reverse, f=flash, d=dim, h=hilite(bold),
//   b=bell(ignored), g=gag(ignored), n=none(reset).
//   Cred=fg red, Cbgblue=bg blue, Cbgbrightcyan=bright bg cyan.
//
static std::string attrs_to_sgr_classic(const std::string& attrs) {
    std::vector<std::string> codes;
    size_t i = 0;

    while (i < attrs.size()) {
        char ch = attrs[i++];
        switch (ch) {
        case ',': break;  // skip
        case 'n': codes.clear(); break;  // reset
        case 'B': case 'h': codes.push_back("1"); break;  // bold/hilite
        case 'd': codes.push_back("2"); break;  // dim
        case 'u': codes.push_back("4"); break;  // underline
        case 'f': codes.push_back("5"); break;  // flash/blink
        case 'r': codes.push_back("7"); break;  // reverse
        case 'b': break;  // bell — no visual attribute
        case 'g': break;  // gag — no visual attribute
        case 'x': break;  // exclusive — no visual attribute
        case 'C': {
            // Read color name: all lowercase letters are part of the name.
            // Stop at comma, uppercase code letter, or end of string.
            size_t start = i;
            while (i < attrs.size() && attrs[i] != ',' &&
                   !(attrs[i] >= 'A' && attrs[i] <= 'Z')) {
                i++;
            }
            std::string cname = attrs.substr(start, i - start);
            // Lowercase for matching.
            std::string lower;
            for (char c : cname)
                lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            bool is_bg = (lower.substr(0, 2) == "bg");
            if (is_bg) lower = lower.substr(2);

            bool bright = false;
            if (lower.substr(0, 6) == "bright") {
                bright = true;
                lower = lower.substr(6);
            }

            int idx = color_name_to_index(lower);
            if (idx >= 0) {
                if (is_bg) {
                    codes.push_back(std::to_string(bright ? 100 + idx : 40 + idx));
                } else {
                    codes.push_back(std::to_string(bright ? 90 + idx : 30 + idx));
                }
            } else if (lower == "gray" || lower == "grey") {
                codes.push_back(is_bg ? "100" : "90");
            }
            break;
        }
        default: break;  // unknown — skip
        }
    }

    if (codes.empty()) return {};
    std::string sgr = "\033[";
    for (size_t j = 0; j < codes.size(); ++j) {
        if (j > 0) sgr += ';';
        sgr += codes[j];
    }
    sgr += 'm';
    return sgr;
}

// Detect whether an attribute string uses classic TF single-char format
// or the word-token format.  If it contains any word delimiters
// (, + | space), use word mode.  Otherwise, classic TF mode.
//
static bool is_word_format(const std::string& attrs) {
    for (char ch : attrs) {
        if (ch == ',' || ch == '+' || ch == '|' || ch == ' ') return true;
    }
    return false;
}

// Parse an attribute string into ANSI SGR codes.
//
// Word-token format (separated by , + | or space):
//   Text:   bold/B, dim, underline/u, blink/flash, reverse/rev/r
//   FG:     black, red, green, yellow, blue, magenta, cyan, white
//   Bright: bright_red, brightred, etc.  (or gray for bright black)
//   BG:     bg_black, bgblack, bg_red, bgred, etc.
//   Bright BG: bgbright_red, bgbrightred, etc.
//   xterm-256: color0..color255 (fg), bg_color0..bg_color255 (bg)
//
// Classic TF format (no delimiters):
//   B=bold, u=underline, r=reverse, f=flash, d=dim, h=hilite
//   Cred=fg red, Cbgblue=bg blue, etc.
//
static std::string attrs_to_sgr(const std::string& attrs) {
    if (attrs.empty()) return {};

    // Classic TF single-char mode if no word delimiters present.
    if (!is_word_format(attrs)) return attrs_to_sgr_classic(attrs);

    std::vector<std::string> codes;

    std::string token;
    auto flush = [&]() {
        if (token.empty()) return;

        // Text attributes.
        //
        if (token == "bold" || token == "b") { codes.push_back("1"); }
        else if (token == "dim")             { codes.push_back("2"); }
        else if (token == "underline" || token == "u") { codes.push_back("4"); }
        else if (token == "blink" || token == "flash") { codes.push_back("5"); }
        else if (token == "reverse" || token == "rev" || token == "r") { codes.push_back("7"); }

        // Background colors: bg_<color>, bg<color>, bgbright_<color>, bgbright<color>.
        //
        else if (token.substr(0, 2) == "bg") {
            std::string rest = token.substr(2);
            if (!rest.empty() && rest[0] == '_') rest = rest.substr(1);

            // bgbright<color>
            bool bright = false;
            if (rest.substr(0, 6) == "bright") {
                bright = true;
                rest = rest.substr(6);
                if (!rest.empty() && rest[0] == '_') rest = rest.substr(1);
            }

            // bg_color<N> (xterm-256)
            if (rest.size() > 5 && rest.substr(0, 5) == "color") {
                int n = std::atoi(rest.substr(5).c_str());
                if (n >= 0 && n <= 255) {
                    codes.push_back("48;5;" + std::to_string(n));
                }
            } else if (rest == "gray" || rest == "grey") {
                codes.push_back("100");
            } else {
                int idx = color_name_to_index(rest);
                if (idx >= 0) {
                    codes.push_back(std::to_string(bright ? 100 + idx : 40 + idx));
                }
            }
        }

        // gray/grey = bright black foreground.
        //
        else if (token == "gray" || token == "grey") {
            codes.push_back("90");
        }

        // Bright foreground: bright_<color>, bright<color>.
        //
        else if (token.substr(0, 6) == "bright") {
            std::string rest = token.substr(6);
            if (!rest.empty() && rest[0] == '_') rest = rest.substr(1);
            int idx = color_name_to_index(rest);
            if (idx >= 0) {
                codes.push_back(std::to_string(90 + idx));
            }
        }

        // color<N> (xterm-256 foreground).
        //
        else if (token.size() > 5 && token.substr(0, 5) == "color") {
            int n = std::atoi(token.substr(5).c_str());
            if (n >= 0 && n <= 255) {
                codes.push_back("38;5;" + std::to_string(n));
            }
        }

        // Plain foreground color name.
        //
        else {
            int idx = color_name_to_index(token);
            if (idx >= 0) {
                codes.push_back(std::to_string(30 + idx));
            }
        }

        token.clear();
    };

    for (char ch : attrs) {
        if (ch == ',' || ch == '+' || ch == '|' || ch == ' ') {
            if (!token.empty()) flush();
        } else {
            token += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
    }
    if (!token.empty()) flush();

    if (codes.empty()) return {};

    std::string sgr = "\033[";
    for (size_t i = 0; i < codes.size(); ++i) {
        if (i > 0) sgr += ';';
        sgr += codes[i];
    }
    sgr += 'm';
    return sgr;
}

std::string Terminal::style_status_field(const std::string& field, const std::string& text) {
    if (text.empty()) return text;

    // Merge field-spec attrs with dynamic attribute variable.
    //
    std::string attrs = status_field_attrs(field);

    // Look up the dynamic attribute variable.
    //
    std::string name = status_field_name(field);
    std::string dyn_attrs;
    if (vars_ && !name.empty()) {
        if (!name.empty() && name[0] == '@') {
            auto it = vars_->find("status_attr_int_" + name.substr(1));
            if (it != vars_->end()) dyn_attrs = it->second;
        } else {
            auto it = vars_->find("status_attr_var_" + name);
            if (it != vars_->end()) dyn_attrs = it->second;
        }
    }

    // Combine: field-spec attrs first, then dynamic attrs.
    //
    std::string combined = attrs;
    if (!dyn_attrs.empty()) {
        if (!combined.empty()) combined += ',';
        combined += dyn_attrs;
    }

    std::string sgr = attrs_to_sgr(combined);
    if (sgr.empty()) return text;
    return sgr + text + "\033[0m";
}

void Terminal::status_add_field(const std::string& field) {
    if (field.empty()) return;
    if (status_fields_by_row_.empty()) status_fields_by_row_.resize(1);
    status_fields_by_row_[0].push_back(field);
    redraw_status();
}

bool Terminal::status_insert_fields(const std::vector<std::string>& fields,
                                    const std::string& before_name,
                                    const std::string& after_name,
                                    int spacer,
                                    int row,
                                    bool reset,
                                    bool nodup) {
    if (row < 0 || row >= MAX_STATUS_ROWS) return false;
    if ((int)status_fields_by_row_.size() <= row) status_fields_by_row_.resize(row + 1);
    auto& row_fields = status_fields_by_row_[row];
    std::vector<std::string> additions;
    additions.reserve(fields.size() + (spacer != 0 ? 1 : 0));

    int variable_widths = 0;
    auto count_variable = [&](const std::vector<std::string>& source) {
        for (const auto& field : source) {
            bool explicit_width = false;
            int width = status_field_width(field, &explicit_width);
            if (explicit_width && width == 0 && !status_field_name(field).empty()) {
                variable_widths++;
            }
        }
    };

    if (!reset) count_variable(row_fields);
    for (const auto& field : fields) {
        if (field.empty()) continue;
        if (nodup) {
            std::string name = status_field_name(field);
            auto duplicate = std::find_if(row_fields.begin(), row_fields.end(),
                [&](const std::string& existing) { return status_field_name(existing) == name; });
            if (duplicate != row_fields.end()) continue;
        }
        additions.push_back(field);
    }

    if (spacer != 0 && !additions.empty()) {
        std::string spacer_field = ":" + std::to_string(std::abs(spacer));
        if (spacer < 0) additions.insert(additions.begin(), spacer_field);
        else additions.push_back(spacer_field);
    }

    count_variable(additions);
    if (variable_widths > 1) return false;

    int old_rows = status_rows();
    if (reset) row_fields.clear();

    if (!before_name.empty() || !after_name.empty()) {
        auto it = before_name.empty()
            ? std::find_if(row_fields.begin(), row_fields.end(),
                  [&](const std::string& existing) { return status_field_name(existing) == after_name; })
            : std::find_if(row_fields.begin(), row_fields.end(),
                  [&](const std::string& existing) { return status_field_name(existing) == before_name; });
        if (it == row_fields.end()) return false;
        if (before_name.empty()) ++it;
        row_fields.insert(it, additions.begin(), additions.end());
    } else {
        row_fields.insert(row_fields.end(), additions.begin(), additions.end());
    }

    if (initialized_ && status_rows() != old_rows) {
        destroy_windows();
        create_windows();
        return true;
    }

    redraw_status();
    return true;
}

bool Terminal::status_edit_field(const std::string& field, int row) {
    std::string name = status_field_name(field);
    if (name.empty()) return false;
    int start = row < 0 ? 0 : row;
    int end = row < 0 ? (int)status_fields_by_row_.size() : row + 1;
    for (int r = start; r < end; ++r) {
        if (r < 0 || r >= (int)status_fields_by_row_.size()) continue;
        for (auto& existing : status_fields_by_row_[r]) {
            if (status_field_name(existing) == name) {
                existing = field;
                redraw_status();
                return true;
            }
        }
    }
    return false;
}

bool Terminal::status_remove_field(const std::string& name, int row) {
    int old_rows = status_rows();
    int start = row < 0 ? 0 : row;
    int end = row < 0 ? (int)status_fields_by_row_.size() : row + 1;
    for (int r = start; r < end; ++r) {
        if (r < 0 || r >= (int)status_fields_by_row_.size()) continue;
        auto& row_fields = status_fields_by_row_[r];
        auto it = std::find_if(row_fields.begin(), row_fields.end(),
            [&](const std::string& field) { return status_field_name(field) == name; });
        if (it == row_fields.end()) continue;
        row_fields.erase(it);
        if (initialized_ && status_rows() != old_rows) {
            destroy_windows();
            create_windows();
        } else {
            redraw_status();
        }
        return true;
    }
    return false;
}

std::string Terminal::status_fields() const {
    std::string out;
    for (size_t row = 0; row < status_fields_by_row_.size(); ++row) {
        if (status_fields_by_row_[row].empty()) continue;
        if (!out.empty()) out += " | ";
        for (size_t i = 0; i < status_fields_by_row_[row].size(); i++) {
            if (i) out += ' ';
            out += status_fields_by_row_[row][i];
        }
    }
    return out;
}

int Terminal::status_rows() const {
    int rows = 1;
    for (size_t i = 0; i < status_fields_by_row_.size(); ++i) {
        if (!status_fields_by_row_[i].empty()) rows = std::max(rows, (int)i + 1);
    }
    return rows;
}

void Terminal::refresh() {
    if (!initialized_) return;
    wnoutrefresh(win_output_);
    for (auto* w : win_status_) wnoutrefresh(w);
    wnoutrefresh(win_input_);
    doupdate();
}

void Terminal::update_status() {
    redraw_status();
    refresh();
}

void Terminal::scroll_up(int lines) {
    auto& screen = current_output();
    int max_off = (int)screen.lines.size() - max_output_lines();
    if (max_off < 0) max_off = 0;
    screen.scroll_offset = std::min(screen.scroll_offset + lines, max_off);
    redraw_output();
}

void Terminal::scroll_down(int lines) {
    auto& screen = current_output();
    screen.scroll_offset = std::max(screen.scroll_offset - lines, 0);
    redraw_output();
}

void Terminal::scroll_page_up() {
    scroll_up(max_output_lines() - 1);
}

void Terminal::scroll_page_down() {
    scroll_down(max_output_lines() - 1);
}

void Terminal::scroll_to_bottom() {
    current_output().scroll_offset = 0;
    redraw_output();
}

bool Terminal::more_paused() const {
    return current_output().scroll_offset > 0;
}

int Terminal::more_size() const {
    return current_output().scroll_offset;
}

int Terminal::more_scroll(int lines) {
    if (lines > 0) {
        scroll_up(lines);
    } else if (lines < 0) {
        scroll_down(-lines);
    }
    return more_size();
}

void Terminal::history_up() {
    editor_.history_up();
    redraw_input();
}

void Terminal::history_down() {
    editor_.history_down();
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
                                    fg = alloc_rgb_color(params[p + 2], params[p + 3], params[p + 4]);
                                    p += 4;
                                }
                            }
                            else if (v == 48 && p + 1 < params.size()) {
                                if (params[p + 1] == 5 && p + 2 < params.size()) {
                                    bg = params[p + 2]; p += 2;
                                } else if (params[p + 1] == 2 && p + 4 < params.size()) {
                                    bg = alloc_rgb_color(params[p + 2], params[p + 3], params[p + 4]);
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
    const auto& screen = current_output();

    werase(win_output_);

    int total = (int)screen.lines.size();
    int start = total - out_h - screen.scroll_offset;
    if (start < 0) start = 0;
    int end = start + out_h;
    if (end > total) end = total;

    for (int i = start; i < end; i++) {
        render_ansi_line(win_output_, screen.lines[i], i - start);
    }
}

void Terminal::redraw_input() {
    if (!win_input_) return;

    // Grow or shrink the input area as needed.
    int needed = editor_.desired_rows();
    if (needed != input_rows_) {
        resize_input_area(needed);
    }

    werase(win_input_);

    // Build visual lines from the editor's reflow data.
    const auto& vlines = editor_.visual_lines();
    const auto& buf = editor_.text();
    int total_lines = (int)vlines.size();

    // Assemble display strings for each visual line (prompt on first).
    std::vector<std::string> display_lines;
    display_lines.reserve(total_lines);
    for (int i = 0; i < total_lines; ++i) {
        std::string line;
        if (i == 0) line = prompt_text_;
        line.append(buf, vlines[i].byte_start,
                    vlines[i].byte_end - vlines[i].byte_start);
        display_lines.push_back(std::move(line));
    }

    int cursor_line = editor_.cursor_vrow();
    int cursor_col  = editor_.cursor_vcol();

    // Choose the view window so the cursor line is always visible.
    int view_start = 0;
    if (cursor_line >= input_rows_) {
        view_start = cursor_line - input_rows_ + 1;
    }
    if (view_start + input_rows_ > total_lines) {
        view_start = total_lines - input_rows_;
    }
    if (view_start < 0) view_start = 0;

    // Render visible lines.
    for (int r = 0; r < input_rows_ && (view_start + r) < total_lines; ++r) {
        render_ansi_line(win_input_, display_lines[view_start + r], r);
    }

    // Position the cursor.
    int cur_row = cursor_line - view_start;
    if (cur_row < 0) cur_row = 0;
    if (cur_row >= input_rows_) cur_row = input_rows_ - 1;
    if (cursor_col >= cols_) cursor_col = cols_ - 1;
    if (cursor_col < 0) cursor_col = 0;
    wmove(win_input_, cur_row, cursor_col);
}

void Terminal::redraw_status() {
    if (win_status_.empty()) return;
    for (size_t row = 0; row < win_status_.size(); ++row) {
        WINDOW* w = win_status_[row];
        werase(w);

        std::string text = (row == 0) ? status_text_ : "";
        const std::vector<std::string> empty;
        const auto& fields = row < status_fields_by_row_.size() ? status_fields_by_row_[row] : empty;

        if (!fields.empty()) {
            if (!text.empty()) text += "  ";
            int flex_index = -1;
            int fixed_width = 0;
            for (size_t i = 0; i < fields.size(); ++i) {
                bool explicit_width = false;
                int width = status_field_width(fields[i], &explicit_width);
                if (explicit_width && width == 0 && !status_field_name(fields[i]).empty()) {
                    if (flex_index < 0) flex_index = (int)i;
                    continue;
                }
                fixed_width += display_width_ansi(style_status_field(fields[i], expand_status_field(fields[i])));
            }

            int prefix_width = display_width_ansi(text);
            int remaining = cols_ - 1 - prefix_width - fixed_width;
            if (remaining < 0) remaining = 0;

            for (size_t i = 0; i < fields.size(); ++i) {
                if ((int)i == flex_index) {
                    std::string field = fields[i];
                    size_t first = field.find(':');
                    size_t second = field.find(':', first == std::string::npos ? 0 : first + 1);
                    std::string rebuilt = (first == std::string::npos)
                        ? field + ":" + std::to_string(remaining)
                        : field.substr(0, first + 1) + std::to_string(remaining)
                            + (second == std::string::npos ? "" : field.substr(second));
                    text += style_status_field(rebuilt, expand_status_field(rebuilt));
                } else {
                    text += style_status_field(fields[i], expand_status_field(fields[i]));
                }
            }
        }
        mvwaddnstr(w, 0, 0, text.c_str(), cols_ - 1);
    }
}
