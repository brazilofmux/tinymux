// ritsuView.h : interface of the CRitsuView class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_RITSUVIEW_H__224BD6AA_4992_4663_8063_E9BE98217074__INCLUDED_)
#define AFX_RITSUVIEW_H__224BD6AA_4992_4663_8063_E9BE98217074__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


class CRitsuView : public CEditView
{
protected: // create from serialization only
    CRitsuView();
    DECLARE_DYNCREATE(CRitsuView)

    // Attributes
public:
    CRitsuDoc* GetDocument();

    // Operations
public:

    // Overrides
    // ClassWizard generated virtual function overrides
    //{{AFX_VIRTUAL(CRitsuView)
public:
    virtual void OnDraw(CDC* pDC);  // overridden to draw this view
    virtual BOOL PreCreateWindow(CREATESTRUCT& cs);
protected:
    virtual BOOL OnPreparePrinting(CPrintInfo* pInfo);
    virtual void OnBeginPrinting(CDC* pDC, CPrintInfo* pInfo);
    virtual void OnEndPrinting(CDC* pDC, CPrintInfo* pInfo);
    //}}AFX_VIRTUAL

    // Implementation
public:
    virtual ~CRitsuView();
#ifdef _DEBUG
    virtual void AssertValid() const;
    virtual void Dump(CDumpContext& dc) const;
#endif
    
protected:
    
    // Generated message map functions
protected:
    //{{AFX_MSG(CRitsuView)
    // NOTE - the ClassWizard will add and remove member functions here.
    //    DO NOT EDIT what you see in these blocks of generated code !
    //}}AFX_MSG
    DECLARE_MESSAGE_MAP()
};

#ifndef _DEBUG  // debug version in ritsuView.cpp
inline CRitsuDoc* CRitsuView::GetDocument()
{ return (CRitsuDoc*)m_pDocument; }
#endif

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_RITSUVIEW_H__224BD6AA_4992_4663_8063_E9BE98217074__INCLUDED_)
