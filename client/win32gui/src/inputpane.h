// inputpane.h -- Multi-line input editor (1-3 rows).
#ifndef INPUTPANE_H
#define INPUTPANE_H

#include "window.h"
#include <string>
#include <deque>

class CInputPane : public CWindow {
public:
    static const wchar_t* ClassName();
    static bool Register(HINSTANCE hInst);
    bool Create(HWND hParent, HINSTANCE hInst, int x, int y, int cx, int cy);

    // Returns true and fills out_line when the user presses Enter.
    bool OnKeyDown(UINT vk, bool ctrl, bool shift, bool alt, std::string& out_line);
    void OnChar(wchar_t ch);

    void SetFont(HFONT font);
    int DesiredHeight() const;  // Rows * char_height

    // History
    void SetHistoryContext(const std::string& key);

    const std::string& text() const { return buf_; }
    void SetText(const std::string& text);

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnPaint();
    void Redraw();

    // Editing helpers
    void InsertChar(uint32_t cp);
    void DeleteBackward();
    void DeleteForward();
    void MoveCursorLeft();
    void MoveCursorRight();
    void HistoryUp();
    void HistoryDown();

    std::string buf_;
    size_t cursor_pos_ = 0;
    HFONT font_ = nullptr;
    int char_width_ = 8;
    int char_height_ = 16;

    // History
    std::string history_key_;
    std::deque<std::string> history_;
    int history_pos_ = -1;
    std::string saved_input_;
    static constexpr size_t MAX_HISTORY = 500;

    static constexpr int MAX_INPUT_ROWS = 3;
};

#endif // INPUTPANE_H
