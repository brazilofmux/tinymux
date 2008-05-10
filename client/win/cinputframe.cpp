#include "stdafx.h"

bool CInputFrame::Create(CWindow *pParentWindow, int x, int y, int cx, int cy)
{
    // Create Input Frame Window.
    //
    WCHAR szTitle[MAX_LOADSTRING];
    g_theApp.LoadString(IDS_APP_TITLE, szTitle, MAX_LOADSTRING);

    cx -= 2 * ::GetSystemMetrics(SM_CXFRAME);
    cy -= 2 * ::GetSystemMetrics(SM_CYFRAME);

    CreateParams cp;
    cp.pWindow = this;
    cp.pParentWindow = pParentWindow;
    g_theApp.m_pTemp = this;
    HWND hWnd = ::CreateWindowEx(0L, g_theApp.m_szInputClass, szTitle,
        WS_CHILD | WS_BORDER | WS_CLIPCHILDREN | WS_VISIBLE,
        x, y, cx, cy,
        pParentWindow->m_hwnd, NULL, g_theApp.m_hInstance, &cp);

    if (NULL == hWnd)
    {
        return false;
    }
    return true;
}

LRESULT CInputFrame::OnCreate(CREATESTRUCT *pcs)
{
    CreateParams *pcp = (CreateParams *)pcs->lpCreateParams;
    if (NULL != pcp)
    {
        m_pParentWindow = pcp->pParentWindow;
    }
    g_theApp.LoadString(IDS_HELLO, m_szHello, MAX_LOADSTRING);
    return 0;
}

LRESULT CInputFrame::OnPaint(void)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);
    RECT rt;
    GetClientRect(m_hwnd, &rt);
    FillRect(hdc, &rt, g_theApp.m_brushBlack);
    DrawText(hdc, m_szHello, wcslen(m_szHello), &rt, DT_CENTER);
    EndPaint(m_hwnd, &ps);
    return 0;
}

CInputFrame::CInputFrame()
{
    m_pParentWindow = NULL;
    m_szHello[0] = L'\0';
}

CInputFrame::~CInputFrame()
{
}

LRESULT CALLBACK CInputFrame::InputWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LRESULT lRes = 0;
    CInputFrame *pWnd = (CInputFrame *)GetWindowPointer(hWnd);

    switch (message)
    {
    case WM_CREATE:
        {
            CREATESTRUCT *pcs = (CREATESTRUCT *)lParam;
            lRes = pWnd->OnCreate(pcs);
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        lRes = pWnd->OnPaint();
        break;

    case WM_NCDESTROY:
        {
            lRes = pWnd->DefaultWindowHandler(message, wParam, lParam);
            (void)Detach(hWnd);
            delete pWnd;
            pWnd = NULL;
        }
        break;
    
    default:
        lRes = pWnd->DefaultWindowHandler(message, wParam, lParam);
    }
    return lRes;
}
