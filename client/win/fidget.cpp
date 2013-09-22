// fidget.cpp : Entry point for this application is wWinMain().
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
//                    procedure. WM_GETMINMAXINFO can be sent earlier.
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
CFidgetApp g_theApp;

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

inline CWindow *GetWindowPointer(HWND hwnd)
{
    mux_assert(NULL != hwnd);
    return reinterpret_cast<CWindow *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
}

void Attach(HWND hwnd, CWindow *pWnd)
{
    mux_assert(NULL != hwnd);
    mux_assert(NULL != pWnd);

    SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(g_theApp.m_pTemp));
    pWnd->m_hwnd = hwnd;
}

CWindow *Detach(HWND hwnd)
{
    mux_assert(NULL != hwnd);
    CWindow *pWnd = reinterpret_cast<CWindow *>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
    mux_assert(NULL != pWnd);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, NULL);
    pWnd->m_hwnd = hwnd;
    return NULL;
}

CFidgetApp::CFidgetApp()
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
    m_hwndAbout = NULL;
    m_hwndNewSession = NULL;
}

LRESULT CALLBACK CFidgetApp::CBTProc
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

bool CFidgetApp::EnableHook(void)
{
    m_hhk = SetWindowsHookEx(WH_CBT, CBTProc, NULL, ::GetCurrentThreadId());
    return (NULL != m_hhk);
}

bool CFidgetApp::DisableHook(void)
{
    bool bRet = true;
    if (NULL != m_hhk)
    {
        bRet = (FALSE != UnhookWindowsHookEx(m_hhk));
        m_hhk = NULL;
    }
    return bRet;
}

bool CFidgetApp::Initialize(HINSTANCE hInstance, int nCmdShow)
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

    m_pMainFrame = new (std::nothrow) CMainFrame;

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

bool CFidgetApp::Finalize(void)
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

WPARAM CFidgetApp::Run(void)
{
    HACCEL hAccelTable = LoadAccelerators(m_hInstance, (LPCTSTR)IDC_FIDGET);

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
                    if (  (  NULL == m_hwndNewSession
                          || !IsDialogMessage(m_hwndNewSession, &msg))
                       && (  NULL == m_hwndAbout
                          || !IsDialogMessage(m_hwndAbout, &msg))
                       && !TranslateMDISysAccel(g_theApp.m_pMainFrame->m_pMDIControl->m_hwnd, &msg)
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

int CFidgetApp::LoadString(UINT uID, LPTSTR lpBuffer, int nBufferMax)
{
    return ::LoadString(m_hInstance, uID, lpBuffer, nBufferMax);
}

HICON CFidgetApp::LoadIcon(LPCTSTR lpIconName)
{
    return ::LoadIcon(m_hInstance, lpIconName);
}

bool CFidgetApp::RegisterClasses(void)
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
    wcex.hIcon          = g_theApp.LoadIcon((LPCTSTR)IDI_FIDGET);
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground  = NULL; // (HBRUSH)(COLOR_WINDOW+1);
    wcex.lpszMenuName   = (LPCTSTR)IDC_FIDGET;
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

bool CFidgetApp::UnregisterClasses(void)
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

// Mesage handler for about box.
//
LRESULT CALLBACK CFidgetApp::AboutProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_INITDIALOG:
        return TRUE;

    case WM_COMMAND:
        if (  IDOK     == LOWORD(wParam)
           || IDCANCEL == LOWORD(wParam))
        {
            DestroyWindow(hDlg);
            g_theApp.m_hwndAbout = NULL;
            return TRUE;
        }
        break;
    }
    return FALSE;
}

// A hostname is limited to 255 characters total.
// It is broken into labels separated by periods.
// Each label is limited to between 1 and 63 characters.
// Each label may contain ASCII letters 'a' through 'z' (case insensitive), digits ('0' through '9'), or hyphen.
// No label may begin or end with a hyphen.
//
bool ValidateHost(WCHAR aHost[])
{
    bool fValid = true;
    size_t i = 0;
    size_t j = 0;

    // Skip leading invalid characters. Hyphen and period may not occur at the beginning.
    //
    while (  L'\0' != aHost[i]
          && (aHost[i] < L'a' || L'z' < aHost[i])
          && (aHost[i] < L'A' || L'Z' < aHost[i])
          && (aHost[i] < L'0' || L'9' < aHost[i]))
    {
        i++;
    }

    // Copy valid characters until whitespace or end of string.
    //
    size_t nLabel = 0;
    for (;;)
    {
        if (  L'\0' == aHost[i]
           || iswspace(aHost[i]))
        {
            break;
        }
        else if (  (L'a' <= aHost[i] && aHost[i] <= L'z')
                || (L'0' <= aHost[i] && aHost[i] <= L'9'))
        {
            nLabel++;
            if (63 < nLabel)
            {
                // A label must be 63 characters or less.
                //
                fValid = false;
            }
            aHost[j++] = aHost[i++];
        }
        else if (L'A' <= aHost[i] && aHost[i] <= L'Z')
        {
            nLabel++;
            if (63 < nLabel)
            {
                // A label must be 63 characters or less.
                //
                fValid = false;
            }

            // Convert to lower-case.
            //
            aHost[j++] = aHost[i++] + (L'a' - L'A');
        }
        else if (L'.' == aHost[i])
        {
            if (0 == nLabel)
            {
                // A label must contain at least one character.
                //
                fValid = false;
            }
            else if (  0 < j
                    && L'-' == aHost[j-1])
            {
                // A label cannot end with a hyphen.
                //
                fValid = false;
            }
            nLabel = 0;
            aHost[j++] = aHost[i++];
        }
        else if (L'-' == aHost[i])
        {
            if (0 == nLabel)
            {
                // A label cannot start with a hyphen.
                //
                fValid = false;
            }
            nLabel++;
            if (63 < nLabel)
            {
                // A label must be 63 characters or less.
                //
                fValid = false;
            }
            aHost[j++] = aHost[i++];
        }
        else
        {
            // Skip it.
            //
            i++;
        }
    }

    if (0 == nLabel)
    {
        // A label must contain at least one character.
        //
        fValid = false;
    }
    else if (  0 < j
            && L'-' == aHost[j-1])
    {
        // A label cannot end with a hyphen.
        //
        fValid = false;
    }

    if (255 < j)
    {
        fValid = false;
    }

    aHost[j++] = L'\0';

    return fValid;
}

bool ValidatePort(WCHAR aPort[])
{
    bool fValid = true;
    size_t i = 0;
    size_t j = 0;

    // Skip leading spaces and zeroes.
    //
    while (  L'\0' != aPort[i]
          && (  iswspace(aPort[i])
             || '0' == aPort[i]))
    {
        i++;
    }

    // Validate and count digits.
    //
    size_t n = 0;
    while (  L'\0' != aPort[i]
          && !iswspace(aPort[i])
          && n <= 5)
    {
        if (iswdigit(aPort[i]))
        {
            aPort[j++] = aPort[i];
            n++;
        }
        else
        {
            fValid = false;
        }
        i++;
    }

    // Skip trailing spaces.
    //
    while (  L'\0' != aPort[i]
          && iswspace(aPort[i]))
    {
        i++;
    }

    if (L'\0' != aPort[i])
    {
        fValid = false;
    }

    if (0 == n)
    {
        fValid = false;
    }

    aPort[j] = L'\0';

    if (fValid)
    {
        // Data is validated sufficiently tightly to risk calling _wtoi().
        //
        for (;;)
        {
            long iPort = _wtoi(aPort);
            if (0 < iPort && iPort < 65536)
            {
                break;
            }
            else if (  0 < j
                    && 65536 <= iPort)
            {
                // Truncate last digit
                //
                j--;
                aPort[j] = L'\0';
                fValid = false;
            }
            else
            {
                fValid = false;
                break;
            }
        }
    }

    return fValid;
}

// Mesage handler for New Session Dialog.
//
LRESULT CALLBACK CFidgetApp::NewSessionProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    WCHAR aTcpIpHost[256];
    WCHAR aTcpIpPort[6];

    switch (message)
    {
    case WM_INITDIALOG:

        // Host name is limited to be less than 255 characters long.
        // Port is limited to be numeric by the dialog resource.  It is also
        // limited here to be no longer than five characters (i.e., 0-65535).
        // It is still possible to paste non-digit characters into
        // IDC_TCPIP_PORT.  These can be removed at EN_KILLFOCUS time or at
        // IDOK time.
        //
        ::SendMessage(::GetDlgItem(hDlg, IDC_TCPIP_HOST), EM_SETLIMITTEXT, 255, 0);
        ::SendMessage(::GetDlgItem(hDlg, IDC_TCPIP_PORT), EM_SETLIMITTEXT, 5, 0);

        // The dialog automatically positions at IDC_TCPIP_HOST since it
        // appears first in the dialog resource.
        //
        return TRUE;

    case WM_COMMAND:
        {
            WORD wControlId  = LOWORD(wParam);
            WORD wNotifyCode = HIWORD(wParam);
            if (IDOK == wControlId)
            {
                const int nMaxTcpIpHost = sizeof(aTcpIpHost)/sizeof(WCHAR);
                UINT  nTcpIpHost = ::GetDlgItemText(hDlg, IDC_TCPIP_HOST, aTcpIpHost, nMaxTcpIpHost);
                if (0 == nTcpIpHost)
                {
                    aTcpIpHost[nTcpIpHost] = L'\0';
                }

                const int nMaxTcpIpPort = sizeof(aTcpIpPort)/sizeof(WCHAR);
                UINT  nTcpIpPort = ::GetDlgItemText(hDlg, IDC_TCPIP_PORT, aTcpIpPort, nMaxTcpIpPort);
                if (0 == nTcpIpPort)
                {
                    aTcpIpPort[nTcpIpPort] = L'\0';
                }

                bool fValidHost = ValidateHost(aTcpIpHost);
                bool fValidPort = ValidatePort(aTcpIpPort);

                // Update all fields.
                //
                ::SetDlgItemText(hDlg, IDC_TCPIP_HOST, aTcpIpHost);
                ::SetDlgItemText(hDlg, IDC_TCPIP_PORT, aTcpIpPort);

                // Focus on first invalid field.
                //
                if (!fValidHost)
                {
                    ::SetFocus(::GetDlgItem(hDlg, IDC_TCPIP_HOST));
                    return FALSE;
                }

                if (!fValidPort)
                {
                    ::SetFocus(::GetDlgItem(hDlg, IDC_TCPIP_PORT));
                    return FALSE;
                }

                CSessionFrame *pChild = g_theApp.m_pMainFrame->m_pMDIControl->CreateNewChild();

                DestroyWindow(hDlg);
                g_theApp.m_hwndNewSession = NULL;
                return TRUE;
            }
            else if (IDCANCEL == wControlId)
            {
                DestroyWindow(hDlg);
                g_theApp.m_hwndNewSession = NULL;
                return TRUE;
            }
            else if (IDC_TCPIP_HOST == wControlId)
            {
                if (EN_KILLFOCUS == wNotifyCode)
                {
                    const int nMaxTcpIpHost = sizeof(aTcpIpHost)/sizeof(WCHAR);
                    UINT  nTcpIpHost = ::GetDlgItemText(hDlg, IDC_TCPIP_HOST, aTcpIpHost, nMaxTcpIpHost);
                    if (0 == nTcpIpHost)
                    {
                        aTcpIpHost[nTcpIpHost] = L'\0';
                    }
                    ValidateHost(aTcpIpHost);
                    ::SetDlgItemText(hDlg, IDC_TCPIP_HOST, aTcpIpHost);
                }
            }
            else if (IDC_TCPIP_PORT == wControlId)
            {
                if (EN_KILLFOCUS == wNotifyCode)
                {
                    const int nMaxTcpIpPort = sizeof(aTcpIpPort)/sizeof(WCHAR);
                    UINT  nTcpIpPort = ::GetDlgItemText(hDlg, IDC_TCPIP_PORT, aTcpIpPort, nMaxTcpIpPort);
                    if (0 == nTcpIpPort)
                    {
                        aTcpIpPort[nTcpIpPort] = L'\0';
                    }
                    ValidatePort(aTcpIpPort);
                    ::SetDlgItemText(hDlg, IDC_TCPIP_PORT, aTcpIpPort);
                }
            }
            break;
        }
    }
    return FALSE;
}
