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

IMPLEMENT_DYNCREATE(CRitsuView, CEditView)

BEGIN_MESSAGE_MAP(CRitsuView, CEditView)
    //{{AFX_MSG_MAP(CRitsuView)
        // NOTE - the ClassWizard will add and remove mapping macros here.
        //    DO NOT EDIT what you see in these blocks of generated code!
    //}}AFX_MSG_MAP
    // Standard printing commands
    ON_COMMAND(ID_FILE_PRINT, CEditView::OnFilePrint)
    ON_COMMAND(ID_FILE_PRINT_DIRECT, CEditView::OnFilePrint)
    ON_COMMAND(ID_FILE_PRINT_PREVIEW, CEditView::OnFilePrintPreview)
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
    BOOL bPreCreated = CEditView::PreCreateWindow(cs);

    // Enable word-wrapping.
    //
    cs.style &= ~(ES_AUTOHSCROLL|WS_HSCROLL);

    return bPreCreated;
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
    // default CEditView preparation
    return CEditView::OnPreparePrinting(pInfo);
}

void CRitsuView::OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo)
{
    // Default CEditView begin printing.
    CEditView::OnBeginPrinting(pDC, pInfo);
}

void CRitsuView::OnEndPrinting(CDC* pDC, CPrintInfo* pInfo)
{
    // Default CEditView end printing
    CEditView::OnEndPrinting(pDC, pInfo);
}

/////////////////////////////////////////////////////////////////////////////
// CRitsuView diagnostics

#ifdef _DEBUG
void CRitsuView::AssertValid() const
{
    CEditView::AssertValid();
}

void CRitsuView::Dump(CDumpContext& dc) const
{
    CEditView::Dump(dc);
}

CRitsuDoc* CRitsuView::GetDocument() // non-debug version is inline
{
    ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CRitsuDoc)));
    return (CRitsuDoc*)m_pDocument;
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CRitsuView message handlers
