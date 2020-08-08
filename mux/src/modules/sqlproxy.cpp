/*! \file sqlproxy.cpp
 * \brief SQLProxy Module
 *
 */

#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"
#include "sql.h"

static INT32 g_cComponents  = 0;
static INT32 g_cServerLocks = 0;

#define NUM_CLASSES 1
static MUX_CLASS_INFO sum_classes[NUM_CLASSES] =
{
    { CID_QueryControlProxy }
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

    if (CID_QueryControlProxy == cid)
    {
        CQueryControlProxyFactory *pQueryControlProxyFactory = NULL;
        try
        {
            pQueryControlProxyFactory = new CQueryControlProxyFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pQueryControlProxyFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pQueryControlProxyFactory->QueryInterface(iid, ppv);
        pQueryControlProxyFactory->Release();
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

// QueryControlProxy component which is not directly accessible.
//
CQueryControlProxy::CQueryControlProxy(void) : m_cRef(1), m_nChannel(CHANNEL_INVALID)
{
    g_cComponents++;
}

MUX_RESULT CQueryControlProxy::FinalConstruct(void)
{
    return MUX_S_OK;
}

CQueryControlProxy::~CQueryControlProxy()
{
    g_cComponents--;
}

MUX_RESULT CQueryControlProxy::QueryInterface(MUX_IID iid, void **ppv)
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

UINT32 CQueryControlProxy::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CQueryControlProxy::Release(void)
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

MUX_RESULT CQueryControlProxy::GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid)
{
    UNUSED_PARAMETER(riid);
    UNUSED_PARAMETER(ctx);
    UNUSED_PARAMETER(pcid);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryControlProxy::MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void *pv, marshal_context ctx)
{
    UNUSED_PARAMETER(pqi);
    UNUSED_PARAMETER(riid);
    UNUSED_PARAMETER(pv);
    UNUSED_PARAMETER(ctx);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryControlProxy::UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv)
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

MUX_RESULT CQueryControlProxy::ReleaseMarshalData(QUEUE_INFO *pqi)
{
    UNUSED_PARAMETER(pqi);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryControlProxy::DisconnectObject(void)
{
    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryControlProxy::Connect(const UTF8 *pServer, const UTF8 *pDatabase, const UTF8 *pUser, const UTF8 *pPassword)
{
    // Communicate with the remote component to service this request.
    //
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    UINT32 iMethod = 3;
    Pipe_AppendBytes(&qiFrame, sizeof(iMethod), &iMethod);

    struct FRAME
    {
        size_t nServer;
        size_t nDatabase;
        size_t nUser;
        size_t nPassword;
    } CallFrame;

    CallFrame.nServer   = (strlen((char *)pServer)+1)*sizeof(UTF8);
    CallFrame.nDatabase = (strlen((char *)pDatabase)+1)*sizeof(UTF8);
    CallFrame.nUser     = (strlen((char *)pUser)+1)*sizeof(UTF8);
    CallFrame.nPassword = (strlen((char *)pPassword)+1)*sizeof(UTF8);

    Pipe_AppendBytes(&qiFrame, sizeof(CallFrame), &CallFrame);
    Pipe_AppendBytes(&qiFrame, CallFrame.nServer, pServer);
    Pipe_AppendBytes(&qiFrame, CallFrame.nDatabase, pDatabase);
    Pipe_AppendBytes(&qiFrame, CallFrame.nUser, pUser);
    Pipe_AppendBytes(&qiFrame, CallFrame.nPassword, pPassword);

    mr = Pipe_SendCallPacketAndWait(m_nChannel, &qiFrame);

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

MUX_RESULT CQueryControlProxy::Advise(mux_IQuerySink *pIQuerySink)
{
    // Communicate with the remote component to service this request.
    //
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    UINT32 iMethod = 4;
    Pipe_AppendBytes(&qiFrame, sizeof(iMethod), &iMethod);

    mr = mux_MarshalInterface(&qiFrame, IID_IQuerySink, pIQuerySink, CrossProcess);
    if (MUX_SUCCEEDED(mr))
    {
        mr = Pipe_SendCallPacketAndWait(m_nChannel, &qiFrame);

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
    }
    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

MUX_RESULT CQueryControlProxy::Query(UINT32 iQueryHandle, const UTF8 *pDatabaseName, const UTF8 *pQuery)
{
    // Communicate with the remote component to service this request.
    //
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    UINT32 iMethod = 5;
    Pipe_AppendBytes(&qiFrame, sizeof(iMethod), &iMethod);

    struct FRAME
    {
        UINT32 iQueryHandle;
        size_t nDatabaseName;
        size_t nQuery;
    } CallFrame;

    CallFrame.iQueryHandle  = iQueryHandle;
    CallFrame.nDatabaseName = (strlen((char *)pDatabaseName)+1)*sizeof(UTF8);
    CallFrame.nQuery        = (strlen((char *)pQuery)+1)*sizeof(UTF8);

    Pipe_AppendBytes(&qiFrame, sizeof(CallFrame), &CallFrame);
    Pipe_AppendBytes(&qiFrame, CallFrame.nDatabaseName, pDatabaseName);
    Pipe_AppendBytes(&qiFrame, CallFrame.nQuery, pQuery);

    mr = Pipe_SendMsgPacket(m_nChannel, &qiFrame);

    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

// Factory for QueryControlProxy component which is not directly accessible.
//
CQueryControlProxyFactory::CQueryControlProxyFactory(void) : m_cRef(1)
{
}

CQueryControlProxyFactory::~CQueryControlProxyFactory()
{
}

MUX_RESULT CQueryControlProxyFactory::QueryInterface(MUX_IID iid, void **ppv)
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

UINT32 CQueryControlProxyFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CQueryControlProxyFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQueryControlProxyFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (NULL != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CQueryControlProxy *pQueryControlProxy = NULL;
    try
    {
        pQueryControlProxy = new CQueryControlProxy;
    }
    catch (...)
    {
        ; // Nothing.
    }

    MUX_RESULT mr;
    if (NULL == pQueryControlProxy)
    {
        return MUX_E_OUTOFMEMORY;
    }
    else
    {
        mr = pQueryControlProxy->FinalConstruct();
        if (MUX_FAILED(mr))
        {
            pQueryControlProxy->Release();
            return mr;
        }
    }

    mr = pQueryControlProxy->QueryInterface(iid, ppv);
    pQueryControlProxy->Release();
    return mr;
}

MUX_RESULT CQueryControlProxyFactory::LockServer(bool bLock)
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
