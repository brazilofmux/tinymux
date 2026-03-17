#ifndef TERMINAL_H
#define TERMINAL_H

#include <ncurses.h>
#include <string>
#include <deque>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <ctime>

#include "input.h"

class Terminal {
public:
    Terminal();
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    // Initialize ncurses (output only — input is via InputLexer + raw stdin)
    bool init();
    void shutdown();

    // Process an InputEvent from the Ragel lexer.
    // Returns true and fills out_line when the user submits a complete line.
    bool handle_key(const InputEvent& ev, std::string& out_line);

    // Output to the main output pane
    void print_line(const std::string& line);
    void print_line_to(const std::string& context, const std::string& line);
    void print_system(const std::string& msg);
    void set_prompt(const std::string& prompt);
    void clear_prompt();
    void set_input_text(const std::string& text);
    const std::string& input_text() const { return input_buf_; }
    size_t cursor_pos() const { return cursor_pos_; }
    std::string input_head() const;
    std::string input_tail() const;
    void set_cursor_pos(size_t pos);
    void delete_at_cursor(size_t count = 1);
    size_t word_left_pos(size_t pos) const;
    size_t word_right_pos(size_t pos) const;
    int match_bracket(int start = -1) const;
    void replace_last_output_line(const std::string& line);
    void clear_output();

    // Status bar
    void set_status(const std::string& text);
    const std::string& status_text() const { return status_text_; }
    void status_add_field(const std::string& field);
    bool status_edit_field(const std::string& field);
    bool status_remove_field(const std::string& name);
    std::string status_fields() const;

    // Refresh all windows (single doupdate per loop iteration)
    void refresh();

    // Handle SIGWINCH
    void handle_resize();

    // Scrollback
    void scroll_up(int lines = 1);
    void scroll_down(int lines = 1);
    void scroll_page_up();
    void scroll_page_down();
    void scroll_to_bottom();
    bool more_paused() const;
    int more_size() const;
    int more_scroll(int lines);

    // Input history
    void set_history_context(const std::string& key);
    void history_up();
    void history_down();

    int max_output_lines() const;
    int get_cols() const { return cols_; }
    int get_rows() const { return rows_; }
    void set_output_context(const std::string& key);
    const std::string& output_context() const { return output_key_; }

private:
    void create_windows();
    void destroy_windows();
    void redraw_output();
    void redraw_input();
    void redraw_status();
    void render_ansi_line(WINDOW* win, const std::string& line, int row);
    static int display_width_ansi(const std::string& line);
    int get_color_pair(int fg, int bg);
    int normalize_color(int color) const;
    static int rgb_to_xterm(int r, int g, int b);
    size_t normalize_cursor_pos(size_t pos) const;
    static std::string status_field_name(const std::string& field);
    std::string expand_status_field(const std::string& field) const;
    struct OutputScreen {
        std::deque<std::string> lines;
        int scroll_offset = 0;
    };
    OutputScreen& current_output();
    const OutputScreen& current_output() const;

    // UTF-8 / grapheme cluster navigation for input buffer
    static size_t utf8_char_len(unsigned char lead);
    static size_t utf8_prev_start(const std::string& s, size_t pos);
    size_t cluster_end(size_t pos) const;
    size_t cluster_start(size_t pos) const;
    int display_width_of(size_t from, size_t to) const;
    int cursor_display_col() const;

    // Insert a Unicode code point (as UTF-8) at cursor_pos_
    void insert_codepoint(uint32_t cp);
    // Delete the grapheme cluster before cursor (backspace)
    void delete_cluster_before();
    // Delete the grapheme cluster at cursor (delete key)
    void delete_cluster_at();

    WINDOW* win_output_ = nullptr;
    WINDOW* win_status_ = nullptr;
    WINDOW* win_input_  = nullptr;

    int rows_ = 0;
    int cols_ = 0;

    // Output scrollback
    std::unordered_map<std::string, OutputScreen> output_screens_;
    std::string output_key_;
    static constexpr size_t MAX_SCROLLBACK = 10000;

    // Input line editing
    std::string input_buf_;
    size_t cursor_pos_ = 0;

    // Input history
    std::unordered_map<std::string, std::deque<std::string>> input_histories_;
    std::string history_key_;
    static constexpr size_t MAX_HISTORY = 500;
    int history_pos_ = -1;
    std::string saved_input_;

    std::string status_text_;
    std::vector<std::string> status_fields_;
    std::string prompt_text_;
    std::unordered_map<uint32_t, int> color_pairs_;
    int next_pair_ = 1;
    bool initialized_ = false;
};

#endif // TERMINAL_H
