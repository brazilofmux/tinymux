// main.cpp -- Entry point for the Win32 GUI MU* client.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>

#include "window.h"
#include "mainframe.h"
#include "../res/resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // Enable visual styles
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_TAB_CLASSES };
    InitCommonControlsEx(&icc);

    // Set console code page for UTF-8
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    // Install CBT hook
    if (!InstallCBTHook()) return 1;

    // Register window classes
    CMainFrame::Register(hInstance);
    COutputPane::Register(hInstance);
    CInputPane::Register(hInstance);
    CTabBar::Register(hInstance);
    CStatusBar::Register(hInstance);

    // Create main frame
    CMainFrame frame;
    if (!frame.Create(hInstance, nCmdShow)) {
        RemoveCBTHook();
        return 1;
    }

    // Load accelerator table
    HACCEL hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_ACCEL));

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (hAccel && TranslateAcceleratorW(frame.hwnd(), hAccel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    RemoveCBTHook();
    return (int)msg.wParam;
}
