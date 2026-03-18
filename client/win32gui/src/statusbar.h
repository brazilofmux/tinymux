// statusbar.h -- Bottom status bar.
#ifndef STATUSBAR_H
#define STATUSBAR_H

#include "window.h"
#include <string>

class CStatusBar : public CWindow {
public:
    static const wchar_t* ClassName();
    static bool Register(HINSTANCE hInst);
    bool Create(HWND hParent, HINSTANCE hInst, int x, int y, int cx, int cy);

    void SetText(const std::string& text);
    void SetFont(HFONT font);

    static constexpr int BAR_HEIGHT = 22;

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);
    void OnPaint();

    std::string text_;
    HFONT font_ = nullptr;
};

#endif // STATUSBAR_H
