// ritsu.cpp : Defines the entry point for the application.
//

#include "stdafx.h"
#include "resource.h"

#define MAX_LOADSTRING 100

// This rounds up to the nearest multiple of sizeof(long)
//
#define LONGCEIL(x) (((x) + sizeof(long) - 1) & (~(sizeof(long) - 1)))

const int StartChildren = 3000;

class CWindow
{
public:
    CWindow();
    virtual ~CWindow();
    virtual bool Finalize(HINSTANCE hInstance) = 0;

    HWND        m_hwnd;
    ATOM        m_atm;
    TCHAR       m_szClass[MAX_LOADSTRING];
};

class CMainFrame : public CWindow
{
public:

    CMainFrame();
    void Initialize(void);
    bool RegisterClasses(HINSTANCE hInstance);
    bool Create(void);
    void ShowWindow(int nCmdShow) { ::ShowWindow(m_hwnd, nCmdShow); }
    void UpdateWindow(void) { ::UpdateWindow(m_hwnd); }
    virtual bool Finalize(HINSTANCE hInstance);
    virtual ~CMainFrame();

    // MDI Control
    //
    HWND        m_hChildControl;

    // Class and atom for child windows.
    //
    TCHAR       m_szChildFrameClass[MAX_LOADSTRING];
    ATOM        m_atmChildFrame;

    // Document stuff.
    //
    TCHAR       m_szHello[MAX_LOADSTRING];
};

class CChildFrame : public CWindow
{
public:
};

void *GetWindowPointer(HWND hwnd);

class CRitsuApp
{
public:
    CRitsuApp();

    bool   Initialize(HINSTANCE hInstance, int nCmdShow);
    WPARAM Run(void);
    bool   Finalize(void);

    int LoadString(UINT uID, LPTSTR lpBuffer, int nBufferMax);

    ~CRitsuApp() {};

    // Temp (used during window creation).  Using this assumes a single
    // thread.
    //
    CWindow *m_pTemp;

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
        CWindow *pWindow;
        LONG l[(sizeof(void *) + sizeof(long) - 1)/sizeof(long)];
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
    CMainFrame *pWnd = (CMainFrame *)GetWindowPointer(hWnd);
    switch (message)
    {
    case WM_CREATE:
        {
            // Create MDI Child Control.
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
                pWnd->m_hChildControl = hChildControl;
                HWND hChildWnd = CreateNewMDIChild(hChildControl);
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
        return DefFrameProc(hWnd, pWnd->m_hChildControl, message, wParam, lParam);
        break;
   }
   return 0;
}

LRESULT CALLBACK ChildWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    CChildFrame *pWnd = (CChildFrame *)GetWindowPointer(hWnd);

    switch (message)
    {
    case WM_CREATE:
        break;

    case WM_MDIACTIVATE:
        {
            HMENU hMenu, hFileMenu;
            UINT EnableFlag;

            hMenu = GetMenu(g_theApp.m_pMainFrame->m_hwnd);
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

            DrawMenuBar(g_theApp.m_pMainFrame->m_hwnd);
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
    m_hInstance = hInstance;

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
       || !m_pMainFrame->Create())
    {
        return false;
    }

    m_pMainFrame->ShowWindow(nCmdShow);
    m_pMainFrame->UpdateWindow();

    return true;
}

bool CRitsuApp::Finalize(void)
{
    m_pMainFrame->Finalize(m_hInstance);
    delete m_pMainFrame;
    m_pMainFrame = NULL;
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

int CRitsuApp::LoadString(UINT uID, LPTSTR lpBuffer, int nBufferMax)
{
    return ::LoadString(m_hInstance, uID, lpBuffer, nBufferMax);
}

CWindow::CWindow()
{
    m_hwnd = NULL;
    m_atm  = NULL;
}

CWindow::~CWindow()
{
}

CMainFrame::CMainFrame()
{
    m_hChildControl = NULL;
    m_atmChildFrame = 0;
}

CMainFrame::~CMainFrame()
{
}

bool CMainFrame::RegisterClasses(HINSTANCE hInstance)
{
    LoadString(hInstance, IDC_MAIN_FRAME, m_szClass, MAX_LOADSTRING);

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
    wcex.lpszClassName  = m_szClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, (LPCTSTR)IDI_SMALL);

    ATOM atm = RegisterClassEx(&wcex);
    if (0 == atm)
    {
        return false;
    }
    m_atm = atm;

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

bool CMainFrame::Create(void)
{
    // Create Main Frame Window.
    //
    TCHAR szTitle[MAX_LOADSTRING];
    g_theApp.LoadString(IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    g_theApp.LoadString(IDS_HELLO, m_szHello, MAX_LOADSTRING);

    int xSize = ::GetSystemMetrics(SM_CXSCREEN);
    int ySize = ::GetSystemMetrics(SM_CYSCREEN);
    int cx = (9*xSize)/10;
    int cy = (9*ySize)/10;

    g_theApp.m_pTemp = this;
    HWND hWnd = CreateWindowEx(0L, m_szClass, szTitle,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        (xSize - cx)/2, (ySize - cy)/2, cx, cy,
        NULL, NULL, g_theApp.m_hInstance, NULL);

    if (!hWnd)
    {
        return false;
    }
    return true;
}

void *GetWindowPointer(HWND hwnd)
{
    ExtraWindowData ewd;
    if (NULL == g_theApp.m_pTemp)
    {
        for (int i = 0; i < sizeof(ewd.u.l)/sizeof(ewd.u.l[0]); i++)
        {
            ewd.u.l[i] = GetWindowLong(hwnd, i*sizeof(long));
        }
    }
    else
    {
        ewd.u.pWindow = g_theApp.m_pTemp;
        ewd.u.pWindow->m_hwnd = hwnd;
        g_theApp.m_pTemp = NULL;
        for (int i = 0; i < sizeof(ewd.u.l)/sizeof(ewd.u.l[0]); i++)
        {
            SetWindowLong(hwnd, i*sizeof(long), ewd.u.l[i]);
        }
    }
    return ewd.u.pWindow;
}

bool CWindow::Finalize(HINSTANCE hInstance)
{
    bool b = true;
    if (0 != m_atm)
    {
        if (FALSE == UnregisterClass(MAKEINTATOM(m_atm), hInstance))
        {
            b = false;
        }
        m_atm = 0;
    }
    return b;
}

bool CMainFrame::Finalize(HINSTANCE hInstance)
{
    bool b = true;
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
