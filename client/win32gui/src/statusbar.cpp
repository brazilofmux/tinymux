// statusbar.cpp -- Bottom status bar.
#include "statusbar.h"

static const wchar_t STATUS_CLASS[] = L"TinyMUXStatusBar";

const wchar_t* CStatusBar::ClassName() { return STATUS_CLASS; }

bool CStatusBar::Register(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(0, 0, 128));
    wc.lpszClassName = STATUS_CLASS;
    return RegisterClassExW(&wc) != 0;
}

bool CStatusBar::Create(HWND hParent, HINSTANCE hInst, int x, int y, int cx, int cy) {
    m_parent = WindowFromHWND(hParent);
    SetPendingWindow(this);
    HWND hwnd = CreateWindowExW(0, STATUS_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE,
        x, y, cx, cy, hParent, nullptr, hInst, nullptr);
    return hwnd != nullptr;
}

void CStatusBar::SetText(const std::string& text) {
    text_ = text;
    Invalidate();
}

void CStatusBar::SetFont(HFONT font) {
    font_ = font;
}

LRESULT CALLBACK CStatusBar::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CStatusBar* self = (CStatusBar*)WindowFromHWND(hwnd);
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CStatusBar::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: OnPaint(); return 0;
    }
    return DefProc(msg, wParam, lParam);
}

void CStatusBar::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    RECT rc;
    GetClientRect(m_hwnd, &rc);

    HBRUSH bg = CreateSolidBrush(RGB(0, 0, 128));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    HFONT old_font = font_ ? (HFONT)SelectObject(hdc, font_) : nullptr;
    SetTextColor(hdc, RGB(255, 255, 255));
    SetBkMode(hdc, TRANSPARENT);

    if (!text_.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, text_.c_str(), (int)text_.size(), nullptr, 0);
        if (wlen > 0) {
            wchar_t wbuf[512];
            if (wlen > 511) wlen = 511;
            MultiByteToWideChar(CP_UTF8, 0, text_.c_str(), (int)text_.size(), wbuf, wlen);
            TextOutW(hdc, 4, (rc.bottom - 16) / 2, wbuf, wlen);
        }
    }

    if (old_font) SelectObject(hdc, old_font);
    EndPaint(m_hwnd, &ps);
}
