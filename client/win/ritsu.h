
#if !defined(CRITSUAPP_H)
#define CRITSUAPP_H

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

class CRitsuApp
{
public:
    static LRESULT CALLBACK CBTProc(int nCode, WPARAM wParam, LPARAM lParam);
    CRitsuApp();

    bool   Initialize(HINSTANCE hInstance, int nCmdShow);
    WPARAM Run(void);
    bool   Finalize(void);

    bool   RegisterClasses(void);
    bool   UnregisterClasses(void);
    bool   EnableHook(void);
    bool   DisableHook(void);

    int    LoadString(UINT uID, LPTSTR lpBuffer, int nBufferMax);
    HICON  LoadIcon(LPCTSTR lpIconName);

    ~CRitsuApp() {};

    // Mechanism for associating CWindow objects allocated by us with the
    // window handles allocated by the platform.  This approach assumes a
    // single thread. MFC does basically the same thing for multiple threads.
    // We use a window creation hook and a thread-local global variable
    // (CRitsuApp::m_pTemp) to attach the initial CWindow pointer with the
    // window handle. The CWindow is disassociated and destructed during
    // WM_NCDESTROY.
    //
    CWindow    *m_pTemp;
    HHOOK       m_hhk;
    CHashTable  m_mapHandles;

    // Application
    //
    HBRUSH      m_brushBlack;
    HINSTANCE   m_hInstance;
    ATOM        m_atmMain;
    ATOM        m_atmSession;
    ATOM        m_atmOutput;
    WCHAR       m_szMainClass[MAX_LOADSTRING];
    WCHAR       m_szSessionClass[MAX_LOADSTRING];
    WCHAR       m_szOutputClass[MAX_LOADSTRING];

    // Main Frame.
    //
    CMainFrame  *m_pMainFrame;
};

extern CRitsuApp g_theApp;

#endif // CRITSUAPP_H
