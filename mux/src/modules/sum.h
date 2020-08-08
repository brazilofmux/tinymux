/*! \file sum.h
 * \brief Sum Out-of-Proc Module
 *
 */

#ifndef SUM_H
#define SUM_H

const MUX_CID CID_Sum        = UINT64_C(0x0000000214D47B2A);
const MUX_CID CID_SumProxy   = UINT64_C(0x00000002FA46961E);
const MUX_IID IID_ISum       = UINT64_C(0x00000002BAB94F6D);

interface ISum : public mux_IUnknown
{
public:
    virtual MUX_RESULT Add(int a, int b, int *psum) = 0;
};

class CSum : public ISum, public mux_IMarshal
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IMarshal
    //
    virtual MUX_RESULT GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid);
    virtual MUX_RESULT MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void *pv, marshal_context ctx);
    virtual MUX_RESULT UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv);
    virtual MUX_RESULT ReleaseMarshalData(QUEUE_INFO *pqi);
    virtual MUX_RESULT DisconnectObject(void);

    // ISum
    //
    virtual MUX_RESULT Add(int a, int b, int *sum);

    CSum(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CSum();

private:
    UINT32        m_cRef;
};

class CSumFactory : public mux_IClassFactory
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

    CSumFactory(void);
    virtual ~CSumFactory();

private:
    UINT32 m_cRef;
};

#endif // SUM_H
