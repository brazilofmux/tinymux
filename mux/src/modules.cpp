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
    { CID_LogPSFactory       },
    { CID_Notify             },
    { CID_ObjectInfo         },
    { CID_AttributeAccess    },
    { CID_Evaluator          },
    { CID_Permissions        },
    { CID_MailDelivery       }
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
    else if (CID_Notify == cid)
    {
        CNotifyFactory *pNotifyFactory = nullptr;
        try
        {
            pNotifyFactory = new CNotifyFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pNotifyFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pNotifyFactory->QueryInterface(iid, ppv);
        pNotifyFactory->Release();
    }
    else if (CID_ObjectInfo == cid)
    {
        CObjectInfoFactory *pObjectInfoFactory = nullptr;
        try
        {
            pObjectInfoFactory = new CObjectInfoFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pObjectInfoFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pObjectInfoFactory->QueryInterface(iid, ppv);
        pObjectInfoFactory->Release();
    }
    else if (CID_AttributeAccess == cid)
    {
        CAttributeAccessFactory *pAttributeAccessFactory = nullptr;
        try
        {
            pAttributeAccessFactory = new CAttributeAccessFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pAttributeAccessFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pAttributeAccessFactory->QueryInterface(iid, ppv);
        pAttributeAccessFactory->Release();
    }
    else if (CID_Evaluator == cid)
    {
        CEvaluatorFactory *pEvaluatorFactory = nullptr;
        try
        {
            pEvaluatorFactory = new CEvaluatorFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pEvaluatorFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pEvaluatorFactory->QueryInterface(iid, ppv);
        pEvaluatorFactory->Release();
    }
    else if (CID_Permissions == cid)
    {
        CPermissionsFactory *pPermissionsFactory = nullptr;
        try
        {
            pPermissionsFactory = new CPermissionsFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pPermissionsFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pPermissionsFactory->QueryInterface(iid, ppv);
        pPermissionsFactory->Release();
    }
    else if (CID_MailDelivery == cid)
    {
        CMailDeliveryFactory *pMailDeliveryFactory = nullptr;
        try
        {
            pMailDeliveryFactory = new CMailDeliveryFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pMailDeliveryFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pMailDeliveryFactory->QueryInterface(iid, ppv);
        pMailDeliveryFactory->Release();
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
class CQueryClient : public mux_IQuerySink
{
public:
    // mux_IUnknown
    //
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t     AddRef(void);
    virtual uint32_t     Release(void);

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

// ---------------------------------------------------------------------------
// CNotify — mux_INotify implementation (in-process only, no marshaling).
// ---------------------------------------------------------------------------

class CNotify : public mux_INotify
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT Notify(dbref target, const UTF8 *msg);
    virtual MUX_RESULT RawNotify(dbref target, const UTF8 *msg);
    virtual MUX_RESULT NotifyCheck(dbref target, dbref sender,
        const UTF8 *msg, int key);

    CNotify(void);
    virtual ~CNotify();

private:
    uint32_t m_cRef;
};

CNotify::CNotify(void) : m_cRef(1)
{
}

CNotify::~CNotify()
{
}

MUX_RESULT CNotify::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_INotify *>(this);
    }
    else if (IID_INotify == iid)
    {
        *ppv = static_cast<mux_INotify *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CNotify::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CNotify::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CNotify::Notify(dbref target, const UTF8 *msg)
{
    if (!Good_obj(target))
    {
        return MUX_E_INVALIDARG;
    }
    notify(target, msg);
    return MUX_S_OK;
}

MUX_RESULT CNotify::RawNotify(dbref target, const UTF8 *msg)
{
    if (!Good_obj(target))
    {
        return MUX_E_INVALIDARG;
    }
    raw_notify(target, msg);
    return MUX_S_OK;
}

MUX_RESULT CNotify::NotifyCheck(dbref target, dbref sender,
    const UTF8 *msg, int key)
{
    if (!Good_obj(target))
    {
        return MUX_E_INVALIDARG;
    }
    notify_check(target, sender, msg, key);
    return MUX_S_OK;
}

CNotifyFactory::CNotifyFactory(void) : m_cRef(1)
{
}

CNotifyFactory::~CNotifyFactory()
{
}

MUX_RESULT CNotifyFactory::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CNotifyFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CNotifyFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CNotifyFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    if (nullptr != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CNotify *pNotify = nullptr;
    try
    {
        pNotify = new CNotify;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pNotify)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pNotify->QueryInterface(iid, ppv);
    pNotify->Release();
    return mr;
}

MUX_RESULT CNotifyFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CObjectInfo — mux_IObjectInfo implementation (in-process only).
// ---------------------------------------------------------------------------

class CObjectInfo : public mux_IObjectInfo
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT IsValid(dbref obj, bool *pValid);
    virtual MUX_RESULT GetName(dbref obj, const UTF8 **ppName);
    virtual MUX_RESULT GetOwner(dbref obj, dbref *pOwner);
    virtual MUX_RESULT GetLocation(dbref obj, dbref *pLocation);
    virtual MUX_RESULT GetType(dbref obj, int *pType);
    virtual MUX_RESULT IsConnected(dbref obj, bool *pConnected);
    virtual MUX_RESULT IsPlayer(dbref obj, bool *pPlayer);
    virtual MUX_RESULT IsGoing(dbref obj, bool *pGoing);
    virtual MUX_RESULT GetMoniker(dbref obj, const UTF8 **ppMoniker);
    virtual MUX_RESULT MatchThing(dbref executor, const UTF8 *pName,
        dbref *pResult);

    CObjectInfo(void);
    virtual ~CObjectInfo();

private:
    uint32_t m_cRef;
};

CObjectInfo::CObjectInfo(void) : m_cRef(1)
{
}

CObjectInfo::~CObjectInfo()
{
}

MUX_RESULT CObjectInfo::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IObjectInfo *>(this);
    }
    else if (IID_IObjectInfo == iid)
    {
        *ppv = static_cast<mux_IObjectInfo *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CObjectInfo::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CObjectInfo::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CObjectInfo::IsValid(dbref obj, bool *pValid)
{
    *pValid = Good_obj(obj);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::GetName(dbref obj, const UTF8 **ppName)
{
    if (!Good_obj(obj))
    {
        *ppName = nullptr;
        return MUX_E_INVALIDARG;
    }
    *ppName = Name(obj);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::GetOwner(dbref obj, dbref *pOwner)
{
    if (!Good_obj(obj))
    {
        *pOwner = NOTHING;
        return MUX_E_INVALIDARG;
    }
    *pOwner = Owner(obj);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::GetLocation(dbref obj, dbref *pLocation)
{
    if (!Good_obj(obj))
    {
        *pLocation = NOTHING;
        return MUX_E_INVALIDARG;
    }
    *pLocation = Location(obj);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::GetType(dbref obj, int *pType)
{
    if (!Good_obj(obj))
    {
        *pType = TYPE_GARBAGE;
        return MUX_E_INVALIDARG;
    }
    *pType = Typeof(obj);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::IsConnected(dbref obj, bool *pConnected)
{
    if (nullptr == pConnected)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pConnected = false;
        return MUX_E_INVALIDARG;
    }
    *pConnected = Connected(obj);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::IsPlayer(dbref obj, bool *pPlayer)
{
    if (nullptr == pPlayer)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pPlayer = false;
        return MUX_E_INVALIDARG;
    }
    *pPlayer = isPlayer(obj);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::IsGoing(dbref obj, bool *pGoing)
{
    if (nullptr == pGoing)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pGoing = false;
        return MUX_E_INVALIDARG;
    }
    *pGoing = Going(obj);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::GetMoniker(dbref obj, const UTF8 **ppMoniker)
{
    if (nullptr == ppMoniker)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *ppMoniker = nullptr;
        return MUX_E_INVALIDARG;
    }
    *ppMoniker = Moniker(obj);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::MatchThing(dbref executor, const UTF8 *pName,
    dbref *pResult)
{
    if (nullptr == pResult)
    {
        return MUX_E_INVALIDARG;
    }
    if (nullptr == pName)
    {
        *pResult = NOTHING;
        return MUX_E_INVALIDARG;
    }
    *pResult = match_thing(executor, pName);
    return MUX_S_OK;
}

CObjectInfoFactory::CObjectInfoFactory(void) : m_cRef(1)
{
}

CObjectInfoFactory::~CObjectInfoFactory()
{
}

MUX_RESULT CObjectInfoFactory::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CObjectInfoFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CObjectInfoFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CObjectInfoFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    if (nullptr != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CObjectInfo *pObjectInfo = nullptr;
    try
    {
        pObjectInfo = new CObjectInfo;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pObjectInfo)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pObjectInfo->QueryInterface(iid, ppv);
    pObjectInfo->Release();
    return mr;
}

MUX_RESULT CObjectInfoFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CAttributeAccess — mux_IAttributeAccess implementation (in-process only).
// ---------------------------------------------------------------------------

class CAttributeAccess : public mux_IAttributeAccess
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT GetAttribute(dbref executor, dbref obj,
        const UTF8 *pAttrName, UTF8 *pValue, size_t nValueMax,
        size_t *pnValueLen);
    virtual MUX_RESULT SetAttribute(dbref executor, dbref obj,
        const UTF8 *pAttrName, const UTF8 *pValue);

    CAttributeAccess(void);
    virtual ~CAttributeAccess();

private:
    uint32_t m_cRef;
};

CAttributeAccess::CAttributeAccess(void) : m_cRef(1)
{
}

CAttributeAccess::~CAttributeAccess()
{
}

MUX_RESULT CAttributeAccess::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IAttributeAccess *>(this);
    }
    else if (IID_IAttributeAccess == iid)
    {
        *ppv = static_cast<mux_IAttributeAccess *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CAttributeAccess::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CAttributeAccess::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CAttributeAccess::GetAttribute(dbref executor, dbref obj,
    const UTF8 *pAttrName, UTF8 *pValue, size_t nValueMax,
    size_t *pnValueLen)
{
    if (nullptr == pValue || 0 == nValueMax)
    {
        return MUX_E_INVALIDARG;
    }
    pValue[0] = '\0';
    if (nullptr != pnValueLen)
    {
        *pnValueLen = 0;
    }

    if (!Good_obj(obj))
    {
        return MUX_E_INVALIDARG;
    }

    ATTR *pattr = atr_str(pAttrName);
    if (nullptr == pattr)
    {
        return MUX_E_NOTFOUND;
    }

    if (!bCanReadAttr(executor, obj, pattr, false))
    {
        return MUX_E_PERMISSION;
    }

    dbref aowner;
    int   aflags;
    size_t nLen;
    UTF8 buf[LBUF_SIZE];
    atr_get_str_LEN(buf, obj, pattr->number, &aowner, &aflags, &nLen);

    if (0 == nLen)
    {
        return MUX_S_OK;
    }

    if (nLen >= nValueMax)
    {
        nLen = nValueMax - 1;
    }
    memcpy(pValue, buf, nLen);
    pValue[nLen] = '\0';

    if (nullptr != pnValueLen)
    {
        *pnValueLen = nLen;
    }
    return MUX_S_OK;
}

MUX_RESULT CAttributeAccess::SetAttribute(dbref executor, dbref obj,
    const UTF8 *pAttrName, const UTF8 *pValue)
{
    if (!Good_obj(obj))
    {
        return MUX_E_INVALIDARG;
    }

    // mkattr() looks up or creates the vattr as needed.
    //
    int anum = mkattr(executor, pAttrName);
    if (anum <= 0)
    {
        return MUX_E_NOTFOUND;
    }

    ATTR *pattr = atr_num(anum);
    if (nullptr == pattr)
    {
        return MUX_E_NOTFOUND;
    }

    if (!bCanSetAttr(executor, obj, pattr))
    {
        return MUX_E_PERMISSION;
    }

    dbref aowner;
    int   aflags;
    atr_pget_info(obj, pattr->number, &aowner, &aflags);
    atr_add(obj, pattr->number, pValue, Owner(executor), aflags);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CAttributeAccessFactory
// ---------------------------------------------------------------------------

CAttributeAccessFactory::CAttributeAccessFactory(void) : m_cRef(1)
{
}

CAttributeAccessFactory::~CAttributeAccessFactory()
{
}

MUX_RESULT CAttributeAccessFactory::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CAttributeAccessFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CAttributeAccessFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CAttributeAccessFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    CAttributeAccess *pAttributeAccess = nullptr;
    try
    {
        pAttributeAccess = new CAttributeAccess;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pAttributeAccess)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pAttributeAccess->QueryInterface(iid, ppv);
    pAttributeAccess->Release();
    return mr;
}

MUX_RESULT CAttributeAccessFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CEvaluator — mux_IEvaluator implementation (in-process only).
// ---------------------------------------------------------------------------

class CEvaluator : public mux_IEvaluator
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT Eval(dbref executor, dbref caller, dbref enactor,
        const UTF8 *pExpr, UTF8 *pResult, size_t nResultMax,
        size_t *pnResultLen);

    CEvaluator(void);
    virtual ~CEvaluator();

private:
    uint32_t m_cRef;
};

CEvaluator::CEvaluator(void) : m_cRef(1)
{
}

CEvaluator::~CEvaluator()
{
}

MUX_RESULT CEvaluator::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IEvaluator *>(this);
    }
    else if (IID_IEvaluator == iid)
    {
        *ppv = static_cast<mux_IEvaluator *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CEvaluator::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CEvaluator::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CEvaluator::Eval(dbref executor, dbref caller, dbref enactor,
    const UTF8 *pExpr, UTF8 *pResult, size_t nResultMax,
    size_t *pnResultLen)
{
    if (nullptr == pExpr || nullptr == pResult || 0 == nResultMax)
    {
        return MUX_E_INVALIDARG;
    }

    if (!Good_obj(executor))
    {
        pResult[0] = '\0';
        if (nullptr != pnResultLen)
        {
            *pnResultLen = 0;
        }
        return MUX_E_INVALIDARG;
    }

    // Use the caller/enactor if valid, otherwise default to executor.
    //
    if (!Good_obj(caller))
    {
        caller = executor;
    }
    if (!Good_obj(enactor))
    {
        enactor = executor;
    }

    // Evaluate into an LBUF, then copy to the caller's buffer.
    //
    UTF8 buf[LBUF_SIZE];
    UTF8 *bufc = buf;
    size_t nExpr = strlen((const char *)pExpr);
    mux_exec(pExpr, nExpr, buf, &bufc, executor, caller, enactor,
             EV_FCHECK | EV_STRIP_CURLY | EV_EVAL, nullptr, 0);
    *bufc = '\0';

    size_t nLen = bufc - buf;
    if (nLen >= nResultMax)
    {
        nLen = nResultMax - 1;
    }
    memcpy(pResult, buf, nLen);
    pResult[nLen] = '\0';

    if (nullptr != pnResultLen)
    {
        *pnResultLen = nLen;
    }
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CEvaluatorFactory
// ---------------------------------------------------------------------------

CEvaluatorFactory::CEvaluatorFactory(void) : m_cRef(1)
{
}

CEvaluatorFactory::~CEvaluatorFactory()
{
}

MUX_RESULT CEvaluatorFactory::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CEvaluatorFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CEvaluatorFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CEvaluatorFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    CEvaluator *pEvaluator = nullptr;
    try
    {
        pEvaluator = new CEvaluator;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pEvaluator)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pEvaluator->QueryInterface(iid, ppv);
    pEvaluator->Release();
    return mr;
}

MUX_RESULT CEvaluatorFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CPermissions — mux_IPermissions implementation (in-process only).
// ---------------------------------------------------------------------------

class CPermissions : public mux_IPermissions
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT IsWizard(dbref obj, bool *pWizard);
    virtual MUX_RESULT IsGod(dbref obj, bool *pGod);
    virtual MUX_RESULT HasControl(dbref who, dbref what, bool *pControls);
    virtual MUX_RESULT HasCommAll(dbref obj, bool *pCommAll);

    CPermissions(void);
    virtual ~CPermissions();

private:
    uint32_t m_cRef;
};

CPermissions::CPermissions(void) : m_cRef(1)
{
}

CPermissions::~CPermissions()
{
}

MUX_RESULT CPermissions::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IPermissions *>(this);
    }
    else if (IID_IPermissions == iid)
    {
        *ppv = static_cast<mux_IPermissions *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CPermissions::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CPermissions::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CPermissions::IsWizard(dbref obj, bool *pWizard)
{
    if (nullptr == pWizard)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pWizard = false;
        return MUX_E_INVALIDARG;
    }
    *pWizard = Wizard(obj);
    return MUX_S_OK;
}

MUX_RESULT CPermissions::IsGod(dbref obj, bool *pGod)
{
    if (nullptr == pGod)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pGod = false;
        return MUX_E_INVALIDARG;
    }
    *pGod = God(obj);
    return MUX_S_OK;
}

MUX_RESULT CPermissions::HasControl(dbref who, dbref what, bool *pControls)
{
    if (nullptr == pControls)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(who) || !Good_obj(what))
    {
        *pControls = false;
        return MUX_E_INVALIDARG;
    }
    *pControls = Controls(who, what);
    return MUX_S_OK;
}

MUX_RESULT CPermissions::HasCommAll(dbref obj, bool *pCommAll)
{
    if (nullptr == pCommAll)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pCommAll = false;
        return MUX_E_INVALIDARG;
    }
    *pCommAll = Comm_All(obj);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CPermissionsFactory
// ---------------------------------------------------------------------------

CPermissionsFactory::CPermissionsFactory(void) : m_cRef(1)
{
}

CPermissionsFactory::~CPermissionsFactory()
{
}

MUX_RESULT CPermissionsFactory::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CPermissionsFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CPermissionsFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CPermissionsFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    CPermissions *pPermissions = nullptr;
    try
    {
        pPermissions = new CPermissions;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pPermissions)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pPermissions->QueryInterface(iid, ppv);
    pPermissions->Release();
    return mr;
}

MUX_RESULT CPermissionsFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CMailDelivery — server-provided implementation of mux_IMailDelivery.
//
// Wraps server-internal lock evaluation, attribute triggers, flag management,
// and throttling so that the mail module can deliver mail without linking
// against server internals.
// ---------------------------------------------------------------------------

class CMailDelivery : public mux_IMailDelivery
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT MailCheck(dbref player, dbref target, bool *pResult);
    virtual MUX_RESULT NotifyDelivery(dbref sender, dbref target,
        const UTF8 *subject, bool silent);
    virtual MUX_RESULT IsComposing(dbref player, bool *pResult);
    virtual MUX_RESULT SetComposing(dbref player, bool bComposing);
    virtual MUX_RESULT ThrottleCheck(dbref player, bool *pResult);

    CMailDelivery(void);
    virtual ~CMailDelivery();

private:
    uint32_t m_cRef;
};

CMailDelivery::CMailDelivery(void) : m_cRef(1)
{
}

CMailDelivery::~CMailDelivery()
{
}

MUX_RESULT CMailDelivery::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IMailDelivery *>(this);
    }
    else if (IID_IMailDelivery == iid)
    {
        *ppv = static_cast<mux_IMailDelivery *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CMailDelivery::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CMailDelivery::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

// MailCheck — Evaluate A_LMAIL lock in both directions.
//
// Mirrors the server's mail_check() function:
//   1. If player can't pass target's A_LMAIL, call mail_return (sends MFAIL).
//   2. If target can't pass player's A_LMAIL, reject unless player is Wizard.
//
MUX_RESULT CMailDelivery::MailCheck(dbref player, dbref target, bool *pResult)
{
    if (nullptr == pResult)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(player) || !Good_obj(target))
    {
        *pResult = false;
        return MUX_E_INVALIDARG;
    }

    if (!could_doit(player, target, A_LMAIL))
    {
        // Target rejects player's mail — send MFAIL message.
        //
        dbref aowner;
        int aflags;
        UTF8 *str = atr_pget(target, A_MFAIL, &aowner, &aflags);
        if (*str)
        {
            UTF8 *str2, *bp;
            str2 = bp = alloc_lbuf("mail_delivery.check");
            mux_exec(str, LBUF_SIZE-1, str2, &bp, target, player, player,
                 AttrTrace(aflags, EV_FCHECK|EV_EVAL|EV_TOP|EV_NO_LOCATION),
                 nullptr, 0);
            *bp = '\0';
            if (*str2)
            {
                CLinearTimeAbsolute ltaNow;
                ltaNow.GetLocal();
                FIELDEDTIME ft;
                ltaNow.ReturnFields(&ft);

                raw_notify(player, tprintf(T("MAIL: Reject message from %s: %s"),
                    Moniker(target), str2));
                raw_notify(target, tprintf(T("[%d:%02d] MAIL: Reject message sent to %s."),
                    ft.iHour, ft.iMinute, Moniker(player)));
            }
            free_lbuf(str2);
        }
        else
        {
            raw_notify(player, tprintf(T("Sorry, %s is not accepting mail."),
                Moniker(target)));
        }
        free_lbuf(str);
        *pResult = false;
        return MUX_S_OK;
    }

    if (!could_doit(target, player, A_LMAIL))
    {
        if (Wizard(player))
        {
            raw_notify(player, tprintf(
                T("Warning: %s can\xE2\x80\x99t return your mail."),
                Moniker(target)));
            *pResult = true;
        }
        else
        {
            raw_notify(player, tprintf(
                T("Sorry, %s can\xE2\x80\x99t return your mail."),
                Moniker(target)));
            *pResult = false;
        }
        return MUX_S_OK;
    }

    *pResult = true;
    return MUX_S_OK;
}

// NotifyDelivery — Send post-delivery notifications and trigger attributes.
//
// Called by the module after a message has been stored. This handles:
//   - "You sent your message to <target>" to sender (unless silent)
//   - "You have new mail from <sender>" to target
//   - did_it(sender, target, A_MAIL, ..., A_AMAIL, ...) for attribute triggers
//
MUX_RESULT CMailDelivery::NotifyDelivery(dbref sender, dbref target,
    const UTF8 *subject, bool silent)
{
    if (!Good_obj(sender) || !Good_obj(target))
    {
        return MUX_E_INVALIDARG;
    }

    if (!silent)
    {
        raw_notify(sender, tprintf(T("MAIL: You sent your message to %s."),
            Moniker(target)));
    }

    raw_notify(target, tprintf(
        T("MAIL: You have a new message from %s. Subject: %s"),
        Moniker(sender), subject));

    did_it(sender, target, A_MAIL, nullptr, 0, nullptr, A_AMAIL, 0,
        nullptr, NOTHING);

    return MUX_S_OK;
}

// IsComposing — Check PLAYER_MAILS flag in Flags2.
//
MUX_RESULT CMailDelivery::IsComposing(dbref player, bool *pResult)
{
    if (nullptr == pResult)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(player))
    {
        *pResult = false;
        return MUX_E_INVALIDARG;
    }
    *pResult = (Flags2(player) & PLAYER_MAILS) != 0;
    return MUX_S_OK;
}

// SetComposing — Set or clear the PLAYER_MAILS flag in Flags2.
//
MUX_RESULT CMailDelivery::SetComposing(dbref player, bool bComposing)
{
    if (!Good_obj(player))
    {
        return MUX_E_INVALIDARG;
    }
    if (bComposing)
    {
        s_Flags(player, FLAG_WORD2, Flags2(player) | PLAYER_MAILS);
    }
    else
    {
        s_Flags(player, FLAG_WORD2, Flags2(player) & ~PLAYER_MAILS);
    }
    return MUX_S_OK;
}

// ThrottleCheck — Has player sent too much mail recently?
//
MUX_RESULT CMailDelivery::ThrottleCheck(dbref player, bool *pResult)
{
    if (nullptr == pResult)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(player))
    {
        *pResult = true;
        return MUX_E_INVALIDARG;
    }
    *pResult = ThrottleMail(player);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CMailDeliveryFactory
// ---------------------------------------------------------------------------

CMailDeliveryFactory::CMailDeliveryFactory(void) : m_cRef(1)
{
}

CMailDeliveryFactory::~CMailDeliveryFactory()
{
}

MUX_RESULT CMailDeliveryFactory::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CMailDeliveryFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CMailDeliveryFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CMailDeliveryFactory::CreateInstance(mux_IUnknown *pUnknownOuter,
    MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    CMailDelivery *pMailDelivery = nullptr;
    try
    {
        pMailDelivery = new CMailDelivery;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pMailDelivery)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pMailDelivery->QueryInterface(iid, ppv);
    pMailDelivery->Release();
    return mr;
}

MUX_RESULT CMailDeliveryFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}
