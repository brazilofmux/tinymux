// outputpane.h -- Custom-drawn scrollback output pane.
#ifndef OUTPUTPANE_H
#define OUTPUTPANE_H

#include "window.h"
#include "outputbuffer.h"

class COutputPane : public CWindow {
public:
    static const wchar_t* ClassName();
    static bool Register(HINSTANCE hInst);
    bool Create(HWND hParent, HINSTANCE hInst, int x, int y, int cx, int cy);

    void SetBuffer(OutputBuffer* buf);
    void ScrollUp(int lines = 1);
    void ScrollDown(int lines = 1);
    void ScrollPageUp();
    void ScrollPageDown();
    void ScrollToBottom();

    // Selection
    void BeginSelection(int x, int y);
    void ExtendSelection(int x, int y);
    void EndSelection();
    std::string GetSelectedText() const;
    bool HasSelection() const { return sel_active_; }
    void ClearSelection();

    void SetFont(HFONT font);
    void SetWordWrap(bool enable);
    bool word_wrap() const { return word_wrap_; }

    // Find — highlights and scrolls to the matching line.
    bool SearchText(const std::string& pattern, bool search_up = true);

    int char_w() const { return char_width_; }
    int char_h() const { return char_height_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnPaint();
    void OnSize(int cx, int cy);
    void OnVScroll(WPARAM wParam);
    void OnMouseWheel(short delta);
    void UpdateScrollBar();

    // Hit test: screen coordinates to line/column.
    void HitTest(int px, int py, int& out_line, int& out_col) const;

    OutputBuffer* buf_ = nullptr;
    HFONT font_ = nullptr;
    int char_width_ = 8;
    int char_height_ = 16;
    int visible_rows_ = 0;
    int visible_cols_ = 0;

    // Selection state (byte offsets into lines)
    bool sel_active_ = false;
    bool sel_dragging_ = false;
    int  sel_start_line_ = 0;
    int  sel_start_col_ = 0;
    int  sel_end_line_ = 0;
    int  sel_end_col_ = 0;

    bool word_wrap_ = true;

    // Word-wrapped line mapping: for a given buffer line, how many
    // screen rows does it occupy?  Computed lazily during paint.
    int wrap_line_rows(const OutputLine& line) const;

    // Find state
    int find_line_ = -1;
    std::string find_pattern_;
};

#endif // OUTPUTPANE_H
