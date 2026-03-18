// window.h -- CWindow base class with CBT hook for HWND<->object association.
#ifndef WINDOW_H
#define WINDOW_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

class CWindow {
public:
    CWindow();
    virtual ~CWindow();

    HWND hwnd() const { return m_hwnd; }

    LRESULT SendMsg(UINT msg, WPARAM wParam, LPARAM lParam);
    void Show(int nCmdShow);
    void Invalidate(bool erase = false);

protected:
    LRESULT DefProc(UINT msg, WPARAM wParam, LPARAM lParam);

    HWND     m_hwnd = nullptr;
    CWindow* m_parent = nullptr;

    friend class CApp;
    friend CWindow* WindowFromHWND(HWND hwnd);
    friend void AttachWindow(HWND hwnd, CWindow* wnd);
    friend CWindow* DetachWindow(HWND hwnd);
};

// Retrieve the CWindow pointer associated with an HWND.
inline CWindow* WindowFromHWND(HWND hwnd) {
    return reinterpret_cast<CWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

// Associate a CWindow pointer with an HWND.
inline void AttachWindow(HWND hwnd, CWindow* wnd) {
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(wnd));
    wnd->m_hwnd = hwnd;
}

// Remove the CWindow association.
inline CWindow* DetachWindow(HWND hwnd) {
    CWindow* wnd = WindowFromHWND(hwnd);
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
    return wnd;
}

// CBT hook management — install once at startup, remove at shutdown.
// The hook intercepts HCBT_CREATEWND to call AttachWindow before
// the first WM_NCCREATE, so the WndProc always has a valid pointer.
bool InstallCBTHook();
void RemoveCBTHook();

// Set the "next window to create" pointer — must be called immediately
// before CreateWindowEx.
void SetPendingWindow(CWindow* wnd);

#endif // WINDOW_H
