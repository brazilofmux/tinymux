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

    if (g_theApp.m_bMsftEdit)
    {
        // Rich Edit 4.1 is available on Windows XP SP1 and later.
        //
        m_hwndRichEdit = ::CreateWindowEx(0L, MSFTEDIT_CLASS, NULL,
            ES_MULTILINE | ES_AUTOVSCROLL | WS_CHILD | WS_BORDER | WS_CLIPCHILDREN | WS_VISIBLE | WS_VSCROLL,
            pcs->x, pcs->y, pcs->cx, pcs->cy,
            m_hwnd, NULL, g_theApp.m_hRichEdit, NULL);
    }
    else
    {
        // Rich Edit 2.0 (98/NT4) or 3.0 (Me/2K/XP) control.
        //
        m_hwndRichEdit = ::CreateWindowEx(0L, RICHEDIT_CLASS, NULL,
            ES_MULTILINE | ES_AUTOVSCROLL | WS_CHILD | WS_BORDER | WS_CLIPCHILDREN | WS_VISIBLE | WS_VSCROLL,
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
    lRes = ::SendMessage(m_hwndRichEdit, EM_SETTEXTMODE, TM_PLAINTEXT, 0);
    if (g_theApp.m_bMsftEdit)
    {
        lRes = ::SendMessage(m_hwndRichEdit, EM_SETCHARFORMAT, SCF_ALL, (LPARAM) &cf2);
    }
    else
    {
        lRes = ::SendMessage(m_hwndRichEdit, EM_SETCHARFORMAT, SCF_DEFAULT, (LPARAM) &cf2);
    }
    lRes = ::SendMessage(m_hwndRichEdit, EM_SETEVENTMASK, SCF_DEFAULT, ENM_KEYEVENTS);
    lRes = ::SendMessage(m_hwndRichEdit, EM_EXLIMITTEXT, 0, 20*1024*1024);
    return 0;
}

void CInputFrame::OnSize(UINT nType, int cx, int cy)
{
    ::MoveWindow(m_hwndRichEdit, 0, 0, cx, cy, true);
}

DWORD CALLBACK CInputFrame::EditStreamCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb)
{
    *pcb = cb;
    if ((cb % sizeof(WCHAR)) == 0)
    {
        size_t nBuffer = cb/sizeof(WCHAR);
        WCHAR *pBuffer = (WCHAR *)pbBuff;
        if (  2 != nBuffer
           || pBuffer[0] != L'\r'
           || pBuffer[1] != L'\n')
        {
            CInputFrame *pWndInput = (CInputFrame *)dwCookie;
            CSessionFrame *pWndSession = (CSessionFrame *)pWndInput->m_pParentWindow;
            if (!pWndInput->m_bFirst)
            {
                pWndSession->m_pOutputWindow->AppendText(2, L"\r\n");
            }
            pWndInput->m_bFirst = false;
            pWndSession->m_pOutputWindow->AppendText(nBuffer, pBuffer);
        }
    }
    return 0;
}

LRESULT CInputFrame::OnNotify(NMHDR *phdr)
{
    if (EN_MSGFILTER == phdr->code)
    {
        MSGFILTER *pmsgflt = (MSGFILTER *)phdr;
        if ('\r' == pmsgflt->wParam)
        {
            // Stream it out.
            //
            EDITSTREAM es;
            es.dwCookie = (DWORD_PTR)this;
            es.dwError  = ERROR_SUCCESS;
            es.pfnCallback = CInputFrame::EditStreamCallback;
            (void)::SendMessage(m_hwndRichEdit, EM_STREAMOUT, SF_TEXT|SF_UNICODE, (LPARAM)&es);

            LRESULT lRes = ::SendMessage(m_hwndRichEdit, WM_SETTEXT, 0, NULL);
            return 1;
        }
        return 0;
    }
    return 0;
}

CInputFrame::CInputFrame()
{
    m_pParentWindow = NULL;
    m_hwndRichEdit  = NULL;
    m_bFirst = true;
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
    case WM_NOTIFY:
        {
            NMHDR *phdr = (NMHDR *)lParam;
            pWnd->OnNotify(phdr);
        }
        break;

    case WM_CREATE:
        {
            CREATESTRUCT *pcs = (CREATESTRUCT *)lParam;
            lRes = pWnd->OnCreate(pcs);
        }
        break;

    case WM_ACTIVATE:
    case WM_SETFOCUS:
        ::SetFocus(pWnd->m_hwndRichEdit);
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
