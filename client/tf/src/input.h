#ifndef INPUT_H
#define INPUT_H

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>

// Key identifiers for input events.
enum class Key : uint16_t {
    CHAR = 0,       // Unicode character — code point in InputEvent::cp

    // Navigation
    UP, DOWN, LEFT, RIGHT,
    HOME, END,
    PAGE_UP, PAGE_DOWN,
    CTRL_LEFT, CTRL_RIGHT,   // word-level movement

    // Editing
    ENTER,
    BACKSPACE,
    DELETE_KEY,
    TAB,
    ESCAPE,
    INSERT,

    // Ctrl-letter (those not mapped above)
    CTRL_A, CTRL_B, CTRL_C, CTRL_D, CTRL_E, CTRL_F,
    CTRL_G,         // Ctrl-H = BACKSPACE, Ctrl-I = TAB
    CTRL_K, CTRL_L, CTRL_N, CTRL_O,
    CTRL_P, CTRL_Q, CTRL_R, CTRL_S, CTRL_T, CTRL_U,
    CTRL_V, CTRL_W, CTRL_X, CTRL_Y, CTRL_Z,

    // Function keys
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,

    // Ctrl+navigation (Home/End/Up/Down with Ctrl modifier)
    CTRL_HOME, CTRL_END,
    CTRL_UP, CTRL_DOWN,

    UNKNOWN,
};

struct InputEvent {
    Key      key;
    uint32_t cp;    // Unicode code point (valid when key == CHAR)
};

// InputLexer: Ragel -G2 state machine that converts raw terminal bytes
// into InputEvent sequences.  Feed it bytes from read(STDIN_FILENO),
// then drain the event queue.
class InputLexer {
public:
    InputLexer();

    // Feed raw bytes from stdin.  Events are appended to the internal queue.
    void feed(const unsigned char* data, size_t len);

    // Call when select() times out with a pending bare ESC.
    void flush_pending_esc();

    // True if a bare ESC byte is buffered awaiting disambiguation.
    bool has_pending_esc() const { return pending_esc_; }

    // Drain accumulated events.
    std::vector<InputEvent>& events() { return events_; }
    void clear_events() { events_.clear(); }

private:
    void emit(Key k);
    void emit_char(uint32_t cp);
    void dispatch_csi(const unsigned char* start, const unsigned char* final_p);
    void dispatch_ss3(unsigned char ch);

    // Ragel state (unused between calls — scanner reinits per feed())
    int cs_ = 0;

    // Event queue
    std::vector<InputEvent> events_;

    // ESC disambiguation
    bool pending_esc_ = false;
};

#endif // INPUT_H
