/*! \file funcs.h
 * \brief Funcs Module
 *
 * $Id$
 *
 */

#ifndef FUNCS_H
#define FUNCS_H

const MUX_CID CID_Funcs       = UINT64_C(0x000000026552A4A8);

interface mux_IFunctionSinkControl : public mux_IUnknown
{
public:
    virtual void Unregistering(void) = 0;
};

class CFuncs : public mux_IFunctionSinkControl, mux_IFunction
{
private:
    mux_ILog               *m_pILog;
    mux_IFunctionsControl  *m_pIFunctionsControl;

public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // IFunctionSinkControl
    //
    virtual void Unregistering(void);

    // IFunction
    //
    virtual MUX_RESULT Call(unsigned int nKey, __in UTF8 *buff, __deref_inout UTF8 **bufc, dbref executor, dbref caller,
        dbref enactor, int eval, __in UTF8 *fargs[], int nfargs, __in const UTF8 *cargs[], int ncargs);

    CFuncs(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CFuncs();

private:
    UINT32        m_cRef;
};

class CFuncsFactory : public mux_IClassFactory
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IClassFactory
    //
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);

    CFuncsFactory(void);
    virtual ~CFuncsFactory();

private:
    UINT32 m_cRef;
};

#endif // FUNCS_H
