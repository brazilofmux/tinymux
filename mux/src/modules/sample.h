/*! \file sample.h
 * \brief Sample Module
 *
 */

#ifndef SAMPLE_H
#define SAMPLE_H

const MUX_CID CID_Sample        = UINT64_C(0x0000000265E759EF);
const MUX_CID CID_SumProxy      = UINT64_C(0x00000002FA46961E);
const MUX_CID CID_Sum           = UINT64_C(0x0000000214D47B2A);
const MUX_IID IID_ISample       = UINT64_C(0x00000002462F47F3);
const MUX_IID IID_ISum          = UINT64_C(0x00000002BAB94F6D);

interface ISample : public mux_IUnknown
{
public:
    virtual void Unregistering(void) = 0;
};

class CSample : public ISample, public mux_IServerEventsSink
{
private:
    mux_ILog                 *m_pILog;
    mux_IServerEventsControl *m_pIServerEventsControl;

public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual UINT32     AddRef(void);
    virtual UINT32     Release(void);

    // mux_IServerEventsSink
    //
    virtual void startup(void);
    virtual void presync_database(void);
    virtual void presync_database_sigsegv(void);
    virtual void dump_database(int dump_type);
    virtual void dump_complete_signal(void);
    virtual void shutdown(void);
    virtual void dbck(void);
    virtual void connect(dbref player, int isnew, int num);
    virtual void disconnect(dbref player, int num);
    virtual void data_create(dbref object);
    virtual void data_clone(dbref clone, dbref source);
    virtual void data_free(dbref object);

    // ISample
    //
    virtual void Unregistering(void);

    CSample(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CSample();

private:
    UINT32 m_cRef;
};

class CSampleFactory : public mux_IClassFactory
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

    CSampleFactory(void);
    virtual ~CSampleFactory();

private:
    UINT32 m_cRef;
};

interface ISum : public mux_IUnknown
{
public:
    virtual MUX_RESULT Add(int a, int b, int *psum) = 0;
};

class CSumProxy : public ISum, public mux_IMarshal
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

    // ISumProxy
    //
    virtual MUX_RESULT Add(int a, int b, int *sum);

    CSumProxy(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CSumProxy();

private:
    UINT32 m_nChannel;
    UINT32 m_cRef;
};

class CSumProxyFactory : public mux_IClassFactory
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

    CSumProxyFactory(void);
    virtual ~CSumProxyFactory();

private:
    UINT32 m_cRef;
};

#endif // SAMPLE_H
