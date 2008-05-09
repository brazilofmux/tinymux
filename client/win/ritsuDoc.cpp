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

// {9B360243-2E8F-4F7B-87A7-D31EE2F29C9C}
static const IID IID_IRitsu =
{ 0x9b360243, 0x2e8f, 0x4f7b, { 0x87, 0xa7, 0xd3, 0x1e, 0xe2, 0xf2, 0x9c, 0x9c } };

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
	if (ar.IsStoring())
	{
		// TODO: add storing code here
	}
	else
	{
		// TODO: add loading code here
	}
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
