#ifndef CINPUTFRAME_H
#define CINPUTFRAME_H

class CInputFrame : public CWindow
{
public:
    static LRESULT CALLBACK InputWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

    CInputFrame();
    bool Create(CWindow *pParentWindow, int x, int y, int cx, int cy);
    virtual ~CInputFrame();

    // Handlers.
    //
    LRESULT OnCreate(CREATESTRUCT *pcs);
    LRESULT OnPaint(void);

    // Document stuff.
    //
    WCHAR        m_szHello[MAX_LOADSTRING];
};

#endif // CINPUTFRAME_H
