// mainframe.cpp -- Top-level frame window.
#include "mainframe.h"
#include "../res/resource.h"
#include <winsock2.h>
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

#pragma comment(lib, "ws2_32.lib")

static const wchar_t MAIN_CLASS[] = L"TitanClientWnd";

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

    // Load settings
    settings_dir = Settings::GetSettingsDir();
    settings.Load(settings_dir);

    // Create font from settings
    wchar_t wfont[64];
    MultiByteToWideChar(CP_UTF8, 0, settings.font_name.c_str(), -1, wfont, 64);
    font_ = CreateFontW(-settings.font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, wfont);

    // Restore window position from settings, or default to 90% of screen.
    int x, y, cx, cy;
    if (settings.win_cx > 100 && settings.win_cy > 100) {
        x = settings.win_x;
        y = settings.win_y;
        cx = settings.win_cx;
        cy = settings.win_cy;
    } else {
        int xSize = GetSystemMetrics(SM_CXSCREEN);
        int ySize = GetSystemMetrics(SM_CYSCREEN);
        cx = (9 * xSize) / 10;
        cy = (9 * ySize) / 10;
        x = (xSize - cx) / 2;
        y = (ySize - cy) / 2;
    }

    SetPendingWindow(this);
    HWND hwnd = CreateWindowExW(0, MAIN_CLASS, L"Titan",
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

    // Start the IOCP thread.
    iocp_shutdown = false;
    iocp_thread = CreateThread(nullptr, 0, IocpThreadProc, this, 0, nullptr);

    // Create a system tab so input always has somewhere to go.
    AddWorld("(System)");

    // PUA color encoding helpers:
    //   U+F500 = reset:         EF 94 80
    //   U+F501 = bold:          EF 94 81
    //   U+F600+n = FG color n:  EF (98+n/64) (80+n%64)
    //   U+F700+n = BG color n:  EF (9C+n/64) (80+n%64)
    // ANSI colors: 0=black,1=red,2=green,3=yellow,4=blue,5=magenta,6=cyan,7=white
    auto pua_fg = [](int color) -> std::string {
        unsigned char buf[3];
        buf[0] = 0xEF;
        buf[1] = (unsigned char)(0x98 + color / 64);
        buf[2] = (unsigned char)(0x80 + color % 64);
        return std::string((char*)buf, 3);
    };
    auto pua_bold = []() -> std::string {
        return std::string("\xEF\x94\x81", 3);
    };
    auto pua_reset = []() -> std::string {
        return std::string("\xEF\x94\x80", 3);
    };

    // Welcome text with color
    tab_states[0]->buffer.append(
        pua_bold() + pua_fg(6) + "Titan" + pua_reset() + " for Windows");
    tab_states[0]->buffer.append(
        "Type " + pua_fg(3) + "/help" + pua_reset() +
        " for commands, " + pua_fg(2) + "File > Connect" + pua_reset() +
        " to connect.");
    tab_states[0]->buffer.append("");
    tab_states[0]->buffer.append(
        pua_fg(1) + "Red " + pua_fg(2) + "Green " + pua_fg(3) + "Yellow " +
        pua_fg(4) + "Blue " + pua_fg(5) + "Magenta " + pua_fg(6) + "Cyan " +
        pua_fg(7) + "White" + pua_reset());
    tab_states[0]->buffer.append(
        pua_bold() + pua_fg(1) + "Bold Red " + pua_fg(2) + "Bold Green " +
        pua_fg(4) + "Bold Blue" + pua_reset());

    ShowWindow(hwnd, settings.win_maximized ? SW_SHOWMAXIMIZED : nCmdShow);
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

int CMainFrame::ConnectWorld(const std::string& name, const std::string& host,
                             const std::string& port, bool ssl) {
    int idx = AddWorld(name);
    auto& ts = tab_states[idx];

    ts->conn = std::make_unique<Connection>(name, host, port, ssl, iocp);
    if (!ts->conn->begin_connect()) {
        ts->buffer.append("% Failed to connect to " + host + ":" + port);
        ts->conn.reset();
    } else {
        ts->buffer.append("% Connecting to " + host + ":" + port +
                          (ssl ? " (ssl)" : "") + "...");
    }
    output.Invalidate();
    SwitchToTab(idx);
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
    output.ClearSelection();
    input.SetHistoryContext(tab_states[index]->name);

    // Clear activity indicator on the tab we're switching to.
    TabInfo ti;
    ti.name = tab_states[index]->name;
    ti.active = false;
    ti.connected = tab_states[index]->conn && tab_states[index]->conn->is_connected();
    ti.ssl = tab_states[index]->conn && tab_states[index]->conn->uses_ssl();
    tabbar.UpdateTab(index, ti);

    SetFocus(input.hwnd());
}

void CMainFrame::OnInputSubmitted(const std::string& line) {
    if (active_tab < 0 || active_tab >= (int)tab_states.size()) return;
    auto& ts = tab_states[active_tab];

    if (ts->conn && ts->conn->is_connected()) {
        ts->conn->send_line(line);
        // If no remote echo, echo locally
        if (!ts->conn->remote_echo()) {
            ts->buffer.append("> " + line);
        }
    } else {
        ts->buffer.append("> " + line);
    }
    output.Invalidate();
    output.ScrollToBottom();
}

// IOCP thread — blocks on GetQueuedCompletionStatus, posts to UI thread.
DWORD WINAPI CMainFrame::IocpThreadProc(LPVOID param) {
    CMainFrame* self = (CMainFrame*)param;

    for (;;) {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED overlapped = nullptr;

        BOOL ok = GetQueuedCompletionStatus(self->iocp, &bytes, &key, &overlapped, INFINITE);

        if (self->iocp_shutdown) break;

        if (!overlapped) continue;

        // Package the completion and post to the UI thread.
        auto* msg = new IocpMsg();
        msg->conn = (Connection*)key;
        msg->ctx = CONTAINING_RECORD(overlapped, IoContext, overlapped);
        msg->bytes = bytes;
        msg->error = ok ? 0 : GetLastError();

        PostMessageW(self->m_hwnd, WM_APP_IOCP, 0, (LPARAM)msg);
    }
    return 0;
}

// Called on the UI thread when WM_APP_IOCP arrives.
void CMainFrame::OnIocpCompletion(IocpMsg* msg) {
    auto lines = msg->conn->on_completion(msg->ctx, msg->bytes, msg->error);

    // Find the tab that owns this connection
    for (int i = 0; i < (int)tab_states.size(); i++) {
        if (tab_states[i]->conn.get() == msg->conn) {
            for (auto& line : lines) {
                msg->conn->add_to_scrollback(line);
                tab_states[i]->buffer.append(line);
            }

            if (!msg->conn->is_connected()) {
                tab_states[i]->buffer.append("% Connection lost.");
                tab_states[i]->conn.reset();
                TabInfo ti;
                ti.name = tab_states[i]->name;
                ti.connected = false;
                tabbar.UpdateTab(i, ti);
            } else if (i != active_tab) {
                TabInfo ti;
                ti.name = tab_states[i]->name;
                ti.active = true;
                ti.connected = true;
                ti.ssl = msg->conn->uses_ssl();
                tabbar.UpdateTab(i, ti);
            }

            if (i == active_tab) {
                output.ScrollToBottom();
                output.Invalidate();
            }
            break;
        }
    }

    delete msg;
}

void CMainFrame::CheckPrompts() {
    for (int i = 0; i < (int)tab_states.size(); i++) {
        auto& ts = tab_states[i];
        if (!ts->conn) continue;
        std::string prompt = ts->conn->check_prompt(500);
        if (!prompt.empty()) {
            ts->conn->add_to_scrollback(prompt);
            ts->buffer.append(prompt);
            if (i == active_tab) output.Invalidate();
        }
    }
}

void CMainFrame::UpdateStatusBar() {
    if (active_tab < 0 || active_tab >= (int)tab_states.size()) {
        status.SetText("(no connection)");
        return;
    }
    auto& ts = tab_states[active_tab];
    std::string s = ts->name;
    if (ts->conn) {
        if (ts->conn->uses_ssl()) s += " [ssl]";
        if (ts->conn->is_connected()) {
            int idle = ts->conn->idle_secs();
            if (idle > 0) s += "  idle:" + std::to_string(idle) + "s";
        } else {
            s += " (disconnected)";
        }
        if (ts->conn->is_logging()) s += "  [LOG]";
    }
    int nconn = 0;
    for (auto& t : tab_states) if (t->conn && t->conn->is_connected()) nconn++;
    if (nconn > 1) s += "  [" + std::to_string(nconn) + " conn]";
    status.SetText(s);
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

    case WM_TIMER:
        // Periodic: prompt detection and status bar update only.
        // IOCP completions arrive via WM_APP_IOCP from the IOCP thread.
        CheckPrompts();
        UpdateStatusBar();
        return 0;

    case WM_APP_IOCP:
        OnIocpCompletion((IocpMsg*)lParam);
        return 0;

    case WM_DESTROY: {
        // Save window position.
        WINDOWPLACEMENT wp = { sizeof(wp) };
        GetWindowPlacement(m_hwnd, &wp);
        settings.win_x = wp.rcNormalPosition.left;
        settings.win_y = wp.rcNormalPosition.top;
        settings.win_cx = wp.rcNormalPosition.right - wp.rcNormalPosition.left;
        settings.win_cy = wp.rcNormalPosition.bottom - wp.rcNormalPosition.top;
        settings.win_maximized = (wp.showCmd == SW_SHOWMAXIMIZED);

        // Save settings before exit.
        settings.Save(settings_dir);

        // Shut down the IOCP thread.
        iocp_shutdown = true;
        if (iocp != INVALID_HANDLE_VALUE) {
            // Post a dummy completion to unblock GetQueuedCompletionStatus.
            PostQueuedCompletionStatus(iocp, 0, 0, nullptr);
        }
        if (iocp_thread) {
            WaitForSingleObject(iocp_thread, 2000);
            CloseHandle(iocp_thread);
            iocp_thread = nullptr;
        }
        // Disconnect all before destroying.
        tab_states.clear();
        if (font_) { DeleteObject(font_); font_ = nullptr; }
        PostQuitMessage(0);
        return 0;
    }

    case WM_SETFOCUS:
        // Always redirect focus to input pane
        if (input.hwnd()) SetFocus(input.hwnd());
        return 0;

    // Custom messages from child panes
    case WM_APP + 1: {
        // Input line submitted (lParam = UTF-8 string)
        const char* line = (const char*)lParam;
        if (line) OnInputSubmitted(line);
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

    // Send NAWS to all connected worlds.
    RECT rc;
    if (output.hwnd() && GetClientRect(output.hwnd(), &rc)) {
        int cols = rc.right / (output.hwnd() ? 8 : 8);  // approximate
        int rows = rc.bottom / 16;
        for (auto& ts : tab_states) {
            if (ts->conn && ts->conn->is_connected()) {
                ts->conn->send_naws((uint16_t)cols, (uint16_t)rows);
            }
        }
    }
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

// Find dialog state.
static wchar_t g_find_text[256];
static bool g_find_ok, g_find_up;

static INT_PTR CALLBACK FindDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowTextW(hDlg, L"Find in Scrollback");
        CreateWindowExW(0, L"STATIC", L"Find:", WS_CHILD|WS_VISIBLE, 10,12,40,20, hDlg, nullptr, nullptr, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", g_find_text, WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL, 55,10,200,22, hDlg, (HMENU)101, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Search Up", WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON|WS_TABSTOP, 55,40,90,20, hDlg, (HMENU)102, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Search Down", WS_CHILD|WS_VISIBLE|BS_AUTORADIOBUTTON, 150,40,100,20, hDlg, (HMENU)103, nullptr, nullptr);
        SendDlgItemMessageW(hDlg, 102, BM_SETCHECK, BST_CHECKED, 0);
        CreateWindowExW(0, L"BUTTON", L"Find", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON|WS_TABSTOP, 55,70,80,26, hDlg, (HMENU)IDOK, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 145,70,80,26, hDlg, (HMENU)IDCANCEL, nullptr, nullptr);
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        EnumChildWindows(hDlg, [](HWND hw, LPARAM lp) -> BOOL { SendMessageW(hw, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hFont);
        SetFocus(GetDlgItem(hDlg, 101));
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            GetDlgItemTextW(hDlg, 101, g_find_text, 256);
            g_find_up = (SendDlgItemMessageW(hDlg, 102, BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_find_ok = true;
            EndDialog(hDlg, IDOK);
        } else if (LOWORD(wParam) == IDCANCEL) {
            g_find_ok = false;
            EndDialog(hDlg, IDCANCEL);
        }
        return TRUE;
    }
    return FALSE;
}

// Connect dialog state (file-scope for lambda access).
static wchar_t g_conn_host[128], g_conn_port[32];
static bool g_conn_ssl, g_conn_ok;

static INT_PTR CALLBACK ConnDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowTextW(hDlg, L"Connect");
        CreateWindowExW(0, L"STATIC", L"Host:", WS_CHILD|WS_VISIBLE, 10,12,40,20, hDlg, nullptr, nullptr, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL, 55,10,200,22, hDlg, (HMENU)101, nullptr, nullptr);
        CreateWindowExW(0, L"STATIC", L"Port:", WS_CHILD|WS_VISIBLE, 10,42,40,20, hDlg, nullptr, nullptr, nullptr);
        CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"4201", WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL, 55,40,80,22, hDlg, (HMENU)102, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"SSL", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP, 145,42,50,20, hDlg, (HMENU)103, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Connect", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON|WS_TABSTOP, 55,75,80,26, hDlg, (HMENU)IDOK, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 145,75,80,26, hDlg, (HMENU)IDCANCEL, nullptr, nullptr);
        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        EnumChildWindows(hDlg, [](HWND hw, LPARAM lp) -> BOOL { SendMessageW(hw, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hFont);
        SetFocus(GetDlgItem(hDlg, 101));
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            GetDlgItemTextW(hDlg, 101, g_conn_host, 128);
            GetDlgItemTextW(hDlg, 102, g_conn_port, 32);
            g_conn_ssl = (SendDlgItemMessageW(hDlg, 103, BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_conn_ok = true;
            EndDialog(hDlg, IDOK);
        } else if (LOWORD(wParam) == IDCANCEL) {
            g_conn_ok = false;
            EndDialog(hDlg, IDCANCEL);
        }
        return TRUE;
    }
    return FALSE;
}

void CMainFrame::OnCommand(int id) {
    switch (id) {
    case IDM_FILE_EXIT:
        DestroyWindow(m_hwnd);
        break;

    case IDM_FILE_CONNECT: {
        g_conn_ok = false;
        #pragma pack(push, 4)
        struct { DWORD style; DWORD exStyle; WORD cdit; short x, y, cx, cy;
                 WORD menu; WORD cls; WORD title; } tmpl = {
            DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU,
            0, 0, 0, 0, 270, 110, 0, 0, 0
        };
        #pragma pack(pop)
        DialogBoxIndirectParamW(hInst_, (LPCDLGTEMPLATEW)&tmpl,
                                m_hwnd, ConnDlgProc, 0);
        if (g_conn_ok && g_conn_host[0]) {
            char host8[256], port8[64];
            WideCharToMultiByte(CP_UTF8, 0, g_conn_host, -1,
                                host8, (int)sizeof(host8), nullptr, nullptr);
            WideCharToMultiByte(CP_UTF8, 0, g_conn_port, -1,
                                port8, (int)sizeof(port8), nullptr, nullptr);
            std::string name = std::string(host8) + ":" + port8;
            ConnectWorld(name, host8, port8, g_conn_ssl);

            // Save to worlds list if not already present
            bool found = false;
            for (auto& w : settings.worlds) {
                if (w.host == host8 && w.port == port8) { found = true; break; }
            }
            if (!found) {
                WorldDef wd;
                wd.name = name;
                wd.host = host8;
                wd.port = port8;
                wd.ssl = g_conn_ssl;
                settings.worlds.push_back(wd);
            }
        }
        break;
    }

    case IDM_FILE_DISCONNECT:
        if (active_tab > 0) RemoveWorld(active_tab);  // Don't close system tab (0)
        break;

    case IDM_WORLD_RECONNECT:
        if (active_tab >= 0 && active_tab < (int)tab_states.size()) {
            auto& ts = tab_states[active_tab];
            if (ts->conn && !ts->conn->is_connected()) {
                // Reconnect using saved host/port
                std::string host = ts->conn->host();
                std::string port = ts->conn->port();
                bool ssl = ts->conn->uses_ssl();
                ts->conn.reset();
                ts->conn = std::make_unique<Connection>(ts->name, host, port, ssl, iocp);
                if (ts->conn->begin_connect()) {
                    ts->buffer.append("% Reconnecting...");
                } else {
                    ts->buffer.append("% Reconnect failed.");
                    ts->conn.reset();
                }
                output.Invalidate();
            }
        }
        break;

    case IDM_TAB_NEXT:
        if (tab_states.size() > 1) {
            int next = (active_tab + 1) % (int)tab_states.size();
            SwitchToTab(next);
        }
        break;

    case IDM_TAB_PREV:
        if (tab_states.size() > 1) {
            int prev = (active_tab - 1 + (int)tab_states.size()) % (int)tab_states.size();
            SwitchToTab(prev);
        }
        break;

    case IDM_EDIT_COPY: {
        std::string sel = output.GetSelectedText();
        if (!sel.empty()) {
            int wlen = MultiByteToWideChar(CP_UTF8, 0, sel.c_str(), (int)sel.size(), nullptr, 0);
            if (wlen > 0 && OpenClipboard(m_hwnd)) {
                EmptyClipboard();
                HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, (wlen + 1) * sizeof(wchar_t));
                if (hg) {
                    wchar_t* p = (wchar_t*)GlobalLock(hg);
                    MultiByteToWideChar(CP_UTF8, 0, sel.c_str(), (int)sel.size(), p, wlen);
                    p[wlen] = 0;
                    GlobalUnlock(hg);
                    SetClipboardData(CF_UNICODETEXT, hg);
                }
                CloseClipboard();
            }
        }
        break;
    }

    case IDM_EDIT_PASTE: {
        if (OpenClipboard(m_hwnd)) {
            HGLOBAL hg = GetClipboardData(CF_UNICODETEXT);
            if (hg) {
                wchar_t* p = (wchar_t*)GlobalLock(hg);
                if (p) {
                    int len = WideCharToMultiByte(CP_UTF8, 0, p, -1, nullptr, 0, nullptr, nullptr);
                    if (len > 0) {
                        std::string utf8(len - 1, '\0');
                        WideCharToMultiByte(CP_UTF8, 0, p, -1, utf8.data(), len, nullptr, nullptr);
                        input.SetText(input.text() + utf8);
                    }
                    GlobalUnlock(hg);
                }
            }
            CloseClipboard();
        }
        break;
    }

    case IDM_VIEW_SCROLL_BOTTOM:
        output.ScrollToBottom();
        break;

    case IDM_VIEW_FONT: {
        CHOOSEFONTW cf = {};
        LOGFONTW lf = {};
        if (font_) GetObjectW(font_, sizeof(lf), &lf);
        cf.lStructSize = sizeof(cf);
        cf.hwndOwner = m_hwnd;
        cf.lpLogFont = &lf;
        cf.Flags = CF_SCREENFONTS | CF_FIXEDPITCHONLY | CF_INITTOLOGFONTSTRUCT;
        if (ChooseFontW(&cf)) {
            if (font_) DeleteObject(font_);
            font_ = CreateFontIndirectW(&lf);
            tabbar.SetFont(font_);
            output.SetFont(font_);
            input.SetFont(font_);
            status.SetFont(font_);
            // Save to settings
            char name8[64];
            WideCharToMultiByte(CP_UTF8, 0, lf.lfFaceName, -1, name8, 64, nullptr, nullptr);
            settings.font_name = name8;
            settings.font_size = -lf.lfHeight;
            LayoutChildren();
        }
        break;
    }

    case IDM_VIEW_CLEAR:
        if (active_tab >= 0 && active_tab < (int)tab_states.size()) {
            tab_states[active_tab]->buffer.clear();
            output.Invalidate();
        }
        break;

    case IDM_EDIT_FIND: {
        g_find_ok = false;
        #pragma pack(push, 4)
        struct { DWORD style; DWORD exStyle; WORD cdit; short x, y, cx, cy;
                 WORD menu; WORD cls; WORD title; } tmpl = {
            DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU,
            0, 0, 0, 0, 270, 105, 0, 0, 0
        };
        #pragma pack(pop)
        DialogBoxIndirectParamW(hInst_, (LPCDLGTEMPLATEW)&tmpl,
                                m_hwnd, FindDlgProc, 0);
        if (g_find_ok && g_find_text[0]) {
            char pat8[512];
            WideCharToMultiByte(CP_UTF8, 0, g_find_text, -1,
                                pat8, (int)sizeof(pat8), nullptr, nullptr);
            if (!output.SearchText(pat8, g_find_up)) {
                // Wrap search or show "not found"
                if (active_tab >= 0 && active_tab < (int)tab_states.size()) {
                    tab_states[active_tab]->buffer.append("% Not found: " + std::string(pat8));
                    output.Invalidate();
                }
            }
        }
        break;
    }

    case IDM_HELP_ABOUT:
        MessageBoxW(m_hwnd, L"Titan for Windows\nWin32 GUI MU* Client\n\nPart of the TinyMUX project",
                    L"About Titan", MB_OK | MB_ICONINFORMATION);
        break;
    }
}
