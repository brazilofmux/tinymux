#include "stdafx.h"

bool CInputFrame::Create(CWindow *pParentWindow, int x, int y, int cx, int cy)
{
    return false;
}

LRESULT CInputFrame::OnCreate(CREATESTRUCT *pcs)
{
    return 0;
}

CInputFrame::CInputFrame()
{
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
