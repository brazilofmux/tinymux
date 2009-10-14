#if !defined(CFIDGETAPP_H)
#define CFIDGETAPP_H

#include "resource.h"

typedef struct
{
    CWindow *pWindow;
    CWindow *pParentWindow;
} CreateParams;

CWindow *GetWindowPointer(HWND hwnd);
void Attach(HWND hwnd, CWindow *pWnd);
CWindow *Detach(HWND hwnd);

#define MAX_LOADSTRING 100

class CFidgetApp
{
public:
    static LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK AboutProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK NewSessionProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

    CFidgetApp();

    bool   Initialize(HINSTANCE hInstance, int nCmdShow);
    WPARAM Run(void);
    bool   Finalize(void);

    bool   RegisterClasses(void);
    bool   UnregisterClasses(void);
    bool   EnableHook(void);
    bool   DisableHook(void);

    int    LoadString(UINT uID, LPTSTR lpBuffer, int nBufferMax);
    HICON  LoadIcon(LPCTSTR lpIconName);

    ~CFidgetApp() {};

    // Mechanism for associating CWindow objects allocated by us with the
    // window handles allocated by the platform.  This approach assumes a
    // single thread. MFC does basically the same thing for multiple threads.
    // We use a window creation hook and a thread-local global variable
    // (CFidgetApp::m_pTemp) to attach the initial CWindow pointer with the
    // window handle. The CWindow is disassociated and destructed during
    // WM_NCDESTROY.
    //
    CWindow    *m_pTemp;
    HHOOK       m_hhk;

    // Application
    //
    HBRUSH      m_brushBlack;
    HINSTANCE   m_hInstance;
    ATOM        m_atmMain;
    ATOM        m_atmSession;
    ATOM        m_atmOutput;
    ATOM        m_atmInput;
    WCHAR       m_szMainClass[MAX_LOADSTRING];
    WCHAR       m_szSessionClass[MAX_LOADSTRING];
    WCHAR       m_szOutputClass[MAX_LOADSTRING];
    WCHAR       m_szInputClass[MAX_LOADSTRING];

    bool        m_bMsftEdit;
    HMODULE     m_hRichEdit;

    HWND        m_hwndAbout;
    HWND        m_hwndNewSession;

    // Main Frame.
    //
    CMainFrame  *m_pMainFrame;
};

extern CFidgetApp g_theApp;

#endif // CFIDGETAPP_H
