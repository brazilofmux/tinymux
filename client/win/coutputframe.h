#ifndef COUTPUTFRAME_H
#define COUTPUTFRAME_H

class COutputFrame : public CWindow
{
public:
    static LRESULT CALLBACK OutputWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    COutputFrame();
    bool Create(CWindow *pParentWindow, int x, int y, int cx, int cy);
    ~COutputFrame();

    // Handlers.
    //
    LRESULT OnCreate(CREATESTRUCT *pcs);
    LRESULT OnPaint(void);

    // Document stuff.
    //
    WCHAR        m_szHello[MAX_LOADSTRING];
};

#endif // COUTPUTFRAME_H
