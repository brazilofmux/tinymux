/*! \file sqlslave.cpp
 * \brief SQLSlave Module
 *
 * $Id$
 *
 */

#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"
#include "sql.h"

static INT32 g_cComponents  = 0;
static INT32 g_cServerLocks = 0;

#define NUM_CLASSES 2
static CLASS_INFO sum_classes[NUM_CLASSES] =
{
    { CID_QueryServer    },
    { CID_QuerySinkProxy }
};

// The following four functions are for access by dlopen.
//
extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CanUnloadNow(void)
{
    if (  0 == g_cComponents
       && 0 == g_cServerLocks)
    {
        return MUX_S_OK;
    }
    else
    {
        return MUX_S_FALSE;
    }
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_QueryServer == cid)
    {
        CQueryServerFactory *pQueryServerFactory = NULL;
        try
        {
            pQueryServerFactory = new CQueryServerFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pQueryServerFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pQueryServerFactory->QueryInterface(iid, ppv);
        pQueryServerFactory->Release();
    }
    else if (CID_QuerySinkProxy == cid)
    {
        CQuerySinkProxyFactory *pQuerySinkProxyFactory = NULL;
        try
        {
            pQuerySinkProxyFactory = new CQuerySinkProxyFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pQuerySinkProxyFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pQuerySinkProxyFactory->QueryInterface(iid, ppv);
        pQuerySinkProxyFactory->Release();
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register(void)
{
    // Advertise our components.
    //
    MUX_RESULT mr = mux_RegisterClassObjects(NUM_CLASSES, sum_classes, NULL);
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Unregister(void)
{
    return mux_RevokeClassObjects(NUM_CLASSES, sum_classes);
}

// QueryServer component which is not directly accessible.
//
CQueryServer::CQueryServer(void) : m_cRef(1), m_pIQuerySink(NULL)
{
    g_cComponents++;
}

MUX_RESULT CQueryServer::FinalConstruct(void)
{
    MUX_RESULT mr = MUX_S_OK;
    return mr;
}

CQueryServer::~CQueryServer()
{
    if (NULL != m_pIQuerySink)
    {
        m_pIQuerySink->Release();
        m_pIQuerySink = NULL;
    }

    g_cComponents--;
}

MUX_RESULT CQueryServer::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IQueryControl *>(this);
    }
    else if (IID_IQueryControl == iid)
    {
        *ppv = static_cast<mux_IQueryControl *>(this);
    }
    else if (mux_IID_IMarshal == iid)
    {
        *ppv = static_cast<mux_IMarshal *>(this);
    }
    else
    {
        *ppv = NULL;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CQueryServer::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CQueryServer::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQueryServer::GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid)
{
    UNUSED_PARAMETER(ctx);

    if (NULL == pcid)
    {
        return MUX_E_INVALIDARG;
    }
    else if (  IID_IQueryControl == riid
            && CrossProcess == ctx)
    {
        // We only support cross-process at the moment.
        //
        *pcid = CID_QueryControlProxy;
        return MUX_S_OK;
    }
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryControl_Disconnect(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    UNUSED_PARAMETER(pqi);

    // Get our interface pointer from the channel.
    //
    mux_IUnknown *pIUnknown= static_cast<mux_IUnknown *>(pci->pInterface);
    pci->pInterface = NULL;

    // Tear down our side of the communication.  Our callback functions will
    // no longer be called.
    //
    Pipe_FreeChannel(pci);

    if (NULL != pIUnknown)
    {
        pIUnknown->Release();
        return MUX_S_OK;
    }
    else
    {
        return MUX_E_NOINTERFACE;
    }
}

MUX_RESULT CQueryControl_Call(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    mux_IQueryControl *pIQueryControl = static_cast<mux_IQueryControl *>(pci->pInterface);
    if (NULL == pIQueryControl)
    {
        return MUX_E_NOINTERFACE;
    }

    UINT32 iMethod;
    size_t nWanted = sizeof(iMethod);
    if (  !Pipe_GetBytes(pqi, &nWanted, &iMethod)
       || nWanted != sizeof(iMethod))
    {
        return MUX_E_INVALIDARG;
    }

    // The IUnknown methods (0, 1, and 2) do not make it across, so we don't
    // attempt to handle them here.  Instead, when the reference count on
    // CQueryServerProxy goes to zero, it drops the connection and destroys
    // itself.  We see that as a call to CQueryControl_Disconnect.
    //
    switch (iMethod)
    {
    case 3:  // MUX_RESULT Connect(UTF8 *pServer, UTF8 *pDatabase, UTF8 *pUser, UTF8 *pPassword);
        {
            struct FRAME
            {
                size_t nServer;
                size_t nDatabase;
                size_t nUser;
                size_t nPassword;
            } CallFrame;

            struct RETURN
            {
                MUX_RESULT mr;
            } ReturnFrame = { MUX_S_OK };

            nWanted = sizeof(CallFrame);
            if (  !Pipe_GetBytes(pqi, &nWanted, &CallFrame)
               || nWanted != sizeof(CallFrame))
            {
                ReturnFrame.mr = MUX_E_INVALIDARG;
            }

            if (MUX_SUCCEEDED(ReturnFrame.mr))
            {
                UTF8 *pServer = NULL;
                UTF8 *pDatabase = NULL;
                UTF8 *pUser = NULL;
                UTF8 *pPassword = NULL;
                try
                {
                    pServer   = new UTF8[CallFrame.nServer];
                    pDatabase = new UTF8[CallFrame.nDatabase];
                    pUser     = new UTF8[CallFrame.nUser];
                    pPassword = new UTF8[CallFrame.nPassword];
                }
                catch (...)
                {
                    ; // Nothing.
                }

                if (  NULL != pServer
                   && NULL != pDatabase
                   && NULL != pUser
                   && NULL != pPassword)
                {
                    nWanted = CallFrame.nServer;
                    if (  Pipe_GetBytes(pqi, &nWanted, pServer)
                       && nWanted == CallFrame.nServer)
                    {
                        nWanted = CallFrame.nDatabase;
                        if (  Pipe_GetBytes(pqi, &nWanted, pDatabase)
                           && nWanted == CallFrame.nDatabase)
                        {
                            nWanted = CallFrame.nUser;
                            if (  Pipe_GetBytes(pqi, &nWanted, pUser)
                               && nWanted == CallFrame.nUser)
                            {
                                nWanted = CallFrame.nPassword;
                                if (  Pipe_GetBytes(pqi, &nWanted, pPassword)
                                   && nWanted == CallFrame.nPassword)
                                {
                                    ReturnFrame.mr = pIQueryControl->Connect(pServer, pDatabase, pUser, pPassword);
                                }
                                else
                                {
                                    ReturnFrame.mr = MUX_E_INVALIDARG;
                                }
                            }
                            else
                            {
                                ReturnFrame.mr = MUX_E_INVALIDARG;
                            }
                        }
                        else
                        {
                            ReturnFrame.mr = MUX_E_INVALIDARG;
                        }
                    }
                    else
                    {
                        ReturnFrame.mr = MUX_E_INVALIDARG;
                    }
                }
                else
                {
                    ReturnFrame.mr = MUX_E_OUTOFMEMORY;
                }

                if (NULL != pServer)
                {
                    delete [] pServer;
                    pServer = NULL;
                }

                if (NULL != pDatabase)
                {
                    delete [] pDatabase;
                    pDatabase = NULL;
                }

                if (NULL != pUser)
                {
                    delete [] pUser;
                    pUser = NULL;
                }

                if (NULL != pPassword)
                {
                    delete [] pPassword;
                    pPassword = NULL;
                }
            }
            Pipe_EmptyQueue(pqi);
            Pipe_AppendBytes(pqi, sizeof(ReturnFrame), &ReturnFrame);
            return MUX_S_OK;
        }
        break;

    case 4:  // MUX_RESULT Advise(mux_IQuerySink *pIQuerySink);
        {
            struct RETURN
            {
                MUX_RESULT mr;
            } ReturnFrame = { MUX_S_OK };

            mux_IQuerySink *pIQuerySink = NULL;
            ReturnFrame.mr = mux_UnmarshalInterface(pqi, IID_IQuerySink, (void **)&pIQuerySink);

            if (MUX_SUCCEEDED(ReturnFrame.mr))
            {
                ReturnFrame.mr = pIQueryControl->Advise(pIQuerySink);
            }

            Pipe_EmptyQueue(pqi);
            Pipe_AppendBytes(pqi, sizeof(ReturnFrame), &ReturnFrame);
            return MUX_S_OK;
        }
        break;

    case 5:  // MUX_RESULT Query(UINT32 iQueryHandle, UTF8 *pDatabaseName, UTF8 *pQuery);
        {
            struct FRAME
            {
                UINT32 iQueryHandle;
                size_t nDatabaseName;
                size_t nQuery;
            } CallFrame;

            struct RETURN
            {
                MUX_RESULT mr;
            } ReturnFrame = { MUX_S_OK };

            nWanted = sizeof(CallFrame);
            if (  !Pipe_GetBytes(pqi, &nWanted, &CallFrame)
               || nWanted != sizeof(CallFrame))
            {
                ReturnFrame.mr = MUX_E_INVALIDARG;
            }

            if (MUX_SUCCEEDED(ReturnFrame.mr))
            {
                UTF8 *pDatabaseName = NULL;
                UTF8 *pQuery = NULL;
                try
                {
                    pDatabaseName = new UTF8[CallFrame.nDatabaseName];
                    pQuery        = new UTF8[CallFrame.nQuery];
                }
                catch (...)
                {
                    ; // Nothing.
                }

                if (  NULL != pDatabaseName
                   && NULL != pQuery)
                {
                    nWanted = CallFrame.nDatabaseName;
                    if (  Pipe_GetBytes(pqi, &nWanted, pDatabaseName)
                       && nWanted == CallFrame.nDatabaseName)
                    {
                        nWanted = CallFrame.nQuery;
                        if (  Pipe_GetBytes(pqi, &nWanted, pQuery)
                           && nWanted == CallFrame.nQuery)
                        {
                            ReturnFrame.mr = pIQueryControl->Query(CallFrame.iQueryHandle, pDatabaseName, pQuery);
                        }
                        else
                        {
                            ReturnFrame.mr = MUX_E_INVALIDARG;
                        }
                    }
                    else
                    {
                        ReturnFrame.mr = MUX_E_INVALIDARG;
                    }
                }
                else
                {
                    ReturnFrame.mr = MUX_E_OUTOFMEMORY;
                }

                if (NULL != pDatabaseName)
                {
                    delete [] pDatabaseName;
                    pDatabaseName = NULL;
                }

                if (NULL != pQuery)
                {
                    delete [] pQuery;
                    pQuery = NULL;
                }
            }
            Pipe_EmptyQueue(pqi);
            Pipe_AppendBytes(pqi, sizeof(ReturnFrame), &ReturnFrame);
            return MUX_S_OK;
        }
        break;
    }
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryControl_Msg(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    // The same as CQueryControl_Call except that the caller is no longer
    // available to receive the ReturnFrame.
    //
    return CQueryControl_Call(pci, pqi);
}

MUX_RESULT CQueryServer::MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, marshal_context ctx)
{
    // Parameter validation and initialization.
    //
    MUX_RESULT mr = MUX_S_OK;
    if (NULL == pqi)
    {
        mr = MUX_E_INVALIDARG;
    }
    else if (IID_IQueryControl != riid)
    {
        mr = MUX_E_FAIL;
    }
    else if (CrossProcess != ctx)
    {
        mr = MUX_E_NOTIMPLEMENTED;
    }
    else
    {
        mux_IQueryControl *pIQueryControl = NULL;
        mr = QueryInterface(IID_IQueryControl, (void **)&pIQueryControl);
        if (MUX_SUCCEEDED(mr))
        {
            // Construct a packet sufficient to allow the proxy to communicate with us.
            //
            CHANNEL_INFO *pChannel = Pipe_AllocateChannel(CQueryControl_Call, CQueryControl_Msg, CQueryControl_Disconnect);
            if (NULL != pChannel)
            {
                pChannel->pInterface = pIQueryControl;
                Pipe_AppendBytes(pqi, sizeof(pChannel->nChannel), (UTF8*)(&pChannel->nChannel));
                mr =  MUX_S_OK;
            }
            else
            {
                pIQueryControl->Release();
                pIQueryControl = NULL;
                mr = MUX_E_OUTOFMEMORY;
            }
        }
    }
    return mr;
}

MUX_RESULT CQueryServer::UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv)
{
    UNUSED_PARAMETER(pqi);
    UNUSED_PARAMETER(riid);
    UNUSED_PARAMETER(ppv);

    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryServer::ReleaseMarshalData(QUEUE_INFO *pqi)
{
    // Since the Marshal Data is like an extra reference on an object, if the
    // Marshaled Data is never unmarshalled, libmux should use this function
    // to release the reference to the component.  This is only implemented on
    // the server side -- not the proxy.
    //
    UINT32 nChannel;
    size_t nWanted = sizeof(nChannel);
    if (  Pipe_GetBytes(pqi, &nWanted, &nChannel)
       && sizeof(nChannel) == nWanted)
    {
        CHANNEL_INFO *pChannel = Pipe_FindChannel(nChannel);
        if (NULL != pChannel)
        {
            CQueryControl_Disconnect(pChannel, pqi);
        }
    }
    return MUX_S_OK;
}

MUX_RESULT CQueryServer::DisconnectObject(void)
{
    // This is called when the hosting process is about to go down to give the
    // component a chance to notify its proxy that it is about to shut down.
    // This is only implemented on the server side -- not the proxy.
    //
    // TODO: There isn't a mechanism for sending such a notification, yet.
    //
    return MUX_S_OK;
}

MUX_RESULT CQueryServer::Connect(const UTF8 *pServer, const UTF8 *pDatabase, const UTF8 *pUser, const UTF8 *pPassword)
{
    UNUSED_PARAMETER(pServer);
    UNUSED_PARAMETER(pDatabase);
    UNUSED_PARAMETER(pUser);
    UNUSED_PARAMETER(pPassword);

    // TODO: Use these as necessary to make a connection to MySQL.
    //
    return MUX_S_OK;
}

MUX_RESULT CQueryServer::Advise(mux_IQuerySink *pIQuerySink)
{
    if (NULL != m_pIQuerySink)
    {
        m_pIQuerySink->Release();
        m_pIQuerySink = NULL;
    }

    if (NULL == pIQuerySink)
    {
        return MUX_E_INVALIDARG;
    }

    m_pIQuerySink = pIQuerySink;
    return MUX_S_OK;
}

MUX_RESULT CQueryServer::Query(UINT32 iQueryHandle, const UTF8 *pDatabaseName, const UTF8 *pQuery)
{
    UNUSED_PARAMETER(pQuery);
    UNUSED_PARAMETER(pDatabaseName);

    if (NULL != m_pIQuerySink)
    {
        return m_pIQuerySink->Result(iQueryHandle, T("Yeah, I'm here."));
    }
    else
    {
        return MUX_E_NOTREADY;
    }
}

// Factory for CQueryServer component which is not directly accessible.
//
CQueryServerFactory::CQueryServerFactory(void) : m_cRef(1)
{
}

CQueryServerFactory::~CQueryServerFactory()
{
}

MUX_RESULT CQueryServerFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else if (mux_IID_IClassFactory == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else
    {
        *ppv = NULL;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CQueryServerFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CQueryServerFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQueryServerFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (NULL != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CQueryServer *pQueryServer = NULL;
    try
    {
        pQueryServer = new CQueryServer;
    }
    catch (...)
    {
        ; // Nothing.
    }

    MUX_RESULT mr;
    if (NULL == pQueryServer)
    {
        return MUX_E_OUTOFMEMORY;
    }
    else
    {
        mr = pQueryServer->FinalConstruct();
        if (MUX_FAILED(mr))
        {
            pQueryServer->Release();
            return mr;
        }
    }

    mr = pQueryServer->QueryInterface(iid, ppv);
    pQueryServer->Release();
    return mr;
}

MUX_RESULT CQueryServerFactory::LockServer(bool bLock)
{
    if (bLock)
    {
        g_cServerLocks++;
    }
    else
    {
        g_cServerLocks--;
    }
    return MUX_S_OK;
}

// QuerySinkProxy component which is not directly accessible.
//
CQuerySinkProxy::CQuerySinkProxy(void) : m_cRef(1), m_nChannel(CHANNEL_INVALID)
{
    g_cComponents++;
}

MUX_RESULT CQuerySinkProxy::FinalConstruct(void)
{
    return MUX_S_OK;
}

CQuerySinkProxy::~CQuerySinkProxy()
{
    g_cComponents--;
}

MUX_RESULT CQuerySinkProxy::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IQuerySink *>(this);
    }
    else if (IID_IQuerySink == iid)
    {
        *ppv = static_cast<mux_IQuerySink *>(this);
    }
    else if (mux_IID_IMarshal == iid)
    {
        *ppv = static_cast<mux_IMarshal *>(this);
    }
    else
    {
        *ppv = NULL;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CQuerySinkProxy::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CQuerySinkProxy::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        // The last reference to the proxy was released, we need to clean up
        // the connection as well.
        //
        QUEUE_INFO qiFrame;
        Pipe_InitializeQueueInfo(&qiFrame);
        (void)Pipe_SendDiscPacket(m_nChannel, &qiFrame);
        m_nChannel = CHANNEL_INVALID;
        Pipe_EmptyQueue(&qiFrame);

        delete this;
        return 0;
    }
    return m_cRef;
}
MUX_RESULT CQuerySinkProxy::GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid)
{
    UNUSED_PARAMETER(riid);
    UNUSED_PARAMETER(ctx);
    UNUSED_PARAMETER(pcid);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQuerySinkProxy::MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, marshal_context ctx)
{
    UNUSED_PARAMETER(pqi);
    UNUSED_PARAMETER(riid);
    UNUSED_PARAMETER(ctx);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQuerySinkProxy::UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv)
{
    // Use the channel number in the marshal packet from the remote component
    // to support a proxy IQueryControl.
    //
    size_t nWanted = sizeof(m_nChannel);
    if (  Pipe_GetBytes(pqi, &nWanted, &m_nChannel)
       && nWanted == sizeof(m_nChannel))
    {
        return QueryInterface(riid, ppv);
    }
    return MUX_E_NOINTERFACE;
}

MUX_RESULT CQuerySinkProxy::ReleaseMarshalData(QUEUE_INFO *pqi)
{
    UNUSED_PARAMETER(pqi);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQuerySinkProxy::DisconnectObject(void)
{
    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQuerySinkProxy::Result(UINT32 iQueryHandle, const UTF8 *pResultSet)
{
    UNUSED_PARAMETER(iQueryHandle);

    // Communicate with the remote component to service this request.
    //
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    UINT32 iMethod = 3;
    Pipe_AppendBytes(&qiFrame, sizeof(iMethod), &iMethod);

    struct FRAME
    {
        size_t iQueryHandle;
        size_t nResultSet;
    } CallFrame;

    CallFrame.nResultSet = (strlen((char *)pResultSet)+1)*sizeof(UTF8);

    Pipe_AppendBytes(&qiFrame, sizeof(CallFrame), &CallFrame);
    Pipe_AppendBytes(&qiFrame, CallFrame.nResultSet, pResultSet);

    mr = Pipe_SendMsgPacket(m_nChannel, &qiFrame);

    if (MUX_SUCCEEDED(mr))
    {
        struct RETURN
        {
            MUX_RESULT mr;
        } ReturnFrame;

        size_t nWanted = sizeof(ReturnFrame);
        if (  Pipe_GetBytes(&qiFrame, &nWanted, &ReturnFrame)
           && nWanted == sizeof(ReturnFrame))
        {
            mr = ReturnFrame.mr;
        }
        else
        {
            mr = MUX_E_FAIL;
        }
    }
    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

// Factory for QuerySinkProxy component which is not directly accessible.
//
CQuerySinkProxyFactory::CQuerySinkProxyFactory(void) : m_cRef(1)
{
}

CQuerySinkProxyFactory::~CQuerySinkProxyFactory()
{
}

MUX_RESULT CQuerySinkProxyFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else if (mux_IID_IClassFactory == iid)
    {
        *ppv = static_cast<mux_IClassFactory *>(this);
    }
    else
    {
        *ppv = NULL;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CQuerySinkProxyFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CQuerySinkProxyFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQuerySinkProxyFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (NULL != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CQuerySinkProxy *pQuerySinkProxy = NULL;
    try
    {
        pQuerySinkProxy = new CQuerySinkProxy;
    }
    catch (...)
    {
        ; // Nothing.
    }

    MUX_RESULT mr;
    if (NULL == pQuerySinkProxy)
    {
        return MUX_E_OUTOFMEMORY;
    }
    else
    {
        mr = pQuerySinkProxy->FinalConstruct();
        if (MUX_FAILED(mr))
        {
            pQuerySinkProxy->Release();
            return mr;
        }
    }

    mr = pQuerySinkProxy->QueryInterface(iid, ppv);
    pQuerySinkProxy->Release();
    return mr;
}

MUX_RESULT CQuerySinkProxyFactory::LockServer(bool bLock)
{
    if (bLock)
    {
        g_cServerLocks++;
    }
    else
    {
        g_cServerLocks--;
    }
    return MUX_S_OK;
}
