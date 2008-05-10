#include "stdafx.h"

bool COutputFrame::Create(CWindow *pParentWindow, int x, int y, int cx, int cy)
{
    // Create Output Frame Window.
    //
    WCHAR szTitle[MAX_LOADSTRING];
    g_theApp.LoadString(IDS_APP_TITLE, szTitle, MAX_LOADSTRING);

    x  = 0;
    y  = 0;
    cx -= 2 * ::GetSystemMetrics(SM_CXFRAME);
    cy -= 2 * ::GetSystemMetrics(SM_CYFRAME);

    CreateParams cp;
    cp.pWindow = this;
    cp.pParentWindow = pParentWindow;
    g_theApp.m_pTemp = this;
    HWND hWnd = ::CreateWindowEx(0L, g_theApp.m_szOutputClass, szTitle,
        WS_CHILD | WS_BORDER | WS_CLIPCHILDREN | WS_VISIBLE,
        x, y, cx, cy,
        pParentWindow->m_hwnd, NULL, g_theApp.m_hInstance, &cp);

    if (NULL == hWnd)
    {
        return false;
    }
    return true;
}

LRESULT COutputFrame::OnCreate(CREATESTRUCT *pcs)
{
    CreateParams *pcp = (CreateParams *)pcs->lpCreateParams;
    if (NULL != pcp)
    {
        m_pParentWindow = pcp->pParentWindow;
    }

    if (g_theApp.m_bMsftEdit)
    {
        // Rich Edit 4.1 is available on Windows XP SP1 and later.
        //
        m_hwndRichEdit = ::CreateWindowEx(0L, MSFTEDIT_CLASS, NULL,
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_CHILD | WS_BORDER | WS_CLIPCHILDREN | WS_VISIBLE,
            pcs->x, pcs->y, pcs->cx, pcs->cy,
            m_hwnd, NULL, g_theApp.m_hRichEdit, NULL);
    }
    else
    {
        // Rich Edit 2.0 (98/NT4) or 3.0 (Me/2K/XP) control.
        //
        m_hwndRichEdit = ::CreateWindowEx(0L, RICHEDIT_CLASS, NULL,
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_CHILD | WS_BORDER | WS_CLIPCHILDREN | WS_VISIBLE,
            pcs->x, pcs->y, pcs->cx, pcs->cy,
            m_hwnd, NULL, g_theApp.m_hRichEdit, NULL);
    }
    return 0;
}

void COutputFrame::OnSize(UINT nType, int cx, int cy)
{
    ::MoveWindow(m_hwndRichEdit, 0, 0, cx, cy, true);
}

COutputFrame::COutputFrame()
{
    m_pParentWindow = NULL;
    m_hwndRichEdit = NULL;
}

COutputFrame::~COutputFrame()
{
}

LRESULT CALLBACK COutputFrame::OutputWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LRESULT lRes = 0;
    COutputFrame *pWnd = (COutputFrame *)GetWindowPointer(hWnd);

    switch (message)
    {
    case WM_CREATE:
        {
            CREATESTRUCT *pcs = (CREATESTRUCT *)lParam;
            lRes = pWnd->OnCreate(pcs);
        }
        break;

    case WM_SIZE:
        pWnd->OnSize(wParam, LOWORD(lParam), HIWORD(lParam));
        lRes = pWnd->DefaultWindowHandler(message, wParam, lParam);
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
