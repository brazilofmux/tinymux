// mainframe.cpp -- Top-level frame window.
#include "mainframe.h"
#include "../res/resource.h"
#include <winsock2.h>
#include <commdlg.h>
#include <sstream>
#include <algorithm>
#include <regex>

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
    output.ScrollToBottom();
    output.Invalidate();

    ShowWindow(hwnd, settings.win_maximized ? SW_SHOWMAXIMIZED : nCmdShow);
    UpdateWindow(hwnd);
    SetFocus(input.hwnd());

    return true;
}

int CMainFrame::AddWorld(const std::string& name) {
    auto state = std::make_unique<TabState>();
    state->name = name;

    tab_states.push_back(std::move(state));
    int idx = tabbar.AddTab(name);

    if (active_tab < 0) SwitchToTab(idx);
    return idx;
}

int CMainFrame::ConnectWorld(const std::string& name, const std::string& host,
                             const std::string& port, bool ssl) {
    int idx = AddWorld(name);
    auto& ts = tab_states[idx];

    auto telnet = std::make_unique<Connection>(name, host, port, ssl, iocp);
    if (!telnet->begin_connect()) {
        ts->buffer.append("% Failed to connect to " + host + ":" + port);
    } else {
        ts->buffer.append("% Connecting to " + host + ":" + port +
                          (ssl ? " (ssl)" : "") + "...");
        ts->conn = std::move(telnet);
    }
    output.Invalidate();
    SwitchToTab(idx);
    return idx;
}

#ifdef HYDRA_GRPC
int CMainFrame::ConnectHydra(const std::string& name, const std::string& host,
                              const std::string& port, const std::string& user,
                              const std::string& pass, const std::string& game,
                              bool use_tls) {
    int idx = AddWorld(name);
    auto& ts = tab_states[idx];

    auto hconn = std::make_unique<HydraConnection>(name, host, port, user, pass, game, iocp, use_tls);
    hconn->setColorFormat(4);  // PUA_UTF8 for custom GUI rendering
    ts->buffer.append("% Connecting via Hydra to " + host + ":" + port + "...");
    ts->conn = std::move(hconn);

    auto* hydra = static_cast<HydraConnection*>(ts->conn.get());
    if (!hydra->connect()) {
        auto chunks = hydra->drain_output_chunks();
        for (auto& chunk : chunks) {
            AppendHydraChunk(*ts, *hydra, chunk);
        }
        ts->conn.reset();
    }

    output.Invalidate();
    SwitchToTab(idx);
    return idx;
}
#endif

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

    // Slash commands
    if (!line.empty() && line[0] == '/') {
        HandleSlashCommand(line);
        output.Invalidate();
        output.ScrollToBottom();
        return;
    }

    if (ts->conn && ts->conn->is_connected()) {
        ts->conn->send_line(line);
        if (!ts->conn->remote_echo()) {
            ts->buffer.append("> " + line);
        }
    } else {
        ts->buffer.append("> " + line);
    }
    output.Invalidate();
    output.ScrollToBottom();
}

void CMainFrame::HandleSlashCommand(const std::string& input) {
    // Tokenize
    std::istringstream ss(input);
    std::vector<std::string> args;
    std::string tok;
    while (ss >> tok) args.push_back(tok);
    if (args.empty()) return;

    std::string cmd = args[0].substr(1); // strip /
    std::transform(cmd.begin(), cmd.end(), cmd.begin(), ::tolower);

    auto& ts = tab_states[active_tab];
    auto sys = [&](const std::string& msg) {
        ts->buffer.append(msg);
    };

    if (cmd == "hook") {
        // /hook name event = command
        std::string rest = input.substr(input.find(' ') + 1);
        auto eq = rest.find('=');
        if (eq == std::string::npos) {
            sys("% Usage: /hook <name> <event> = <command>");
            return;
        }
        std::string before = rest.substr(0, eq);
        std::string body = rest.substr(eq + 1);
        while (!before.empty() && before.back() == ' ') before.pop_back();
        while (!body.empty() && body.front() == ' ') body = body.substr(1);
        std::istringstream bs(before);
        std::string hname, event;
        bs >> hname >> event;
        std::transform(event.begin(), event.end(), event.begin(), ::toupper);
        Hook h; h.name = hname; h.event = event; h.body = body;
        hooks.add(std::move(h));
        sys("% Hook '" + hname + "' on " + event + " -> " + body);
    } else if (cmd == "unhook") {
        if (args.size() >= 2 && hooks.remove(args[1]))
            sys("% Hook '" + args[1] + "' removed.");
        else sys("% No hook: " + (args.size() >= 2 ? args[1] : ""));
    } else if (cmd == "hooks") {
        if (hooks.all().empty()) { sys("% No hooks."); return; }
        for (auto& h : hooks.all())
            sys("  " + h.name + ": " + h.event + " -> " + h.body);
    } else if (cmd == "spawn") {
        if (args.size() < 2) {
            if (spawns.all().empty()) { sys("% No spawns."); return; }
            for (auto& s : spawns.all()) {
                std::string d = "  " + s.name + " (" + s.path + "):";
                for (auto& p : s.patterns) d += " /" + p + "/";
                sys(d);
            }
        } else {
            std::string sub = args[1];
            std::transform(sub.begin(), sub.end(), sub.begin(), ::tolower);
            if (sub == "add" && args.size() >= 4) {
                SpawnConfig sc;
                sc.name = args[2];
                sc.path = args[2];
                std::transform(sc.path.begin(), sc.path.end(), sc.path.begin(), ::tolower);
                sc.patterns.push_back(args[3]);
                spawns.add(std::move(sc));
                sys("% Spawn '" + args[2] + "' added.");
            } else if ((sub == "remove" || sub == "del") && args.size() >= 3) {
                std::string p = args[2];
                std::transform(p.begin(), p.end(), p.begin(), ::tolower);
                spawns.remove(p);
                sys("% Spawn removed.");
            } else {
                sys("% Usage: /spawn [add|remove|list] ...");
            }
        }
    } else if (cmd == "set") {
        if (args.size() >= 3) {
            std::string val;
            for (size_t i = 2; i < args.size(); i++) {
                if (i > 2) val += " ";
                val += args[i];
            }
            vars[args[1]] = val;
            sys("% Set " + args[1] + " = " + val);
        } else sys("% Usage: /set <name> <value>");
    } else if (cmd == "unset") {
        if (args.size() >= 2) { vars.erase(args[1]); sys("% Unset " + args[1]); }
        else sys("% Usage: /unset <name>");
    } else if (cmd == "vars") {
        if (vars.empty()) { sys("% No variables."); return; }
        for (auto& [k, v] : vars) sys("  " + k + " = " + v);
    } else if (cmd == "def") {
        // /def [name] -t'pattern' [-p pri] [-n shots] [-g] [-h] [-s'find/replace'] [-c class] body
        std::string rest = input.substr(input.find(' ') + 1);
        Macro m;
        std::string err;
        if (!parse_def(rest, m, err)) {
            sys("% Error: " + err);
        } else {
            macros.define(std::move(m));
            sys("% Defined: " + rest);
        }
    } else if (cmd == "undef") {
        if (args.size() >= 2) {
            if (macros.undef(args[1])) sys("% Removed: " + args[1]);
            else sys("% No macro: " + args[1]);
        } else sys("% Usage: /undef <name>");
    } else if (cmd == "list") {
        auto& all = macros.all();
        if (all.empty()) { sys("% No macros."); return; }
        for (auto& m : all) {
            std::string d = "  " + m.name;
            if (!m.trigger.empty()) d += " -t'" + m.trigger + "'";
            if (m.gag) d += " -g";
            if (m.hilite) d += " -h";
            if (!m.substitute_find.empty()) d += " -s'" + m.substitute_find + "/" + m.substitute_replace + "'";
            if (!m.line_class.empty()) d += " -c" + m.line_class;
            if (m.priority) d += " -p" + std::to_string(m.priority);
            if (m.shots >= 0) d += " -n" + std::to_string(m.shots);
            if (!m.body.empty()) d += " = " + m.body;
            sys(d);
        }
    } else if (cmd == "repeat") {
        // /repeat name seconds command
        if (args.size() < 4) {
            sys("% Usage: /repeat <name> <seconds> <command>");
            return;
        }
        int ms = 0;
        try { ms = (int)(std::stod(args[2]) * 1000); } catch (...) {}
        if (ms <= 0) { sys("% Invalid interval."); return; }
        std::string tcmd;
        for (size_t i = 3; i < args.size(); i++) {
            if (i > 3) tcmd += " ";
            tcmd += args[i];
        }
        timers.add(args[1], tcmd, ms);
        sys("% Timer '" + args[1] + "': every " + args[2] + "s");
    } else if (cmd == "killtimer") {
        if (args.size() >= 2) {
            if (timers.remove(args[1])) sys("% Timer '" + args[1] + "' removed.");
            else sys("% No timer: " + args[1]);
        } else sys("% Usage: /killtimer <name>");
    } else if (cmd == "listtimers") {
        auto& all = timers.all();
        if (all.empty()) { sys("% No timers."); return; }
        for (auto& t : all) {
            std::string d = "  " + t.name + " every " +
                std::to_string(t.interval_ms / 1000) + "s";
            if (t.shots >= 0) d += " (" + std::to_string(t.shots) + " left)";
            d += " = " + t.command;
            sys(d);
        }
    } else if (cmd == "log") {
        if (active_tab < 0) return;
        auto& tc = tab_states[active_tab];
        if (!tc->conn) { sys("% Not connected."); return; }
        if (args.size() < 2) {
            if (tc->conn->is_logging()) {
                tc->conn->stop_log();
                sys("% Logging stopped.");
            } else {
                sys("% Usage: /log <filename> | /log (to stop)");
            }
        } else {
            if (tc->conn->start_log(args[1])) sys("% Logging to " + args[1]);
            else sys("% Failed to open: " + args[1]);
        }
    } else if (cmd == "help") {
        sys("% Commands:");
        sys("%   /def [name] -t'pat' [-g] [-h] [-s'f/r'] body - Define trigger");
        sys("%   /undef <name>                 - Remove trigger");
        sys("%   /list                         - List triggers");
        sys("%   /repeat <name> <sec> <cmd>    - Create timer");
        sys("%   /killtimer <name>             - Remove timer");
        sys("%   /listtimers                   - List timers");
        sys("%   /log [filename]               - Toggle logging");
        sys("%   /hook <name> <event> = <cmd>  - Define hook");
        sys("%   /unhook <name>                - Remove hook");
        sys("%   /hooks                        - List hooks");
        sys("%   /spawn [add|remove|list]      - Output routing");
        sys("%   /set <var> <value>            - Set variable");
        sys("%   /unset <var>                  - Remove variable");
        sys("%   /vars                         - List variables");
        sys("%   /help                         - This help");
        sys("% Also: File > Connect, File > Worlds, Edit > Find");
#ifdef HYDRA_GRPC
        sys("%   /hcreate <user> <pass>       - Create Hydra account");
        sys("%   /hconnect <game>             - Connect to game via Hydra");
        sys("%   /hswitch <link#>             - Switch active Hydra link");
        sys("%   /hlinks                      - List Hydra links");
        sys("%   /hdisconnect <link#>         - Disconnect a Hydra link");
        sys("%   /hsession                    - Show Hydra session info");
        sys("%   /hdetach                     - Detach from Hydra session");
#endif
#ifdef HYDRA_GRPC
    } else if (cmd.size() > 1 && cmd[0] == 'h') {
        auto* h = dynamic_cast<HydraConnection*>(ts->conn.get());
        if (!h) {
            sys("% Not connected via Hydra.");
        } else if (h->handleCommand(input)) {
            // Drain output pushed by handleCommand
            for (auto& line : h->drain_output()) {
                ts->buffer.append(line);
            }
            output.Invalidate();
        } else {
            sys("% Unknown Hydra command. Try /hhelp.");
        }
#endif
    } else {
        // Not a local command — send to server (some MUDs use / commands)
        if (ts->conn && ts->conn->is_connected()) {
            ts->conn->send_line(input);
            if (!ts->conn->remote_echo()) ts->buffer.append("> " + input);
        } else {
            sys("% Unknown command: /" + cmd);
        }
    }
}

void CMainFrame::ProcessHydraTriggerText(TabState& ts, const std::string& text) {
    ts.hydra_line_buffer += text;

    size_t nl = 0;
    while ((nl = ts.hydra_line_buffer.find('\n')) != std::string::npos) {
        std::string display = ts.hydra_line_buffer.substr(0, nl);
        ts.hydra_line_buffer.erase(0, nl + 1);
        if (!display.empty() && display.back() == '\r') {
            display.pop_back();
        }

        TriggerResult tr = CheckTriggers(display);
        if (tr.gagged) {
            continue;
        }

        auto matched = spawns.match(display);
        for (auto& path : matched) {
            auto& sl = spawn_lines[ts.name][path];
            sl.push_back(display);
            while (sl.size() > 20000) sl.pop_front();
        }
    }
}

void CMainFrame::AppendHydraChunk(TabState& ts, HydraConnection& hydra,
                                  const HydraConnection::OutputChunk& chunk) {
    hydra.add_to_scrollback(chunk.text);

    if (chunk.is_stream_text) {
        ts.buffer.append_text(chunk.text);
        ProcessHydraTriggerText(ts, chunk.text);
        return;
    }

    std::string display = chunk.text;
    TriggerResult tr = CheckTriggers(display);
    if (tr.gagged) {
        return;
    }

    ts.buffer.append(display);
    auto matched = spawns.match(display);
    for (auto& path : matched) {
        auto& sl = spawn_lines[ts.name][path];
        sl.push_back(display);
        while (sl.size() > 20000) sl.pop_front();
    }
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

#ifdef HYDRA_GRPC
        if (key == IOCP_KEY_HYDRA) {
            // Hydra gRPC output ready — notify the UI thread.
            PostMessageW(self->m_hwnd, WM_APP_HYDRA_DATA, 0, 0);
            continue;
        }
#endif

        if (!overlapped) continue;

        // Package the completion and post to the UI thread.
        auto* msg = new IocpMsg();
        msg->conn = (Connection*)(void*)key;
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

                // Check triggers
                std::string display = line;
                TriggerResult tr = CheckTriggers(display);

                if (!tr.gagged) {
                    tab_states[i]->buffer.append(display);
                }

                // Route to matching spawns (only if not gagged)
                if (!tr.gagged) {
                    auto matched = spawns.match(display);
                    for (auto& path : matched) {
                        auto& sl = spawn_lines[tab_states[i]->name][path];
                        sl.push_back(display);
                        while (sl.size() > 20000) sl.pop_front();
                    }
                }
            }

            if (!msg->conn->is_connected()) {
                tab_states[i]->buffer.append("% Connection lost.");
                // Fire DISCONNECT hooks and cancel timers
                for (auto& cmd : hooks.fire_event("DISCONNECT")) {
                    tab_states[i]->buffer.append("% [hook] " + cmd);
                }
                timers.cancel_all();
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
        auto* hydra = dynamic_cast<HydraConnection*>(ts->conn.get());
        if (hydra && hydra->is_reconnecting()) {
            s += "  reconnecting";
        } else if (ts->conn->is_connected()) {
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
        // Periodic: prompt detection, timer firing, and status bar update.
        CheckPrompts();
        FireTimers();
        UpdateStatusBar();
        return 0;

    case WM_APP_IOCP:
        OnIocpCompletion((IocpMsg*)lParam);
        return 0;

#ifdef HYDRA_GRPC
    case WM_APP_HYDRA_DATA: {
        // Drain all Hydra connections
        for (int i = 0; i < (int)tab_states.size(); i++) {
            auto* hydra = dynamic_cast<HydraConnection*>(tab_states[i]->conn.get());
            if (!hydra) continue;
            bool hadOutput = false;
            for (auto& chunk : hydra->drain_output_chunks()) {
                hadOutput = true;
                AppendHydraChunk(*tab_states[i], *hydra, chunk);
            }
            if (!hydra->is_connected()) {
                tab_states[i]->buffer.append("% Hydra connection lost.");
                for (auto& cmd : hooks.fire_event("DISCONNECT")) {
                    tab_states[i]->buffer.append("% [hook] " + cmd);
                }
                timers.cancel_all();
                tab_states[i]->conn.reset();
                TabInfo ti;
                ti.name = tab_states[i]->name;
                ti.connected = false;
                tabbar.UpdateTab(i, ti);
            } else if (hadOutput && i != active_tab) {
                TabInfo ti;
                ti.name = tab_states[i]->name;
                ti.active = true;
                ti.connected = true;
                ti.ssl = hydra->uses_ssl();
                tabbar.UpdateTab(i, ti);
            }
            if (i == active_tab) {
                output.ScrollToBottom();
            }
        }
        output.Invalidate();
        return 0;
    }
#endif

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

// Settings dialog state.
static Settings* g_settings_ptr;

static INT_PTR CALLBACK SettingsDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowTextW(hDlg, L"Settings");
        int y = 10;
        auto label = [&](const wchar_t* text, int x, int w) {
            CreateWindowExW(0, L"STATIC", text, WS_CHILD|WS_VISIBLE, x, y+2, w, 20, hDlg, nullptr, nullptr, nullptr);
        };
        auto edit = [&](int id, const wchar_t* val, int x, int w) -> HWND {
            HWND h = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", val, WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL, x, y, w, 22, hDlg, (HMENU)(INT_PTR)id, nullptr, nullptr);
            return h;
        };

        wchar_t buf[64];

        label(L"Font:", 10, 80);
        MultiByteToWideChar(CP_UTF8, 0, g_settings_ptr->font_name.c_str(), -1, buf, 64);
        edit(201, buf, 95, 150);
        y += 28;

        label(L"Font size:", 10, 80);
        wsprintfW(buf, L"%d", g_settings_ptr->font_size);
        edit(202, buf, 95, 60);
        y += 28;

        label(L"FG color:", 10, 80);
        wsprintfW(buf, L"#%06X", g_settings_ptr->default_fg);
        edit(203, buf, 95, 80);
        y += 28;

        label(L"BG color:", 10, 80);
        wsprintfW(buf, L"#%06X", g_settings_ptr->default_bg);
        edit(204, buf, 95, 80);
        y += 28;

        label(L"Scrollback:", 10, 80);
        wsprintfW(buf, L"%d", g_settings_ptr->scrollback_lines);
        edit(205, buf, 95, 80);
        y += 28;

        label(L"Prompt ms:", 10, 80);
        wsprintfW(buf, L"%d", g_settings_ptr->prompt_timeout_ms);
        edit(206, buf, 95, 80);
        y += 34;

        CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON|WS_TABSTOP, 55, y, 80, 26, hDlg, (HMENU)IDOK, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 145, y, 80, 26, hDlg, (HMENU)IDCANCEL, nullptr, nullptr);

        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        EnumChildWindows(hDlg, [](HWND hw, LPARAM lp) -> BOOL { SendMessageW(hw, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hFont);
        SetFocus(GetDlgItem(hDlg, 201));
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[128];
            GetDlgItemTextW(hDlg, 201, buf, 128);
            char utf8[128];
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, utf8, 128, nullptr, nullptr);
            g_settings_ptr->font_name = utf8;

            g_settings_ptr->font_size = GetDlgItemInt(hDlg, 202, nullptr, FALSE);

            GetDlgItemTextW(hDlg, 203, buf, 128);
            char hex8[32];
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, hex8, 32, nullptr, nullptr);
            const char* p = hex8; if (*p == '#') p++;
            g_settings_ptr->default_fg = (uint32_t)strtoul(p, nullptr, 16);

            GetDlgItemTextW(hDlg, 204, buf, 128);
            WideCharToMultiByte(CP_UTF8, 0, buf, -1, hex8, 32, nullptr, nullptr);
            p = hex8; if (*p == '#') p++;
            g_settings_ptr->default_bg = (uint32_t)strtoul(p, nullptr, 16);

            g_settings_ptr->scrollback_lines = GetDlgItemInt(hDlg, 205, nullptr, FALSE);
            g_settings_ptr->prompt_timeout_ms = GetDlgItemInt(hDlg, 206, nullptr, FALSE);

            EndDialog(hDlg, IDOK);
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        return TRUE;
    }
    return FALSE;
}

// World Editor sub-dialog — Add or Edit a single world.
static WorldDef g_world_edit;
static bool g_world_edit_ok;

static INT_PTR CALLBACK WorldEditDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowTextW(hDlg, g_world_edit.name.empty() ? L"Add World" : L"Edit World");
        wchar_t buf[128];
        int y = 10;
        auto label = [&](const wchar_t* t, int x, int w) {
            CreateWindowExW(0, L"STATIC", t, WS_CHILD|WS_VISIBLE, x, y+2, w, 20, hDlg, nullptr, nullptr, nullptr);
        };
        auto edit = [&](int id, const char* val, int x, int w) {
            MultiByteToWideChar(CP_UTF8, 0, val, -1, buf, 128);
            CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", buf, WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL, x, y, w, 22, hDlg, (HMENU)(INT_PTR)id, nullptr, nullptr);
        };

        label(L"Name:", 10, 50); edit(301, g_world_edit.name.c_str(), 65, 190); y += 28;
        label(L"Host:", 10, 50); edit(302, g_world_edit.host.c_str(), 65, 190); y += 28;
        label(L"Port:", 10, 50); edit(303, g_world_edit.port.c_str(), 65, 80); y += 28;
        label(L"Char:", 10, 50); edit(304, g_world_edit.character.c_str(), 65, 190); y += 28;

        HWND hSSL = CreateWindowExW(0, L"BUTTON", L"SSL/TLS", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP, 65, y, 80, 20, hDlg, (HMENU)305, nullptr, nullptr);
        if (g_world_edit.ssl) SendMessageW(hSSL, BM_SETCHECK, BST_CHECKED, 0);
        HWND hAuto = CreateWindowExW(0, L"BUTTON", L"Auto-connect", WS_CHILD|WS_VISIBLE|BS_AUTOCHECKBOX|WS_TABSTOP, 155, y, 110, 20, hDlg, (HMENU)306, nullptr, nullptr);
        if (g_world_edit.auto_connect) SendMessageW(hAuto, BM_SETCHECK, BST_CHECKED, 0);
        y += 30;

        CreateWindowExW(0, L"BUTTON", L"OK", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON|WS_TABSTOP, 65, y, 80, 26, hDlg, (HMENU)IDOK, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Cancel", WS_CHILD|WS_VISIBLE|WS_TABSTOP, 155, y, 80, 26, hDlg, (HMENU)IDCANCEL, nullptr, nullptr);

        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        EnumChildWindows(hDlg, [](HWND hw, LPARAM lp) -> BOOL { SendMessageW(hw, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hFont);
        SetFocus(GetDlgItem(hDlg, 301));
        return FALSE;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[256]; char u8[256];
            GetDlgItemTextW(hDlg, 301, buf, 256); WideCharToMultiByte(CP_UTF8,0,buf,-1,u8,256,0,0); g_world_edit.name = u8;
            GetDlgItemTextW(hDlg, 302, buf, 256); WideCharToMultiByte(CP_UTF8,0,buf,-1,u8,256,0,0); g_world_edit.host = u8;
            GetDlgItemTextW(hDlg, 303, buf, 256); WideCharToMultiByte(CP_UTF8,0,buf,-1,u8,256,0,0); g_world_edit.port = u8;
            GetDlgItemTextW(hDlg, 304, buf, 256); WideCharToMultiByte(CP_UTF8,0,buf,-1,u8,256,0,0); g_world_edit.character = u8;
            g_world_edit.ssl = (SendDlgItemMessageW(hDlg, 305, BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_world_edit.auto_connect = (SendDlgItemMessageW(hDlg, 306, BM_GETCHECK, 0, 0) == BST_CHECKED);
            g_world_edit_ok = true;
            EndDialog(hDlg, IDOK);
        } else if (LOWORD(wParam) == IDCANCEL) {
            g_world_edit_ok = false;
            EndDialog(hDlg, IDCANCEL);
        }
        return TRUE;
    }
    return FALSE;
}

// World Manager dialog.
static Settings* g_wm_settings;
static CMainFrame* g_wm_frame;

static void WorldMgr_Refresh(HWND hList) {
    SendMessageW(hList, LB_RESETCONTENT, 0, 0);
    for (auto& w : g_wm_settings->worlds) {
        std::string entry = w.name + "  " + w.host + ":" + w.port;
        if (w.ssl) entry += " ssl";
        if (w.auto_connect) entry += " [auto]";
        wchar_t wbuf[256];
        MultiByteToWideChar(CP_UTF8, 0, entry.c_str(), -1, wbuf, 256);
        SendMessageW(hList, LB_ADDSTRING, 0, (LPARAM)wbuf);
    }
}

static bool WorldMgr_EditWorld(HWND hParent, HINSTANCE hInst) {
    g_world_edit_ok = false;
    #pragma pack(push, 4)
    struct { DWORD style; DWORD exStyle; WORD cdit; short x, y, cx, cy;
             WORD menu; WORD cls; WORD title; } tmpl = {
        DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU,
        0, 0, 0, 0, 280, 200, 0, 0, 0
    };
    #pragma pack(pop)
    DialogBoxIndirectParamW(hInst, (LPCDLGTEMPLATEW)&tmpl, hParent, WorldEditDlgProc, 0);
    return g_world_edit_ok;
}

static INT_PTR CALLBACK WorldMgrDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_INITDIALOG: {
        SetWindowTextW(hDlg, L"World Manager");
        CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|WS_VSCROLL|LBS_NOTIFY,
            10, 10, 260, 180, hDlg, (HMENU)401, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Add",     WS_CHILD|WS_VISIBLE|WS_TABSTOP, 280,10,70,26,  hDlg, (HMENU)402, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Edit",    WS_CHILD|WS_VISIBLE|WS_TABSTOP, 280,42,70,26,  hDlg, (HMENU)403, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Delete",  WS_CHILD|WS_VISIBLE|WS_TABSTOP, 280,74,70,26,  hDlg, (HMENU)404, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Connect", WS_CHILD|WS_VISIBLE|BS_DEFPUSHBUTTON|WS_TABSTOP, 280,120,70,26, hDlg, (HMENU)405, nullptr, nullptr);
        CreateWindowExW(0, L"BUTTON", L"Close",   WS_CHILD|WS_VISIBLE|WS_TABSTOP, 280,152,70,26, hDlg, (HMENU)IDCANCEL, nullptr, nullptr);

        HFONT hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        EnumChildWindows(hDlg, [](HWND hw, LPARAM lp) -> BOOL { SendMessageW(hw, WM_SETFONT, lp, TRUE); return TRUE; }, (LPARAM)hFont);

        WorldMgr_Refresh(GetDlgItem(hDlg, 401));
        return TRUE;
    }
    case WM_COMMAND: {
        HWND hList = GetDlgItem(hDlg, 401);
        int sel = (int)SendMessageW(hList, LB_GETCURSEL, 0, 0);

        switch (LOWORD(wParam)) {
        case 402: { // Add
            g_world_edit = WorldDef();
            g_world_edit.port = "4201";
            if (WorldMgr_EditWorld(hDlg, (HINSTANCE)GetWindowLongPtrW(hDlg, GWLP_HINSTANCE))) {
                if (!g_world_edit.name.empty() && !g_world_edit.host.empty()) {
                    g_wm_settings->worlds.push_back(g_world_edit);
                    WorldMgr_Refresh(hList);
                }
            }
            break;
        }
        case 403: { // Edit
            if (sel >= 0 && sel < (int)g_wm_settings->worlds.size()) {
                g_world_edit = g_wm_settings->worlds[sel];
                if (WorldMgr_EditWorld(hDlg, (HINSTANCE)GetWindowLongPtrW(hDlg, GWLP_HINSTANCE))) {
                    g_wm_settings->worlds[sel] = g_world_edit;
                    WorldMgr_Refresh(hList);
                }
            }
            break;
        }
        case 404: { // Delete
            if (sel >= 0 && sel < (int)g_wm_settings->worlds.size()) {
                g_wm_settings->worlds.erase(g_wm_settings->worlds.begin() + sel);
                WorldMgr_Refresh(hList);
            }
            break;
        }
        case 405: { // Connect
            if (sel >= 0 && sel < (int)g_wm_settings->worlds.size()) {
                auto& w = g_wm_settings->worlds[sel];
#ifdef HYDRA_GRPC
                if (w.use_hydra) {
                    g_wm_frame->ConnectHydra(w.name, w.host, w.port,
                                              w.hydra_user, w.hydra_pass, w.hydra_game);
                } else
#endif
                {
                    g_wm_frame->ConnectWorld(w.name, w.host, w.port, w.ssl);
                }
                EndDialog(hDlg, IDOK);
            }
            break;
        }
        case 401: // Listbox double-click = Connect
            if (HIWORD(wParam) == LBN_DBLCLK) {
                SendMessageW(hDlg, WM_COMMAND, 405, 0);
            }
            break;
        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            break;
        }
        return TRUE;
    }
    }
    return FALSE;
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

TriggerResult CMainFrame::CheckTriggers(std::string& text) {
    TriggerResult result;
    auto matches = macros.match_triggers(text);
    for (auto* m : matches) {
        result.matched = true;
        if (m->gag) result.gagged = true;

        // Substitution
        if (!m->substitute_find.empty() && !result.gagged) {
            try {
                std::regex sub_re(m->substitute_find,
                                  std::regex::ECMAScript | std::regex::icase);
                text = std::regex_replace(text, sub_re, m->substitute_replace);
            } catch (...) {}
        }

        // Execute body
        if (!m->body.empty()) {
            if (m->body[0] == '/') {
                HandleSlashCommand(m->body);
            } else if (active_tab >= 0 && active_tab < (int)tab_states.size()) {
                auto& ts = tab_states[active_tab];
                if (ts->conn && ts->conn->is_connected()) {
                    ts->conn->send_line(m->body);
                }
            }
        }

        if (m->shots > 0) m->shots--;
    }
    return result;
}

void CMainFrame::FireTimers() {
    auto commands = timers.check_and_fire();
    for (auto& cmd : commands) {
        if (cmd[0] == '/') {
            HandleSlashCommand(cmd);
        } else if (active_tab >= 0 && active_tab < (int)tab_states.size()) {
            auto& ts = tab_states[active_tab];
            if (ts->conn && ts->conn->is_connected()) {
                ts->conn->send_line(cmd);
            }
        }
    }
}

void CMainFrame::OnCommand(int id) {
    switch (id) {
    case IDM_FILE_EXIT:
        DestroyWindow(m_hwnd);
        break;

    case IDM_FILE_WORLDS: {
        g_wm_settings = &settings;
        g_wm_frame = this;
        #pragma pack(push, 4)
        struct { DWORD style; DWORD exStyle; WORD cdit; short x, y, cx, cy;
                 WORD menu; WORD cls; WORD title; } tmpl = {
            DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU,
            0, 0, 0, 0, 370, 210, 0, 0, 0
        };
        #pragma pack(pop)
        DialogBoxIndirectParamW(hInst_, (LPCDLGTEMPLATEW)&tmpl,
                                m_hwnd, WorldMgrDlgProc, 0);
        break;
    }

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
                auto reconn = std::make_unique<Connection>(ts->name, host, port, ssl, iocp);
                if (reconn->begin_connect()) {
                    ts->buffer.append("% Reconnecting...");
                    ts->conn = std::move(reconn);
                } else {
                    ts->buffer.append("% Reconnect failed.");
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

    case IDM_FILE_SETTINGS: {
        g_settings_ptr = &settings;
        #pragma pack(push, 4)
        struct { DWORD style; DWORD exStyle; WORD cdit; short x, y, cx, cy;
                 WORD menu; WORD cls; WORD title; } tmpl = {
            DS_MODALFRAME | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU,
            0, 0, 0, 0, 270, 220, 0, 0, 0
        };
        #pragma pack(pop)
        INT_PTR result = DialogBoxIndirectParamW(hInst_, (LPCDLGTEMPLATEW)&tmpl,
                                                 m_hwnd, SettingsDlgProc, 0);
        if (result == IDOK) {
            // Rebuild font from updated settings
            if (font_) DeleteObject(font_);
            wchar_t wfont[64];
            MultiByteToWideChar(CP_UTF8, 0, settings.font_name.c_str(), -1, wfont, 64);
            font_ = CreateFontW(-settings.font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                CLEARTYPE_QUALITY, FIXED_PITCH | FF_MODERN, wfont);
            tabbar.SetFont(font_);
            output.SetFont(font_);
            input.SetFont(font_);
            status.SetFont(font_);
            LayoutChildren();
            output.Invalidate();
        }
        break;
    }

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
