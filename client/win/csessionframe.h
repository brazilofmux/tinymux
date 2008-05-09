#ifndef CSessionFrame_H
#define CSessionFrame_H

const int StartChildren = 3000;

class CSessionFrame : public CWindow
{
public:
    static LRESULT CALLBACK SessionWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    CSessionFrame();
    ~CSessionFrame();

    // Handlers.
    //
    LRESULT OnCreate(CREATESTRUCT *pcs);
    LRESULT OnDestroy(void);
    LRESULT OnPaint(void);

    CMainFrame  *m_pParentWindow;

    // Document stuff.
    //
    WCHAR        m_szHello[MAX_LOADSTRING];
};

#endif // CSessionFrame_H
