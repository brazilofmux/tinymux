// outputpane.cpp -- Custom-drawn scrollback output pane.
#include "outputpane.h"
#include <windowsx.h>
#include <algorithm>

static const wchar_t OUTPUT_CLASS[] = L"TinyMUXOutput";

const wchar_t* COutputPane::ClassName() { return OUTPUT_CLASS; }

bool COutputPane::Register(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = OUTPUT_CLASS;
    return RegisterClassExW(&wc) != 0;
}

bool COutputPane::Create(HWND hParent, HINSTANCE hInst, int x, int y, int cx, int cy) {
    m_parent = WindowFromHWND(hParent);
    SetPendingWindow(this);
    HWND hwnd = CreateWindowExW(0, OUTPUT_CLASS, nullptr,
        WS_CHILD | WS_VISIBLE | WS_VSCROLL,
        x, y, cx, cy, hParent, nullptr, hInst, nullptr);
    return hwnd != nullptr;
}

void COutputPane::SetBuffer(OutputBuffer* buf) {
    buf_ = buf;
    Invalidate();
}

void COutputPane::SetFont(HFONT font) {
    font_ = font;
    // Measure character size
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

LRESULT CALLBACK COutputPane::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    COutputPane* self = (COutputPane*)WindowFromHWND(hwnd);
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT COutputPane::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT:      OnPaint(); return 0;
    case WM_SIZE:       OnSize(LOWORD(lParam), HIWORD(lParam)); return 0;
    case WM_VSCROLL:    OnVScroll(wParam); return 0;
    case WM_MOUSEWHEEL: OnMouseWheel(GET_WHEEL_DELTA_WPARAM(wParam)); return 0;
    case WM_LBUTTONDOWN:
        BeginSelection(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        SetCapture(m_hwnd);
        return 0;
    case WM_MOUSEMOVE:
        if (sel_dragging_) ExtendSelection(GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (sel_dragging_) { EndSelection(); ReleaseCapture(); }
        return 0;
    }
    return DefProc(msg, wParam, lParam);
}

void COutputPane::SetWordWrap(bool enable) {
    word_wrap_ = enable;
    Invalidate();
}

int COutputPane::wrap_line_rows(const OutputLine& line) const {
    if (!word_wrap_ || visible_cols_ <= 0 || line.display_width <= visible_cols_)
        return 1;
    return (line.display_width + visible_cols_ - 1) / visible_cols_;
}

bool COutputPane::SearchText(const std::string& pattern, bool search_up) {
    if (!buf_ || pattern.empty()) return false;
    find_pattern_ = pattern;
    auto& lines = buf_->lines();
    int total = (int)lines.size();
    int start = (find_line_ >= 0 && find_line_ < total) ? find_line_ : total - 1;

    if (search_up) {
        for (int i = start - 1; i >= 0; i--) {
            if (lines[i].text.find(pattern) != std::string::npos) {
                find_line_ = i;
                // Scroll to show this line
                int from_bottom = total - 1 - i;
                buf_->scroll_offset = std::max(from_bottom - visible_rows_ / 2, 0);
                UpdateScrollBar();
                Invalidate();
                return true;
            }
        }
    } else {
        for (int i = start + 1; i < total; i++) {
            if (lines[i].text.find(pattern) != std::string::npos) {
                find_line_ = i;
                int from_bottom = total - 1 - i;
                buf_->scroll_offset = std::max(from_bottom - visible_rows_ / 2, 0);
                UpdateScrollBar();
                Invalidate();
                return true;
            }
        }
    }
    return false;
}

void COutputPane::OnSize(int cx, int cy) {
    if (char_height_ > 0) visible_rows_ = cy / char_height_;
    if (char_width_ > 0) visible_cols_ = cx / char_width_;
    UpdateScrollBar();
}

void COutputPane::OnPaint() {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);

    HFONT old_font = font_ ? (HFONT)SelectObject(hdc, font_) : nullptr;
    SetBkMode(hdc, OPAQUE);

    RECT rc;
    GetClientRect(m_hwnd, &rc);

    // Default colors
    COLORREF def_fg = RGB(192, 192, 192);
    COLORREF def_bg = RGB(0, 0, 0);

    if (!buf_ || buf_->lines().empty()) {
        FillRect(hdc, &rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
        if (old_font) SelectObject(hdc, old_font);
        EndPaint(m_hwnd, &ps);
        return;
    }

    auto& lines = buf_->lines();
    int total = (int)lines.size();
    int offset = buf_->scroll_offset;
    int first = total - visible_rows_ - offset;
    if (first < 0) first = 0;

    for (int row = 0; row < visible_rows_; row++) {
        int idx = first + row;
        int y = row * char_height_;

        if (idx < 0 || idx >= total) {
            RECT row_rc = { 0, y, rc.right, y + char_height_ };
            FillRect(hdc, &row_rc, (HBRUSH)GetStockObject(BLACK_BRUSH));
            continue;
        }

        // Highlight find match line
        if (idx == find_line_ && !find_pattern_.empty()) {
            RECT hl_rc = { 0, y, rc.right, y + char_height_ };
            HBRUSH hl = CreateSolidBrush(RGB(40, 40, 80));
            FillRect(hdc, &hl_rc, hl);
            DeleteObject(hl);
        }

        auto& line = lines[idx];
        int x = 0;

        // Draw text in color runs
        size_t i = 0;
        while (i < line.text.size()) {
            // Find run of identical attrs
            size_t run_start = i;
            const co_color_attr& a = line.attrs[i];
            while (i < line.text.size() &&
                   memcmp(&line.attrs[i], &a, sizeof(co_color_attr)) == 0) {
                i++;
            }

            // Resolve colors
            COLORREF fg, bg;
            if (a.fg_type == 0) fg = def_fg;
            else fg = RGB((a.fg >> 16) & 0xFF, (a.fg >> 8) & 0xFF, a.fg & 0xFF);
            if (a.bg_type == 0) bg = def_bg;
            else bg = RGB((a.bg >> 16) & 0xFF, (a.bg >> 8) & 0xFF, a.bg & 0xFF);

            if (a.inverse) { COLORREF tmp = fg; fg = bg; bg = tmp; }

            SetTextColor(hdc, fg);
            SetBkColor(hdc, bg);

            // Draw the run
            int run_len = (int)(i - run_start);
            const char* run_text = line.text.c_str() + run_start;

            // Convert UTF-8 run to wide chars for ExtTextOutW
            int wlen = MultiByteToWideChar(CP_UTF8, 0, run_text, run_len, nullptr, 0);
            if (wlen > 0) {
                wchar_t wbuf[4096];
                if (wlen > 4095) wlen = 4095;
                MultiByteToWideChar(CP_UTF8, 0, run_text, run_len, wbuf, wlen);
                ExtTextOutW(hdc, x, y, ETO_OPAQUE, nullptr, wbuf, wlen, nullptr);

                SIZE sz;
                GetTextExtentPoint32W(hdc, wbuf, wlen, &sz);
                x += sz.cx;
            }
        }

        // Fill remainder of row with background
        if (x < rc.right) {
            RECT fill = { x, y, rc.right, y + char_height_ };
            HBRUSH br = CreateSolidBrush(def_bg);
            FillRect(hdc, &fill, br);
            DeleteObject(br);
        }
    }

    // Fill any remaining rows below content
    int content_bottom = visible_rows_ * char_height_;
    if (content_bottom < rc.bottom) {
        RECT fill = { 0, content_bottom, rc.right, rc.bottom };
        FillRect(hdc, &fill, (HBRUSH)GetStockObject(BLACK_BRUSH));
    }

    if (old_font) SelectObject(hdc, old_font);
    EndPaint(m_hwnd, &ps);
}

void COutputPane::OnVScroll(WPARAM wParam) {
    if (!buf_) return;
    int total = (int)buf_->lines().size();
    int max_offset = total > visible_rows_ ? total - visible_rows_ : 0;

    switch (LOWORD(wParam)) {
    case SB_LINEUP:     buf_->scroll_offset = std::min(buf_->scroll_offset + 1, max_offset); break;
    case SB_LINEDOWN:   buf_->scroll_offset = std::max(buf_->scroll_offset - 1, 0); break;
    case SB_PAGEUP:     buf_->scroll_offset = std::min(buf_->scroll_offset + visible_rows_, max_offset); break;
    case SB_PAGEDOWN:   buf_->scroll_offset = std::max(buf_->scroll_offset - visible_rows_, 0); break;
    case SB_THUMBTRACK: buf_->scroll_offset = max_offset - HIWORD(wParam); break;
    }
    UpdateScrollBar();
    Invalidate();
}

void COutputPane::OnMouseWheel(short delta) {
    if (!buf_) return;
    int lines = delta / WHEEL_DELTA * 3;
    int total = (int)buf_->lines().size();
    int max_offset = total > visible_rows_ ? total - visible_rows_ : 0;
    buf_->scroll_offset = std::clamp(buf_->scroll_offset + lines, 0, max_offset);
    UpdateScrollBar();
    Invalidate();
}

void COutputPane::UpdateScrollBar() {
    if (!buf_ || !m_hwnd) return;
    int total = (int)buf_->lines().size();
    int max_offset = total > visible_rows_ ? total - visible_rows_ : 0;

    SCROLLINFO si = {};
    si.cbSize = sizeof(si);
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    si.nMin = 0;
    si.nMax = total > 0 ? total - 1 : 0;
    si.nPage = visible_rows_;
    si.nPos = max_offset - buf_->scroll_offset;
    SetScrollInfo(m_hwnd, SB_VERT, &si, TRUE);
}

void COutputPane::ScrollUp(int lines) {
    if (!buf_) return;
    int total = (int)buf_->lines().size();
    int max_offset = total > visible_rows_ ? total - visible_rows_ : 0;
    buf_->scroll_offset = std::min(buf_->scroll_offset + lines, max_offset);
    UpdateScrollBar();
    Invalidate();
}

void COutputPane::ScrollDown(int lines) {
    if (!buf_) return;
    buf_->scroll_offset = std::max(buf_->scroll_offset - lines, 0);
    UpdateScrollBar();
    Invalidate();
}

void COutputPane::ScrollPageUp() { ScrollUp(std::max(visible_rows_ - 1, 1)); }
void COutputPane::ScrollPageDown() { ScrollDown(std::max(visible_rows_ - 1, 1)); }
void COutputPane::ScrollToBottom() {
    if (buf_) buf_->scroll_offset = 0;
    UpdateScrollBar();
    Invalidate();
}

// -- Selection --

void COutputPane::HitTest(int px, int py, int& out_line, int& out_col) const {
    int row = py / char_height_;
    out_col = px / char_width_;
    if (!buf_) { out_line = 0; return; }
    int total = (int)buf_->lines().size();
    int first = total - visible_rows_ - buf_->scroll_offset;
    if (first < 0) first = 0;
    out_line = first + row;
    if (out_line >= total) out_line = total - 1;
    if (out_line < 0) out_line = 0;
}

void COutputPane::BeginSelection(int x, int y) {
    HitTest(x, y, sel_start_line_, sel_start_col_);
    sel_end_line_ = sel_start_line_;
    sel_end_col_ = sel_start_col_;
    sel_active_ = true;
    sel_dragging_ = true;
}

void COutputPane::ExtendSelection(int x, int y) {
    HitTest(x, y, sel_end_line_, sel_end_col_);
    Invalidate();
}

void COutputPane::EndSelection() {
    sel_dragging_ = false;
}

void COutputPane::ClearSelection() {
    sel_active_ = false;
    sel_dragging_ = false;
    Invalidate();
}

std::string COutputPane::GetSelectedText() const {
    if (!buf_ || !sel_active_) return "";
    auto& lines = buf_->lines();
    int l1 = sel_start_line_, c1 = sel_start_col_;
    int l2 = sel_end_line_, c2 = sel_end_col_;
    if (l1 > l2 || (l1 == l2 && c1 > c2)) {
        std::swap(l1, l2); std::swap(c1, c2);
    }

    std::string result;
    for (int i = l1; i <= l2 && i < (int)lines.size(); i++) {
        auto& line = lines[i];
        int start = (i == l1) ? std::min(c1, (int)line.text.size()) : 0;
        int end = (i == l2) ? std::min(c2, (int)line.text.size()) : (int)line.text.size();
        if (start < end) {
            result.append(line.text, start, end - start);
        }
        if (i < l2) result.push_back('\n');
    }
    return result;
}
