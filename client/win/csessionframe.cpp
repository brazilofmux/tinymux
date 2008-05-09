#include "stdafx.h"

LRESULT CSessionFrame::OnDestroy(void)
{
    CMainFrame *pWnd = (CMainFrame *)m_pParentWindow;
    (void)pWnd->IncreaseDecreaseSessionCount(false);
    return 0;
}

LRESULT CSessionFrame::OnPaint(void)
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

LRESULT CALLBACK CSessionFrame::SessionWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    LRESULT lRes = 0;
    CSessionFrame *pWnd = (CSessionFrame *)GetWindowPointer(hWnd);

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
        break;

    case WM_NCDESTROY:
        {
            lRes = pWnd->DefaultMDIChildHandler(message, wParam, lParam);
            (void)Detach(hWnd);
            delete pWnd;
            pWnd = NULL;
        }
        break;
    
    default:
        lRes = pWnd->DefaultMDIChildHandler(message, wParam, lParam);
    }
    return lRes;
}

LRESULT CSessionFrame::OnCreate(CREATESTRUCT *pcs)
{
    MDICREATESTRUCT *pmdics = (MDICREATESTRUCT *)pcs->lpCreateParams;
    if (NULL != pmdics)
    {
        CreateParams *pcp = (CreateParams *)pmdics->lParam;
        if (NULL != pcp)
        {
            m_pParentWindow = pcp->pParentWindow;
            CMainFrame *pWnd = (CMainFrame *)m_pParentWindow;
            (void)pWnd->IncreaseDecreaseSessionCount(true);
        }
    }
    g_theApp.LoadString(IDS_HELLO, m_szHello, MAX_LOADSTRING);
    return 0;
}

CSessionFrame::CSessionFrame()
{
    m_pParentWindow = NULL;
    m_szHello[0] = L'\0';
}

CSessionFrame::~CSessionFrame()
{
}
