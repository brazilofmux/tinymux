#ifndef CINPUTFRAME_H
#define CINPUTFRAME_H

class CInputFrame : public CWindow
{
public:
    static LRESULT CALLBACK InputWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static DWORD CALLBACK EditStreamCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb);

    CInputFrame();
    bool Create(CWindow *pParentWindow, int x, int y, int cx, int cy);
    virtual ~CInputFrame();

    // Handlers.
    //
    LRESULT OnCreate(CREATESTRUCT *pcs);
    void OnSize(UINT nType, int cx, int cy);
    LRESULT OnNotify(NMHDR *phdr);

    HWND    m_hwndRichEdit;
    bool    m_bFirst;
};

#endif // CINPUTFRAME_H
