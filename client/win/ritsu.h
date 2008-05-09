// ritsu.h : main header file for the RITSU application
//

#if !defined(AFX_RITSU_H__39AD7724_62DB_4194_8974_C24A061085E2__INCLUDED_)
#define AFX_RITSU_H__39AD7724_62DB_4194_8974_C24A061085E2__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#ifndef __AFXWIN_H__
    #error include 'stdafx.h' before including this file for PCH
#endif

#include "resource.h"       // main symbols

/////////////////////////////////////////////////////////////////////////////
// CRitsuApp:
// See ritsu.cpp for the implementation of this class
//

class CRitsuApp : public CWinApp
{
public:
    CRitsuApp();

// Overrides
    // ClassWizard generated virtual function overrides
    //{{AFX_VIRTUAL(CRitsuApp)
    public:
    virtual BOOL InitInstance();
    //}}AFX_VIRTUAL

// Implementation
    COleTemplateServer m_server;
        // Server object for document creation
    //{{AFX_MSG(CRitsuApp)
    afx_msg void OnAppAbout();
        // NOTE - the ClassWizard will add and remove member functions here.
        //    DO NOT EDIT what you see in these blocks of generated code !
    //}}AFX_MSG
    DECLARE_MESSAGE_MAP()
};


/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_RITSU_H__39AD7724_62DB_4194_8974_C24A061085E2__INCLUDED_)
