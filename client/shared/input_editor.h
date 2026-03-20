// input_editor.h -- Platform-independent multi-line input editor.
//
// Shared between the tf (ncurses) and console (Win32) clients.
// All editing logic, reflow, cursor movement, and history live here.
// Terminal backends are thin rendering layers that call into this class.
//
#ifndef INPUT_EDITOR_H
#define INPUT_EDITOR_H

#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <cstdint>
#include <cstddef>

// A single visual line within the wrapped input buffer.
struct VisualLine {
    size_t byte_start;    // byte offset into buffer
    size_t byte_end;      // exclusive
    int    display_width; // columns consumed on screen
};

// Result of handling a key event.
enum class EditResult {
    NONE,     // nothing happened
    REDRAW,   // buffer changed, redraw input
    RESIZE,   // visual row count changed, resize + redraw
    SUBMIT,   // user pressed Enter, line ready
};

class InputEditor {
public:
    InputEditor();

    // --- Mutation ---

    // Handle a key press.  For the tf client, `key` is the Key enum
    // cast to int, `cp` is the Unicode code point for CHAR keys.
    // For the console client, map InputEvent fields to the same
    // abstract key codes.
    //
    // Key code constants (matching tf Key enum values):
    enum KeyCode {
        K_CHAR = 0,
        K_UP, K_DOWN, K_LEFT, K_RIGHT,
        K_HOME, K_END,
        K_PAGE_UP, K_PAGE_DOWN,
        K_CTRL_LEFT, K_CTRL_RIGHT,
        K_ENTER,
        K_BACKSPACE,
        K_DELETE,
        K_TAB,
        K_ESCAPE,
        K_INSERT,
        K_CTRL_A, K_CTRL_B, K_CTRL_C, K_CTRL_D, K_CTRL_E, K_CTRL_F,
        K_CTRL_G,
        K_CTRL_K, K_CTRL_L, K_CTRL_N, K_CTRL_O,
        K_CTRL_P, K_CTRL_Q, K_CTRL_R, K_CTRL_S, K_CTRL_T, K_CTRL_U,
        K_CTRL_V, K_CTRL_W, K_CTRL_X, K_CTRL_Y, K_CTRL_Z,
        K_F1, K_F2, K_F3, K_F4, K_F5, K_F6,
        K_F7, K_F8, K_F9, K_F10, K_F11, K_F12,
        K_CTRL_HOME, K_CTRL_END,
        K_CTRL_UP, K_CTRL_DOWN,
        K_UNKNOWN,
    };

    EditResult handle_key(int key, uint32_t cp);

    // After SUBMIT, retrieve and clear the submitted line.
    std::string take_line();

    // --- Query (for rendering) ---

    const std::string& text() const { return buf_; }
    size_t cursor() const { return cursor_; }
    int desired_rows() const;
    const std::vector<VisualLine>& visual_lines() const { return vlines_; }
    int cursor_vrow() const { return cursor_vrow_; }
    int cursor_vcol() const { return cursor_vcol_; }

    // --- Configuration ---

    void set_cols(int cols);
    void set_prompt_width(int w);
    void set_max_rows(int max);

    int cols() const { return cols_; }
    int prompt_width() const { return prompt_width_; }
    int max_rows() const { return max_rows_; }

    // --- History ---

    void set_history_context(const std::string& key);
    void history_up();
    void history_down();

    // Direct access for serialization/restoration.
    const std::unordered_map<std::string, std::deque<std::string>>& histories() const {
        return histories_;
    }
    void set_history(const std::string& key, std::deque<std::string> hist) {
        histories_[key] = std::move(hist);
    }

    // --- External control ---

    void set_text(const std::string& text);
    void set_cursor(size_t byte_pos);

    // --- Grapheme / display helpers (public for terminal backends) ---

    // Display width of buf_[from..to) in columns.
    int display_width_of(size_t from, size_t to) const;

    // Advance past one grapheme cluster starting at pos.
    size_t cluster_end(size_t pos) const;

    // Find start of grapheme cluster ending at/before pos.
    size_t cluster_start(size_t pos) const;

    // Word boundary positions.
    size_t word_left_pos(size_t pos) const;
    size_t word_right_pos(size_t pos) const;

    // Normalize pos to nearest grapheme boundary.
    size_t normalize_cursor_pos(size_t pos) const;

    // Substring helpers.
    std::string head() const { return buf_.substr(0, cursor_); }
    std::string tail() const { return buf_.substr(cursor_); }

    static constexpr size_t MAX_HISTORY = 500;

private:
    // --- Buffer state ---
    std::string buf_;
    size_t cursor_ = 0;    // byte offset, always on grapheme boundary

    // --- Layout ---
    int cols_ = 80;
    int prompt_width_ = 0;
    int max_rows_ = 3;
    int goal_col_ = -1;    // preserved across vertical moves

    // --- Visual line state (recomputed by reflow) ---
    std::vector<VisualLine> vlines_;
    int cursor_vrow_ = 0;
    int cursor_vcol_ = 0;

    // --- History ---
    std::unordered_map<std::string, std::deque<std::string>> histories_;
    std::string history_key_;
    int history_pos_ = -1;
    std::string saved_input_;

    // --- Internal operations ---

    void reflow();
    size_t byte_at_column(int vline_idx, int target_col) const;

    // UTF-8 helpers
    static size_t utf8_char_len(unsigned char lead);
    static size_t utf8_prev_start(const std::string& s, size_t pos);

    // Editing primitives (all call reflow internally)
    void insert_codepoint(uint32_t cp);
    void delete_cluster_before();
    void delete_cluster_at();

    // Set goal_col from current cursor position.
    void update_goal_col() { goal_col_ = cursor_vcol_; }
};

#endif // INPUT_EDITOR_H
