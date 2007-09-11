/*! \file sum.h
 * \brief Sum Out-of-Proc Module
 *
 * $Id$
 *
 */

#ifndef SUM_H
#define SUM_H

#ifdef WIN32
const MUX_CID CID_Sum        = 0x0000000214D47B2Ai64;
const MUX_CID CID_SumProxy   = 0x00000002FA46961Ei64;
const MUX_IID IID_ISum       = 0x00000002BAB94F6Di64;
#else
const MUX_CID CID_Sum        = 0x0000000214D47B2Aull;
const MUX_CID CID_SumProxy   = 0x00000002FA46961Eull;
const MUX_IID IID_ISum       = 0x00000002BAB94F6Dull;
#endif

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
    virtual MUX_RESULT MarshalInterface(size_t *pnBuffer, void **pBuffer, MUX_IID riid, marshal_context ctx);
    virtual MUX_RESULT UnmarshalInterface(size_t nBuffer, void *pBuffer, MUX_IID riid, void **ppv);
    virtual MUX_RESULT ReleaseMarshalData(char *pBuffer);
    virtual MUX_RESULT DisconnectObject(void);

    // ISum
    //
    virtual MUX_RESULT Add(int a, int b, int *sum);

    CSum(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CSum();

private:
    UINT32        m_cRef;
    CHANNEL_INFO *m_pChannel;

    struct
    {
        UINT32 nChannel;
    } m_mipkt;
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
