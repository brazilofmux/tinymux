// ritsuView.cpp : implementation of the CRitsuView class
//

#include "stdafx.h"
#include "ritsu.h"

#include "ritsuDoc.h"
#include "ritsuView.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// CRitsuView

IMPLEMENT_DYNCREATE(CRitsuView, CView)

BEGIN_MESSAGE_MAP(CRitsuView, CView)
	//{{AFX_MSG_MAP(CRitsuView)
		// NOTE - the ClassWizard will add and remove mapping macros here.
		//    DO NOT EDIT what you see in these blocks of generated code!
	//}}AFX_MSG_MAP
	// Standard printing commands
	ON_COMMAND(ID_FILE_PRINT, CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_DIRECT, CView::OnFilePrint)
	ON_COMMAND(ID_FILE_PRINT_PREVIEW, CView::OnFilePrintPreview)
END_MESSAGE_MAP()

/////////////////////////////////////////////////////////////////////////////
// CRitsuView construction/destruction

CRitsuView::CRitsuView()
{
	// TODO: add construction code here

}

CRitsuView::~CRitsuView()
{
}

BOOL CRitsuView::PreCreateWindow(CREATESTRUCT& cs)
{
	// TODO: Modify the Window class or styles here by modifying
	//  the CREATESTRUCT cs

	return CView::PreCreateWindow(cs);
}

/////////////////////////////////////////////////////////////////////////////
// CRitsuView drawing

void CRitsuView::OnDraw(CDC* pDC)
{
	CRitsuDoc* pDoc = GetDocument();
	ASSERT_VALID(pDoc);
	// TODO: add draw code for native data here
}

/////////////////////////////////////////////////////////////////////////////
// CRitsuView printing

BOOL CRitsuView::OnPreparePrinting(CPrintInfo* pInfo)
{
	// default preparation
	return DoPreparePrinting(pInfo);
}

void CRitsuView::OnBeginPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
	// TODO: add extra initialization before printing
}

void CRitsuView::OnEndPrinting(CDC* /*pDC*/, CPrintInfo* /*pInfo*/)
{
	// TODO: add cleanup after printing
}

/////////////////////////////////////////////////////////////////////////////
// CRitsuView diagnostics

#ifdef _DEBUG
void CRitsuView::AssertValid() const
{
	CView::AssertValid();
}

void CRitsuView::Dump(CDumpContext& dc) const
{
	CView::Dump(dc);
}

CRitsuDoc* CRitsuView::GetDocument() // non-debug version is inline
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CRitsuDoc)));
	return (CRitsuDoc*)m_pDocument;
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CRitsuView message handlers
