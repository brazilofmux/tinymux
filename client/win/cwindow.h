#ifndef CWINDOW_H
#define CWINDOW_H

class CWindow
{
public:
    CWindow();
    virtual ~CWindow();

    LRESULT DefaultWindowHandler(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT DefaultMDIFrameHandler(UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT DefaultMDIChildHandler(UINT message, WPARAM wParam, LPARAM lParam);

    LRESULT SendMessage(UINT message, WPARAM wParam, LPARAM lParam);
    void ShowWindow(int nCmdShow);
    void UpdateWindow(void);
    bool DrawMenuBar(void);
    void MoveWindow
    (
        int x,
        int y,
        int cx,
        int cy,
        bool bRepaint = true
    );

    HWND        m_hwnd;
    CWindow    *m_pParentWindow;
};

#endif // CWINDOW_H
