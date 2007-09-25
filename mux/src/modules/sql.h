/*! \file sql.h
 * \brief Definitions for SQLProxy and SQLSlave Modules
 *
 * $Id$
 *
 */

#ifndef SQL_H
#define SQL_H

class CQueryServer : public mux_IQueryControl, public mux_IMarshal
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
    virtual MUX_RESULT MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, marshal_context ctx);
    virtual MUX_RESULT UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv);
    virtual MUX_RESULT ReleaseMarshalData(QUEUE_INFO *pqi);
    virtual MUX_RESULT DisconnectObject(void);

    // mux_IQueryControl
    //
    virtual MUX_RESULT Connect(UTF8 *pServer, UTF8 *pDatabase, UTF8 *pUser, UTF8 *pPassword);
    virtual MUX_RESULT Advise(mux_IQuerySink *pIQuerySink);
    virtual MUX_RESULT Query(UINT32 iQueryHandle, UTF8 *pDatabaseName, UTF8 *pQuery);

    CQueryServer(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CQueryServer();

private:
    UINT32 m_cRef;
};

class CQueryServerFactory : public mux_IClassFactory
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

    CQueryServerFactory(void);
    virtual ~CQueryServerFactory();

private:
    UINT32 m_cRef;
};

class CQuerySinkProxy : public mux_IQuerySink, public mux_IMarshal
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
    virtual MUX_RESULT MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, marshal_context ctx);
    virtual MUX_RESULT UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv);
    virtual MUX_RESULT ReleaseMarshalData(QUEUE_INFO *pqi);
    virtual MUX_RESULT DisconnectObject(void);

    // mux_IQuerySink
    //
    virtual MUX_RESULT Result(UINT32 iQueryHandle, UTF8 *pResultSet);

    CQuerySinkProxy(void);
    MUX_RESULT FinalConstruct(void);
    virtual ~CQuerySinkProxy();

private:
    UINT32 m_cRef;
    UINT32 m_nChannel;
};

class CQuerySinkProxyFactory : public mux_IClassFactory
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

    CQuerySinkProxyFactory(void);
    virtual ~CQuerySinkProxyFactory();

private:
    UINT32 m_cRef;
};

#endif // SQL_H
