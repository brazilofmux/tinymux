// terminal.h -- Win32 Console terminal: output, input line, status bar.
#ifndef TERMINAL_H
#define TERMINAL_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <string>
#include <deque>
#include <vector>
#include <unordered_map>
#include <cstdint>

// libmux color/Unicode API
#include "color_ops.h"
#include "input_editor.h"

struct InputEvent {
    enum Type {
        None,
        Char,           // Unicode character input
        Key_Up, Key_Down, Key_Left, Key_Right,
        Key_Home, Key_End,
        Key_PageUp, Key_PageDown,
        Key_Backspace, Key_Delete,
        Key_Enter,
        Key_Tab,
        Key_Escape,
        Key_F1, Key_F2, Key_F3, Key_F4,
        Key_F5, Key_F6, Key_F7, Key_F8,
        Key_F9, Key_F10, Key_F11, Key_F12,
        Resize,
    };
    Type     type = None;
    uint32_t codepoint = 0;   // For Char type
    bool     ctrl = false;
    bool     alt = false;
    bool     shift = false;
};

class Terminal {
public:
    Terminal();
    ~Terminal();

    Terminal(const Terminal&) = delete;
    Terminal& operator=(const Terminal&) = delete;

    bool init();
    void shutdown();

    // Translate a Win32 INPUT_RECORD to our InputEvent.
    InputEvent translate(const INPUT_RECORD& rec);

    // Process an InputEvent. Returns true and fills out_line on Enter.
    bool handle_key(const InputEvent& ev, std::string& out_line);

    // Output
    void print_line(const std::string& line);
    void print_line_to(const std::string& context, const std::string& line);
    void set_partial_line(const std::string& context, const std::string& line);
    void clear_partial_line(const std::string& context);
    void print_system(const std::string& msg);

    // Status bar
    void set_status(const std::string& text);

    // Refresh display
    void refresh();

    // Handle console resize
    void handle_resize();

    // Scrollback
    void scroll_up(int lines = 1);
    void scroll_down(int lines = 1);
    void scroll_page_up();
    void scroll_page_down();
    void scroll_to_bottom();

    // Input history (per-world)
    void set_history_context(const std::string& key);
    void history_up();
    void history_down();

    // Scrollback search
    std::vector<std::string> recall(const std::string& pattern, int max_lines = 20) const;

    int get_cols() const { return cols_; }
    int get_rows() const { return rows_; }

    void set_output_context(const std::string& key);
    const std::string& output_context() const { return output_key_; }

private:
    void update_size();
    void redraw_output();
    void redraw_input();
    void redraw_status();
    void write_at(int row, int col, const std::string& text, WORD attr = 0);
    void clear_row(int row, WORD attr = 0);

    // Render a PUA-colored line to ANSI for console output.
    std::string render_line(const std::string& line);

    // Map console InputEvent to InputEditor key code.
    static int event_to_editor_key(const InputEvent& ev);

    HANDLE hOut_ = INVALID_HANDLE_VALUE;
    HANDLE hIn_  = INVALID_HANDLE_VALUE;
    DWORD  orig_out_mode_ = 0;
    DWORD  orig_in_mode_ = 0;
    bool   vt_enabled_ = false;   // ENABLE_VIRTUAL_TERMINAL_PROCESSING

    int rows_ = 0;
    int cols_ = 0;

    // Layout: rows 0..(rows_-3) = output, row (rows_-2) = status, row (rows_-1) = input
    int output_rows() const { return rows_ - 2; }
    int status_row() const { return rows_ - 2; }
    int input_row() const { return rows_ - 1; }

    // Output scrollback per context
    struct OutputScreen {
        std::deque<std::string> lines;
        std::string partial_line;
        int scroll_offset = 0;
    };
    std::unordered_map<std::string, OutputScreen> output_screens_;
    std::string output_key_;
    OutputScreen& current_output();
    static constexpr size_t MAX_SCROLLBACK = 10000;

    // Shared input editor (editing, reflow, history).
    InputEditor editor_;

    std::string status_text_;
    bool initialized_ = false;
};

#endif // TERMINAL_H
