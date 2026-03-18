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
};

#endif // OUTPUTPANE_H
