#ifndef COUTPUTFRAME_H
#define COUTPUTFRAME_H

class COutputFrame : public CWindow
{
public:
    static LRESULT CALLBACK OutputWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
    static DWORD CALLBACK EditStreamCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb);

    COutputFrame();
    bool Create(CWindow *pParentWindow, int x, int y, int cx, int cy);
    void AppendText(size_t nBuffer, WCHAR *pBuffer);
    ~COutputFrame();

    // Handlers.
    //
    LRESULT OnCreate(CREATESTRUCT *pcs);
    void OnSize(UINT nType, int cx, int cy);

    HWND    m_hwndRichEdit;
};

#endif // COUTPUTFRAME_H
