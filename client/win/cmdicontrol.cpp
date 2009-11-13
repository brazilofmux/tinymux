#include "stdafx.h"

CMDIControl::CMDIControl(CWindow *pParentWindow, CLIENTCREATESTRUCT *pccs, CREATESTRUCT *pcs)
{
    HWND hChildControl = ::CreateWindowEx(WS_EX_CLIENTEDGE, L"mdiclient", NULL,
        WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL | WS_VISIBLE,
        pcs->x, pcs->y, pcs->cx, pcs->cy, pParentWindow->m_hwnd,
        (HMENU)IDC_MAIN_MDI, g_theApp.m_hInstance, pccs);

    if (NULL != hChildControl)
    {
        m_pParentWindow = pParentWindow;
        m_hwnd = hChildControl;
    }
}

CSessionFrame *CMDIControl::CreateNewChild(void)
{
    CSessionFrame *pChild = new (std::nothrow) CSessionFrame;
    if (NULL != pChild)
    {
        // The Child Window sets the m_pParentWindow in its OnCreate().
        //
        CreateParams cp;
        cp.pParentWindow = m_pParentWindow;
        cp.pWindow = pChild;

        MDICREATESTRUCT mcs;
        memset(&mcs, 0, sizeof(mcs));
        mcs.szTitle = L"Untitled";
        mcs.szClass = g_theApp.m_szSessionClass;
        mcs.hOwner = g_theApp.m_hInstance;
        mcs.x = mcs.cx = CW_USEDEFAULT;
        mcs.y = mcs.cy = CW_USEDEFAULT;
        mcs.style = MDIS_ALLCHILDSTYLES;
        mcs.lParam = (LPARAM)&cp;

        g_theApp.m_pTemp = pChild;
        (void)::SendMessage(m_hwnd, WM_MDICREATE, 0, (LPARAM)&mcs);
    }
    return pChild;
}

CSessionFrame *CMDIControl::GetActive(void)
{
    CSessionFrame *pWnd = NULL;
    HWND hwnd = (HWND)::SendMessage(m_hwnd, WM_MDIGETACTIVE, 0, 0);
    if (NULL != hwnd)
    {
        pWnd = static_cast<CSessionFrame *>(GetWindowPointer(hwnd));
    }
    return pWnd;
}

LRESULT CMDIControl::Tile(void)
{
    return ::SendMessage(m_hwnd, WM_MDITILE, 0, 0);
}

LRESULT CMDIControl::Cascade(void)
{
    return ::SendMessage(m_hwnd, WM_MDICASCADE, 0, 0);
}

LRESULT CMDIControl::IconArrange(void)
{
    return ::SendMessage(m_hwnd, WM_MDIICONARRANGE, 0, 0);
}
