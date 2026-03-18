// main.cpp -- Entry point for the Win32 GUI MU* client (Titan).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winsock2.h>
#include <windowsx.h>
#include <commctrl.h>

#include "window.h"
#include "mainframe.h"
#include "../res/resource.h"

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ws2_32.lib")

// Timer ID for periodic IOCP polling / prompt checks / status updates.
#define IDT_POLL 1

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int nCmdShow) {
    // Initialize Winsock
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return 1;

    // Enable visual styles
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_BAR_CLASSES | ICC_TAB_CLASSES };
    InitCommonControlsEx(&icc);

    // Install CBT hook
    if (!InstallCBTHook()) { WSACleanup(); return 1; }

    // Register window classes
    CMainFrame::Register(hInstance);
    COutputPane::Register(hInstance);
    CInputPane::Register(hInstance);
    CTabBar::Register(hInstance);
    CStatusBar::Register(hInstance);

    // Create main frame
    CMainFrame frame;
    frame.iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (!frame.iocp || !frame.Create(hInstance, nCmdShow)) {
        RemoveCBTHook();
        WSACleanup();
        return 1;
    }

    // Timer for prompt detection and status bar updates.
    // Network I/O arrives via WM_APP_IOCP from the IOCP thread.
    SetTimer(frame.hwnd(), IDT_POLL, 500, nullptr);

    // Load accelerator table
    HACCEL hAccel = LoadAcceleratorsW(hInstance, MAKEINTRESOURCEW(IDR_ACCEL));

    // Standard message loop — IOCP is polled via WM_TIMER.
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        if (hAccel && TranslateAcceleratorW(frame.hwnd(), hAccel, &msg)) continue;
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    KillTimer(frame.hwnd(), IDT_POLL);
    if (frame.iocp) CloseHandle(frame.iocp);
    RemoveCBTHook();
    WSACleanup();
    return (int)msg.wParam;
}
