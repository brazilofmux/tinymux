// ritsu.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "resource.h"

#define MAX_LOADSTRING 100

const int StartChildren = 3000;

class CRitsuApp
{
public:
    CRitsuApp();

    bool Initialize(HINSTANCE hInstance, int nCmdShow);
    bool Finalize(void);

    ~CRitsuApp() {};

//private:

    HINSTANCE   m_hInstance;
    ATOM        m_atmMainFrame;
    ATOM        m_atmChildFrame;
    TCHAR       m_szHello[MAX_LOADSTRING];
    HWND        m_hMainFrame;
    HWND        m_hChildFrame;
};

// Global Variables:
//
CRitsuApp g_theApp;

int APIENTRY wWinMain
(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR     lpCmdLine,
    int       nCmdShow
)
{
    if (!g_theApp.Initialize(hInstance, nCmdShow))
    {
        g_theApp.Finalize();
        return 0;
    }

    // Main message loop.
    //
    MSG msg;
    BOOL bRet;
    HACCEL hAccelTable = LoadAccelerators(hInstance, (LPCTSTR)IDC_RITSU);
    while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0)
    {
        if (bRet == -1)
        {
            break;
        }
        else if (  !TranslateMDISysAccel(g_theApp.m_hChildFrame, &msg)
                && !TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }
    g_theApp.m_hMainFrame = NULL;
    DestroyAcceleratorTable(hAccelTable);
    g_theApp.Finalize();
    return msg.wParam;
}

// Mesage handler for about box.
//
LRESULT CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        return TRUE;

    case WM_COMMAND:
        if (  IDOK == LOWORD(wParam)
           || IDCANCEL == LOWORD(wParam))
        {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

void CreateNewMDIChild(HWND hChildWnd)
{
    MDICREATESTRUCT mcs;
    memset(&mcs, 0, sizeof(mcs));
    mcs.szTitle = _T("Untitled");
    mcs.szClass = _T("RitsuChildFrame");
    mcs.hOwner = g_theApp.m_hInstance;
    mcs.x = mcs.cx = CW_USEDEFAULT;
    mcs.y = mcs.cy = CW_USEDEFAULT;
    mcs.style = MDIS_ALLCHILDSTYLES;

    SendMessage(hChildWnd, WM_MDICREATE, 0, (LPARAM)&mcs);
}

//
//  FUNCTION: MainWndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        {
            // Create Child Frame Window.
            //
            CLIENTCREATESTRUCT ccs;
            memset(&ccs, 0, sizeof(ccs));
            ccs.hWindowMenu = GetSubMenu(GetMenu(hWnd), 1);
            ccs.idFirstChild = StartChildren;
            HWND hChildWnd = CreateWindowEx(WS_EX_CLIENTEDGE, _T("MDICLIENT"), NULL, WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL | WS_VISIBLE,
                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, hWnd, (HMENU)IDM_FILE_NEW, g_theApp.m_hInstance, &ccs);

            if (NULL != hChildWnd)
            {
                g_theApp.m_hChildFrame = hChildWnd;
                CreateNewMDIChild(g_theApp.m_hChildFrame);
                ShowWindow(hChildWnd, SW_SHOW);
            }
        }
        break;

    case WM_COMMAND:
        {
            // Parse the menu selections:
            //
            int wmId    = LOWORD(wParam);
            int wmEvent = HIWORD(wParam);
            switch (wmId)
            {
            case IDM_FILE_NEW:
                CreateNewMDIChild(g_theApp.m_hChildFrame);
                break;

            case IDM_FILE_CLOSE:
                {
                    HWND hWndCurrent = (HWND)SendMessage(g_theApp.m_hChildFrame, WM_MDIGETACTIVE, 0, 0);
                    if (NULL != hWndCurrent)
                    {
                        SendMessage(hWndCurrent, WM_CLOSE, 0, 0);
                    }
                }
                break;

            case IDM_ABOUT:
                DialogBox(g_theApp.m_hInstance, (LPCTSTR)IDD_ABOUTBOX, hWnd, (DLGPROC)About);
                break;

            case IDM_EXIT:
                DestroyWindow(hWnd);
                break;

            case IDM_WINDOW_TILE:
                SendMessage(g_theApp.m_hChildFrame, WM_MDITILE, 0, 0);
                break;

            case IDM_WINDOW_CASCADE:
                SendMessage(g_theApp.m_hChildFrame, WM_MDICASCADE, 0, 0);
                break;

            case IDM_WINDOW_ARRANGE:
                SendMessage(g_theApp.m_hChildFrame, WM_MDIICONARRANGE, 0, 0);
                break;

            case IDM_WINDOW_CLOSE_ALL:
                {
                    HWND hWndCurrent;

                    do {
                        hWndCurrent = (HWND)SendMessage(g_theApp.m_hChildFrame, WM_MDIGETACTIVE,0,0);
                        if (NULL != hWndCurrent)
                        {
                            SendMessage(hWndCurrent, WM_CLOSE, 0, 0);
                        }
                    } while (NULL != hWndCurrent);
                }
                break;

            default:
                if (StartChildren <= wmId)
                {
                    return DefFrameProc(hWnd, g_theApp.m_hChildFrame, WM_COMMAND, wParam, lParam);
                }
                else
                {
                    HWND hWndCurrent = (HWND)SendMessage(g_theApp.m_hChildFrame, WM_MDIGETACTIVE, 0, 0);
                    if (NULL != hWndCurrent)
                    {
                        SendMessage(hWndCurrent, WM_COMMAND, wParam, lParam);
                    }
                }
            }
        }
        break;

    case WM_DESTROY:

        PostQuitMessage(0);
        break;

    default:

        return DefFrameProc(hWnd, g_theApp.m_hChildFrame, message, wParam, lParam);
   }
   return 0;
}

LRESULT CALLBACK ChildWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        break;

    case WM_MDIACTIVATE:
        {
            HMENU hMenu, hFileMenu;
            UINT EnableFlag;

            hMenu = GetMenu(g_theApp.m_hMainFrame);
            if (hWnd == (HWND)lParam)
            {
                EnableFlag = MF_ENABLED;
            }
            else
            {
                EnableFlag = MF_GRAYED;
            }

            EnableMenuItem(hMenu, 1, MF_BYPOSITION | EnableFlag);

            hFileMenu = GetSubMenu(hMenu, 0);

            EnableMenuItem(hFileMenu, IDM_FILE_CLOSE, MF_BYCOMMAND | EnableFlag);
            EnableMenuItem(hFileMenu, IDM_WINDOW_CLOSE_ALL, MF_BYCOMMAND | EnableFlag);

            DrawMenuBar(g_theApp.m_hMainFrame);
        }
        break;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rt;
            GetClientRect(hWnd, &rt);
            DrawText(hdc, g_theApp.m_szHello, wcslen(g_theApp.m_szHello), &rt, DT_CENTER);
            EndPaint(hWnd, &ps);
        }
        break;
    default:

        return DefMDIChildProc(hWnd, message, wParam, lParam);
   }
   return 0;
}

CRitsuApp::CRitsuApp()
{
    m_hInstance     = NULL;
    m_hMainFrame    = NULL;
    m_hChildFrame   = NULL;
    m_atmChildFrame = 0;
    m_atmMainFrame  = 0;
}

bool CRitsuApp::Initialize(HINSTANCE hInstance, int nCmdShow)
{
    m_hInstance  = hInstance;

    TCHAR szMainFrameClass[MAX_LOADSTRING];
    LoadString(hInstance, IDC_MAIN_FRAME, szMainFrameClass, MAX_LOADSTRING);

    // Register Main Frame Window class.
    //
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = (WNDPROC)MainWndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, (LPCTSTR)IDI_RITSU);
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = (LPCTSTR)IDC_RITSU;
    wcex.lpszClassName  = szMainFrameClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, (LPCTSTR)IDI_SMALL);

    ATOM atm = RegisterClassEx(&wcex);
    if (0 == atm)
    {
        return false;
    }
    m_atmMainFrame = atm;

    // Register Child Frame Window class.
    //
    wcex.lpfnWndProc   = (WNDPROC) ChildWndProc;
    wcex.hIcon         = LoadIcon(hInstance, (LPCTSTR)IDI_BACKSCROLL);
    wcex.lpszMenuName  = (LPCTSTR) NULL;
    wcex.lpszClassName = _T("RitsuChildFrame");
    wcex.hIconSm       = LoadIcon(hInstance, (LPCTSTR)IDI_BACKSCROLL);

    atm = RegisterClassEx(&wcex);
    if (0 == atm)
    {
        return false;
    }
    m_atmChildFrame = atm;

    // Create Main Frame Window.
    //
    TCHAR szTitle[MAX_LOADSTRING];
    LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadString(hInstance, IDS_HELLO, m_szHello, MAX_LOADSTRING);

    HWND hWnd = CreateWindowEx(0L, szMainFrameClass, szTitle, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, hInstance, NULL);

    if (!hWnd)
    {
        return false;
    }
    m_hMainFrame = hWnd;

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    return true;
}

bool CRitsuApp::Finalize(void)
{
    bool b;
    if (0 != m_atmMainFrame)
    {
        if (FALSE == UnregisterClass(MAKEINTATOM(m_atmMainFrame), m_hInstance))
        {
            b = false;
        }
        m_atmMainFrame = 0;
    }

    if (0 != m_atmChildFrame)
    {
        if (FALSE == UnregisterClass(MAKEINTATOM(m_atmChildFrame), m_hInstance))
        {
            b = false;
        }
        m_atmChildFrame = 0;
    }
    return b;
}
