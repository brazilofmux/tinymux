// mainframe.h -- Top-level frame window: owns tab bar, output, input, status.
#ifndef MAINFRAME_H
#define MAINFRAME_H

#include "window.h"
#include "tabbar.h"
#include "outputpane.h"
#include "inputpane.h"
#include "statusbar.h"
#include "outputbuffer.h"
#include "iconnection.h"
#include "connection.h"
#ifdef HYDRA_GRPC
#include "hydra_connection.h"
#endif
#include "world.h"
#include "settings.h"
#include "hook.h"
#include "spawn.h"
#include "macro.h"
#include "timer.h"
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>

class CMainFrame : public CWindow {
public:
    static const wchar_t* ClassName();
    static bool Register(HINSTANCE hInst);
    bool Create(HINSTANCE hInst, int nCmdShow);

    CTabBar      tabbar;
    COutputPane  output;
    CInputPane   input;
    CStatusBar   status;

    // Per-tab state
    struct TabState {
        std::string name;
        OutputBuffer buffer;
        std::unique_ptr<IConnection> conn;   // null for system tab
    };
    std::vector<std::unique_ptr<TabState>> tab_states;
    int active_tab = -1;

    // IOCP for networking
    HANDLE iocp = INVALID_HANDLE_VALUE;
    WorldDB worlddb;
    Settings settings;
    std::string settings_dir;

    // Subsystems
    HookDB   hooks;
    SpawnDB  spawns;
    MacroDB  macros;
    TimerDB  timers;
    std::unordered_map<std::string, SpawnLines> spawn_lines;
    std::unordered_map<std::string, std::string> vars;

    // Tab management
    int AddWorld(const std::string& name);
    int ConnectWorld(const std::string& name, const std::string& host,
                     const std::string& port, bool ssl);
#ifdef HYDRA_GRPC
    int ConnectHydra(const std::string& name, const std::string& host,
                     const std::string& port, const std::string& user,
                     const std::string& pass, const std::string& game);
#endif
    void RemoveWorld(int index);
    void SwitchToTab(int index);
    void OnInputSubmitted(const std::string& line);
    void HandleSlashCommand(const std::string& input);
    TriggerResult CheckTriggers(std::string& text);
    void FireTimers();

    // Networking — IOCP thread posts WM_APP_IOCP to the UI thread.
    static constexpr UINT WM_APP_IOCP       = WM_APP + 10;
    static constexpr UINT WM_APP_HYDRA_DATA = WM_APP + 11;

    // Payload posted from the IOCP thread to the UI thread.
    struct IocpMsg {
        Connection* conn;
        IoContext*  ctx;
        DWORD       bytes;
        DWORD       error;
    };

    void OnIocpCompletion(IocpMsg* msg);
    void CheckPrompts();
    void UpdateStatusBar();

    // IOCP thread management
    HANDLE iocp_thread = nullptr;
    volatile bool iocp_shutdown = false;
    static DWORD WINAPI IocpThreadProc(LPVOID param);

    HFONT font() const { return font_; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    LRESULT HandleMessage(UINT msg, WPARAM wParam, LPARAM lParam);

    void OnSize(int cx, int cy);
    void OnCommand(int id);
    void LayoutChildren();

    HINSTANCE hInst_ = nullptr;
    HFONT font_ = nullptr;
    int input_height_ = 20;
};

#endif // MAINFRAME_H
