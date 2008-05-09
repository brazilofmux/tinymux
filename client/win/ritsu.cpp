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
    bool DrawMenuBar(void);

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
    static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    CMainFrame();
    bool Create(void);
    void ShowWindow(int nCmdShow) { ::ShowWindow(m_hwnd, nCmdShow); }
    void UpdateWindow(void) { ::UpdateWindow(m_hwnd); }
    LRESULT EnableDisableCloseItem(bool bActivate);
    void IncreaseDecreaseChildCount(bool bIncrease);
    virtual ~CMainFrame();

    // Handlers.
    //
    LRESULT OnCreate(CREATESTRUCT *pcs);
    LRESULT OnCommand(WPARAM wParam, LPARAM lParam);
    LRESULT OnDefaultHandler(UINT message, WPARAM wParam, LPARAM lParam);

    // MDI Control
    //
    CMDIControl *m_pMDIControl;
    int          m_nChildren;

    // Document stuff.
    //
    WCHAR        m_szHello[MAX_LOADSTRING];
};

class CChildFrame : public CWindow
{
public:
    static LRESULT CALLBACK ChildWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    CChildFrame();
    ~CChildFrame();

    // Handlers.
    //
    LRESULT OnCreate(CREATESTRUCT *pcs);
    LRESULT OnMDIActivate(bool bActivate);
    LRESULT OnDestroy(void);
    LRESULT OnPaint(void);

    CMainFrame  *m_pParentWindow;
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

    // Temporary used during window creation process.  This approach assumes a
    // single thread. MFC does basically the same thing for multiple threads
    // using a Window creation hook and thread-local storage to pass the
    // initial CWindow pointer to the corresponding window procedure.
    //
    CWindow *m_pTemp;

    // Application
    //
    HINSTANCE   m_hInstance;
    ATOM        m_atmMain;
    ATOM        m_atmChild;
    WCHAR       m_szMainClass[MAX_LOADSTRING];
    WCHAR       m_szChildClass[MAX_LOADSTRING];

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
LRESULT CALLBACK AboutProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
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
LRESULT CALLBACK CMainFrame::MainWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
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

void CMainFrame::IncreaseDecreaseChildCount(bool bIncrease)
{
    if (bIncrease)
    {
        if (0 == m_nChildren)
        {
            EnableDisableCloseItem(true);
        }
        m_nChildren++;
    }
    else
    {
        m_nChildren--;
        if (0 == m_nChildren)
        {
            EnableDisableCloseItem(false);
        }
    }
}

LRESULT CMainFrame::EnableDisableCloseItem(bool bActivate)
{
    HMENU hMenu = GetMenu(m_hwnd);

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

    DrawMenuBar();
    return 0;
}

bool CWindow::DrawMenuBar(void)
{
    return (0 == ::DrawMenuBar(m_hwnd));
}

LRESULT CChildFrame::OnMDIActivate(bool bActivate)
{
    return 0;
}

LRESULT CChildFrame::OnDestroy(void)
{
    (void)m_pParentWindow->IncreaseDecreaseChildCount(false);
    return 0;
}

LRESULT CChildFrame::OnPaint(void)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(m_hwnd, &ps);
    RECT rt;
    GetClientRect(m_hwnd, &rt);
    FillRect(hdc, &rt, (HBRUSH)(COLOR_WINDOW+1));
    DrawText(hdc, g_theApp.m_pMainFrame->m_szHello, wcslen(g_theApp.m_pMainFrame->m_szHello), &rt, DT_CENTER);
    EndPaint(m_hwnd, &ps);
    return 0;
}

LRESULT CALLBACK CChildFrame::ChildWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
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
        lRes = pWnd->OnMDIActivate(hWnd == (HWND)lParam);
        break;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
        lRes = pWnd->OnPaint();
        break;

    case WM_DESTROY:
        lRes = pWnd->OnDestroy();

    default:

        lRes = DefMDIChildProc(hWnd, message, wParam, lParam);
    }
    return lRes;
}

CMDIControl::CMDIControl(CWindow *pParentWindow, CLIENTCREATESTRUCT *pccs, CREATESTRUCT *pcs)
{
    HWND hChildControl = CreateWindowEx(WS_EX_CLIENTEDGE, L"mdiclient", NULL,
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
        mcs.szTitle = L"Untitled";
        mcs.szClass = L"RitsuChildFrame";
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
        m_pParentWindow = (CMainFrame *)pmdics->lParam;
    }
    (void)m_pParentWindow->IncreaseDecreaseChildCount(true);
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
        (void)m_pMDIControl->CreateNewChild();
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
        DialogBox(g_theApp.m_hInstance, (LPCTSTR)IDD_ABOUTBOX, m_hwnd, (DLGPROC)AboutProc);
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
    m_hInstance  = NULL;
    m_atmMain    = NULL;
    m_atmChild   = NULL;
    m_pMainFrame = NULL;
    m_pTemp      = NULL;
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
    HACCEL hAccelTable = LoadAccelerators(m_hInstance, (LPCTSTR)IDC_RITSU);

    // Main message loop:
    //
    WPARAM iReturn = 0;
    bool bFinished = false;
    while (!bFinished)
    {
        CLinearTimeAbsolute ltaCurrent;
        ltaCurrent.GetUTC();

        // Execute background tasks at specifically scheduled times.
        //
        scheduler.RunTasks(ltaCurrent);
        CLinearTimeAbsolute ltaWakeUp;
        if (!scheduler.WhenNext(&ltaWakeUp))
        {
            ltaWakeUp = ltaCurrent + time_30m;
        }
        else if (ltaWakeUp < ltaCurrent)
        {
            // This is necessary to deal with computer time jumping backwards
            // which can happen when someone sets or resets the computer clock.
            //
            ltaWakeUp = ltaCurrent;
        }

        CLinearTimeDelta ltdTimeOut = ltaWakeUp - ltaCurrent;
        DWORD dwTimeout = ltdTimeOut.ReturnMilliseconds();

        DWORD nHandles = 0;
        DWORD dwObject = MsgWaitForMultipleObjectsEx(nHandles, NULL, dwTimeout, QS_ALLINPUT, 0);
        if (WAIT_OBJECT_0 + nHandles == dwObject)
        {
            for (; !bFinished;)
            {
                // There is at least one new message waiting to be processed.
                //
                MSG msg;
                BOOL bGM = GetMessage(&msg, NULL, 0, 0);
                if (0 == bGM)
                {
                    // WM_QUIT message was received. It is time to terminate
                    // ourselves.
                    //
                    iReturn = msg.wParam;
                    bFinished = true;
                }
                else if (-1 == bGM)
                {
                    // An unexpected problem occured.
                    //
                    bFinished = true;
                }
                else
                {
                    // Translate and dispatch message to Windows Procedure.
                    //
                    if (  !TranslateMDISysAccel(g_theApp.m_pMainFrame->m_pMDIControl->m_hChildControl, &msg)
                       && !TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
                    {
                        TranslateMessage(&msg);
                        DispatchMessage(&msg);
                    }
                }

                // We must process all messages in the queue before making
                // another MsgWaitForMultipleObjectsEx() call.
                //
                if (!PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE|PM_NOYIELD))
                {
                    break;
                }
            }
        }
        else if (WAIT_TIMEOUT != dwObject)
        {
            // An unexpected event occured.
            //
            bFinished = true;
        }

        // It's time to perform some background task.
        //
    }
    DestroyAcceleratorTable(hAccelTable);
    return iReturn;
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
    m_nChildren   = 0;
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
    wcex.lpfnWndProc    = (WNDPROC)CMainFrame::MainWndProc;
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
    wcex.lpfnWndProc   = (WNDPROC)CChildFrame::ChildWndProc;
    wcex.hIcon         = g_theApp.LoadIcon((LPCTSTR)IDI_BACKSCROLL);
    wcex.lpszMenuName  = (LPCTSTR) NULL;
    wcex.lpszClassName = L"RitsuChildFrame";
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
    WCHAR szTitle[MAX_LOADSTRING];
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
