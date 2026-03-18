// mainframe.cpp -- Top-level frame window.
#include "mainframe.h"
#include "../res/resource.h"

static const wchar_t MAIN_CLASS[] = L"TinyMUXClientWnd";

const wchar_t* CMainFrame::ClassName() { return MAIN_CLASS; }

bool CMainFrame::Register(HINSTANCE hInst) {
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MAINMENU);
    wc.lpszClassName = MAIN_CLASS;
    wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wc.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);
    return RegisterClassExW(&wc) != 0;
}

bool CMainFrame::Create(HINSTANCE hInst, int nCmdShow) {
    hInst_ = hInst;

    // Create the default font
    font_ = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");

    int xSize = GetSystemMetrics(SM_CXSCREEN);
    int ySize = GetSystemMetrics(SM_CYSCREEN);
    int cx = (9 * xSize) / 10;
    int cy = (9 * ySize) / 10;
    int x = (xSize - cx) / 2;
    int y = (ySize - cy) / 2;

    SetPendingWindow(this);
    HWND hwnd = CreateWindowExW(0, MAIN_CLASS, L"TinyMUX Client",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, cx, cy, nullptr, nullptr, hInst, nullptr);

    if (!hwnd) return false;

    // Create child panes
    tabbar.Create(hwnd, hInst, 0, 0, cx, CTabBar::TAB_HEIGHT);
    tabbar.SetFont(font_);

    output.Create(hwnd, hInst, 0, CTabBar::TAB_HEIGHT, cx,
                  cy - CTabBar::TAB_HEIGHT - CStatusBar::BAR_HEIGHT - 20);
    output.SetFont(font_);

    input_height_ = 20;
    input.Create(hwnd, hInst, 0,
                 cy - CStatusBar::BAR_HEIGHT - input_height_,
                 cx, input_height_);
    input.SetFont(font_);

    status.Create(hwnd, hInst, 0, cy - CStatusBar::BAR_HEIGHT,
                  cx, CStatusBar::BAR_HEIGHT);
    status.SetFont(font_);
    status.SetText("(no connection)");

    LayoutChildren();

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);
    SetFocus(input.hwnd());

    return true;
}

int CMainFrame::AddWorld(const std::string& name) {
    auto state = std::make_unique<TabState>();
    state->name = name;

    // Add a demo line
    state->buffer.append("% Connected to " + name);

    tab_states.push_back(std::move(state));
    int idx = tabbar.AddTab(name);

    if (active_tab < 0) SwitchToTab(idx);
    return idx;
}

void CMainFrame::RemoveWorld(int index) {
    if (index < 0 || index >= (int)tab_states.size()) return;
    tab_states.erase(tab_states.begin() + index);
    tabbar.RemoveTab(index);
    if (active_tab >= (int)tab_states.size()) {
        active_tab = (int)tab_states.size() - 1;
    }
    if (active_tab >= 0) {
        SwitchToTab(active_tab);
    } else {
        output.SetBuffer(nullptr);
        status.SetText("(no connection)");
    }
}

void CMainFrame::SwitchToTab(int index) {
    if (index < 0 || index >= (int)tab_states.size()) return;
    active_tab = index;
    tabbar.SetCurrentTab(index);
    output.SetBuffer(&tab_states[index]->buffer);
    output.ScrollToBottom();
    status.SetText(tab_states[index]->name);
    SetFocus(input.hwnd());
}

LRESULT CALLBACK CMainFrame::WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    CMainFrame* self = (CMainFrame*)WindowFromHWND(hwnd);
    if (self) return self->HandleMessage(msg, wParam, lParam);
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

LRESULT CMainFrame::HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        OnSize(LOWORD(lParam), HIWORD(lParam));
        return 0;

    case WM_COMMAND:
        OnCommand(LOWORD(wParam));
        return 0;

    case WM_DESTROY:
        if (font_) { DeleteObject(font_); font_ = nullptr; }
        PostQuitMessage(0);
        return 0;

    case WM_SETFOCUS:
        // Always redirect focus to input pane
        if (input.hwnd()) SetFocus(input.hwnd());
        return 0;

    // Custom messages from child panes
    case WM_APP + 1: {
        // Input line submitted (lParam = UTF-8 string)
        // For now, echo to output as a demo
        const char* line = (const char*)lParam;
        if (active_tab >= 0 && active_tab < (int)tab_states.size()) {
            tab_states[active_tab]->buffer.append(std::string("> ") + line);
            output.Invalidate();
            output.ScrollToBottom();
        }
        return 0;
    }

    case WM_APP + 2:
        // Input pane height change request
        input_height_ = (int)lParam;
        LayoutChildren();
        return 0;

    case WM_APP + 3:
        // Tab close request (wParam = tab index)
        RemoveWorld((int)wParam);
        return 0;

    case WM_APP + 4:
        // Tab switch (wParam = tab index)
        SwitchToTab((int)wParam);
        return 0;
    }

    return DefProc(msg, wParam, lParam);
}

void CMainFrame::OnSize(int cx, int cy) {
    LayoutChildren();
}

void CMainFrame::LayoutChildren() {
    RECT rc;
    GetClientRect(m_hwnd, &rc);
    int cx = rc.right;
    int cy = rc.bottom;

    int tab_h = CTabBar::TAB_HEIGHT;
    int status_h = CStatusBar::BAR_HEIGHT;
    int input_h = input_height_;
    int output_h = cy - tab_h - input_h - status_h;
    if (output_h < 20) output_h = 20;

    if (tabbar.hwnd())
        MoveWindow(tabbar.hwnd(), 0, 0, cx, tab_h, TRUE);
    if (output.hwnd())
        MoveWindow(output.hwnd(), 0, tab_h, cx, output_h, TRUE);
    if (input.hwnd())
        MoveWindow(input.hwnd(), 0, tab_h + output_h, cx, input_h, TRUE);
    if (status.hwnd())
        MoveWindow(status.hwnd(), 0, tab_h + output_h + input_h, cx, status_h, TRUE);
}

void CMainFrame::OnCommand(int id) {
    switch (id) {
    case IDM_FILE_EXIT:
        DestroyWindow(m_hwnd);
        break;

    case IDM_FILE_CONNECT:
        // Demo: add a test world
        AddWorld("TestWorld");
        break;

    case IDM_FILE_DISCONNECT:
        if (active_tab >= 0) RemoveWorld(active_tab);
        break;

    case IDM_VIEW_SCROLL_BOTTOM:
        output.ScrollToBottom();
        break;

    case IDM_VIEW_CLEAR:
        if (active_tab >= 0 && active_tab < (int)tab_states.size()) {
            tab_states[active_tab]->buffer.clear();
            output.Invalidate();
        }
        break;

    case IDM_HELP_ABOUT:
        MessageBoxW(m_hwnd, L"TinyMUX Client\nWin32 GUI Reference Implementation",
                    L"About", MB_OK | MB_ICONINFORMATION);
        break;
    }
}
