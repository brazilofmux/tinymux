// mainframe.h -- Top-level frame window: owns tab bar, output, input, status.
#ifndef MAINFRAME_H
#define MAINFRAME_H

#include "window.h"
#include "tabbar.h"
#include "outputpane.h"
#include "inputpane.h"
#include "statusbar.h"
#include "outputbuffer.h"
#include <memory>
#include <vector>
#include <string>

// Menu resource ID (matches res/app.rc)
#define IDR_MAINMENU 1

class CMainFrame : public CWindow {
public:
    static const wchar_t* ClassName();
    static bool Register(HINSTANCE hInst);
    bool Create(HINSTANCE hInst, int nCmdShow);

    CTabBar      tabbar;
    COutputPane  output;
    CInputPane   input;
    CStatusBar   status;

    // Per-tab output buffers
    struct TabState {
        std::string name;
        OutputBuffer buffer;
    };
    std::vector<std::unique_ptr<TabState>> tab_states;
    int active_tab = -1;

    // Add a tab and create its output buffer.
    int AddWorld(const std::string& name);
    void RemoveWorld(int index);
    void SwitchToTab(int index);

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
