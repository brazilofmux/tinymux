/*! \file modules.cpp
 * \brief netmux-provided modules.
 *
 * Interfaces and classes declared here are built into the netmux server and
 * are available to netmux itself and to dynamically-loaded external modules.
 *
 * $Id$
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

#if defined(HAVE_DLOPEN) || defined(WIN32)

#define NUM_CLASSES 4
static CLASS_INFO netmux_classes[NUM_CLASSES] =
{
    { CID_Log                },
    { CID_ServerEventsSource },
    { CID_StubSlaveProxy     },
    { CID_QueryClient        }
};

extern "C" MUX_RESULT DCL_API netmux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_Log == cid)
    {
        CLogFactory *pLogFactory = NULL;
        try
        {
            pLogFactory = new CLogFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pLogFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pLogFactory->QueryInterface(iid, ppv);
        pLogFactory->Release();
    }
    else if (CID_ServerEventsSource == cid)
    {
        CServerEventsSourceFactory *pServerEventsSourceFactory = NULL;
        try
        {
            pServerEventsSourceFactory = new CServerEventsSourceFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pServerEventsSourceFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pServerEventsSourceFactory->QueryInterface(iid, ppv);
        pServerEventsSourceFactory->Release();
    }
    else if (CID_StubSlaveProxy == cid)
    {
        CStubSlaveProxyFactory *pStubSlaveProxyFactory = NULL;
        try
        {
            pStubSlaveProxyFactory = new CStubSlaveProxyFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pStubSlaveProxyFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pStubSlaveProxyFactory->QueryInterface(iid, ppv);
        pStubSlaveProxyFactory->Release();
    }
    else if (CID_QueryClient == cid)
    {
        CQueryClientFactory *pQueryClientFactory = NULL;
        try
        {
            pQueryClientFactory = new CQueryClientFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pQueryClientFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pQueryClientFactory->QueryInterface(iid, ppv);
        pQueryClientFactory->Release();
    }
    return mr;
}

#ifdef STUB_SLAVE
QUEUE_INFO Queue_In;
QUEUE_INFO Queue_Out;
#endif

void init_modules(void)
{
#ifdef STUB_SLAVE
    Pipe_InitializeQueueInfo(&Queue_In);
    Pipe_InitializeQueueInfo(&Queue_Out);
    MUX_RESULT mr = mux_InitModuleLibrary(IsMainProcess, pipepump, &Queue_In, &Queue_Out);
#else
    MUX_RESULT mr = mux_InitModuleLibrary(IsMainProcess, NULL, NULL, NULL);
#endif
    if (MUX_SUCCEEDED(mr))
    {
        mr = mux_RegisterClassObjects(NUM_CLASSES, netmux_classes, netmux_GetClassObject);
    }

    if (MUX_SUCCEEDED(mr))
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf("Registered netmux modules.");
        ENDLOG;

#if defined(STUB_SLAVE)
        if (NULL != mudstate.pISlaveControl)
        {
            mudstate.pISlaveControl->Release();
            mudstate.pISlaveControl = NULL;
        }

        mr = mux_CreateInstance(CID_StubSlave, NULL, UseSlaveProcess, IID_ISlaveControl, (void **)&mudstate.pISlaveControl);
        if (MUX_SUCCEEDED(mr))
        {
            STARTLOG(LOG_ALWAYS, "INI", "LOAD");
            log_printf("Opened interface for StubSlave management.");
            ENDLOG;
        }
        else
        {
            STARTLOG(LOG_ALWAYS, "INI", "LOAD");
            log_printf("Failed to open interface for StubSlave management (%d).", mr);
            ENDLOG;
        }
#endif // STUB_SLAVE
    }
    else
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf("Failed to register netmux modules (%d).", mr);
        ENDLOG;
    }
}

void final_modules(void)
{
    MUX_RESULT mr = MUX_S_OK;

#if defined(STUB_SLAVE)
    if (NULL != mudstate.pISlaveControl)
    {
        mr = mudstate.pISlaveControl->ShutdownSlave();
        if (MUX_FAILED(mr))
        {
            STARTLOG(LOG_ALWAYS, "INI", "LOAD");
            log_printf("Failed to request stubslave to shutdown (%d).", mr);
            ENDLOG;
        }

        mudstate.pISlaveControl->Release();
        mudstate.pISlaveControl = NULL;
    }
#endif // STUB_SLAVE

    mr = mux_RevokeClassObjects(NUM_CLASSES, netmux_classes);
    if (MUX_FAILED(mr))
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf("Failed to revoke netmux modules (%d).", mr);
        ENDLOG;
    }
    else
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf("Revoked netmux modules.", mr);
        ENDLOG;
    }
    mux_FinalizeModuleLibrary();
}

// CLog component which is not directly accessible.
//
CLog::CLog(void) : m_cRef(1)
{
}

CLog::~CLog()
{
}

MUX_RESULT CLog::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_ILog *>(this);
    }
    else if (IID_ILog == iid)
    {
        *ppv = static_cast<mux_ILog *>(this);
    }
    else
    {
        *ppv = NULL;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CLog::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CLog::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

bool CLog::start_log(int key, const UTF8 *primary, const UTF8 *secondary)
{
    if (  ((key) & mudconf.log_options) != 0
       && ::start_log(primary, secondary))
    {
        return true;
    }
    else
    {
        return false;
    }
}

void CLog::log_perror(const UTF8 *primary, const UTF8 *secondary, const UTF8 *extra, const UTF8 *failing_object)
{
    ::log_perror(primary, secondary, extra, failing_object);
}

void CLog::log_text(const UTF8 *text)
{
    ::log_text(text);
}

void CLog::log_number(int num)
{
    ::log_number(num);
}

void DCL_CDECL CLog::log_printf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    UTF8 aTempBuffer[SIZEOF_LOG_BUFFER];
    size_t nString = mux_vsnprintf(aTempBuffer, SIZEOF_LOG_BUFFER, fmt, ap);
    va_end(ap);
    Log.WriteBuffer(nString, aTempBuffer);
}

void CLog::log_name(dbref target)
{
    ::log_name(target);
}

void CLog::log_name_and_loc(dbref player)
{
    ::log_name_and_loc(player);
}

void CLog::log_type_and_name(dbref thing)
{
    ::log_type_and_name(thing);
}

void CLog::end_log(void)
{
    ::end_log();
}

// Factory for CLog component which is not directly accessible.
//
CLogFactory::CLogFactory(void) : m_cRef(1)
{
}

CLogFactory::~CLogFactory()
{
}

MUX_RESULT CLogFactory::QueryInterface(MUX_IID iid, void **ppv)
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

UINT32 CLogFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CLogFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CLogFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (NULL != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CLog *pLog = NULL;
    try
    {
        pLog = new CLog;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == pLog)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pLog->QueryInterface(iid, ppv);
    pLog->Release();
    return mr;
}

MUX_RESULT CLogFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// CServerEventsSource component which is not directly accessible.
//
CServerEventsSource::CServerEventsSource(void) : m_cRef(1), m_pSink(NULL)
{
}

ServerEventsSinkNode *g_pServerEventsSinkListHead = NULL;

CServerEventsSource::~CServerEventsSource()
{
    if (NULL != m_pSink)
    {
        ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
        ServerEventsSinkNode *q = NULL;
        while (NULL != p)
        {
            if (p->pSink == m_pSink)
            {
                // Unlink node p from list.
                //
                if (NULL == q)
                {
                    g_pServerEventsSinkListHead = p->pNext;
                }
                else
                {
                    q->pNext = p->pNext;
                }
                p->pNext = NULL;

                // Free sink and node.
                //
                p->pSink->Release();
                p->pSink = NULL;
                delete p;
                break;
            }
            q = p;
            p = p->pNext;
        }

        m_pSink->Release();
        m_pSink = NULL;
    }
}

MUX_RESULT CServerEventsSource::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IServerEventsControl *>(this);
    }
    else if (IID_IServerEventsControl == iid)
    {
        *ppv = static_cast<mux_IServerEventsControl *>(this);
    }
    else
    {
        *ppv = NULL;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CServerEventsSource::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CServerEventsSource::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CServerEventsSource::Advise(mux_IServerEventsSink *pIServerEventsSink)
{
    if (NULL == pIServerEventsSink)
    {
        return MUX_E_INVALIDARG;
    }

    // If this pointer is already in the list, we will prevent it from being
    // added again.
    //
    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (NULL != p)
    {
        if (p->pSink == pIServerEventsSink)
        {
            return MUX_E_FAIL;
        }
    }

    // Allocate a list node.
    //
    p = NULL;
    try
    {
        p = new ServerEventsSinkNode;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == p)
    {
        return MUX_E_OUTOFMEMORY;
    }

    // Add the pointer to the list.
    //
    p->pNext = g_pServerEventsSinkListHead;
    pIServerEventsSink->AddRef();
    p->pSink = pIServerEventsSink;
    pIServerEventsSink->AddRef();
    m_pSink = pIServerEventsSink;
    g_pServerEventsSinkListHead = p;

    return MUX_S_OK;
}

// Factory for CServerEventsSource component which is not directly accessible.
//
CServerEventsSourceFactory::CServerEventsSourceFactory(void) : m_cRef(1)
{
}

CServerEventsSourceFactory::~CServerEventsSourceFactory()
{
}

MUX_RESULT CServerEventsSourceFactory::QueryInterface(MUX_IID iid, void **ppv)
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

UINT32 CServerEventsSourceFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CServerEventsSourceFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CServerEventsSourceFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (NULL != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CServerEventsSource *pServerEventsSource = NULL;
    try
    {
        pServerEventsSource = new CServerEventsSource;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == pServerEventsSource)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pServerEventsSource->QueryInterface(iid, ppv);
    pServerEventsSource->Release();
    return mr;
}

MUX_RESULT CServerEventsSourceFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// StubSlaveProxy component which is not directly accessible.
//
CStubSlaveProxy::CStubSlaveProxy(void) : m_cRef(1), m_nChannel(CHANNEL_INVALID), m_pModuleName(NULL)
{
}

MUX_RESULT CStubSlaveProxy::FinalConstruct(void)
{
    return MUX_S_OK;
}

CStubSlaveProxy::~CStubSlaveProxy()
{
}

MUX_RESULT CStubSlaveProxy::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_ISlaveControl *>(this);
    }
    else if (IID_ISlaveControl == iid)
    {
        *ppv = static_cast<mux_ISlaveControl *>(this);
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

UINT32 CStubSlaveProxy::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CStubSlaveProxy::Release(void)
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

MUX_RESULT CStubSlaveProxy::GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid)
{
    UNUSED_PARAMETER(riid);
    UNUSED_PARAMETER(ctx);
    UNUSED_PARAMETER(pcid);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CStubSlaveProxy::MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, marshal_context ctx)
{
    UNUSED_PARAMETER(pqi);
    UNUSED_PARAMETER(riid);
    UNUSED_PARAMETER(ctx);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CStubSlaveProxy::UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv)
{
    // Use the channel number in the marshal packet from the remote component
    // to support a proxy mux_ISlaveControl.
    //
    size_t nWanted = sizeof(m_nChannel);
    if (  Pipe_GetBytes(pqi, &nWanted, &m_nChannel)
       && nWanted == sizeof(m_nChannel))
    {
        return QueryInterface(riid, ppv);
    }
    return MUX_E_NOINTERFACE;
}

MUX_RESULT CStubSlaveProxy::ReleaseMarshalData(QUEUE_INFO *pqi)
{
    UNUSED_PARAMETER(pqi);

    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CStubSlaveProxy::DisconnectObject(void)
{
    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

#ifdef WIN32
MUX_RESULT CStubSlaveProxy::AddModule(const UTF8 aModuleName[], const UTF16 aFileName[])
#else
MUX_RESULT CStubSlaveProxy::AddModule(const UTF8 aModuleName[], const UTF8 aFileName[])
#endif // WIN32
{
    // Communicate with the remote component to service this request.
    //
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    UINT32 iMethod = 3;
    struct FRAME
    {
        size_t nModuleName;
        size_t nFileName;
    } CallFrame;

    CallFrame.nModuleName = strlen((const char *)aModuleName)+1;
#ifdef WIN32
    CallFrame.nFileName   = (wcslen(aFileName)+1)*sizeof(UTF16);
#else
    CallFrame.nFileName   = (strlen((const char *)aFileName)+1)*sizeof(UTF8);
#endif

    Pipe_AppendBytes(&qiFrame, sizeof(iMethod), &iMethod);
    Pipe_AppendBytes(&qiFrame, sizeof(CallFrame), &CallFrame);
    Pipe_AppendBytes(&qiFrame, CallFrame.nModuleName, aModuleName);
    Pipe_AppendBytes(&qiFrame, CallFrame.nFileName, aFileName);

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

MUX_RESULT CStubSlaveProxy::RemoveModule(const UTF8 aModuleName[])
{
    // Communicate with the remote component to service this request.
    //
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    UINT32 iMethod = 4;
    struct FRAME
    {
        size_t nModuleName;
    } CallFrame;

    CallFrame.nModuleName = strlen((const char *)aModuleName)+1;

    Pipe_AppendBytes(&qiFrame, sizeof(iMethod), &iMethod);
    Pipe_AppendBytes(&qiFrame, sizeof(CallFrame), &CallFrame);
    Pipe_AppendBytes(&qiFrame, CallFrame.nModuleName, aModuleName);

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

MUX_RESULT CStubSlaveProxy::ModuleInfo(int iModule, MUX_MODULE_INFO *pModuleInfo)
{
    // Communicate with the remote component to service this request.
    //
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    UINT32 iMethod = 5;
    struct FRAME
    {
        int    iModule;
    } CallFrame;

    CallFrame.iModule = iModule;

    Pipe_AppendBytes(&qiFrame, sizeof(iMethod), &iMethod);
    Pipe_AppendBytes(&qiFrame, sizeof(CallFrame), &CallFrame);

    mr = Pipe_SendCallPacketAndWait(m_nChannel, &qiFrame);

    if (MUX_SUCCEEDED(mr))
    {
        struct RETURN
        {
            size_t     nName;
            bool       bLoaded;
            MUX_RESULT mr;
        } ReturnFrame;

        size_t nWanted = sizeof(ReturnFrame);
        if (  Pipe_GetBytes(&qiFrame, &nWanted, &ReturnFrame)
           && nWanted == sizeof(ReturnFrame))
        {
            if (NULL != m_pModuleName)
            {
                delete m_pModuleName;
                m_pModuleName = NULL;
            }

            if (0 < ReturnFrame.nName)
            {
                try
                {
                    m_pModuleName = new UTF8[ReturnFrame.nName];
                }
                catch (...)
                {
                    ; // Nothing.
                }

                if (NULL != m_pModuleName)
                {
                    nWanted = ReturnFrame.nName;
                    if (  Pipe_GetBytes(&qiFrame, &nWanted, m_pModuleName)
                       && nWanted == ReturnFrame.nName)
                    {
                        pModuleInfo->bLoaded = ReturnFrame.bLoaded;
                        pModuleInfo->pName   = m_pModuleName;
                        mr = ReturnFrame.mr;
                    }
                    else
                    {
                        mr = MUX_E_FAIL;
                    }
                }
                else
                {
                    mr = MUX_E_OUTOFMEMORY;
                }
            }
            else
            {
                pModuleInfo->bLoaded = ReturnFrame.bLoaded;
                pModuleInfo->pName   = NULL;
                mr = ReturnFrame.mr;
            }
        }
        else
        {
            mr = MUX_E_FAIL;
        }
    }

    Pipe_EmptyQueue(&qiFrame);
    return mr;
}

MUX_RESULT CStubSlaveProxy::ModuleMaintenance(void)
{
    // Communicate with the remote component to service this request.
    //
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    UINT32 iMethod = 6;

    Pipe_AppendBytes(&qiFrame, sizeof(iMethod), &iMethod);

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

MUX_RESULT CStubSlaveProxy::ShutdownSlave(void)
{
    // Communicate with the remote component to service this request.
    //
    MUX_RESULT mr = MUX_S_OK;

    QUEUE_INFO qiFrame;
    Pipe_InitializeQueueInfo(&qiFrame);

    UINT32 iMethod = 7;

    Pipe_AppendBytes(&qiFrame, sizeof(iMethod), &iMethod);

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

// Factory for StubSlaveProxy component which is not directly accessible.
//
CStubSlaveProxyFactory::CStubSlaveProxyFactory(void) : m_cRef(1)
{
}

CStubSlaveProxyFactory::~CStubSlaveProxyFactory()
{
}

MUX_RESULT CStubSlaveProxyFactory::QueryInterface(MUX_IID iid, void **ppv)
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

UINT32 CStubSlaveProxyFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CStubSlaveProxyFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CStubSlaveProxyFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (NULL != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CStubSlaveProxy *pStubSlaveProxy = NULL;
    try
    {
        pStubSlaveProxy = new CStubSlaveProxy;
    }
    catch (...)
    {
        ; // Nothing.
    }

    MUX_RESULT mr;
    if (NULL == pStubSlaveProxy)
    {
        return MUX_E_OUTOFMEMORY;
    }
    else
    {
        mr = pStubSlaveProxy->FinalConstruct();
        if (MUX_FAILED(mr))
        {
            pStubSlaveProxy->Release();
            return mr;
        }
    }

    mr = pStubSlaveProxy->QueryInterface(iid, ppv);
    pStubSlaveProxy->Release();
    return mr;
}

MUX_RESULT CStubSlaveProxyFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// CQueryClient component which is not directly accessible.
//
CQueryClient::CQueryClient(void) : m_cRef(1)
{
}

CQueryClient::~CQueryClient()
{
}

MUX_RESULT CQueryClient::QueryInterface(MUX_IID iid, void **ppv)
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

UINT32 CQueryClient::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CQueryClient::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQueryClient::GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid)
{
    UNUSED_PARAMETER(ctx);

    if (NULL == pcid)
    {
        return MUX_E_INVALIDARG;
    }
    else if (  IID_IQuerySink == riid
            && CrossProcess == ctx)
    {
        // We only support cross-process at the moment.
        //
        *pcid = CID_QuerySinkProxy;
        return MUX_S_OK;
    }
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryClient_Call(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    mux_IQuerySink *pIQuerySink = static_cast<mux_IQuerySink *>(pci->pInterface);
    if (NULL == pIQuerySink)
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
    // CQueryClientProxy goes to zero, it drops the connection and destroys itself.
    // We see that as a call to CQueryClient_Disconnect.
    //
    switch (iMethod)
    {
    case 3:  // MUX_RESULT Result(UINT32 iQueryHandle, const UTF8 *pResultSet)
        {
            struct FRAME
            {
                UINT32 iQueryHandle;
                size_t nResultSet;
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
            else
            {
                UTF8 *pResultSet = NULL;
                try
                {
                    pResultSet = new UTF8[CallFrame.nResultSet];
                }
                catch (...)
                {
                    ; // Nothing.
                }

                if (NULL == pResultSet)
                {
                    ReturnFrame.mr = MUX_E_OUTOFMEMORY;
                }
                else
                {
                    nWanted = CallFrame.nResultSet;
                    if (  Pipe_GetBytes(pqi, &nWanted, pResultSet)
                       && nWanted == CallFrame.nResultSet)
                    {
                        ReturnFrame.mr = pIQuerySink->Result(CallFrame.iQueryHandle, pResultSet);
                    }
                    else
                    {
                        ReturnFrame.mr = MUX_E_INVALIDARG;
                    }
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

MUX_RESULT CQueryClient_Msg(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    // The same as CQueryClient_Call except that the caller is no longer
    // available to receive the ReturnFrame.
    //
    return CQueryClient_Call(pci, pqi);
}

MUX_RESULT CQueryClient_Disconnect(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
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

MUX_RESULT CQueryClient::MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, marshal_context ctx)
{
    // Parameter validation and initialization.
    //
    MUX_RESULT mr = MUX_S_OK;
    if (NULL == pqi)
    {
        mr = MUX_E_INVALIDARG;
    }
    else if (IID_IQuerySink != riid)
    {
        mr = MUX_E_FAIL;
    }
    else if (CrossProcess != ctx)
    {
        mr = MUX_E_NOTIMPLEMENTED;
    }
    else
    {
        mux_IQuerySink *pIQuerySink = NULL;
        mr = QueryInterface(IID_IQuerySink, (void **)&pIQuerySink);
        if (MUX_SUCCEEDED(mr))
        {
            // Construct a packet sufficient to allow the proxy to communicate with us.
            //
            CHANNEL_INFO *pChannel = Pipe_AllocateChannel(CQueryClient_Call, CQueryClient_Msg, CQueryClient_Disconnect);
            if (NULL != pChannel)
            {
                pChannel->pInterface = pIQuerySink;
                Pipe_AppendBytes(pqi, sizeof(pChannel->nChannel), (UTF8*)(&pChannel->nChannel));
                mr =  MUX_S_OK;
            }
            else
            {
                pIQuerySink->Release();
                pIQuerySink = NULL;
                mr = MUX_E_OUTOFMEMORY;
            }
        }
    }
    return mr;
}

MUX_RESULT CQueryClient::UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv)
{
    UNUSED_PARAMETER(pqi);
    UNUSED_PARAMETER(riid);
    UNUSED_PARAMETER(ppv);

    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryClient::ReleaseMarshalData(QUEUE_INFO *pqi)
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
            CQueryClient_Disconnect(pChannel, pqi);
        }
    }
    return MUX_S_OK;
}

MUX_RESULT CQueryClient::DisconnectObject(void)
{
    // This is called when the hosting process is about to go down to give the
    // component a chance to notify its proxy that it is about to shut down.
    // This is only implemented on the server side -- not the proxy.
    //
    // TODO: There isn't a mechanism for sending such a notification, yet.
    //
    return MUX_S_OK;
}


MUX_RESULT CQueryClient::Result(UINT32 iQueryHandle, const UTF8 *pResultSet)
{
    UNUSED_PARAMETER(iQueryHandle);

    // TODO: Use iQueryHandle to lookup the dbref/attr pair to @trigger within a context of pResultSet.
    //
    STARTLOG(LOG_ALWAYS, "INI", "LOAD");
    log_printf("ResultSet is %s", pResultSet);
    ENDLOG;
    return MUX_S_OK;;
}

// Factory for CQueryClient component which is not directly accessible.
//
CQueryClientFactory::CQueryClientFactory(void) : m_cRef(1)
{
}

CQueryClientFactory::~CQueryClientFactory()
{
}

MUX_RESULT CQueryClientFactory::QueryInterface(MUX_IID iid, void **ppv)
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

UINT32 CQueryClientFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CQueryClientFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CQueryClientFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (NULL != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CQueryClient *pLog = NULL;
    try
    {
        pLog = new CQueryClient;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == pLog)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pLog->QueryInterface(iid, ppv);
    pLog->Release();
    return mr;
}

MUX_RESULT CQueryClientFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

#endif
