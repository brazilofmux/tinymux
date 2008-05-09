#ifndef CCHILDFRAME_H
#define CCHILDFRAME_H

const int StartChildren = 3000;

class CChildFrame : public CWindow
{
public:
    static LRESULT CALLBACK ChildWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    CChildFrame();
    ~CChildFrame();

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

#endif // CCHILDFRAME_H
