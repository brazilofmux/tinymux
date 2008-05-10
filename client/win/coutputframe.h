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
    void OnSize(UINT nType, int cx, int cy);

    HWND    m_hwndRichEdit;
};

#endif // COUTPUTFRAME_H
