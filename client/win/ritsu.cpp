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

    virtual LRESULT OnCreate(CREATESTRUCT *pcs) = 0;
    LRESULT SendMessage(UINT message, WPARAM wParam, LPARAM lParam);

    HWND        m_hwnd;
};

class CMDIControl
{
public:
    CMDIControl(CWindow *pParentWindow, CLIENTCREATESTRUCT *pccs, CREATESTRUCT *pcs);
    CWindow *CreateNewChild(void);
    CWindow *GetActive(void);
    LRESULT Tile(void);
    LRESULT Cascade(void);
    LRESULT IconArrange(void);

    CWindow    *m_pParentWindow;
    HWND        m_hChildControl;

    // Handlers.
    //
    LRESULT     OnDefaultHandler(UINT message, WPARAM wParam, LPARAM lParam);
};

class CMainFrame : public CWindow
{
public:

    CMainFrame();
    void Initialize(void);
    bool Create(void);
    void ShowWindow(int nCmdShow) { ::ShowWindow(m_hwnd, nCmdShow); }
    void UpdateWindow(void) { ::UpdateWindow(m_hwnd); }
    virtual ~CMainFrame();

    // Handlers.
    //
    LRESULT OnCreate(CREATESTRUCT *pcs);
    LRESULT OnCommand(WPARAM wParam, LPARAM lParam);
    LRESULT OnDefaultHandler(UINT message, WPARAM wParam, LPARAM lParam);

    // MDI Control
    //
    CMDIControl *m_pMDIControl;

    // Document stuff.
    //
    TCHAR        m_szHello[MAX_LOADSTRING];
};

class CChildFrame : public CWindow
{
public:
    CChildFrame();
    ~CChildFrame();

    // Handlers.
    //
    LRESULT OnCreate(CREATESTRUCT *pcs);
    LRESULT OnMDIActivate(bool bActivate);

    CWindow  *m_pParentWindow;
};

void *GetWindowPointer(HWND hwnd);

class CRitsuApp
{
public:
    CRitsuApp();

    bool   Initialize(HINSTANCE hInstance, int nCmdShow);
    WPARAM Run(void);
    bool   Finalize(void);

    bool   RegisterClasses(void);
    bool   UnregisterClasses(void);

    int    LoadString(UINT uID, LPTSTR lpBuffer, int nBufferMax);
    HICON  LoadIcon(LPCTSTR lpIconName);

    ~CRitsuApp() {};

    // Temp (used during window creation).  Using this assumes a single
    // thread.
    //
    CWindow *m_pTemp;

    // Application
    //
    HINSTANCE   m_hInstance;
    ATOM        m_atmMain;
    ATOM        m_atmChild;
    TCHAR       m_szMainClass[MAX_LOADSTRING];
    TCHAR       m_szChildClass[MAX_LOADSTRING];

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

LRESULT CMDIControl::OnDefaultHandler(UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefFrameProc(m_pParentWindow->m_hwnd, m_hChildControl, message, wParam, lParam);
}

LRESULT CMainFrame::OnDefaultHandler(UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProc(m_hwnd, message, wParam, lParam);
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
    LRESULT lRes = 0;
    CMainFrame *pWnd = (CMainFrame *)GetWindowPointer(hWnd);
    switch (message)
    {
    case WM_CREATE:
        {
            CREATESTRUCT *pcs = (CREATESTRUCT *)lParam;
            lRes = pWnd->OnCreate(pcs);
        }
        break;

    case WM_COMMAND:
        lRes = pWnd->OnCommand(wParam, lParam);
        break;

    case WM_DESTROY:
        PostQuitMessage(0);
        break;

    default:
        if (NULL != pWnd->m_pMDIControl)
        {
            lRes = pWnd->m_pMDIControl->OnDefaultHandler(message, wParam, lParam);
        }
        else
        {
            lRes = pWnd->OnDefaultHandler(message, wParam, lParam);
        }
        break;
   }
   return lRes;
}

LRESULT CChildFrame::OnMDIActivate(bool bActivate)
{
    HMENU hMenu = GetMenu(m_pParentWindow->m_hwnd);

    UINT EnableFlag;
    if (bActivate)
    {
        EnableFlag = MF_ENABLED;
    }
    else
    {
        EnableFlag = MF_GRAYED;
    }

    EnableMenuItem(hMenu, 1, MF_BYPOSITION | EnableFlag);

    HMENU hFileMenu = GetSubMenu(hMenu, 0);

    EnableMenuItem(hFileMenu, IDM_FILE_CLOSE, MF_BYCOMMAND | EnableFlag);
    EnableMenuItem(hFileMenu, IDM_WINDOW_CLOSE_ALL, MF_BYCOMMAND | EnableFlag);

    DrawMenuBar(g_theApp.m_pMainFrame->m_hwnd);
    return 0;
}

LRESULT CALLBACK ChildWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
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

    case WM_MDIACTIVATE:
        {
            lRes = pWnd->OnMDIActivate(hWnd == (HWND)lParam);
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

        lRes = DefMDIChildProc(hWnd, message, wParam, lParam);
    }
    return lRes;
}

CMDIControl::CMDIControl(CWindow *pParentWindow, CLIENTCREATESTRUCT *pccs, CREATESTRUCT *pcs)
{
    HWND hChildControl = CreateWindowEx(WS_EX_CLIENTEDGE, _T("mdiclient"), NULL,
        WS_CHILD | WS_CLIPCHILDREN | WS_VSCROLL | WS_HSCROLL | WS_VISIBLE,
        pcs->x, pcs->y, pcs->cx, pcs->cy, pParentWindow->m_hwnd,
        (HMENU)IDC_MAIN_MDI, g_theApp.m_hInstance, pccs);

    if (NULL != hChildControl)
    {
        m_pParentWindow = pParentWindow;
        m_hChildControl = hChildControl;
    }
}

CWindow *CMDIControl::CreateNewChild(void)
{
    CChildFrame *pChild = NULL;
    try
    {
        pChild = new CChildFrame;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL != pChild)
    {
        // The Child Window sets the m_pParentWindow in its OnCreate().
        //
        MDICREATESTRUCT mcs;
        memset(&mcs, 0, sizeof(mcs));
        mcs.szTitle = _T("Untitled");
        mcs.szClass = _T("RitsuChildFrame");
        mcs.hOwner = g_theApp.m_hInstance;
        mcs.x = mcs.cx = CW_USEDEFAULT;
        mcs.y = mcs.cy = CW_USEDEFAULT;
        mcs.style = MDIS_ALLCHILDSTYLES;
        mcs.lParam = (LPARAM)m_pParentWindow;

        g_theApp.m_pTemp = pChild;
        (void)SendMessage(m_hChildControl, WM_MDICREATE, 0, (LPARAM)&mcs);
    }
    return pChild;
}

LRESULT CMainFrame::OnCreate(CREATESTRUCT *pcs)
{
    // Create MDI Child Control.
    //
    CLIENTCREATESTRUCT ccs;
    memset(&ccs, 0, sizeof(ccs));
    ccs.hWindowMenu = GetSubMenu(GetMenu(m_hwnd), 1);
    ccs.idFirstChild = StartChildren;

    CMDIControl *pMDIControl = NULL;
    try
    {
        pMDIControl = new CMDIControl(this, &ccs, pcs);
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL != pMDIControl)
    {
        m_pMDIControl = pMDIControl;
        (void)m_pMDIControl->CreateNewChild();
    }
    return 0;
}

LRESULT CChildFrame::OnCreate(CREATESTRUCT *pcs)
{
    MDICREATESTRUCT *pmdics = (MDICREATESTRUCT *)pcs->lpCreateParams;
    if (NULL != pmdics)
    {
        m_pParentWindow = (CWindow *)pmdics->lParam;
    }
    return 0;
}

LRESULT CWindow::SendMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
    return ::SendMessage(m_hwnd, message, wParam, lParam);
}

CWindow *CMDIControl::GetActive(void)
{
    HWND hwnd = (HWND)::SendMessage(m_hChildControl, WM_MDIGETACTIVE, 0, 0);
    CWindow *pWnd = (CWindow *)GetWindowPointer(hwnd);
    return pWnd;
}

LRESULT CMDIControl::Tile(void)
{
    return ::SendMessage(m_hChildControl, WM_MDITILE, 0, 0);
}

LRESULT CMDIControl::Cascade(void)
{
    return ::SendMessage(m_hChildControl, WM_MDICASCADE, 0, 0);
}

LRESULT CMDIControl::IconArrange(void)
{
    return ::SendMessage(m_hChildControl, WM_MDIICONARRANGE, 0, 0);
}

LRESULT CMainFrame::OnCommand(WPARAM wParam, LPARAM lParam)
{
    // Parse the menu selections:
    //
    int wmId    = LOWORD(wParam);
    int wmEvent = HIWORD(wParam);
    switch (wmId)
    {
    case IDM_FILE_NEW:
        m_pMDIControl->CreateNewChild();
        break;

    case IDM_FILE_CLOSE:
        {
            CWindow *pChild = m_pMDIControl->GetActive();
            if (NULL != pChild)
            {
                pChild->SendMessage(WM_CLOSE, 0, 0);
            }
        }
        break;

    case IDM_ABOUT:
        DialogBox(g_theApp.m_hInstance, (LPCTSTR)IDD_ABOUTBOX, m_hwnd, (DLGPROC)About);
        break;

    case IDM_EXIT:
        DestroyWindow(m_hwnd);
        break;

    case IDM_WINDOW_TILE:
        m_pMDIControl->Tile();
        break;

    case IDM_WINDOW_CASCADE:
        m_pMDIControl->Cascade();
        break;

    case IDM_WINDOW_ARRANGE:
        m_pMDIControl->IconArrange();
        break;

    case IDM_WINDOW_CLOSE_ALL:
        {
            CWindow *pChild = NULL;

            do {
                pChild = m_pMDIControl->GetActive();
                if (NULL != pChild)
                {
                    pChild->SendMessage(WM_CLOSE, 0, 0);
                }
            } while (NULL != pChild);
        }
        break;

    default:
        if (StartChildren <= wmId)
        {
            return m_pMDIControl->OnDefaultHandler(WM_COMMAND, wParam, lParam);
        }
        else
        {
            CWindow *pChild = m_pMDIControl->GetActive();
            if (NULL != pChild)
            {
                pChild->SendMessage(WM_CLOSE, 0, 0);
            }
        }
    }
    return 0;
}

CRitsuApp::CRitsuApp()
{
    m_hInstance = NULL;
    m_atmMain   = NULL;
    m_atmChild  = NULL;
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
       || !RegisterClasses())
    {
        return false;
    }
    
    if (!m_pMainFrame->Create())
    {
        UnregisterClasses();
        return false;
    }

    m_pMainFrame->ShowWindow(nCmdShow);
    m_pMainFrame->UpdateWindow();

    return true;
}

bool CRitsuApp::Finalize(void)
{
    delete m_pMainFrame;
    m_pMainFrame = NULL;
    UnregisterClasses();
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
        else if (  !TranslateMDISysAccel(g_theApp.m_pMainFrame->m_pMDIControl->m_hChildControl, &msg)
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

HICON CRitsuApp::LoadIcon(LPCTSTR lpIconName)
{
    return ::LoadIcon(m_hInstance, lpIconName);
}

CWindow::CWindow()
{
    m_hwnd = NULL;
}

CWindow::~CWindow()
{
}

CMainFrame::CMainFrame()
{
    m_pMDIControl = NULL;
}

CMainFrame::~CMainFrame()
{
}

CChildFrame::CChildFrame()
{
    m_pParentWindow = NULL;
}

CChildFrame::~CChildFrame()
{
}

bool CRitsuApp::RegisterClasses(void)
{
    LoadString(IDC_MAIN_FRAME, m_szMainClass, MAX_LOADSTRING);

    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = 0; // CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = (WNDPROC)MainWndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = LONGCEIL(sizeof(ExtraWindowData));
    wcex.hInstance      = g_theApp.m_hInstance;
    wcex.hIcon          = g_theApp.LoadIcon((LPCTSTR)IDI_RITSU);
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = (LPCTSTR)IDC_RITSU;
    wcex.lpszClassName  = m_szMainClass;
    wcex.hIconSm        = g_theApp.LoadIcon((LPCTSTR)IDI_SMALL);

    ATOM atm = RegisterClassEx(&wcex);
    if (0 == atm)
    {
        return false;
    }
    m_atmMain = atm;

    // Register Child Frame Window class.
    //
    wcex.lpfnWndProc   = (WNDPROC) ChildWndProc;
    wcex.hIcon         = g_theApp.LoadIcon((LPCTSTR)IDI_BACKSCROLL);
    wcex.lpszMenuName  = (LPCTSTR) NULL;
    wcex.lpszClassName = _T("RitsuChildFrame");
    wcex.hIconSm       = g_theApp.LoadIcon((LPCTSTR)IDI_BACKSCROLL);

    atm = RegisterClassEx(&wcex);
    if (0 == atm)
    {
        return false;
    }
    m_atmChild = atm;
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
    HWND hWnd = CreateWindowEx(0L, g_theApp.m_szMainClass, szTitle,
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

bool CRitsuApp::UnregisterClasses(void)
{
    bool b = true;
    if (0 != m_atmMain)
    {
        if (FALSE == UnregisterClass(MAKEINTATOM(m_atmMain), m_hInstance))
        {
            b = false;
        }
        m_atmMain = 0;
    }

    if (0 != m_atmChild)
    {
        if (FALSE == UnregisterClass(MAKEINTATOM(m_atmChild), m_hInstance))
        {
            b = false;
        }
        m_atmChild = 0;
    }
    return b;
}
