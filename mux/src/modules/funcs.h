/*! \file funcs.h
 * \brief Funcs Module
 *
 */

#ifndef FUNCS_H
#define FUNCS_H

const MUX_CID CID_Funcs = UINT64_C(0x000000026552A4A8);

interface mux_IFunctionSinkControl : public mux_IUnknown
{
public:
    virtual void Unregistering(void) = 0;
};

class CFuncs : public mux_IFunctionSinkControl, mux_IFunction
{
private:
    mux_ILog* m_pILog;
    mux_IFunctionsControl* m_pIFunctionsControl;

public:
    // mux_IUnknown
    //
    MUX_RESULT QueryInterface(MUX_IID iid, void** ppv) override;
    uint32_t AddRef(void) override;
    uint32_t Release(void) override;

    // IFunctionSinkControl
    //
    void Unregistering(void) override;

    // IFunction
    //
    MUX_RESULT Call(unsigned int nKey, UTF8* buff, UTF8** bufc, dbref executor, dbref caller,
                    dbref enactor, int eval, UTF8* fargs[], int nfargs, const UTF8* cargs[],
                    int ncargs) override;

    CFuncs(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CFuncs();

private:
    uint32_t m_cRef;
};

class CFuncsFactory : public mux_IClassFactory
{
public:
    // mux_IUnknown
    //
    MUX_RESULT QueryInterface(MUX_IID iid, void** ppv) override;
    uint32_t AddRef(void) override;
    uint32_t Release(void) override;

    // mux_IClassFactory
    //
    MUX_RESULT CreateInstance(mux_IUnknown* pUnknownOuter, MUX_IID iid, void** ppv) override;
    MUX_RESULT LockServer(bool bLock) override;

    CFuncsFactory(void);
    virtual ~CFuncsFactory();

private:
    uint32_t m_cRef;
};

#endif // FUNCS_H
