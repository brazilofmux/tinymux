#include "stdafx.h"

LRESULT CChildFrame::OnDestroy(void)
{
    (void)m_pParentWindow->IncreaseDecreaseChildCount(false);
    return 0;
}

LRESULT CChildFrame::OnPaint(void)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);
    RECT rt;
    GetClientRect(m_hwnd, &rt);
    FillRect(hdc, &rt, (HBRUSH)(COLOR_WINDOW+1));
    DrawText(hdc, m_szHello, wcslen(m_szHello), &rt, DT_CENTER);
    EndPaint(m_hwnd, &ps);
    return 0;
}

LRESULT CALLBACK CChildFrame::ChildWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LRESULT lRes = 0;
    CChildFrame *pWnd = (CChildFrame *)GetWindowPointer(hWnd);

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

    case WM_DESTROY:
        lRes = pWnd->OnDestroy();

    case WM_NCDESTROY:
        {
            lRes = DefMDIChildProc(hWnd, message, wParam, lParam);
            (void)Detach(hWnd);
            delete pWnd;
            pWnd = NULL;
        }
        break;
    
    default:
        lRes = DefMDIChildProc(hWnd, message, wParam, lParam);
    }
    return lRes;
}

LRESULT CChildFrame::OnCreate(CREATESTRUCT *pcs)
{
    MDICREATESTRUCT *pmdics = (MDICREATESTRUCT *)pcs->lpCreateParams;
    if (NULL != pmdics)
    {
        CreateParams *pcp = (CreateParams *)pmdics->lParam;
        if (NULL != pcp)
        {
            m_pParentWindow = (CMainFrame *)pcp->pParentWindow;
        }
    }
    (void)m_pParentWindow->IncreaseDecreaseChildCount(true);
    g_theApp.LoadString(IDS_HELLO, m_szHello, MAX_LOADSTRING);
    return 0;
}

CChildFrame::CChildFrame()
{
    m_pParentWindow = NULL;
    m_szHello[0] = L'\0';
}

CChildFrame::~CChildFrame()
{
}
