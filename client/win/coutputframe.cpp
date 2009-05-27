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
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_CHILD | WS_BORDER | WS_CLIPCHILDREN | WS_VISIBLE | WS_VSCROLL,
            pcs->x, pcs->y, pcs->cx, pcs->cy,
            m_hwnd, NULL, g_theApp.m_hRichEdit, NULL);
    }
    else
    {
        // Rich Edit 2.0 (98/NT4) or 3.0 (Me/2K/XP) control.
        //
        m_hwndRichEdit = ::CreateWindowEx(0L, RICHEDIT_CLASS, NULL,
            ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_CHILD | WS_BORDER | WS_CLIPCHILDREN | WS_VISIBLE | WS_VSCROLL,
            pcs->x, pcs->y, pcs->cx, pcs->cy,
            m_hwnd, NULL, g_theApp.m_hRichEdit, NULL);
    }
    if (NULL == m_hwndRichEdit)
    {
        return 1;
    }

    CHARFORMAT2 cf2;
    memset(&cf2, 0, sizeof(cf2));
    cf2.cbSize = sizeof(cf2);
    cf2.dwMask = CFM_FACE | CFM_COLOR | CFM_BACKCOLOR | CFM_SIZE;
    cf2.crTextColor = RGB(255,128,0);
    cf2.crBackColor = RGB(60,60,60);
    cf2.yHeight = 20*14;
    memcpy(cf2.szFaceName, L"Courier New", sizeof(L"Courier New"));
    LRESULT lRes = ::SendMessage(m_hwndRichEdit, EM_SETBKGNDCOLOR, 0, (LPARAM)cf2.crBackColor);
    if (g_theApp.m_bMsftEdit)
    {
        lRes = ::SendMessage(m_hwndRichEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM) &cf2);
    }
    else
    {
        lRes = ::SendMessage(m_hwndRichEdit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM) &cf2);
    }
    return 0;
}

void COutputFrame::OnSize(UINT nType, int cx, int cy)
{
    ::MoveWindow(m_hwndRichEdit, 0, 0, cx, cy, true);
}

typedef struct
{
    LONG   cb;
    LPBYTE pbBuff;
} STREAMFRAGMENT;

DWORD CALLBACK COutputFrame::EditStreamCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb)
{
    STREAMFRAGMENT *psf = (STREAMFRAGMENT *)dwCookie;
    LONG cbMove = cb;
    if (psf->cb < cbMove)
    {
        cbMove = psf->cb;
    }
    memcpy(pbBuff, psf->pbBuff, cbMove);
    *pcb = cbMove;
    psf->cb -= cbMove;
    psf->pbBuff += cbMove;
    return 0;
}

void COutputFrame::AppendText(size_t nBuffer, WCHAR *pBuffer)
{
    // Position to the end.
    //
    CHARRANGE cr;
    cr.cpMin = -1; // nLength;
    cr.cpMax = -1; // nLength;
    (void)::SendMessage(m_hwndRichEdit, EM_EXSETSEL, 0, (LPARAM)&cr);

    STREAMFRAGMENT sf;
    sf.cb = nBuffer*sizeof(WCHAR);
    sf.pbBuff = (BYTE *)pBuffer;

    EDITSTREAM es;
    es.dwCookie = (DWORD_PTR)&sf;
    es.dwError  = ERROR_SUCCESS;
    es.pfnCallback = COutputFrame::EditStreamCallback;
    (void)::SendMessage(m_hwndRichEdit, EM_STREAMIN, SF_TEXT | SF_UNICODE | SFF_SELECTION, (LPARAM)&es);

    // Position view to the end.
    //
    (void)::SendMessage(m_hwndRichEdit, WM_VSCROLL, SB_BOTTOM, NULL);
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
