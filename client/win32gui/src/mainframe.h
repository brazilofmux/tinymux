// mainframe.h -- Top-level frame window: owns tab bar, output, input, status.
#ifndef MAINFRAME_H
#define MAINFRAME_H

#include "window.h"
#include "tabbar.h"
#include "outputpane.h"
#include "inputpane.h"
#include "statusbar.h"
#include "outputbuffer.h"
#include "connection.h"
#include "world.h"
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
        std::unique_ptr<Connection> conn;   // null for system tab
    };
    std::vector<std::unique_ptr<TabState>> tab_states;
    int active_tab = -1;

    // IOCP for networking
    HANDLE iocp = INVALID_HANDLE_VALUE;
    WorldDB worlddb;

    // Tab management
    int AddWorld(const std::string& name);
    int ConnectWorld(const std::string& name, const std::string& host,
                     const std::string& port, bool ssl);
    void RemoveWorld(int index);
    void SwitchToTab(int index);
    void OnInputSubmitted(const std::string& line);

    // Networking
    void DrainIOCP();
    void CheckPrompts();
    void UpdateStatusBar();

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
