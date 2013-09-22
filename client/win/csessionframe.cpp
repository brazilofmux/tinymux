#include "stdafx.h"

LRESULT CSessionFrame::OnDestroy(void)
{
    CMainFrame *pWnd = (CMainFrame *)m_pParentWindow;
    (void)pWnd->IncreaseDecreaseSessionCount(false);
    return 0;
}

void CSessionFrame::OnSize(UINT nType, int cx, int cy)
{
    int cyInput  = cy/4;
    int cyOutput = cy - cyInput;
    m_pOutputWindow->MoveWindow(0, 0, cx, cyOutput);
    m_pInputWindow->MoveWindow(0, cyOutput, cx, cyInput);
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

    case WM_DESTROY:
        lRes = pWnd->OnDestroy();
        break;

    case WM_SIZE:
        pWnd->OnSize(wParam, LOWORD(lParam), HIWORD(lParam));
        lRes = pWnd->DefaultMDIChildHandler(message, wParam, lParam);
        break;

    case WM_SETFOCUS:
        ::SetFocus(pWnd->m_pInputWindow->m_hwndRichEdit);
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
        break;
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

    m_pOutputWindow = new (std::nothrow) COutputFrame;;
    m_pInputWindow = new (std::nothrow) CInputFrame;

    if (  NULL == m_pOutputWindow
       || NULL == m_pInputWindow)
    {
        delete m_pOutputWindow;
        delete m_pInputWindow;
        return 1;
    }

    int cyInput  = pcs->cy/4;
    int cyOutput = pcs->cy - cyInput;
    if (  !m_pOutputWindow->Create(this, 0, 0, pcs->cx, cyOutput)
       || !m_pInputWindow->Create(this, 0, cyOutput, pcs->cx, cyInput))
    {
        return 1;
    }
    return 0;
}

CSessionFrame::CSessionFrame()
{
    m_pParentWindow = NULL;
    m_pOutputWindow = NULL;
    m_pInputWindow  = NULL;
}

CSessionFrame::~CSessionFrame()
{
}
