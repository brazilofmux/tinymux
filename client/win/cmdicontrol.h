#ifndef CMDICONTROL_H
#define CMDICONTROL_H

class CMDIControl : public CWindow
{
public:
    CMDIControl(CWindow *pParentWindow, CLIENTCREATESTRUCT *pccs, CREATESTRUCT *pcs);
    CSessionFrame *CreateNewChild(void);
    CSessionFrame *GetActive(void);
    LRESULT Tile(void);
    LRESULT Cascade(void);
    LRESULT IconArrange(void);
};

#endif // CMDICONTROL_H
