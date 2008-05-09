// ritsuDoc.h : interface of the CRitsuDoc class
//
/////////////////////////////////////////////////////////////////////////////

#if !defined(AFX_RITSUDOC_H__0D7A1168_C20F_41C2_B56D_B04E18E8CC5F__INCLUDED_)
#define AFX_RITSUDOC_H__0D7A1168_C20F_41C2_B56D_B04E18E8CC5F__INCLUDED_

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000


class CRitsuDoc : public CDocument
{
protected: // create from serialization only
	CRitsuDoc();
	DECLARE_DYNCREATE(CRitsuDoc)

// Attributes
public:

// Operations
public:

// Overrides
	// ClassWizard generated virtual function overrides
	//{{AFX_VIRTUAL(CRitsuDoc)
	public:
	virtual BOOL OnNewDocument();
	virtual void Serialize(CArchive& ar);
	//}}AFX_VIRTUAL

// Implementation
public:
	virtual ~CRitsuDoc();
#ifdef _DEBUG
	virtual void AssertValid() const;
	virtual void Dump(CDumpContext& dc) const;
#endif

protected:

// Generated message map functions
protected:
	//{{AFX_MSG(CRitsuDoc)
		// NOTE - the ClassWizard will add and remove member functions here.
		//    DO NOT EDIT what you see in these blocks of generated code !
	//}}AFX_MSG
	DECLARE_MESSAGE_MAP()

	// Generated OLE dispatch map functions
	//{{AFX_DISPATCH(CRitsuDoc)
		// NOTE - the ClassWizard will add and remove member functions here.
		//    DO NOT EDIT what you see in these blocks of generated code !
	//}}AFX_DISPATCH
	DECLARE_DISPATCH_MAP()
	DECLARE_INTERFACE_MAP()
};

/////////////////////////////////////////////////////////////////////////////

//{{AFX_INSERT_LOCATION}}
// Microsoft Visual C++ will insert additional declarations immediately before the previous line.

#endif // !defined(AFX_RITSUDOC_H__0D7A1168_C20F_41C2_B56D_B04E18E8CC5F__INCLUDED_)
