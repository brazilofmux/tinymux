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
    void    OnSize(UINT nType, int cx, int cy);
    LRESULT OnDestroy(void);

    COutputFrame *m_pOutputWindow;
    CInputFrame  *m_pInputWindow;
};

#endif // CSessionFrame_H
