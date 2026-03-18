// inputpane.cpp -- Multi-line input editor.
#include "inputpane.h"
#include <algorithm>

extern "C" {
#include "color_ops.h"
}

static const wchar_t INPUT_CLASS[] = L"TinyMUXInput";

const wchar_t* CInputPane::ClassName() { return INPUT_CLASS; }

bool CInputPane::Register(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.hbrBackground = CreateSolidBrush(RGB(32, 32, 32));
    wc.lpszClassName = INPUT_CLASS;
    return RegisterClassExW(&wc) != 0;
}

bool CInputPane::Create(HWND hParent, HINSTANCE hInst, int x, int y, int cx, int cy) {
    m_parent = WindowFromHWND(hParent);
    SetPendingWindow(this);
    HWND hwnd = CreateWindowExW(0, INPUT_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE,
        x, y, cx, cy, hParent, nullptr, hInst, nullptr);
    return hwnd != nullptr;
}

void CInputPane::SetFont(HFONT font) {
    font_ = font;
    HDC hdc = GetDC(m_hwnd);
    HFONT old = (HFONT)SelectObject(hdc, font_);
    TEXTMETRICW tm;
    GetTextMetricsW(hdc, &tm);
    char_width_ = tm.tmAveCharWidth;
    char_height_ = tm.tmHeight;
    SelectObject(hdc, old);
    ReleaseDC(m_hwnd, hdc);
    Invalidate();
}

int CInputPane::DesiredHeight() const {
    // Count lines (newlines + 1), clamped to MAX_INPUT_ROWS
    int rows = 1;
    for (char c : buf_) if (c == '\n') rows++;
    rows = std::min(rows, MAX_INPUT_ROWS);
    return rows * char_height_ + 4;  // 4px padding
}

void CInputPane::SetHistoryContext(const std::string& key) {
    history_key_ = key;
    history_pos_ = -1;
}

void CInputPane::SetText(const std::string& text) {
    buf_ = text;
    cursor_pos_ = buf_.size();
    Invalidate();
}

LRESULT CALLBACK CInputPane::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CInputPane* self = (CInputPane*)WindowFromHWND(hwnd);
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CInputPane::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: OnPaint(); return 0;
    case WM_SETFOCUS: CreateCaret(m_hwnd, nullptr, 2, char_height_); ShowCaret(m_hwnd); Redraw(); return 0;
    case WM_KILLFOCUS: DestroyCaret(); return 0;
    case WM_KEYDOWN: {
        bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
        std::string line;
        if (OnKeyDown((UINT)wParam, ctrl, shift, alt, line)) {
            // Notify parent via custom message
            ::SendMessageW(GetParent(m_hwnd), WM_APP + 1, 0,
                          (LPARAM)line.c_str());
        }
        return 0;
    }
    case WM_CHAR: OnChar((wchar_t)wParam); return 0;
    }
    return DefProc(msg, wParam, lParam);
}

bool CInputPane::OnKeyDown(UINT vk, bool ctrl, bool shift, bool alt, std::string& out_line) {
    switch (vk) {
    case VK_RETURN:
        if (shift) {
            InsertChar('\n');
            return false;
        }
        out_line = buf_;
        if (!buf_.empty()) {
            history_.push_front(buf_);
            if (history_.size() > MAX_HISTORY) history_.pop_back();
        }
        buf_.clear();
        cursor_pos_ = 0;
        history_pos_ = -1;
        Redraw();
        return true;

    case VK_BACK:   DeleteBackward(); return false;
    case VK_DELETE: DeleteForward(); return false;
    case VK_LEFT:   MoveCursorLeft(); return false;
    case VK_RIGHT:  MoveCursorRight(); return false;
    case VK_HOME:   cursor_pos_ = 0; Redraw(); return false;
    case VK_END:    cursor_pos_ = buf_.size(); Redraw(); return false;
    case VK_UP:     HistoryUp(); return false;
    case VK_DOWN:   HistoryDown(); return false;

    default:
        if (ctrl) {
            switch (vk) {
            case 'A': cursor_pos_ = 0; Redraw(); return false;
            case 'E': cursor_pos_ = buf_.size(); Redraw(); return false;
            case 'K': buf_.erase(cursor_pos_); Redraw(); return false;
            case 'U': buf_.clear(); cursor_pos_ = 0; Redraw(); return false;
            }
        }
        break;
    }
    return false;
}

void CInputPane::OnChar(wchar_t ch) {
    if (ch < 32 && ch != '\t') return;  // Control chars handled in OnKeyDown
    InsertChar(ch);
}

void CInputPane::InsertChar(uint32_t cp) {
    char utf8[4];
    int len = 0;
    if (cp < 0x80) {
        utf8[0] = (char)cp; len = 1;
    } else if (cp < 0x800) {
        utf8[0] = (char)(0xC0 | (cp >> 6));
        utf8[1] = (char)(0x80 | (cp & 0x3F)); len = 2;
    } else if (cp < 0x10000) {
        utf8[0] = (char)(0xE0 | (cp >> 12));
        utf8[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        utf8[2] = (char)(0x80 | (cp & 0x3F)); len = 3;
    } else {
        utf8[0] = (char)(0xF0 | (cp >> 18));
        utf8[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
        utf8[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
        utf8[3] = (char)(0x80 | (cp & 0x3F)); len = 4;
    }
    buf_.insert(cursor_pos_, utf8, len);
    cursor_pos_ += len;
    Redraw();
}

void CInputPane::DeleteBackward() {
    if (cursor_pos_ == 0) return;
    // Grapheme-cluster-aware: find previous cluster start
    const unsigned char* data = (const unsigned char*)buf_.data();
    const unsigned char* pe = data + buf_.size();
    const unsigned char* p = data;
    const unsigned char* prev = data;
    while (p < data + cursor_pos_) {
        prev = p;
        p = co_cluster_advance(p, pe, 1, nullptr);
    }
    size_t prev_pos = (size_t)(prev - data);
    buf_.erase(prev_pos, cursor_pos_ - prev_pos);
    cursor_pos_ = prev_pos;
    Redraw();
}

void CInputPane::DeleteForward() {
    if (cursor_pos_ >= buf_.size()) return;
    const unsigned char* data = (const unsigned char*)buf_.data();
    const unsigned char* pe = data + buf_.size();
    const unsigned char* next = co_cluster_advance(data + cursor_pos_, pe, 1, nullptr);
    size_t next_pos = (size_t)(next - data);
    buf_.erase(cursor_pos_, next_pos - cursor_pos_);
    Redraw();
}

void CInputPane::MoveCursorLeft() {
    if (cursor_pos_ == 0) return;
    const unsigned char* data = (const unsigned char*)buf_.data();
    const unsigned char* pe = data + buf_.size();
    const unsigned char* p = data;
    const unsigned char* prev = data;
    while (p < data + cursor_pos_) {
        prev = p;
        p = co_cluster_advance(p, pe, 1, nullptr);
    }
    cursor_pos_ = (size_t)(prev - data);
    Redraw();
}

void CInputPane::MoveCursorRight() {
    if (cursor_pos_ >= buf_.size()) return;
    const unsigned char* data = (const unsigned char*)buf_.data();
    const unsigned char* pe = data + buf_.size();
    const unsigned char* next = co_cluster_advance(data + cursor_pos_, pe, 1, nullptr);
    cursor_pos_ = (size_t)(next - data);
    Redraw();
}

void CInputPane::HistoryUp() {
    if (history_.empty()) return;
    if (history_pos_ < 0) {
        saved_input_ = buf_;
        history_pos_ = 0;
    } else if (history_pos_ < (int)history_.size() - 1) {
        history_pos_++;
    } else return;
    buf_ = history_[history_pos_];
    cursor_pos_ = buf_.size();
    Redraw();
}

void CInputPane::HistoryDown() {
    if (history_pos_ < 0) return;
    history_pos_--;
    buf_ = (history_pos_ < 0) ? saved_input_ : history_[history_pos_];
    cursor_pos_ = buf_.size();
    Redraw();
}

void CInputPane::Redraw() {
    Invalidate();
    // Position caret
    if (m_hwnd && GetFocus() == m_hwnd) {
        int col = (int)co_visual_width((const unsigned char*)buf_.data(), cursor_pos_);
        SetCaretPos(2 + col * char_width_, 2);
    }
    // Notify parent if desired height changed
    ::SendMessageW(GetParent(m_hwnd), WM_APP + 2, 0, DesiredHeight());
}

void CInputPane::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    RECT rc;
    GetClientRect(m_hwnd, &rc);

    // Background
    HBRUSH bg = CreateSolidBrush(RGB(32, 32, 32));
    FillRect(hdc, &rc, bg);
    DeleteObject(bg);

    // Draw text
    HFONT old_font = font_ ? (HFONT)SelectObject(hdc, font_) : nullptr;
    SetTextColor(hdc, RGB(255, 255, 255));
    SetBkMode(hdc, TRANSPARENT);

    if (!buf_.empty()) {
        int wlen = MultiByteToWideChar(CP_UTF8, 0, buf_.c_str(), (int)buf_.size(), nullptr, 0);
        if (wlen > 0) {
            wchar_t wbuf[4096];
            if (wlen > 4095) wlen = 4095;
            MultiByteToWideChar(CP_UTF8, 0, buf_.c_str(), (int)buf_.size(), wbuf, wlen);
            TextOutW(hdc, 2, 2, wbuf, wlen);
        }
    }

    if (old_font) SelectObject(hdc, old_font);
    EndPaint(m_hwnd, &ps);
}
