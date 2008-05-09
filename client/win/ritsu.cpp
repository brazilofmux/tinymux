// ritsu.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "resource.h"

#define MAX_LOADSTRING 100

// This rounds up to the nearest multiple of sizeof(long)
//
#define LONGCEIL(x) (((x) + sizeof(long) - 1) & (~(sizeof(long) - 1)))

const int StartChildren = 3000;

class CMainFrame
{
public:

    CMainFrame();
    void Initialize(void);
    bool RegisterClasses(HINSTANCE hInstance);
    bool CreateWindows(HINSTANCE hInstance);
    void SaveWindowPointer(void);
    void ShowWindow(int nCmdShow) { ::ShowWindow(m_hMainFrame, nCmdShow); }
    void UpdateWindow(void) { ::UpdateWindow(m_hMainFrame); }
    bool Finalize(HINSTANCE hInstance);
    ~CMainFrame();

    TCHAR       m_szMainFrameClass[MAX_LOADSTRING];
    TCHAR       m_szChildFrameClass[MAX_LOADSTRING];

    ATOM        m_atmMainFrame;
    ATOM        m_atmChildFrame;
    HWND        m_hMainFrame;
    HWND        m_hChildControl;

    // Document stuff.
    //
    TCHAR       m_szHello[MAX_LOADSTRING];
};

CMainFrame *GetMainFrame(HWND hwnd);

class CRitsuApp
{
public:
    CRitsuApp();

    bool   Initialize(HINSTANCE hInstance, int nCmdShow);
    WPARAM Run(void);
    bool   Finalize(void);

    ~CRitsuApp() {};

    // Application
    //
    HINSTANCE   m_hInstance;

    // Main Frame.
    //
    CMainFrame  *m_pMainFrame;
};

typedef struct
{
    union
    {
        CMainFrame *p;
        LONG l[(sizeof(CMainFrame *) + sizeof(long) - 1)/sizeof(long)];
    } u;
} ExtraWindowData;

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
    g_theApp.m_pMainFrame->ShowWindow(nCmdShow);
    g_theApp.m_pMainFrame->UpdateWindow();

    int ret = g_theApp.Run();
    g_theApp.Finalize();
    return ret;
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
        if (  IDOK     == LOWORD(wParam)
           || IDCANCEL == LOWORD(wParam))
        {
            EndDialog(hDlg, LOWORD(wParam));
            return TRUE;
        }
        break;
    }
    return FALSE;
}

HWND CreateNewMDIChild(HWND hChildControl)
{
    MDICREATESTRUCT mcs;
    memset(&mcs, 0, sizeof(mcs));
    mcs.szTitle = _T("Untitled");
    mcs.szClass = _T("RitsuChildFrame");
    mcs.hOwner = g_theApp.m_hInstance;
    mcs.x = mcs.cx = CW_USEDEFAULT;
    mcs.y = mcs.cy = CW_USEDEFAULT;
    mcs.style = MDIS_ALLCHILDSTYLES;

    HWND hChildWnd = (HWND)SendMessage(hChildControl, WM_MDICREATE, 0, (LPARAM)&mcs);
    return hChildWnd;
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
    CMainFrame *pWnd = GetMainFrame(hWnd);
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

            CREATESTRUCT *pcs = (CREATESTRUCT *)lParam;

            HWND hChildControl = CreateWindowEx(WS_EX_CLIENTEDGE, _T("mdiclient"), NULL,
                WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL | WS_VISIBLE,
                pcs->x, pcs->y, pcs->cx, pcs->cy, hWnd,
                (HMENU)IDC_MAIN_MDI, g_theApp.m_hInstance, &ccs);

            if (NULL != hChildControl)
            {
                g_theApp.m_pMainFrame->m_hChildControl = hChildControl;
                HWND hChildWnd = CreateNewMDIChild(hChildControl);
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
                CreateNewMDIChild(pWnd->m_hChildControl);
                break;

            case IDM_FILE_CLOSE:
                {
                    HWND hWndCurrent = (HWND)SendMessage(pWnd->m_hChildControl, WM_MDIGETACTIVE, 0, 0);
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
                SendMessage(pWnd->m_hChildControl, WM_MDITILE, 0, 0);
                break;

            case IDM_WINDOW_CASCADE:
                SendMessage(pWnd->m_hChildControl, WM_MDICASCADE, 0, 0);
                break;

            case IDM_WINDOW_ARRANGE:
                SendMessage(pWnd->m_hChildControl, WM_MDIICONARRANGE, 0, 0);
                break;

            case IDM_WINDOW_CLOSE_ALL:
                {
                    HWND hWndChild;

                    do {
                        hWndChild = (HWND)SendMessage(pWnd->m_hChildControl, WM_MDIGETACTIVE,0,0);
                        if (NULL != hWndChild)
                        {
                            SendMessage(hWndChild, WM_CLOSE, 0, 0);
                        }
                    } while (NULL != hWndChild);
                }
                break;

            default:
                if (StartChildren <= wmId)
                {
                    return DefFrameProc(hWnd, pWnd->m_hChildControl, WM_COMMAND, wParam, lParam);
                }
                else
                {
                    HWND hWndChild = (HWND)SendMessage(pWnd->m_hChildControl, WM_MDIGETACTIVE, 0, 0);
                    if (NULL != hWndChild)
                    {
                        SendMessage(hWndChild, WM_COMMAND, wParam, lParam);
                    }
                }
            }
        }
        break;

    case WM_DESTROY:

        PostQuitMessage(0);
        break;

    default:
        {
            HWND hChildControl = NULL;
            if (NULL != pWnd)
            {
                hChildControl = pWnd->m_hChildControl;
            }
            return DefFrameProc(hWnd, g_theApp.m_pMainFrame->m_hChildControl, message, wParam, lParam);
        }
        break;
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

            hMenu = GetMenu(g_theApp.m_pMainFrame->m_hMainFrame);
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

            DrawMenuBar(g_theApp.m_pMainFrame->m_hMainFrame);
        }
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rt;
            GetClientRect(hWnd, &rt);
            FillRect(hdc, &rt, (HBRUSH)(COLOR_WINDOW+1));
            DrawText(hdc, g_theApp.m_pMainFrame->m_szHello, wcslen(g_theApp.m_pMainFrame->m_szHello), &rt, DT_CENTER);
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
}

bool CRitsuApp::Initialize(HINSTANCE hInstance, int nCmdShow)
{
    m_hInstance  = hInstance;

    m_pMainFrame = NULL;
    try
    {
        m_pMainFrame = new CMainFrame;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (  NULL == m_pMainFrame
       || !m_pMainFrame->RegisterClasses(hInstance)
       || !m_pMainFrame->CreateWindows(hInstance))
    {
        return false;
    }
    return true;
}

bool CRitsuApp::Finalize(void)
{
    g_theApp.m_pMainFrame->Finalize(m_hInstance);
    delete g_theApp.m_pMainFrame;
    g_theApp.m_pMainFrame = NULL;
    return true;
}

WPARAM CRitsuApp::Run(void)
{
    // Main message loop.
    //
    MSG msg;
    BOOL bRet;
    HACCEL hAccelTable = LoadAccelerators(m_hInstance, (LPCTSTR)IDC_RITSU);
    while ((bRet = GetMessage(&msg, NULL, 0, 0)) != 0)
    {
        if (-1 == bRet)
        {
            break;
        }
        else if (  !TranslateMDISysAccel(g_theApp.m_pMainFrame->m_hChildControl, &msg)
                && !TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    DestroyAcceleratorTable(hAccelTable);
    return msg.wParam;
}

CMainFrame::CMainFrame()
{
    m_hMainFrame    = NULL;
    m_hChildControl = NULL;
    m_atmChildFrame = 0;
    m_atmMainFrame  = 0;
}

CMainFrame::~CMainFrame()
{
}

bool CMainFrame::RegisterClasses(HINSTANCE hInstance)
{
    LoadString(hInstance, IDC_MAIN_FRAME, m_szMainFrameClass, MAX_LOADSTRING);

    // Register Main Frame Window class.
    //
    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = 0; // CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = (WNDPROC)MainWndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = LONGCEIL(sizeof(ExtraWindowData));
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, (LPCTSTR)IDI_RITSU);
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = (LPCTSTR)IDC_RITSU;
    wcex.lpszClassName  = m_szMainFrameClass;
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
    return true;
}

bool CMainFrame::CreateWindows(HINSTANCE hInstance)
{
    // Create Main Frame Window.
    //
    TCHAR szTitle[MAX_LOADSTRING];
    LoadString(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadString(hInstance, IDS_HELLO, m_szHello, MAX_LOADSTRING);

    int xSize = ::GetSystemMetrics(SM_CXSCREEN);
    int ySize = ::GetSystemMetrics(SM_CYSCREEN);
    int cx = (9*xSize)/10;
    int cy = (9*ySize)/10;

    HWND hWnd = CreateWindowEx(0L, m_szMainFrameClass, szTitle,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        (xSize - cx)/2, (ySize - cy)/2, cx, cy,
        NULL, NULL, hInstance, NULL);

    if (!hWnd)
    {
        return false;
    }
    m_hMainFrame = hWnd;
    SaveWindowPointer();
    return true;
}

void CMainFrame::SaveWindowPointer()
{
    ExtraWindowData ewd;
    ewd.u.p = this;
    for (int i = 0; i < sizeof(ewd.u.l)/sizeof(ewd.u.l[0]); i++)
    {
        SetWindowLong(m_hMainFrame, i*sizeof(long), ewd.u.l[i]);
    }
}

CMainFrame *GetMainFrame(HWND hwnd)
{
    ExtraWindowData ewd;
    for (int i = 0; i < sizeof(ewd.u.l)/sizeof(ewd.u.l[0]); i++)
    {
        ewd.u.l[i] = GetWindowLong(hwnd, i*sizeof(long));
    }
    return ewd.u.p;
}

bool CMainFrame::Finalize(HINSTANCE hInstance)
{
    bool b = true;
    if (0 != m_atmMainFrame)
    {
        if (FALSE == UnregisterClass(MAKEINTATOM(m_atmMainFrame), hInstance))
        {
            b = false;
        }
        m_atmMainFrame = 0;
    }

    if (0 != m_atmChildFrame)
    {
        if (FALSE == UnregisterClass(MAKEINTATOM(m_atmChildFrame), hInstance))
        {
            b = false;
        }
        m_atmChildFrame = 0;
    }
    return b;
}
