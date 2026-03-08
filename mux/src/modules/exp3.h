/*! \file exp3.h
 * \brief Experiment 3 Module — Exercise module/netmux boundaries
 *
 * This module attempts to provide softcode functions that need to call
 * back into netmux for attribute access, object properties, and player
 * notification.  It documents every wall encountered.
 */

#ifndef EXP3_H
#define EXP3_H

const MUX_CID CID_Exp3 = UINT64_C(0x00000002A1B2C3D4);

interface mux_IExp3SinkControl : public mux_IUnknown
{
public:
    virtual void Unregistering(void) = 0;
};

class CExp3 : public mux_IExp3SinkControl, mux_IFunction
{
private:
    mux_ILog               *m_pILog;
    mux_IFunctionsControl  *m_pIFunctionsControl;

public:
    // mux_IUnknown
    //
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    uint32_t   AddRef(void) override;
    uint32_t   Release(void) override;

    // mux_IExp3SinkControl
    //
    void Unregistering(void) override;

    // mux_IFunction
    //
    MUX_RESULT Call(unsigned int nKey, UTF8 *buff, UTF8 **bufc, dbref executor,
                    dbref caller, dbref enactor, int eval, UTF8 *fargs[],
                    int nfargs, const UTF8 *cargs[], int ncargs) override;

    CExp3(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CExp3();

private:
    uint32_t m_cRef;
};

class CExp3Factory : public mux_IClassFactory
{
public:
    MUX_RESULT QueryInterface(MUX_IID iid, void **ppv) override;
    uint32_t   AddRef(void) override;
    uint32_t   Release(void) override;

    MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv) override;
    MUX_RESULT LockServer(bool bLock) override;

    CExp3Factory(void);
    virtual ~CExp3Factory();

private:
    uint32_t m_cRef;
};

#endif // EXP3_H
