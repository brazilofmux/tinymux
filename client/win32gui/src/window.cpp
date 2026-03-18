// window.cpp -- CWindow base class and CBT hook implementation.
#include "window.h"

// Global state for the CBT hook.
static HHOOK g_hhk = nullptr;
static CWindow* g_pending = nullptr;

static LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HCBT_CREATEWND && g_pending != nullptr) {
        HWND hwnd = (HWND)wParam;
        AttachWindow(hwnd, g_pending);
        g_pending = nullptr;
    }
    return CallNextHookEx(g_hhk, nCode, wParam, lParam);
}

bool InstallCBTHook() {
    g_hhk = SetWindowsHookExW(WH_CBT, CBTProc, nullptr, GetCurrentThreadId());
    return g_hhk != nullptr;
}

void RemoveCBTHook() {
    if (g_hhk) {
        UnhookWindowsHookEx(g_hhk);
        g_hhk = nullptr;
    }
}

void SetPendingWindow(CWindow* wnd) {
    g_pending = wnd;
}

// -- CWindow --

CWindow::CWindow() {}
CWindow::~CWindow() {}

LRESULT CWindow::SendMsg(UINT msg, WPARAM wParam, LPARAM lParam) {
    return ::SendMessageW(m_hwnd, msg, wParam, lParam);
}

void CWindow::Show(int nCmdShow) {
    ::ShowWindow(m_hwnd, nCmdShow);
}

void CWindow::Invalidate(bool erase) {
    ::InvalidateRect(m_hwnd, nullptr, erase ? TRUE : FALSE);
}

LRESULT CWindow::DefProc(UINT msg, WPARAM wParam, LPARAM lParam) {
    return ::DefWindowProcW(m_hwnd, msg, wParam, lParam);
}
