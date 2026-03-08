/*! \file modules.cpp
 * \brief netmux-provided modules.
 *
 * Interfaces and classes declared here are built into the netmux server and
 * are available to netmux itself and to dynamically-loaded external modules.
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"

static MUX_CLASS_INFO netmux_classes[] =
{
    { CID_Log                },
    { CID_ServerEventsSource },
    { CID_QueryClient        },
    { CID_Functions          },
    { CID_LogPSFactory       }
};
#define NUM_CLASSES (sizeof(netmux_classes)/sizeof(netmux_classes[0]))

extern "C" MUX_RESULT DCL_API netmux_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_Log == cid)
    {
        CLogFactory *pLogFactory = nullptr;
        try
        {
            pLogFactory = new CLogFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pLogFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pLogFactory->QueryInterface(iid, ppv);
        pLogFactory->Release();
    }
    else if (CID_ServerEventsSource == cid)
    {
        CServerEventsSourceFactory *pServerEventsSourceFactory = nullptr;
        try
        {
            pServerEventsSourceFactory = new CServerEventsSourceFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pServerEventsSourceFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pServerEventsSourceFactory->QueryInterface(iid, ppv);
        pServerEventsSourceFactory->Release();
    }
    else if (CID_QueryClient == cid)
    {
        CQueryClientFactory *pQueryClientFactory = nullptr;
        try
        {
            pQueryClientFactory = new CQueryClientFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pQueryClientFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pQueryClientFactory->QueryInterface(iid, ppv);
        pQueryClientFactory->Release();
    }
    else if (CID_Functions == cid)
    {
        CFunctionsFactory *pFunctionsFactory = nullptr;
        try
        {
            pFunctionsFactory = new CFunctionsFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pFunctionsFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pFunctionsFactory->QueryInterface(iid, ppv);
        pFunctionsFactory->Release();
    }
    else if (CID_LogPSFactory == cid)
    {
        CLogPSFactory *pLogPSFactory = nullptr;
        try
        {
            pLogPSFactory = new CLogPSFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pLogPSFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pLogPSFactory->QueryInterface(iid, ppv);
        pLogPSFactory->Release();
    }
    return mr;
}

static MUX_INTERFACE_INFO netmux_interfaces[] =
{
    { IID_ILog, CID_LogPSFactory }
};
#define NUM_INTERFACES (sizeof(netmux_interfaces)/sizeof(netmux_interfaces[0]))

MUX_RESULT init_modules(void)
{
    MUX_RESULT mr = mux_InitModuleLibrary(IsMainProcess);
    if (MUX_SUCCEEDED(mr))
    {
        mr = mux_RegisterClassObjects(NUM_CLASSES, netmux_classes, netmux_GetClassObject);
    }
    if (MUX_SUCCEEDED(mr))
    {
        mr = mux_RegisterInterfaces(NUM_INTERFACES, netmux_interfaces);
    }
    return mr;
}

#ifdef STUB_SLAVE
QUEUE_INFO Queue_In;
QUEUE_INFO Queue_Out;

MUX_RESULT init_stubslave(void)
{
    Pipe_InitializeQueueInfo(&Queue_In);
    Pipe_InitializeQueueInfo(&Queue_Out);
    MUX_RESULT mr = mux_InitModuleLibraryPump(pipepump, &Queue_In, &Queue_Out);

    if (nullptr != mudstate.pISlaveControl)
    {
        mudstate.pISlaveControl->Release();
        mudstate.pISlaveControl = nullptr;
    }

    mr = mux_CreateInstance(CID_StubSlave, nullptr, UseSlaveProcess, IID_ISlaveControl, (void **)&mudstate.pISlaveControl);
    if (MUX_SUCCEEDED(mr))
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf(T("Opened interface for StubSlave management."));
        ENDLOG;
    }
    else
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf(T("Failed to open interface for StubSlave management (%d)."), mr);
        ENDLOG;
    }
    return mr;
}

void final_stubslave(void)
{
    MUX_RESULT mr = MUX_S_OK;

    if (nullptr != mudstate.pISlaveControl)
    {
        mr = mudstate.pISlaveControl->ShutdownSlave();
        if (MUX_FAILED(mr))
        {
            STARTLOG(LOG_ALWAYS, "INI", "LOAD");
            log_printf(T("Failed to request stubslave to shutdown (%d)."), mr);
            ENDLOG;
        }

        mudstate.pISlaveControl->Release();
        mudstate.pISlaveControl = nullptr;
    }
}
#endif // STUB_SLAVE

void final_modules(void)
{
    MUX_RESULT mr = MUX_S_OK;

    mux_RevokeInterfaces(NUM_INTERFACES, netmux_interfaces);

    mr = mux_RevokeClassObjects(NUM_CLASSES, netmux_classes);
    if (MUX_FAILED(mr))
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf(T("Failed to revoke netmux modules (%d)."), mr);
        ENDLOG;
    }
    else
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf(T("Revoked netmux modules."), mr);
        ENDLOG;
    }
    mux_FinalizeModuleLibrary();
}

// CServerEventsSource component which is not directly accessible.
//
class CServerEventsSource : public mux_IServerEventsControl
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t     AddRef(void);
    virtual uint32_t     Release(void);

    // mux_IServerEventsControl
    //
    virtual MUX_RESULT Advise(mux_IServerEventsSink *pIServerEvents);

    CServerEventsSource(void);
    virtual ~CServerEventsSource();

private:
    uint32_t m_cRef;
    mux_IServerEventsSink *m_pSink;
};

CServerEventsSource::CServerEventsSource(void) : m_cRef(1), m_pSink(nullptr)
{
}

ServerEventsSinkNode *g_pServerEventsSinkListHead = nullptr;

CServerEventsSource::~CServerEventsSource()
{
    if (nullptr != m_pSink)
    {
        ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
        ServerEventsSinkNode *q = nullptr;
        while (nullptr != p)
        {
            if (p->pSink == m_pSink)
            {
                // Unlink node p from list.
                //
                if (nullptr == q)
                {
                    g_pServerEventsSinkListHead = p->pNext;
                }
                else
                {
                    q->pNext = p->pNext;
                }
                p->pNext = nullptr;

                // Free sink and node.
                //
                p->pSink->Release();
                p->pSink = nullptr;
                delete p;
                break;
            }
            q = p;
            p = p->pNext;
        }

        m_pSink->Release();
        m_pSink = nullptr;
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
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CServerEventsSource::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CServerEventsSource::Release(void)
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
    if (nullptr == pIServerEventsSink)
    {
        return MUX_E_INVALIDARG;
    }

    // If this pointer is already in the list, we will prevent it from being
    // added again.
    //
    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        if (p->pSink == pIServerEventsSink)
        {
            return MUX_E_FAIL;
        }
        p = p->pNext;
    }

    // Allocate a list node.
    //
    p = nullptr;
    try
    {
        p = new ServerEventsSinkNode;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == p)
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
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CServerEventsSourceFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CServerEventsSourceFactory::Release(void)
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
    if (nullptr != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CServerEventsSource *pServerEventsSource = nullptr;
    try
    {
        pServerEventsSource = new CServerEventsSource;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pServerEventsSource)
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

// CQueryClient component which is not directly accessible.
//
class CQueryClient : public mux_IQuerySink, public mux_IMarshal
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t     AddRef(void);
    virtual uint32_t     Release(void);

    // mux_IMarshal
    //
    virtual MUX_RESULT GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid);
    virtual MUX_RESULT MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void *pv, marshal_context ctx);
    virtual MUX_RESULT UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv);
    virtual MUX_RESULT ReleaseMarshalData(QUEUE_INFO *pqi);
    virtual MUX_RESULT DisconnectObject(void);

    // mux_IQuerySink
    //
    virtual MUX_RESULT Result(uint32_t iQueryHandle, uint32_t iError, QUEUE_INFO *pqiResultsSet);

    CQueryClient(void);
    virtual ~CQueryClient();

private:
    uint32_t m_cRef;
};

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
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CQueryClient::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CQueryClient::Release(void)
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

    if (nullptr == pcid)
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
    if (nullptr == pIQuerySink)
    {
        return MUX_E_NOINTERFACE;
    }

    uint32_t iMethod;
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
    case 3:  // MUX_RESULT Result(uint32_t iQueryHandle, const UTF8 *pResultSet)
        {
            struct FRAME
            {
                uint32_t iQueryHandle;
                uint32_t iError;
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
                ReturnFrame.mr = pIQuerySink->Result(CallFrame.iQueryHandle, CallFrame.iError, pqi);
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
    pci->pInterface = nullptr;

    // Tear down our side of the communication.  Our callback functions will
    // no longer be called.
    //
    Pipe_FreeChannel(pci);

    if (nullptr != pIUnknown)
    {
        pIUnknown->Release();
        return MUX_S_OK;
    }
    else
    {
        return MUX_E_NOINTERFACE;
    }
}

MUX_RESULT CQueryClient::MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void *pv, marshal_context ctx)
{
    // Parameter validation and initialization.
    //
    MUX_RESULT mr = MUX_S_OK;
    if (nullptr == pqi)
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
        mux_IQuerySink *pIQuerySink = nullptr;
        if (nullptr == pv)
        {
            mr = QueryInterface(IID_IQuerySink, (void **)&pIQuerySink);
        }
        else
        {
            mux_IUnknown *pIUnknown = static_cast<mux_IUnknown *>(pv);
            mr = pIUnknown->QueryInterface(IID_IQuerySink, (void **)&pIQuerySink);
        }
        if (MUX_SUCCEEDED(mr))
        {
            // Construct a packet sufficient to allow the proxy to communicate with us.
            //
            CHANNEL_INFO *pChannel = Pipe_AllocateChannel(CQueryClient_Call, CQueryClient_Msg, CQueryClient_Disconnect);
            if (nullptr != pChannel)
            {
                pChannel->pInterface = pIQuerySink;
                Pipe_AppendBytes(pqi, sizeof(pChannel->nChannel), reinterpret_cast<UTF8 *>(&pChannel->nChannel));
                mr =  MUX_S_OK;
            }
            else
            {
                pIQuerySink->Release();
                pIQuerySink = nullptr;
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
    uint32_t nChannel;
    size_t nWanted = sizeof(nChannel);
    if (  Pipe_GetBytes(pqi, &nWanted, &nChannel)
       && sizeof(nChannel) == nWanted)
    {
        CHANNEL_INFO *pChannel = Pipe_FindChannel(nChannel);
        if (nullptr != pChannel)
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

MUX_RESULT CQueryClient::Result(uint32_t hQuery, uint32_t iError, QUEUE_INFO *pqiResultsSet)
{
#if defined(STUB_SLAVE)
    CResultsSet *prs = nullptr;
    try
    {
        prs = new CResultsSet(pqiResultsSet);
    }
    catch (...)
    {
        ; // Nothing.
    }
    query_complete(hQuery, iError, prs);
    prs->Release();
#else
    UNUSED_PARAMETER(hQuery);
    UNUSED_PARAMETER(iError);
    UNUSED_PARAMETER(pqiResultsSet);
#endif // STUB_SLAVE
    return MUX_S_OK;
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
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CQueryClientFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CQueryClientFactory::Release(void)
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
    if (nullptr != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CQueryClient *pLog = nullptr;
    try
    {
        pLog = new CQueryClient;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pLog)
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

CResultsSet::CResultsSet(QUEUE_INFO *pqi) : m_cRef(1), m_nFields(0),
     m_nBlob(0), m_bLoaded(false), m_iError(QS_SUCCESS), m_nRows(0)
{
    m_pBlob = nullptr;
    m_pRows = nullptr;
    size_t nWanted = sizeof(m_nFields);
    if (  Pipe_GetBytes(pqi, &nWanted, &m_nFields)
       && nWanted == sizeof(m_nFields))
    {
        size_t nRows;
        m_nBlob = Pipe_QueueLength(pqi);
        if (sizeof(nRows) < m_nBlob)
        {
            bool bError = false;
            m_nBlob -= sizeof(nRows);
            if (0 < m_nBlob)
            {
                try
                {
                    m_pBlob = new UTF8[m_nBlob];
                }
                catch (...)
                {
                    ; // Nothing.
                }

                nWanted = m_nBlob;
                if (  nullptr == m_pBlob
                   || !Pipe_GetBytes(pqi, &nWanted, m_pBlob)
                   || nWanted != m_nBlob)
                {
                    bError = true;
                }
            }

            if (!bError)
            {
                nWanted = sizeof(nRows);
                if (  Pipe_GetBytes(pqi, &nWanted, &nRows)
                   && nWanted == sizeof(nRows))
                {
                    m_nRows = static_cast<int>(nRows);
                    try
                    {
                        m_pRows = new PUTF8[m_nRows];
                    }
                    catch (...)
                    {
                        ; // Nothing.
                    }

                    if (nullptr != m_pRows)
                    {
                        int i, j;
                        UTF8 *p = m_pBlob;
                        for (i = 0; i < m_nRows && p < m_pBlob + m_nBlob; i++)
                        {
                            m_pRows[i] = p;
                            for (j = 0; j < m_nFields && p < m_pBlob + m_nBlob; j++)
                            {
                                size_t n;
                                memcpy(&n, p, sizeof(size_t));
                                p += sizeof static_cast<size_t>(+ n);
                            }
                        }

                        if (p == m_pBlob + m_nBlob)
                        {
                            m_bLoaded = true;
                        }
                    }
                }
            }
        }
    }
}

bool CResultsSet::isLoaded(void)
{
    return m_bLoaded;
}

void CResultsSet::SetError(uint32_t iError)
{
    m_iError = iError;
}

uint32_t CResultsSet::GetError(void)
{
    return m_iError;
}

int CResultsSet::GetRowCount(void)
{
    return m_nRows;
}

const UTF8 *CResultsSet::FirstField(int iRow)
{
    if (  0 <= iRow
       && iRow < m_nRows
       && nullptr != m_pRows
       && 0 < m_nFields)
    {
        m_pCurrentField = m_pRows[iRow];
        m_iCurrentField = 1;
    }
    else
    {
        m_pCurrentField = nullptr;
        m_iCurrentField = 1;
    }
    return m_pCurrentField;
}

const UTF8 *CResultsSet::NextField(void)
{
    const UTF8 *pField = nullptr;
    if (  nullptr != m_pCurrentField
       && 0 < m_nFields
       && m_iCurrentField < m_nFields)
    {
        size_t n;

        m_iCurrentField++;
        memcpy(&n, m_pCurrentField, sizeof(size_t));
        m_pCurrentField += sizeof static_cast<size_t>(+ n);
        pField = m_pCurrentField;
    }
    return pField;
}

CResultsSet::~CResultsSet(void)
{
    if (nullptr != m_pBlob)
    {
        delete [] m_pBlob;
        m_pBlob = nullptr;
    }

    if (nullptr != m_pRows)
    {
        delete [] m_pRows;
        m_pRows = nullptr;
    }
}

uint32_t CResultsSet::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

uint32_t CResultsSet::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}
