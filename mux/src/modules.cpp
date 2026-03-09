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
#include "sqlite_backend.h"

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
    { CID_MailDelivery       },
    { CID_HelpSystem         },
    { CID_GameEngine         },
    { CID_ConnectionManager  }
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
    else if (CID_HelpSystem == cid)
    {
        CHelpSystemFactory *pHelpSystemFactory = nullptr;
        try
        {
            pHelpSystemFactory = new CHelpSystemFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pHelpSystemFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pHelpSystemFactory->QueryInterface(iid, ppv);
        pHelpSystemFactory->Release();
    }
    else if (CID_GameEngine == cid)
    {
        CGameEngineFactory *pGameEngineFactory = nullptr;
        try
        {
            pGameEngineFactory = new CGameEngineFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pGameEngineFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pGameEngineFactory->QueryInterface(iid, ppv);
        pGameEngineFactory->Release();
    }
    else if (CID_ConnectionManager == cid)
    {
        CConnectionManagerFactory *pFactory = nullptr;
        try
        {
            pFactory = new CConnectionManagerFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (nullptr == pFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pFactory->QueryInterface(iid, ppv);
        pFactory->Release();
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

// ---------------------------------------------------------------------------
// CHelpSystem — server-provided implementation of mux_IHelpSystem.
//
// Wraps the existing help.cpp lookup and indexing functions so that
// modules can access the in-game help system without linking against
// server internals.
// ---------------------------------------------------------------------------

class CHelpSystem : public mux_IHelpSystem
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT LookupTopic(dbref executor, int iHelpfile,
        const UTF8 *pTopic, UTF8 *pResult, size_t nResultMax,
        size_t *pnResultLen);
    virtual MUX_RESULT FindHelpFile(const UTF8 *pCommandName,
        int *pIndex);
    virtual MUX_RESULT GetHelpFileCount(int *pCount);
    virtual MUX_RESULT ReloadIndexes(dbref player);

    CHelpSystem(void);
    virtual ~CHelpSystem();

private:
    uint32_t m_cRef;
};

CHelpSystem::CHelpSystem(void) : m_cRef(1)
{
}

CHelpSystem::~CHelpSystem()
{
}

MUX_RESULT CHelpSystem::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IHelpSystem *>(this);
    }
    else if (IID_IHelpSystem == iid)
    {
        *ppv = static_cast<mux_IHelpSystem *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CHelpSystem::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CHelpSystem::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

// LookupTopic — look up a help topic and return the rendered text.
//
// Delegates to help_helper() which does the full lookup including
// prefix matching, file reading, and optional softcode evaluation.
//
MUX_RESULT CHelpSystem::LookupTopic(dbref executor, int iHelpfile,
    const UTF8 *pTopic, UTF8 *pResult, size_t nResultMax,
    size_t *pnResultLen)
{
    if (nullptr == pResult || nullptr == pnResultLen)
    {
        return MUX_E_INVALIDARG;
    }

    if (  iHelpfile < 0
       || mudstate.mHelpDesc <= iHelpfile
       || mudstate.nHelpDesc <= iHelpfile)
    {
        *pnResultLen = 0;
        pResult[0] = '\0';
        return MUX_E_NOTFOUND;
    }

    // help_helper writes into an alloc_lbuf using safe_str pattern.
    //
    UTF8 *buff = alloc_lbuf("CHelpSystem.LookupTopic");
    UTF8 *bufc = buff;

    // help_helper expects a mutable topic argument.
    //
    UTF8 topic_buf[LBUF_SIZE];
    mux_strncpy(topic_buf, pTopic, LBUF_SIZE - 1);

    help_helper(executor, iHelpfile, topic_buf, buff, &bufc);
    *bufc = '\0';

    size_t nLen = bufc - buff;
    if (nLen > nResultMax)
    {
        nLen = nResultMax;
    }
    memcpy(pResult, buff, nLen);
    pResult[nLen] = '\0';
    *pnResultLen = nLen;

    free_lbuf(buff);
    return MUX_S_OK;
}

// FindHelpFile — find the help file index for a given command name.
//
MUX_RESULT CHelpSystem::FindHelpFile(const UTF8 *pCommandName,
    int *pIndex)
{
    if (nullptr == pCommandName || nullptr == pIndex)
    {
        return MUX_E_INVALIDARG;
    }

    for (int i = 0; i < mudstate.nHelpDesc; i++)
    {
        if (  nullptr != mudstate.aHelpDesc[i].CommandName
           && 0 == mux_stricmp(pCommandName,
                               mudstate.aHelpDesc[i].CommandName))
        {
            *pIndex = i;
            return MUX_S_OK;
        }
    }

    *pIndex = -1;
    return MUX_E_NOTFOUND;
}

// GetHelpFileCount — return the number of registered help files.
//
MUX_RESULT CHelpSystem::GetHelpFileCount(int *pCount)
{
    if (nullptr == pCount)
    {
        return MUX_E_INVALIDARG;
    }
    *pCount = mudstate.nHelpDesc;
    return MUX_S_OK;
}

// ReloadIndexes — reload all help file indexes.
//
MUX_RESULT CHelpSystem::ReloadIndexes(dbref player)
{
    helpindex_load(player);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CHelpSystemFactory
// ---------------------------------------------------------------------------

CHelpSystemFactory::CHelpSystemFactory(void) : m_cRef(1)
{
}

CHelpSystemFactory::~CHelpSystemFactory()
{
}

MUX_RESULT CHelpSystemFactory::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CHelpSystemFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CHelpSystemFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CHelpSystemFactory::CreateInstance(mux_IUnknown *pUnknownOuter,
    MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    CHelpSystem *pHelpSystem = nullptr;
    try
    {
        pHelpSystem = new CHelpSystem;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pHelpSystem)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pHelpSystem->QueryInterface(iid, ppv);
    pHelpSystem->Release();
    return mr;
}

MUX_RESULT CHelpSystemFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CGameEngine — in-process COM wrapper for the game engine.
//
// The driver creates this via mux_CreateInstance(CID_GameEngine) and calls
// through the mux_IGameEngine interface.  In the current single-binary
// build this is a thin delegation layer; when the engine moves to
// engine.so, this class moves with it and becomes the COM front door.
// ---------------------------------------------------------------------------

class CGameEngine : public mux_IGameEngine
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT LoadGame(const UTF8 *configFile, const UTF8 *inputDb,
        bool bMinDB);
    virtual MUX_RESULT Startup(void);
    virtual MUX_RESULT RunTasks(CLinearTimeAbsolute &ltaNow);
    virtual MUX_RESULT UpdateQuotas(CLinearTimeAbsolute &ltaLast,
        const CLinearTimeAbsolute &ltaCurrent);
    virtual MUX_RESULT WhenNext(CLinearTimeAbsolute *pltaWhen);
    virtual MUX_RESULT DumpDatabase(void);
    virtual MUX_RESULT Shutdown(void);
    virtual MUX_RESULT ShouldShutdown(bool *pbShutdown);

    CGameEngine(void);
    virtual ~CGameEngine();

private:
    uint32_t m_cRef;
};

CGameEngine::CGameEngine(void) : m_cRef(1)
{
}

CGameEngine::~CGameEngine()
{
}

MUX_RESULT CGameEngine::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IGameEngine *>(this);
    }
    else if (IID_IGameEngine == iid)
    {
        *ppv = static_cast<mux_IGameEngine *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CGameEngine::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CGameEngine::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CGameEngine::LoadGame(const UTF8 *configFile,
    const UTF8 *inputDb, bool bMinDB)
{
    UNUSED_PARAMETER(inputDb);

    MUX_RESULT mr;

    // Initialize engine subsystems.
    //
    tcache_init();
    pcache_init();
    cf_init();
    init_cmdtab();
    init_flagtab();
    init_powertab();
    init_functab();
    init_attrtab();

    // Read configuration file.
    //
    if (nullptr != configFile)
    {
        mudconf.config_file = StringClone(configFile);
    }
    cf_read();

    // Sync core-layer globals from mudconf after config is loaded.
    //
    g_float_precision = mudconf.float_precision;
    g_space_compress = mudstate.bStandAlone || mudconf.space_compress;

    // Try to discover the comsys module.  If not loaded, the pointer
    // stays nullptr and the built-in comsys code handles everything.
    //
    mudstate.pIComsysControl = nullptr;
    mr = mux_CreateInstance(CID_Comsys, nullptr, UseSameProcess,
                            IID_IComsysControl,
                            reinterpret_cast<void **>(&mudstate.pIComsysControl));
    if (MUX_SUCCEEDED(mr))
    {
        // Compute SQLite database path from input database path.
        //
        char szDbPath[SIZEOF_PATHNAME];
        mux_strncpy(reinterpret_cast<UTF8 *>(szDbPath), mudconf.indb,
                     sizeof(szDbPath) - 1);
        szDbPath[sizeof(szDbPath) - 1] = '\0';
        size_t nPath = strlen(szDbPath);
        if (nPath > 3 && strcmp(szDbPath + nPath - 3, ".db") == 0)
        {
            strcpy(szDbPath + nPath - 3, ".sqlite");
        }
        else
        {
            strcat(szDbPath, ".sqlite");
        }

        mr = mudstate.pIComsysControl->Initialize(
            reinterpret_cast<const UTF8 *>(szDbPath));
        if (MUX_SUCCEEDED(mr))
        {
            STARTLOG(LOG_ALWAYS, "INI", "MOD");
            log_printf(T("Comsys module initialized with database: %s"),
                       szDbPath);
            ENDLOG;
        }
        else
        {
            STARTLOG(LOG_ALWAYS, "INI", "MOD");
            log_printf(T("Comsys module Initialize failed (mr=%d)."), mr);
            ENDLOG;
        }
    }

    // Try to discover the mail module.  If not loaded, the pointer
    // stays nullptr and the built-in mail code handles everything.
    //
    mudstate.pIMailControl = nullptr;
    mr = mux_CreateInstance(CID_Mail, nullptr, UseSameProcess,
                            IID_IMailControl,
                            reinterpret_cast<void **>(&mudstate.pIMailControl));
    if (MUX_SUCCEEDED(mr))
    {
        // Compute SQLite database path from input database path.
        //
        char szMailDbPath[SIZEOF_PATHNAME];
        mux_strncpy(reinterpret_cast<UTF8 *>(szMailDbPath), mudconf.indb,
                     sizeof(szMailDbPath) - 1);
        szMailDbPath[sizeof(szMailDbPath) - 1] = '\0';
        size_t nMailPath = strlen(szMailDbPath);
        if (nMailPath > 3 && strcmp(szMailDbPath + nMailPath - 3, ".db") == 0)
        {
            strcpy(szMailDbPath + nMailPath - 3, ".sqlite");
        }
        else
        {
            strcat(szMailDbPath, ".sqlite");
        }

        mr = mudstate.pIMailControl->Initialize(
            reinterpret_cast<const UTF8 *>(szMailDbPath),
            mudconf.mail_expiration, mudconf.mail_max_per_player);
        if (MUX_SUCCEEDED(mr))
        {
            STARTLOG(LOG_ALWAYS, "INI", "MOD");
            log_printf(T("Mail module initialized with database: %s"),
                       szMailDbPath);
            ENDLOG;
        }
        else
        {
            STARTLOG(LOG_ALWAYS, "INI", "MOD");
            log_printf(T("Mail module Initialize failed (mr=%d)."), mr);
            ENDLOG;
        }
    }

    mr = mux_CreateInstance(CID_QueryServer, nullptr, UseSlaveProcess,
                            IID_IQueryControl,
                            (void **)&mudstate.pIQueryControl);
    if (MUX_SUCCEEDED(mr))
    {
        mr = mudstate.pIQueryControl->Connect(mudconf.sql_server,
            mudconf.sql_database, mudconf.sql_user, mudconf.sql_password);
        if (MUX_SUCCEEDED(mr))
        {
            mux_IQuerySink *pIQuerySink = nullptr;
            mr = mux_CreateInstance(CID_QueryClient, nullptr, UseSameProcess,
                                    IID_IQuerySink,
                                    (void **)&pIQuerySink);
            if (MUX_SUCCEEDED(mr))
            {
                mr = mudstate.pIQueryControl->Advise(pIQuerySink);
                if (MUX_SUCCEEDED(mr))
                {
                    pIQuerySink->Release();
                    pIQuerySink = nullptr;
                }
                else
                {
                    mudstate.pIQueryControl->Release();
                    mudstate.pIQueryControl = nullptr;

                    STARTLOG(LOG_ALWAYS, "INI", "LOAD");
                    log_printf(T("Couldn\xE2\x80\x99t connect sink to server (%d)."), mr);
                    ENDLOG;
                }
            }
            else
            {
                mudstate.pIQueryControl->Release();
                mudstate.pIQueryControl = nullptr;

                STARTLOG(LOG_ALWAYS, "INI", "LOAD");
                log_printf(T("Couldn\xE2\x80\x99t create Query Sink (%d)."), mr);
                ENDLOG;
            }
        }
        else
        {
            mudstate.pIQueryControl->Release();
            mudstate.pIQueryControl = nullptr;

            STARTLOG(LOG_ALWAYS, "INI", "LOAD");
            log_printf(T("Couldn\xE2\x80\x99t connect to Query Server (%d)."), mr);
            ENDLOG;
        }
    }
    else
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_text(T("Couldn\xE2\x80\x99t create interface to Query Server."));
        ENDLOG;
    }

#if defined(INLINESQL)
    init_sql();
#endif // INLINESQL

    fcache_init();
    helpindex_init();

    // Open or create the SQLite database.
    //
    if (bMinDB)
    {
        // Remove the SQLite database to start fresh.
        //
        char sqlitefile[SIZEOF_PATHNAME];
        mux_strncpy((UTF8 *)sqlitefile, mudconf.indb,
                     sizeof(sqlitefile) - 1);
        sqlitefile[sizeof(sqlitefile) - 1] = '\0';
        size_t n = strlen(sqlitefile);
        if (n > 3 && strcmp(sqlitefile + n - 3, ".db") == 0)
        {
            strcpy(sqlitefile + n - 3, ".sqlite");
        }
        else
        {
            strcat(sqlitefile, ".sqlite");
        }
        RemoveFile((UTF8 *)sqlitefile);
    }
    int ccPageFile = init_dbfile(mudconf.indb);
    if (HF_OPEN_STATUS_ERROR == ccPageFile)
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_text(T("Couldn\xE2\x80\x99t open storage backend."));
        ENDLOG;
        return MUX_E_FAIL;
    }

    mudstate.record_players = 0;
    bool bLoadedGameFromSQLite = false;

    if (bMinDB)
    {
        if (!db_make_minimal())
        {
            STARTLOG(LOG_ALWAYS, "INI", "LOAD")
            log_text(T("Failed to build minimal database."));
            ENDLOG
            return MUX_E_FAIL;
        }
    }
    else
    {
        bool bDoFlatfileLoad = true;
        if (HF_OPEN_STATUS_OLD == ccPageFile)
        {
            int sqlite_load_rc = sqlite_load_game();
            if (sqlite_load_rc > 0)
            {
                // Warm start: loaded everything from SQLite.
                // No flatfile needed.
                //
                bDoFlatfileLoad = false;
                bLoadedGameFromSQLite = true;
            }
            else if (sqlite_load_rc < 0)
            {
                STARTLOG(LOG_ALWAYS, "INI", "LOAD")
                log_text(T("SQLite warm-load failed."));
                ENDLOG
                return MUX_E_FAIL;
            }
        }

        int ccInFile = LOAD_GAME_SUCCESS;
        if (bDoFlatfileLoad)
        {
            ccInFile = load_game(ccPageFile);
        }
        if (LOAD_GAME_NO_INPUT_DB == ccInFile)
        {
            // The input file didn't exist.
            //
            if (HF_OPEN_STATUS_NEW == ccPageFile)
            {
                // Since the .db file didn't exist, and the .pag/.dir files
                // were newly created, just create a minimal DB.
                //
                if (!db_make_minimal())
                {
                    ccInFile = LOAD_GAME_LOADING_PROBLEM;
                }
                else
                {
                    ccInFile = LOAD_GAME_SUCCESS;
                }
            }
        }
        if (ccInFile != LOAD_GAME_SUCCESS)
        {
            STARTLOG(LOG_ALWAYS, "INI", "LOAD")
            log_text(T("Couldn\xE2\x80\x99t load: "));
            log_text(mudconf.indb);
            ENDLOG
            return MUX_E_FAIL;
        }
    }

    // Warm-start path skips load_game(); explicitly load aux SQLite/flatfile
    // subsystems here.
    //
    if (bLoadedGameFromSQLite)
    {
        int load_comsys_rc = sqlite_load_comsys();
        if (load_comsys_rc < 0)
        {
            STARTLOG(LOG_ALWAYS, "INI", "LOAD")
            log_text(T("SQLite comsys load failed."));
            ENDLOG
            return MUX_E_FAIL;
        }
        if (0 == load_comsys_rc)
        {
            load_comsys(mudconf.comsys_db);
        }

        int load_mail_rc = sqlite_load_mail();
        if (load_mail_rc < 0)
        {
            STARTLOG(LOG_ALWAYS, "INI", "LOAD")
            log_text(T("SQLite mail load failed."));
            ENDLOG
            return MUX_E_FAIL;
        }
        if (0 == load_mail_rc)
        {
            FILE *f;
            if (mux_fopen(&f, mudconf.mail_db, T("rb")))
            {
                DebugTotalFiles++;
                setvbuf(f, nullptr, _IOFBF, 16384);
                Log.tinyprintf(T("LOADING: %s" ENDLINE), mudconf.mail_db);
                load_mail(f);
                Log.tinyprintf(T("LOADING: %s (done)" ENDLINE),
                               mudconf.mail_db);
                if (fclose(f) != 0)
                {
                    STARTLOG(LOG_PROBLEMS, "DB", "FCLOSE");
                    log_printf(T("fclose failed for %s"), mudconf.mail_db);
                    ENDLOG;
                }
                DebugTotalFiles--;
            }
        }
    }

    return MUX_S_OK;
}

MUX_RESULT CGameEngine::Startup(void)
{
    Guest.StartUp();

    // Do a consistency check and set up the freelist.
    //
    do_dbck(NOTHING, NOTHING, NOTHING, 0, 0);

    ValidateConfigurationDbrefs();
    process_preload();

    local_startup();

    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->startup();
        p = p->pNext;
    }

    init_timer();
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::RunTasks(CLinearTimeAbsolute &ltaNow)
{
    scheduler.RunTasks(ltaNow);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::UpdateQuotas(CLinearTimeAbsolute &ltaLast,
    const CLinearTimeAbsolute &ltaCurrent)
{
    update_quotas(ltaLast, ltaCurrent);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::WhenNext(CLinearTimeAbsolute *pltaWhen)
{
    if (nullptr == pltaWhen)
    {
        return MUX_E_INVALIDARG;
    }
    scheduler.WhenNext(pltaWhen);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::DumpDatabase(void)
{
    dump_database();
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::Shutdown(void)
{
    local_shutdown();

    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->shutdown();
        p = p->pNext;
    }
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::ShouldShutdown(bool *pbShutdown)
{
    if (nullptr == pbShutdown)
    {
        return MUX_E_INVALIDARG;
    }
    *pbShutdown = mudstate.shutdown_flag;
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CGameEngineFactory
// ---------------------------------------------------------------------------

CGameEngineFactory::CGameEngineFactory(void) : m_cRef(1)
{
}

CGameEngineFactory::~CGameEngineFactory()
{
}

MUX_RESULT CGameEngineFactory::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CGameEngineFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CGameEngineFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CGameEngineFactory::CreateInstance(mux_IUnknown *pUnknownOuter,
    MUX_IID iid, void **ppv)
{
    if (nullptr != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CGameEngine *pGameEngine = nullptr;
    try
    {
        pGameEngine = new CGameEngine;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pGameEngine)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pGameEngine->QueryInterface(iid, ppv);
    pGameEngine->Release();
    return mr;
}

MUX_RESULT CGameEngineFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CConnectionManager — driver-side implementation of mux_IConnectionManager.
// Wraps the net.cpp accessor layer so engine code can interact with
// connections without linking against driver symbols.
// ---------------------------------------------------------------------------

class CConnectionManager : public mux_IConnectionManager
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    // Output
    virtual MUX_RESULT SendText(dbref target, const UTF8 *text);
    virtual MUX_RESULT SendRaw(dbref target, const UTF8 *data, size_t len);
    virtual MUX_RESULT BroadcastAndFlush(int inflags, const UTF8 *text);
    virtual MUX_RESULT SendProgPrompt(dbref target);
    virtual MUX_RESULT SendKeepaliveNops(void);

    // Queries by dbref
    virtual MUX_RESULT GetTotalConnections(int *pCount);
    virtual MUX_RESULT CountPlayerDescs(dbref target, int *pCount);
    virtual MUX_RESULT SumPlayerCommandCount(dbref target, int *pCount);
    virtual MUX_RESULT FetchHeight(dbref target, int *pHeight);
    virtual MUX_RESULT FetchWidth(dbref target, int *pWidth);
    virtual MUX_RESULT FetchIdle(dbref target, int *pIdle);
    virtual MUX_RESULT FetchConnect(dbref target, int *pConnect);

    // Queries by opaque DESC handle
    virtual MUX_RESULT FindDescBySocket(SOCKET s, DESC **ppDesc);
    virtual MUX_RESULT FindDescByPlayer(dbref target, DESC **ppDesc);
    virtual MUX_RESULT DescPlayer(const DESC *d, dbref *pPlayer);
    virtual MUX_RESULT DescHeight(const DESC *d, int *pHeight);
    virtual MUX_RESULT DescWidth(const DESC *d, int *pWidth);
    virtual MUX_RESULT DescEncoding(const DESC *d, int *pEncoding);
    virtual MUX_RESULT DescCommandCount(const DESC *d, int *pCount);
    virtual MUX_RESULT DescTtype(const DESC *d, const UTF8 **ppTtype);
    virtual MUX_RESULT DescLastTime(const DESC *d, CLinearTimeAbsolute *pTime);
    virtual MUX_RESULT DescConnectedAt(const DESC *d, CLinearTimeAbsolute *pTime);
    virtual MUX_RESULT DescNvtHimState(const DESC *d, unsigned char chOption, int *pState);
    virtual MUX_RESULT DescSocketState(const DESC *d, SocketState *pState);

    // Iteration
    virtual MUX_RESULT ForEachConnectedPlayer(
        void (*callback)(dbref player, void *context), void *context);
    virtual MUX_RESULT ForEachConnectedDesc(
        void (*callback)(dbref player, SOCKET sock, void *context), void *context);

    // @program state
    virtual MUX_RESULT PlayerHasProgram(dbref target, bool *pbHas);
    virtual MUX_RESULT DetachPlayerProgram(dbref target, program_data **ppProgram);
    virtual MUX_RESULT SetPlayerProgram(dbref target, program_data *program);

    // Encoding / Display
    virtual MUX_RESULT SetPlayerEncoding(dbref target, int encoding);
    virtual MUX_RESULT ResetPlayerEncoding(dbref target);
    virtual MUX_RESULT SetDoingAll(dbref target, const UTF8 *doing, size_t len);
    virtual MUX_RESULT SetDoingLeastIdle(dbref target, const UTF8 *doing, size_t len, bool *pbFound);

    // Quota
    virtual MUX_RESULT UpdateAllDescQuotas(int nExtra, int nMax);

    // Connection lifecycle
    virtual MUX_RESULT BootOff(dbref target, const UTF8 *message, int *pCount);
    virtual MUX_RESULT BootByPort(SOCKET port, bool bGod, const UTF8 *message, int *pCount);

    // Idle check
    virtual MUX_RESULT CheckIdle(void);
    virtual MUX_RESULT EmergencyShutdown(void);

    CConnectionManager(void);
    virtual ~CConnectionManager();

private:
    uint32_t m_cRef;
};

CConnectionManager::CConnectionManager(void) : m_cRef(1)
{
}

CConnectionManager::~CConnectionManager()
{
}

MUX_RESULT CConnectionManager::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IConnectionManager *>(this);
    }
    else if (IID_IConnectionManager == iid)
    {
        *ppv = static_cast<mux_IConnectionManager *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    AddRef();
    return MUX_S_OK;
}

uint32_t CConnectionManager::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CConnectionManager::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

// --- Output ---

MUX_RESULT CConnectionManager::SendText(dbref target, const UTF8 *text)
{
    send_text_to_player(target, text);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::SendRaw(dbref target, const UTF8 *data, size_t len)
{
    send_raw_to_player(target, data, len);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::BroadcastAndFlush(int inflags, const UTF8 *text)
{
    broadcast_and_flush(inflags, text);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::SendProgPrompt(dbref target)
{
    send_prog_prompt(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::SendKeepaliveNops(void)
{
    send_keepalive_nops();
    return MUX_S_OK;
}

// --- Queries by dbref ---

MUX_RESULT CConnectionManager::GetTotalConnections(int *pCount)
{
    if (nullptr == pCount) return MUX_E_INVALIDARG;
    *pCount = get_total_connections();
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::CountPlayerDescs(dbref target, int *pCount)
{
    if (nullptr == pCount) return MUX_E_INVALIDARG;
    *pCount = count_player_descs(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::SumPlayerCommandCount(dbref target, int *pCount)
{
    if (nullptr == pCount) return MUX_E_INVALIDARG;
    *pCount = sum_player_command_count(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::FetchHeight(dbref target, int *pHeight)
{
    if (nullptr == pHeight) return MUX_E_INVALIDARG;
    *pHeight = fetch_height(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::FetchWidth(dbref target, int *pWidth)
{
    if (nullptr == pWidth) return MUX_E_INVALIDARG;
    *pWidth = fetch_width(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::FetchIdle(dbref target, int *pIdle)
{
    if (nullptr == pIdle) return MUX_E_INVALIDARG;
    *pIdle = fetch_idle(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::FetchConnect(dbref target, int *pConnect)
{
    if (nullptr == pConnect) return MUX_E_INVALIDARG;
    *pConnect = fetch_connect(target);
    return MUX_S_OK;
}

// --- Queries by opaque DESC handle ---

MUX_RESULT CConnectionManager::FindDescBySocket(SOCKET s, DESC **ppDesc)
{
    if (nullptr == ppDesc) return MUX_E_INVALIDARG;
    *ppDesc = find_desc_by_socket(s);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::FindDescByPlayer(dbref target, DESC **ppDesc)
{
    if (nullptr == ppDesc) return MUX_E_INVALIDARG;
    *ppDesc = find_desc_by_player(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescPlayer(const DESC *d, dbref *pPlayer)
{
    if (nullptr == d || nullptr == pPlayer) return MUX_E_INVALIDARG;
    *pPlayer = desc_player(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescHeight(const DESC *d, int *pHeight)
{
    if (nullptr == d || nullptr == pHeight) return MUX_E_INVALIDARG;
    *pHeight = desc_height(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescWidth(const DESC *d, int *pWidth)
{
    if (nullptr == d || nullptr == pWidth) return MUX_E_INVALIDARG;
    *pWidth = desc_width(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescEncoding(const DESC *d, int *pEncoding)
{
    if (nullptr == d || nullptr == pEncoding) return MUX_E_INVALIDARG;
    *pEncoding = desc_encoding(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescCommandCount(const DESC *d, int *pCount)
{
    if (nullptr == d || nullptr == pCount) return MUX_E_INVALIDARG;
    *pCount = desc_command_count(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescTtype(const DESC *d, const UTF8 **ppTtype)
{
    if (nullptr == d || nullptr == ppTtype) return MUX_E_INVALIDARG;
    *ppTtype = desc_ttype(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescLastTime(const DESC *d, CLinearTimeAbsolute *pTime)
{
    if (nullptr == d || nullptr == pTime) return MUX_E_INVALIDARG;
    *pTime = desc_last_time(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescConnectedAt(const DESC *d, CLinearTimeAbsolute *pTime)
{
    if (nullptr == d || nullptr == pTime) return MUX_E_INVALIDARG;
    *pTime = desc_connected_at(d);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescNvtHimState(const DESC *d, unsigned char chOption, int *pState)
{
    if (nullptr == d || nullptr == pState) return MUX_E_INVALIDARG;
    *pState = desc_nvt_him_state(d, chOption);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DescSocketState(const DESC *d, SocketState *pState)
{
    if (nullptr == d || nullptr == pState) return MUX_E_INVALIDARG;
    *pState = desc_socket_state(d);
    return MUX_S_OK;
}

// --- Iteration ---

MUX_RESULT CConnectionManager::ForEachConnectedPlayer(
    void (*callback)(dbref player, void *context), void *context)
{
    if (nullptr == callback) return MUX_E_INVALIDARG;
    for_each_connected_player(callback, context);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::ForEachConnectedDesc(
    void (*callback)(dbref player, SOCKET sock, void *context), void *context)
{
    if (nullptr == callback) return MUX_E_INVALIDARG;
    for_each_connected_desc(callback, context);
    return MUX_S_OK;
}

// --- @program state ---

MUX_RESULT CConnectionManager::PlayerHasProgram(dbref target, bool *pbHas)
{
    if (nullptr == pbHas) return MUX_E_INVALIDARG;
    *pbHas = player_has_program(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::DetachPlayerProgram(dbref target, program_data **ppProgram)
{
    if (nullptr == ppProgram) return MUX_E_INVALIDARG;
    *ppProgram = detach_player_program(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::SetPlayerProgram(dbref target, program_data *program)
{
    set_player_program(target, program);
    return MUX_S_OK;
}

// --- Encoding / Display ---

MUX_RESULT CConnectionManager::SetPlayerEncoding(dbref target, int encoding)
{
    set_player_encoding(target, encoding);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::ResetPlayerEncoding(dbref target)
{
    reset_player_encoding(target);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::SetDoingAll(dbref target, const UTF8 *doing, size_t len)
{
    set_doing_all(target, doing, len);
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::SetDoingLeastIdle(dbref target, const UTF8 *doing, size_t len, bool *pbFound)
{
    if (nullptr == pbFound) return MUX_E_INVALIDARG;
    *pbFound = set_doing_least_idle(target, doing, len);
    return MUX_S_OK;
}

// --- Quota ---

MUX_RESULT CConnectionManager::UpdateAllDescQuotas(int nExtra, int nMax)
{
    update_all_desc_quotas(nExtra, nMax);
    return MUX_S_OK;
}

// --- Connection lifecycle ---

MUX_RESULT CConnectionManager::BootOff(dbref target, const UTF8 *message, int *pCount)
{
    int n = boot_off(target, message);
    if (pCount) *pCount = n;
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::BootByPort(SOCKET port, bool bGod, const UTF8 *message, int *pCount)
{
    int n = boot_by_port(port, bGod, message);
    if (pCount) *pCount = n;
    return MUX_S_OK;
}

// --- Idle check ---

MUX_RESULT CConnectionManager::CheckIdle(void)
{
    check_idle();
    return MUX_S_OK;
}

MUX_RESULT CConnectionManager::EmergencyShutdown(void)
{
    emergency_shutdown();
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CConnectionManagerFactory
// ---------------------------------------------------------------------------

CConnectionManagerFactory::CConnectionManagerFactory(void) : m_cRef(1)
{
}

CConnectionManagerFactory::~CConnectionManagerFactory()
{
}

MUX_RESULT CConnectionManagerFactory::QueryInterface(MUX_IID iid, void **ppv)
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
    AddRef();
    return MUX_S_OK;
}

uint32_t CConnectionManagerFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CConnectionManagerFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CConnectionManagerFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);

    CConnectionManager *pConnMgr = nullptr;
    try
    {
        pConnMgr = new CConnectionManager;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pConnMgr)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pConnMgr->QueryInterface(iid, ppv);
    pConnMgr->Release();
    return mr;
}

MUX_RESULT CConnectionManagerFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}
