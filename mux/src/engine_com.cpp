/*! \file engine_com.cpp
 * \brief Engine-side COM class implementations and COM front-door.
 *
 * These classes implement the server-provided COM interfaces that
 * external modules use to interact with the game engine.
 *
 * engine.so is a proper COM server.  Only 4 extern "C" functions are
 * exported: mux_Register, mux_Unregister, mux_GetClassObject, and
 * mux_CanUnloadNow.  All other symbols have hidden visibility.
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "sqlite_backend.h"

// ---------------------------------------------------------------------------
// Factory class declarations for engine-side COM components.
// These are internal to engine.so (no DCL_EXPORT).
// ---------------------------------------------------------------------------

#define DEFINE_ENGINE_FACTORY(x)                                                               \
class x : public mux_IClassFactory                                                             \
{                                                                                              \
public:                                                                                        \
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);                                \
    virtual uint32_t   AddRef(void);                                                           \
    virtual uint32_t   Release(void);                                                          \
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);   \
    virtual MUX_RESULT LockServer(bool bLock);                                                 \
    x(void);                                                                                   \
    virtual ~x();                                                                              \
private:                                                                                       \
    uint32_t m_cRef;                                                                           \
};

// Factory classes defined in log.cpp (also in engine.so).
// Full declarations needed here for the COM front-door dispatch.
//
DEFINE_ENGINE_FACTORY(CLogFactory)

class CLogPSFactory : public mux_IPSFactoryBuffer, public mux_IClassFactory
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);
    virtual MUX_RESULT CreateProxy(mux_IUnknown *pUnknownOuter, MUX_IID riid, mux_IRpcProxyBuffer **ppProxy, void **ppv);
    virtual MUX_RESULT CreateStub(MUX_IID riid, mux_IUnknown *pUnknownOuter, mux_IRpcStubBuffer **ppStub);
    virtual MUX_RESULT CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv);
    virtual MUX_RESULT LockServer(bool bLock);
    CLogPSFactory(void);
    virtual ~CLogPSFactory();
private:
    uint32_t m_cRef;
};

DEFINE_ENGINE_FACTORY(CServerEventsSourceFactory)
DEFINE_ENGINE_FACTORY(CQueryClientFactory)
DEFINE_ENGINE_FACTORY(CFunctionsFactory)
DEFINE_ENGINE_FACTORY(CNotifyFactory)
DEFINE_ENGINE_FACTORY(CObjectInfoFactory)
DEFINE_ENGINE_FACTORY(CAttributeAccessFactory)
DEFINE_ENGINE_FACTORY(CEvaluatorFactory)
DEFINE_ENGINE_FACTORY(CPermissionsFactory)
DEFINE_ENGINE_FACTORY(CMailDeliveryFactory)
DEFINE_ENGINE_FACTORY(CHelpSystemFactory)
DEFINE_ENGINE_FACTORY(CGameEngineFactory)
DEFINE_ENGINE_FACTORY(CPlayerSessionFactory)

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

DCL_EXPORT ServerEventsSinkNode *g_pServerEventsSinkListHead = nullptr;

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

    // Create the player session interface for driver use.
    //
    mudstate.pIPlayerSession = nullptr;
    mr = mux_CreateInstance(CID_PlayerSession, nullptr, UseSameProcess,
                            IID_IPlayerSession,
                            reinterpret_cast<void **>(&mudstate.pIPlayerSession));
    if (MUX_FAILED(mr))
    {
        STARTLOG(LOG_ALWAYS, "INI", "LOAD");
        log_printf(T("Failed to create PlayerSession interface (%d)."), mr);
        ENDLOG;
    }

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
// CPlayerSession — engine-side implementation of mux_IPlayerSession.
// Handles player authentication, creation, and connect/disconnect lifecycle.
// The driver calls these methods; all player/game-state operations are here.
// ---------------------------------------------------------------------------

class CPlayerSession : public mux_IPlayerSession
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT ConnectPlayer(const UTF8 *name, const UTF8 *password,
        const UTF8 *host, const UTF8 *username, const UTF8 *ipaddr,
        dbref *pPlayer);
    virtual MUX_RESULT CreatePlayer(const UTF8 *name, const UTF8 *password,
        dbref creator, bool isRobot, dbref *pPlayer,
        const UTF8 **ppMsg);
    virtual MUX_RESULT AddToPublicChannel(dbref player);
    virtual MUX_RESULT AddToPlayerChannels(dbref player);
    virtual MUX_RESULT AnnounceConnect(dbref player, int numConnections,
        bool isPueblo, bool isSuspect, const UTF8 *host,
        const UTF8 *username, const UTF8 *ipaddr, int *pTimeout);
    virtual MUX_RESULT AnnounceDisconnect(dbref player, int numConnections,
        bool isSuspect, bool wasAutoDark, const UTF8 *reason);

    CPlayerSession(void);
    virtual ~CPlayerSession();

private:
    uint32_t m_cRef;
};

CPlayerSession::CPlayerSession(void) : m_cRef(1)
{
}

CPlayerSession::~CPlayerSession()
{
}

MUX_RESULT CPlayerSession::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IPlayerSession *>(this);
    }
    else if (IID_IPlayerSession == iid)
    {
        *ppv = static_cast<mux_IPlayerSession *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

uint32_t CPlayerSession::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CPlayerSession::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CPlayerSession::ConnectPlayer(const UTF8 *name,
    const UTF8 *password, const UTF8 *host, const UTF8 *username,
    const UTF8 *ipaddr, dbref *pPlayer)
{
    if (nullptr == pPlayer)
    {
        return MUX_E_INVALIDARG;
    }
    *pPlayer = connect_player(const_cast<UTF8 *>(name),
        const_cast<UTF8 *>(password), const_cast<UTF8 *>(host),
        const_cast<UTF8 *>(username), const_cast<UTF8 *>(ipaddr));
    return (*pPlayer == NOTHING) ? MUX_E_NOTFOUND : MUX_S_OK;
}

MUX_RESULT CPlayerSession::CreatePlayer(const UTF8 *name,
    const UTF8 *password, dbref creator, bool isRobot,
    dbref *pPlayer, const UTF8 **ppMsg)
{
    if (nullptr == pPlayer)
    {
        return MUX_E_INVALIDARG;
    }
    *pPlayer = create_player(name, password, creator, isRobot, ppMsg);
    return (*pPlayer == NOTHING) ? MUX_E_FAIL : MUX_S_OK;
}

MUX_RESULT CPlayerSession::AddToPublicChannel(dbref player)
{
    ::AddToPublicChannel(player);
    return MUX_S_OK;
}

MUX_RESULT CPlayerSession::AddToPlayerChannels(dbref player)
{
    ::AddToPlayerChannels(player);
    return MUX_S_OK;
}

MUX_RESULT CPlayerSession::AnnounceConnect(dbref player, int numConnections,
    bool isPueblo, bool isSuspect, const UTF8 *host,
    const UTF8 *username, const UTF8 *ipaddr, int *pTimeout)
{
    // Preload built-in attributes for the player and their location.
    //
    cache_preload(player);
    const dbref ploc = Location(player);
    if (Good_obj(ploc))
    {
        cache_preload(ploc);
    }

    // Track record player count.
    //
    int count = numConnections;
    if (mudstate.record_players < count)
    {
        mudstate.record_players = count;
        g_pSQLiteBackend->GetDB().PutMeta("record_players",
            mudstate.record_players);
    }

    // Read A_TIMEOUT and return it to the driver.
    //
    if (nullptr != pTimeout)
    {
        UTF8 *buf = alloc_lbuf("AnnounceConnect.timeout");
        dbref aowner;
        int aflags;
        size_t nLen;
        atr_pget_str_LEN(buf, player, A_TIMEOUT, &aowner, &aflags, &nLen);
        if (nLen)
        {
            *pTimeout = mux_atol(buf);
            if (*pTimeout <= 0)
            {
                *pTimeout = mudconf.idle_timeout;
            }
        }
        else
        {
            *pTimeout = mudconf.idle_timeout;
        }
        free_lbuf(buf);
    }

    // Set Connected flag.  Set Html if Pueblo client.
    //
    const dbref loc = Location(player);
    s_Connected(player);

    if (isPueblo)
    {
        s_Html(player);
    }

    // MOTD messages.
    //
    if ('\0' != mudconf.motd_msg[0])
    {
        raw_notify(player, tprintf(T("\n%sMOTD:%s %s\n"), COLOR_INTENSE,
                    COLOR_RESET, mudconf.motd_msg));
    }

    if (Wizard(player))
    {
        if ('\0' != mudconf.wizmotd_msg[0])
        {
            raw_notify(player, tprintf(T("%sWIZMOTD:%s %s\n"), COLOR_INTENSE,
                        COLOR_RESET, mudconf.wizmotd_msg));
        }

        if (!(mudconf.control_flags & CF_LOGIN))
        {
            raw_notify(player, T("*** Logins are disabled."));
        }
    }

    // Page lock warning.
    //
    {
        UTF8 *buf = alloc_lbuf("AnnounceConnect.lpage");
        dbref aowner;
        int aflags;
        size_t nLen;
        atr_get_str_LEN(buf, player, A_LPAGE, &aowner, &aflags, &nLen);
        if (nLen)
        {
            raw_notify(player, T("Your PAGE LOCK is set.  You may be unable to receive some pages."));
        }
        free_lbuf(buf);
    }

    // Check for forced encoding.
    //
    if (Unicode(player))
    {
        set_player_encoding(player, CHARSET_UTF8);
    }
    if (Ascii(player))
    {
        set_player_encoding(player, CHARSET_ASCII);
    }

    // Reset vacation flag.  Clear DARK on guests.
    //
    s_Flags(player, FLAG_WORD2, Flags2(player) & ~VACATION);
    if (Guest(player))
    {
        s_Flags(player, FLAG_WORD1, db[player].fs.word[FLAG_WORD1] & ~DARK);
    }

    // Room and monitor announcements.
    //
    const UTF8 *pRoomAnnounceFmt;
    const UTF8 *pMonitorAnnounceFmt;
    if (numConnections < 2)
    {
        pRoomAnnounceFmt = T("%s has connected.");
        do_comconnect(player);
        if (  Hidden(player)
           && Can_Hide(player))
        {
            pMonitorAnnounceFmt = T("GAME: %s has DARK-connected.");
        }
        else
        {
            pMonitorAnnounceFmt = T("GAME: %s has connected.");
        }
        if (  Suspect(player)
           || isSuspect)
        {
            raw_broadcast(WIZARD, T("[Suspect] %s has connected."),
                Moniker(player));
        }
    }
    else
    {
        pRoomAnnounceFmt = T("%s has reconnected.");
        pMonitorAnnounceFmt = T("GAME: %s has reconnected.");
        if (  Suspect(player)
           || isSuspect)
        {
            raw_broadcast(WIZARD, T("[Suspect] %s has reconnected."),
                Moniker(player));
        }
    }

    UTF8 *buf = alloc_lbuf("AnnounceConnect.room");
    mux_sprintf(buf, LBUF_SIZE, pRoomAnnounceFmt, Moniker(player));
    raw_broadcast(MONITOR, pMonitorAnnounceFmt, Moniker(player));

    int key = MSG_INV;
    if (  loc != NOTHING
       && !(  Hidden(player)
           && Can_Hide(player)))
    {
        key |= (MSG_NBR | MSG_NBR_EXITS | MSG_LOC | MSG_FWDLIST);
    }

    dbref temp = mudstate.curr_enactor;
    mudstate.curr_enactor = player;
#ifdef REALITY_LVLS
    if (NOTHING == loc)
    {
        notify_check(player, player, buf, key);
    }
    else
    {
        notify_except_rlevel(loc, player, player, buf, 0);
    }
#else
    notify_check(player, player, buf, key);
#endif // REALITY_LVLS

    // ACONNECT triggers: player, master room, zone.
    //
    dbref aowner, zone, obj;
    int aflags;
    size_t nLen;
    CLinearTimeAbsolute lta;
    atr_pget_str_LEN(buf, player, A_ACONNECT, &aowner, &aflags, &nLen);
    if (nLen)
    {
        wait_que(player, player, player, AttrTrace(aflags, 0), false, lta,
            NOTHING, 0, buf, 0, nullptr, nullptr);
    }
    if (mudconf.master_room != NOTHING)
    {
        atr_pget_str_LEN(buf, mudconf.master_room, A_ACONNECT, &aowner,
            &aflags, &nLen);
        if (nLen)
        {
            wait_que(mudconf.master_room, player, player,
                AttrTrace(aflags, 0), false, lta, NOTHING, 0, buf,
                0, nullptr, nullptr);
        }
        DOLIST(obj, Contents(mudconf.master_room))
        {
            atr_pget_str_LEN(buf, obj, A_ACONNECT, &aowner, &aflags, &nLen);
            if (nLen)
            {
                wait_que(obj, player, player, AttrTrace(aflags, 0), false, lta,
                    NOTHING, 0, buf, 0, nullptr, nullptr);
            }
        }
    }

    // Zone ACONNECT.
    //
    if (  mudconf.have_zones
       && Good_obj(zone = Zone(loc)))
    {
        switch (Typeof(zone))
        {
        case TYPE_THING:
            atr_pget_str_LEN(buf, zone, A_ACONNECT, &aowner, &aflags, &nLen);
            if (nLen)
            {
                wait_que(zone, player, player, AttrTrace(aflags, 0), false,
                    lta, NOTHING, 0, buf, 0, nullptr, nullptr);
            }
            break;

        case TYPE_ROOM:
            DOLIST(obj, Contents(zone))
            {
                atr_pget_str_LEN(buf, obj, A_ACONNECT, &aowner, &aflags,
                    &nLen);
                if (nLen)
                {
                    wait_que(obj, player, player, AttrTrace(aflags, 0), false,
                        lta, NOTHING, 0, buf, 0, nullptr, nullptr);
                }
            }
            break;

        default:
            log_printf(T("Invalid zone #%d for %s(#%d) has bad type %d"),
                zone, PureName(player), player, Typeof(zone));
        }
    }
    free_lbuf(buf);

    // Record login, check mail, show room.
    //
    CLinearTimeAbsolute ltaNow;
    ltaNow.GetLocal();
    const UTF8 *time_str = ltaNow.ReturnDateString(7);
    record_login(player, true, time_str, host, username, ipaddr);
    check_mail(player, 0, false);
    look_in(player, Location(player), (LK_SHOWEXIT|LK_OBEYTERSE|LK_SHOWVRML));
    mudstate.curr_enactor = temp;

    local_connect(player, 0, numConnections);

    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->connect(player, 0, numConnections);
        p = p->pNext;
    }

    return MUX_S_OK;
}

MUX_RESULT CPlayerSession::AnnounceDisconnect(dbref player,
    int numConnections, bool isSuspect, bool wasAutoDark,
    const UTF8 *reason)
{
    int key;
    const dbref temp = mudstate.curr_enactor;
    mudstate.curr_enactor = player;
    const dbref loc = Location(player);

    if (numConnections < 2)
    {
        // Last connection — full disconnect.
        //
        if (  Suspect(player)
           || isSuspect)
        {
            raw_broadcast(WIZARD, T("[Suspect] %s has disconnected."),
                Moniker(player));
        }
        UTF8 *buf = alloc_lbuf("AnnounceDisconnect.only");

        mux_sprintf(buf, LBUF_SIZE, T("%s has disconnected."),
            Moniker(player));
        key = MSG_INV;
        if (  loc != NOTHING
           && !(  Hidden(player)
               && Can_Hide(player)))
        {
            key |= (MSG_NBR | MSG_NBR_EXITS | MSG_LOC | MSG_FWDLIST);
        }
#ifdef REALITY_LVLS
        if (NOTHING == loc)
        {
            notify_check(player, player, buf, key);
        }
        else
        {
            notify_except_rlevel(loc, player, player, buf, 0);
        }
#else
        notify_check(player, player, buf, key);
#endif // REALITY_LVLS

        do_mail_purge(player);

        raw_broadcast(MONITOR, T("GAME: %s has disconnected. <%s>"),
            Moniker(player), reason);

        c_Connected(player);
        do_comdisconnect(player);

        // ADISCONNECT triggers: player, master room, zone.
        //
        dbref aowner, zone, obj;
        int aflags;
        size_t nLen;
        CLinearTimeAbsolute lta;
        atr_pget_str_LEN(buf, player, A_ADISCONNECT, &aowner, &aflags, &nLen);
        if (nLen)
        {
            wait_que(player, player, player, AttrTrace(aflags, 0), false,
                lta, NOTHING, 0, buf, 1, &reason, nullptr);
        }
        if (mudconf.master_room != NOTHING)
        {
            atr_pget_str_LEN(buf, mudconf.master_room, A_ADISCONNECT, &aowner,
                &aflags, &nLen);
            if (nLen)
            {
                wait_que(mudconf.master_room, player, player,
                    AttrTrace(aflags, 0), false, lta, NOTHING, 0, buf,
                    0, nullptr, nullptr);
            }
            DOLIST(obj, Contents(mudconf.master_room))
            {
                atr_pget_str_LEN(buf, obj, A_ADISCONNECT, &aowner, &aflags,
                    &nLen);
                if (nLen)
                {
                    wait_que(obj, player, player, AttrTrace(aflags, 0), false,
                        lta, NOTHING, 0, buf, 0, nullptr, nullptr);
                }
            }
        }

        // Zone ADISCONNECT.
        //
        if (mudconf.have_zones && Good_obj(zone = Zone(loc)))
        {
            switch (Typeof(zone))
            {
            case TYPE_THING:
                atr_pget_str_LEN(buf, zone, A_ADISCONNECT, &aowner, &aflags,
                    &nLen);
                if (nLen)
                {
                    wait_que(zone, player, player, AttrTrace(aflags, 0),
                        false, lta, NOTHING, 0, buf, 0, nullptr, nullptr);
                }
                break;

            case TYPE_ROOM:
                DOLIST(obj, Contents(zone))
                {
                    atr_pget_str_LEN(buf, obj, A_ADISCONNECT, &aowner, &aflags,
                        &nLen);
                    if (nLen)
                    {
                        wait_que(obj, player, player, AttrTrace(aflags, 0),
                            false, lta, NOTHING, 0, buf, 0, nullptr, nullptr);
                    }
                }
                break;

            default:
                log_printf(T("Invalid zone #%d for %s(#%d) has bad type %d"),
                    zone, PureName(player), player, Typeof(zone));
            }
        }
        free_lbuf(buf);

        // Clear AUTODARK, darken guests, halt guest queues.
        //
        if (wasAutoDark)
        {
            s_Flags(player, FLAG_WORD1,
                db[player].fs.word[FLAG_WORD1] & ~DARK);
        }

        if (Guest(player))
        {
            s_Flags(player, FLAG_WORD1,
                db[player].fs.word[FLAG_WORD1] | DARK);
            halt_que(NOTHING, player);
        }
    }
    else
    {
        // Partial disconnect — other connections remain.
        //
        if (  Suspect(player)
           || isSuspect)
        {
            raw_broadcast(WIZARD,
                T("[Suspect] %s has partially disconnected."),
                Moniker(player));
        }
        UTF8 *mbuf = alloc_mbuf("AnnounceDisconnect.partial");
        mux_sprintf(mbuf, MBUF_SIZE, T("%s has partially disconnected."),
            Moniker(player));
        key = MSG_INV;
        if (  loc != NOTHING
           && !(  Hidden(player)
               && Can_Hide(player)))
        {
            key |= (MSG_NBR | MSG_NBR_EXITS | MSG_LOC | MSG_FWDLIST);
        }
#ifdef REALITY_LVLS
        if (NOTHING == loc)
        {
            notify_check(player, player, mbuf, key);
        }
        else
        {
            notify_except_rlevel(loc, player, player, mbuf, 0);
        }
#else
        notify_check(player, player, mbuf, key);
#endif // REALITY_LVLS
        raw_broadcast(MONITOR, T("GAME: %s has partially disconnected."),
            Moniker(player));
        free_mbuf(mbuf);
    }

    mudstate.curr_enactor = temp;

    local_disconnect(player, numConnections);
    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->disconnect(player, numConnections);
        p = p->pNext;
    }

    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CPlayerSessionFactory
// ---------------------------------------------------------------------------

CPlayerSessionFactory::CPlayerSessionFactory(void) : m_cRef(1)
{
}

CPlayerSessionFactory::~CPlayerSessionFactory()
{
}

MUX_RESULT CPlayerSessionFactory::QueryInterface(MUX_IID iid, void **ppv)
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

uint32_t CPlayerSessionFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

uint32_t CPlayerSessionFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CPlayerSessionFactory::CreateInstance(mux_IUnknown *pUnknownOuter,
    MUX_IID iid, void **ppv)
{
    if (nullptr != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CPlayerSession *pPlayerSession = nullptr;
    try
    {
        pPlayerSession = new CPlayerSession;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (nullptr == pPlayerSession)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pPlayerSession->QueryInterface(iid, ppv);
    pPlayerSession->Release();
    return mr;
}

MUX_RESULT CPlayerSessionFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// ===========================================================================
// COM Front-Door — engine.so exports only these 4 functions.
// ===========================================================================

static MUX_CLASS_INFO engine_classes[] =
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
    { CID_PlayerSession      },
};
#define NUM_ENGINE_CLASSES (sizeof(engine_classes)/sizeof(engine_classes[0]))

#define MAKE_FACTORY(cls, cid)                                      \
    if (cid == cid_arg)                                             \
    {                                                               \
        cls *pFactory = nullptr;                                    \
        try { pFactory = new cls; } catch (...) { ; }              \
        if (nullptr == pFactory) return MUX_E_OUTOFMEMORY;         \
        mr = pFactory->QueryInterface(iid, ppv);                   \
        pFactory->Release();                                        \
        return mr;                                                  \
    }

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_GetClassObject(MUX_CID cid_arg, MUX_IID iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    MAKE_FACTORY(CLogFactory,                CID_Log)
    MAKE_FACTORY(CServerEventsSourceFactory, CID_ServerEventsSource)
    MAKE_FACTORY(CQueryClientFactory,        CID_QueryClient)
    MAKE_FACTORY(CFunctionsFactory,          CID_Functions)
    MAKE_FACTORY(CLogPSFactory,              CID_LogPSFactory)
    MAKE_FACTORY(CNotifyFactory,             CID_Notify)
    MAKE_FACTORY(CObjectInfoFactory,         CID_ObjectInfo)
    MAKE_FACTORY(CAttributeAccessFactory,    CID_AttributeAccess)
    MAKE_FACTORY(CEvaluatorFactory,          CID_Evaluator)
    MAKE_FACTORY(CPermissionsFactory,        CID_Permissions)
    MAKE_FACTORY(CMailDeliveryFactory,       CID_MailDelivery)
    MAKE_FACTORY(CHelpSystemFactory,         CID_HelpSystem)
    MAKE_FACTORY(CGameEngineFactory,         CID_GameEngine)
    MAKE_FACTORY(CPlayerSessionFactory,      CID_PlayerSession)

    return mr;
}

static MUX_INTERFACE_INFO engine_interfaces[] =
{
    { IID_ILog, CID_LogPSFactory }
};
#define NUM_ENGINE_INTERFACES (sizeof(engine_interfaces)/sizeof(engine_interfaces[0]))

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register(void)
{
    MUX_RESULT mr = mux_RegisterClassObjects(NUM_ENGINE_CLASSES, engine_classes, mux_GetClassObject);
    if (MUX_SUCCEEDED(mr))
    {
        mr = mux_RegisterInterfaces(NUM_ENGINE_INTERFACES, engine_interfaces);
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Unregister(void)
{
    mux_RevokeInterfaces(NUM_ENGINE_INTERFACES, engine_interfaces);
    return mux_RevokeClassObjects(NUM_ENGINE_CLASSES, engine_classes);
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_CanUnloadNow(void)
{
    // The engine can never be unloaded.
    return MUX_S_FALSE;
}

