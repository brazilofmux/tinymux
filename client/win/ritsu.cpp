// ritsu.cpp : Entry point for the application is wWinMain().
//
//
// Notes about specific messages:
//
// WM_MDIACTIVATE  -  On WINE, when the last child window of an MDICLIENT
//                    control is closed, WINE does not send the last
//                    WM_MDIACTIVATE(wParam != child HWND) to deactivate the
//                    child window.  This is a bug in WINE.
//
// WM_NCCREATE     -  This is not necessarily the first message to the window
//                    procedure. WM_GETMINMAXINFO (and probably others) can be
//                    sent earlier.  We cannot reliably use lpCreateParams to
//                    pass a CWindow pointer to be associated with the window
//                    handle.  IME may be interacting to cause this.
//
// WM_NCDESTROY    -  This is guaranteed to be the last message sent to the
//                    window procedure before the window handle is released.
//
// WM_MDICREATE    -  The MDICLIENT control creates the child window. When the
//                    currently active child window is maximized, MDICLIENT
//                    sends it a message to restore the existing window before
//                    creating a new child window.  This behavior thwarts
//                    attempts to pass and associate CWindow pointers on the
//                    side as an existing child window will pick up the pointer
//                    instead of the new child.
//
#include "stdafx.h"
#include "resource.h"

// Global Variables:
//
CRitsuApp g_theApp;

int APIENTRY wWinMain
(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPWSTR    lpCmdLine,
    int       nCmdShow
)
{
    int ret = 0;
    if (g_theApp.Initialize(hInstance, nCmdShow))
    {
        ret = g_theApp.Run();
    }
    g_theApp.Finalize();
    return ret;
}

typedef struct
{
    HWND     hwnd;
    CWindow *pWnd;
} recHandlePtr;

CWindow *GetWindowPointer(HWND hwnd)
{
    mux_assert(NULL != hwnd);
    recHandlePtr rec;
    UINT32 nHash = CRC32_ProcessBuffer(0, &hwnd, sizeof(hwnd));

    UINT32 iDir = g_theApp.m_mapHandles.FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        g_theApp.m_mapHandles.Copy(iDir, &nRecord, &rec);

        if (rec.hwnd == hwnd)
        {
            mux_assert(NULL != rec.pWnd);
            return rec.pWnd;
        }
        iDir = g_theApp.m_mapHandles.FindNextKey(iDir, nHash);
    }
    return NULL;
}

void Attach(HWND hwnd, CWindow *pWnd)
{
    mux_assert(NULL != hwnd);
    mux_assert(NULL != pWnd);

    recHandlePtr rec;
    UINT32 nHash = CRC32_ProcessBuffer(0, &hwnd, sizeof(hwnd));

    UINT32 iDir = g_theApp.m_mapHandles.FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        g_theApp.m_mapHandles.Copy(iDir, &nRecord, &rec);

        if (rec.hwnd == hwnd)
        {
            return;
        }
        iDir = g_theApp.m_mapHandles.FindNextKey(iDir, nHash);
    }

    rec.hwnd = hwnd;
    rec.pWnd = pWnd;
    pWnd->m_hwnd = hwnd;
    g_theApp.m_mapHandles.Insert((HP_HEAPLENGTH)sizeof(rec), nHash, &rec);
}

CWindow *Detach(HWND hwnd)
{
    mux_assert(NULL != hwnd);
    recHandlePtr rec;
    UINT32 nHash = CRC32_ProcessBuffer(0, &hwnd, sizeof(hwnd));

    UINT32 iDir = g_theApp.m_mapHandles.FindFirstKey(nHash);
    while (iDir != HF_FIND_END)
    {
        HP_HEAPLENGTH nRecord;
        g_theApp.m_mapHandles.Copy(iDir, &nRecord, &rec);

        if (rec.hwnd == hwnd)
        {
            g_theApp.m_mapHandles.Remove(iDir);
            mux_assert(NULL != rec.pWnd);
            return rec.pWnd;
        }
        iDir = g_theApp.m_mapHandles.FindNextKey(iDir, nHash);
    }
    return NULL;
}

CRitsuApp::CRitsuApp()
{
    m_hInstance  = NULL;
    m_atmMain    = NULL;
    m_atmSession = NULL;
    m_pMainFrame = NULL;
    m_hhk        = NULL;
    m_hRichEdit  = NULL;
    m_brushBlack = NULL;
    m_szSessionClass[0] = L'\0';
    m_szMainClass[0]    = L'\0';
    m_szOutputClass[0]  = L'\0';
    m_szInputClass[0]   = L'\0';
    m_bMsftEdit = false;
}

LRESULT CALLBACK CRitsuApp::CBTProc
(
    int    nCode,
    WPARAM wParam,
    LPARAM lParam
)
{
    if (  HCBT_CREATEWND == nCode
       && NULL != g_theApp.m_pTemp)
    {
        HWND hwnd = (HWND)wParam;
        CWindow *pWnd = GetWindowPointer(hwnd);
        mux_assert(NULL == pWnd);
        if (NULL == pWnd)
        {
            Attach(hwnd, g_theApp.m_pTemp);
            g_theApp.m_pTemp = NULL;
        }
    }

    return CallNextHookEx(g_theApp.m_hhk, nCode, wParam, lParam);      
}

bool CRitsuApp::EnableHook(void)
{
    m_hhk = SetWindowsHookEx(WH_CBT, CBTProc, NULL, ::GetCurrentThreadId()); 
    return (NULL != m_hhk);
}

bool CRitsuApp::DisableHook(void)
{
    bool bRet = true;
    if (NULL != m_hhk)
    {
        bRet = (FALSE != UnhookWindowsHookEx(m_hhk));
        m_hhk = NULL;
    }
    return bRet;
}

bool CRitsuApp::Initialize(HINSTANCE hInstance, int nCmdShow)
{
    m_hInstance = hInstance;

    TIME_Initialize();
    init_timer();
    SeedRandomNumberGenerator();

    if (!EnableHook())
    {
        return false;
    }

    m_hRichEdit = ::LoadLibrary(L"Msftedit.dll");
    if (NULL != m_hRichEdit)
    {
        m_bMsftEdit = true;
    }
    else
    {
        m_hRichEdit = ::LoadLibrary(L"Riched20.dll");
        if (NULL != m_hRichEdit)
        {
            m_bMsftEdit = false;
        }
        else
        {
            return false;
        }
    }

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

    int xSize = ::GetSystemMetrics(SM_CXSCREEN);
    int ySize = ::GetSystemMetrics(SM_CYSCREEN);
    int cx = (9*xSize)/10;
    int cy = (9*ySize)/10;
    int x = (xSize - cx)/2;
    int y = (ySize - cy)/2;
    
    if (!m_pMainFrame->Create(x, y, cx, cy))
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
    m_pMainFrame = NULL;
    DisableHook();
    if (NULL != m_hRichEdit)
    {
        ::FreeLibrary(m_hRichEdit);
        m_hRichEdit = NULL;
    }

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
                    if (  !TranslateMDISysAccel(g_theApp.m_pMainFrame->m_pMDIControl->m_hwnd, &msg)
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

bool CRitsuApp::RegisterClasses(void)
{
    LoadString(IDS_MAIN_FRAME, m_szMainClass, MAX_LOADSTRING);
    LoadString(IDS_SESSION_FRAME, m_szSessionClass, MAX_LOADSTRING);
    LoadString(IDS_OUTPUT_FRAME, m_szOutputClass, MAX_LOADSTRING);
    LoadString(IDS_INPUT_FRAME, m_szInputClass, MAX_LOADSTRING);

    m_brushBlack = CreateSolidBrush(RGB(0, 0, 0));

    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = (WNDPROC)CMainFrame::MainWndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = g_theApp.m_hInstance;
    wcex.hIcon          = g_theApp.LoadIcon((LPCTSTR)IDI_RITSU);
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = NULL; // (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = (LPCTSTR)IDC_RITSU;
    wcex.lpszClassName  = m_szMainClass;
    wcex.hIconSm        = g_theApp.LoadIcon((LPCTSTR)IDI_SMALL);

    ATOM atm = RegisterClassEx(&wcex);
    if (0 == atm)
    {
        return false;
    }
    m_atmMain = atm;

    // Register Session Frame Window class.
    //
    wcex.lpfnWndProc   = (WNDPROC)CSessionFrame::SessionWndProc;
    wcex.hIcon         = g_theApp.LoadIcon((LPCTSTR)IDI_BACKSCROLL);
    wcex.lpszMenuName  = (LPCTSTR) NULL;
    wcex.lpszClassName = m_szSessionClass;
    wcex.hIconSm       = g_theApp.LoadIcon((LPCTSTR)IDI_SMALL);

    atm = RegisterClassEx(&wcex);
    if (0 == atm)
    {
        return false;
    }
    m_atmSession = atm;

    // Register Output Frame Window class.
    //
    wcex.lpfnWndProc   = (WNDPROC)COutputFrame::OutputWndProc;
    wcex.hIcon         = g_theApp.LoadIcon((LPCTSTR)IDI_BACKSCROLL);
    wcex.hbrBackground = NULL;
    wcex.lpszClassName = m_szOutputClass;
    wcex.hIconSm       = g_theApp.LoadIcon((LPCTSTR)IDI_SMALL);

    atm = RegisterClassEx(&wcex);
    if (0 == atm)
    {
        return false;
    }
    m_atmOutput = atm;

    // Register Input Frame Window class.
    //
    wcex.lpfnWndProc   = (WNDPROC)CInputFrame::InputWndProc;
    wcex.hIcon         = g_theApp.LoadIcon((LPCTSTR)IDI_BACKSCROLL);
    wcex.hbrBackground = NULL;
    wcex.lpszClassName = m_szInputClass;
    wcex.hIconSm       = g_theApp.LoadIcon((LPCTSTR)IDI_SMALL);

    atm = RegisterClassEx(&wcex);
    if (0 == atm)
    {
        return false;
    }
    m_atmInput = atm;

    return true;
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

    if (0 != m_atmSession)
    {
        if (FALSE == UnregisterClass(MAKEINTATOM(m_atmSession), m_hInstance))
        {
            b = false;
        }
        m_atmSession = 0;
    }

    if (0 != m_atmOutput)
    {
        if (FALSE == UnregisterClass(MAKEINTATOM(m_atmOutput), m_hInstance))
        {
            b = false;
        }
        m_atmOutput = 0;
    }

    if (0 != m_atmInput)
    {
        if (FALSE == UnregisterClass(MAKEINTATOM(m_atmInput), m_hInstance))
        {
            b = false;
        }
        m_atmInput = 0;
    }

    if (NULL != m_brushBlack)
    {
        if (FALSE == DeleteObject(m_brushBlack))
        {
            b = false;
        }
        m_brushBlack = NULL;
    }

    return b;
}
