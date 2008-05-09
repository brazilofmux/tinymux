// ritsuDoc.cpp : implementation of the CRitsuDoc class
//

#include "stdafx.h"
#include "ritsu.h"

#include "ritsuDoc.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CRitsuDoc

IMPLEMENT_DYNCREATE(CRitsuDoc, CDocument)

BEGIN_MESSAGE_MAP(CRitsuDoc, CDocument)
    //{{AFX_MSG_MAP(CRitsuDoc)
        // NOTE - the ClassWizard will add and remove mapping macros here.
        //    DO NOT EDIT what you see in these blocks of generated code!
    //}}AFX_MSG_MAP
END_MESSAGE_MAP()

BEGIN_DISPATCH_MAP(CRitsuDoc, CDocument)
    //{{AFX_DISPATCH_MAP(CRitsuDoc)
        // NOTE - the ClassWizard will add and remove mapping macros here.
        //      DO NOT EDIT what you see in these blocks of generated code!
    //}}AFX_DISPATCH_MAP
END_DISPATCH_MAP()

// Note: we add support for IID_IRitsu to support typesafe binding
//  from VBA.  This IID must match the GUID that is attached to the 
//  dispinterface in the .ODL file.

// {AC4D49E5-CC72-422B-9643-114D9ABC255D}
static const IID IID_IRitsu =
{ 0xac4d49e5, 0xcc72, 0x422b, { 0x96, 0x43, 0x11, 0x4d, 0x9a, 0xbc, 0x25, 0x5d } };

BEGIN_INTERFACE_MAP(CRitsuDoc, CDocument)
    INTERFACE_PART(CRitsuDoc, IID_IRitsu, Dispatch)
END_INTERFACE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CRitsuDoc construction/destruction

CRitsuDoc::CRitsuDoc()
{
    // TODO: add one-time construction code here

    EnableAutomation();

    AfxOleLockApp();
}

CRitsuDoc::~CRitsuDoc()
{
    AfxOleUnlockApp();
}

BOOL CRitsuDoc::OnNewDocument()
{
    if (!CDocument::OnNewDocument())
        return FALSE;

    // TODO: add reinitialization code here
    // (SDI documents will reuse this document)

    return TRUE;
}



/////////////////////////////////////////////////////////////////////////////
// CRitsuDoc serialization

void CRitsuDoc::Serialize(CArchive& ar)
{
    // CEditView contains an edit control which handles all serialization.
    //
    ((CEditView*)m_viewList.GetHead())->SerializeRaw(ar);
}

/////////////////////////////////////////////////////////////////////////////
// CRitsuDoc diagnostics

#ifdef _DEBUG
void CRitsuDoc::AssertValid() const
{
    CDocument::AssertValid();
}

void CRitsuDoc::Dump(CDumpContext& dc) const
{
    CDocument::Dump(dc);
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CRitsuDoc commands
