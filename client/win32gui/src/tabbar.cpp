// tabbar.cpp -- Owner-drawn tab bar.
#include "tabbar.h"
#include <windowsx.h>
#include <algorithm>

static const wchar_t TAB_CLASS[] = L"TinyMUXTabBar";

const wchar_t* CTabBar::ClassName() { return TAB_CLASS; }

bool CTabBar::Register(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = CreateSolidBrush(RGB(48, 48, 48));
    wc.lpszClassName = TAB_CLASS;
    return RegisterClassExW(&wc) != 0;
}

bool CTabBar::Create(HWND hParent, HINSTANCE hInst, int x, int y, int cx, int cy) {
    m_parent = WindowFromHWND(hParent);
    SetPendingWindow(this);
    HWND hwnd = CreateWindowExW(0, TAB_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE,
        x, y, cx, cy, hParent, nullptr, hInst, nullptr);
    return hwnd != nullptr;
}

int CTabBar::AddTab(const std::string& name) {
    TabInfo ti;
    ti.name = name;
    tabs_.push_back(ti);
    if (current_ < 0) current_ = 0;
    Invalidate();
    return (int)tabs_.size() - 1;
}

void CTabBar::RemoveTab(int index) {
    if (index < 0 || index >= (int)tabs_.size()) return;
    tabs_.erase(tabs_.begin() + index);
    if (current_ >= (int)tabs_.size()) current_ = (int)tabs_.size() - 1;
    Invalidate();
}

void CTabBar::SetCurrentTab(int index) {
    if (index < 0 || index >= (int)tabs_.size()) return;
    current_ = index;
    Invalidate();
}

void CTabBar::UpdateTab(int index, const TabInfo& info) {
    if (index < 0 || index >= (int)tabs_.size()) return;
    tabs_[index] = info;
    Invalidate();
}

const TabInfo* CTabBar::GetTab(int index) const {
    if (index < 0 || index >= (int)tabs_.size()) return nullptr;
    return &tabs_[index];
}

void CTabBar::SetFont(HFONT font) {
    font_ = font;
    HDC hdc = GetDC(m_hwnd);
    HFONT old = (HFONT)SelectObject(hdc, font_);
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    char_width_ = tm.tmAveCharWidth;
    SelectObject(hdc, old);
    ReleaseDC(m_hwnd, hdc);
}

int CTabBar::TabWidth(int index) const {
    if (index < 0 || index >= (int)tabs_.size()) return 0;
    return (int)tabs_[index].name.size() * char_width_ + 24;  // padding
}

LRESULT CALLBACK CTabBar::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CTabBar* self = (CTabBar*)WindowFromHWND(hwnd);
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CTabBar::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: OnPaint(); return 0;
    case WM_LBUTTONDOWN: OnLButtonDown(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)); return 0;
    case WM_MBUTTONDOWN: {
        int idx = HitTest(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        if (idx >= 0) {
            // Notify parent to close this tab
            ::SendMessageW(GetParent(m_hwnd), WM_APP + 3, idx, 0);
        }
        return 0;
    }
    }
    return DefProc(msg, wParam, lParam);
}

int CTabBar::HitTest(int x, int /*y*/) const {
    int tx = 0;
    for (int i = 0; i < (int)tabs_.size(); i++) {
        int w = TabWidth(i);
        if (x >= tx && x < tx + w) return i;
        tx += w;
    }
    return -1;
}

void CTabBar::OnLButtonDown(int x, int y) {
    int idx = HitTest(x, y);
    if (idx >= 0 && idx != current_) {
        current_ = idx;
        Invalidate();
        // Notify parent of tab switch
        ::SendMessageW(GetParent(m_hwnd), WM_APP + 4, idx, 0);
    }
}

void CTabBar::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    RECT rc;
    GetClientRect(m_hwnd, &rc);

    // Background
    HBRUSH bg_brush = CreateSolidBrush(RGB(48, 48, 48));
    FillRect(hdc, &rc, bg_brush);
    DeleteObject(bg_brush);

    HFONT old_font = font_ ? (HFONT)SelectObject(hdc, font_) : nullptr;
    SetBkMode(hdc, TRANSPARENT);

    int x = 0;
    for (int i = 0; i < (int)tabs_.size(); i++) {
        int w = TabWidth(i);
        RECT tab_rc = { x, 0, x + w, rc.bottom };

        if (i == current_) {
            // Active tab: lighter background
            HBRUSH active_br = CreateSolidBrush(RGB(0, 0, 0));
            FillRect(hdc, &tab_rc, active_br);
            DeleteObject(active_br);
            SetTextColor(hdc, RGB(255, 255, 255));
        } else {
            SetTextColor(hdc, RGB(160, 160, 160));
        }

        // Draw tab name
        int wlen = MultiByteToWideChar(CP_UTF8, 0, tabs_[i].name.c_str(),
                                       (int)tabs_[i].name.size(), nullptr, 0);
        if (wlen > 0) {
            wchar_t wbuf[256];
            if (wlen > 255) wlen = 255;
            MultiByteToWideChar(CP_UTF8, 0, tabs_[i].name.c_str(),
                               (int)tabs_[i].name.size(), wbuf, wlen);
            TextOutW(hdc, x + 12, (rc.bottom - 16) / 2, wbuf, wlen);
        }

        // Activity dot
        if (tabs_[i].active && i != current_) {
            HBRUSH dot = CreateSolidBrush(RGB(255, 200, 0));
            RECT dot_rc = { x + 3, (rc.bottom - 6) / 2, x + 9, (rc.bottom - 6) / 2 + 6 };
            FillRect(hdc, &dot_rc, dot);
            DeleteObject(dot);
        }

        // Separator line
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(80, 80, 80));
        HPEN old_pen = (HPEN)SelectObject(hdc, pen);
        MoveToEx(hdc, x + w - 1, 2, nullptr);
        LineTo(hdc, x + w - 1, rc.bottom - 2);
        SelectObject(hdc, old_pen);
        DeleteObject(pen);

        x += w;
    }

    if (old_font) SelectObject(hdc, old_font);
    EndPaint(m_hwnd, &ps);
}
