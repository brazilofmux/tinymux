#ifndef CMDICONTROL_H
#define CMDICONTROL_H

class CMDIControl : public CWindow
{
public:
    CMDIControl(CWindow *pParentWindow, CLIENTCREATESTRUCT *pccs, CREATESTRUCT *pcs);
    CWindow *CreateNewChild(void);
    CWindow *GetActive(void);
    LRESULT Tile(void);
    LRESULT Cascade(void);
    LRESULT IconArrange(void);

    // Handlers.
    //
    LRESULT     OnDefaultHandler(UINT message, WPARAM wParam, LPARAM lParam);

    CWindow    *m_pParentWindow;
};

#endif // CMDICONTROL_H
