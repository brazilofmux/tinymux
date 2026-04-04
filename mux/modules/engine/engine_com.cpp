/*! \file engine_com.cpp
 * \brief Engine-side COM class implementations and COM front-door.
 *
 * These classes implement the server-provided COM interfaces that
 * external modules use to interact with the game engine.
 *
 * engine.so is a proper COM server.  Only 4 extern "C" functions are
 * exported: mux_Register, mux_Unregister, mux_GetClassObject, and
 * mux_CanUnloadNow.  All other symbols have hidden visibility.
 * g_debug_cmd lives in libmux.so (shared crash breadcrumb).
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"
#include "externs.h"
#include "sqlite_backend.h"
#include "mguests.h"
#include "engine_api.h"
#include "routing.h"
#include "walk.h"

// g_debug_cmd moved to libmux.cpp — shared by all layers.

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
DEFINE_ENGINE_FACTORY(CComsysStorageFactory)
DEFINE_ENGINE_FACTORY(CMailStorageFactory)

#if defined(TINYMUX_JIT)
// JIT Compile factory — implementation delegates to jit_lua.cpp.
extern MUX_RESULT jit_compile_create_instance(MUX_IID iid, void **ppv);
DEFINE_ENGINE_FACTORY(CJITCompileFactory)
#endif

// Lua module factory — implementation delegates to lua_mod.cpp.
extern MUX_RESULT lua_mod_create_instance(MUX_IID iid, void **ppv);
DEFINE_ENGINE_FACTORY(CLuaModFactory)

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

    virtual MUX_RESULT GetFlags(dbref obj, int word, unsigned int *pFlags);
    virtual MUX_RESULT SetFlags(dbref obj, int word, unsigned int flags);
    virtual MUX_RESULT GetPowers(dbref obj, unsigned int *pPowers);
    virtual MUX_RESULT GetPennies(dbref obj, int *pPennies);
    virtual MUX_RESULT GetPureName(dbref obj, const UTF8 **ppName);
    virtual MUX_RESULT DecodeFlags(dbref player, dbref obj, UTF8 **ppStr);

    virtual MUX_RESULT IsWizard(dbref obj, bool *pResult);
    virtual MUX_RESULT IsWizRoy(dbref obj, bool *pResult);
    virtual MUX_RESULT CanIdle(dbref obj, bool *pResult);
    virtual MUX_RESULT WizardWho(dbref obj, bool *pResult);
    virtual MUX_RESULT SeeHidden(dbref obj, bool *pResult);

    virtual MUX_RESULT AtrAddRaw(dbref obj, int attrnum, const UTF8 *value);
    virtual MUX_RESULT AtrClr(dbref obj, int attrnum);
    virtual MUX_RESULT AtrGet(dbref obj, int attrnum, UTF8 *pValue,
        size_t nValueMax, dbref *pOwner, int *pFlags);
    virtual MUX_RESULT AtrPGet(dbref obj, int attrnum, UTF8 *pValue,
        size_t nValueMax, dbref *pOwner, int *pFlags);
    virtual MUX_RESULT LookupPlayer(dbref executor, const UTF8 *pName,
        bool bConnected, dbref *pResult);
    virtual MUX_RESULT FetchConnectionInfoFields(dbref player,
        long anFields[4]);
    virtual MUX_RESULT PutConnectionInfoFields(dbref player,
        long anFields[4], CLinearTimeAbsolute &ltaNow);

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

MUX_RESULT CObjectInfo::GetFlags(dbref obj, int word, unsigned int *pFlags)
{
    if (nullptr == pFlags)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj) || word < FLAG_WORD1 || word > FLAG_WORD3)
    {
        *pFlags = 0;
        return MUX_E_INVALIDARG;
    }
    *pFlags = db[obj].fs.word[word];
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::SetFlags(dbref obj, int word, unsigned int flags)
{
    if (!Good_obj(obj) || word < FLAG_WORD1 || word > FLAG_WORD3)
    {
        return MUX_E_INVALIDARG;
    }
    s_Flags(obj, word, flags);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::GetPowers(dbref obj, unsigned int *pPowers)
{
    if (nullptr == pPowers)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pPowers = 0;
        return MUX_E_INVALIDARG;
    }
    *pPowers = Powers(obj);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::GetPennies(dbref obj, int *pPennies)
{
    if (nullptr == pPennies)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pPennies = 0;
        return MUX_E_INVALIDARG;
    }
    *pPennies = Pennies(obj);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::GetPureName(dbref obj, const UTF8 **ppName)
{
    if (nullptr == ppName)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *ppName = nullptr;
        return MUX_E_INVALIDARG;
    }
    *ppName = PureName(obj);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::DecodeFlags(dbref player, dbref obj, UTF8 **ppStr)
{
    if (nullptr == ppStr)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *ppStr = nullptr;
        return MUX_E_INVALIDARG;
    }
    *ppStr = decode_flags(player, &(db[obj].fs));
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::IsWizard(dbref obj, bool *pResult)
{
    if (nullptr == pResult)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pResult = false;
        return MUX_E_INVALIDARG;
    }
    *pResult = Wizard(obj) ? true : false;
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::IsWizRoy(dbref obj, bool *pResult)
{
    if (nullptr == pResult)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pResult = false;
        return MUX_E_INVALIDARG;
    }
    *pResult = WizRoy(obj) ? true : false;
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::CanIdle(dbref obj, bool *pResult)
{
    if (nullptr == pResult)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pResult = false;
        return MUX_E_INVALIDARG;
    }
    *pResult = Can_Idle(obj) ? true : false;
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::WizardWho(dbref obj, bool *pResult)
{
    if (nullptr == pResult)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pResult = false;
        return MUX_E_INVALIDARG;
    }
    *pResult = Wizard_Who(obj) ? true : false;
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::SeeHidden(dbref obj, bool *pResult)
{
    if (nullptr == pResult)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pResult = false;
        return MUX_E_INVALIDARG;
    }
    *pResult = See_Hidden(obj) ? true : false;
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::AtrAddRaw(dbref obj, int attrnum, const UTF8 *value)
{
    if (!Good_obj(obj))
    {
        return MUX_E_INVALIDARG;
    }
    atr_add_raw(obj, attrnum, value);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::AtrClr(dbref obj, int attrnum)
{
    if (!Good_obj(obj))
    {
        return MUX_E_INVALIDARG;
    }
    atr_clr(obj, attrnum);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::AtrGet(dbref obj, int attrnum, UTF8 *pValue,
    size_t nValueMax, dbref *pOwner, int *pFlags)
{
    if (nullptr == pValue || nullptr == pOwner || nullptr == pFlags)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pValue = '\0';
        return MUX_E_INVALIDARG;
    }
    const UTF8 *p = atr_get("com_bridge", obj, attrnum, pOwner, pFlags);
    mux_strncpy(pValue, p, nValueMax - 1);
    free_lbuf(const_cast<UTF8 *>(p));
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::AtrPGet(dbref obj, int attrnum, UTF8 *pValue,
    size_t nValueMax, dbref *pOwner, int *pFlags)
{
    if (nullptr == pValue || nullptr == pOwner || nullptr == pFlags)
    {
        return MUX_E_INVALIDARG;
    }
    if (!Good_obj(obj))
    {
        *pValue = '\0';
        return MUX_E_INVALIDARG;
    }
    const UTF8 *p = atr_pget(obj, attrnum, pOwner, pFlags);
    mux_strncpy(pValue, p, nValueMax - 1);
    free_lbuf(const_cast<UTF8 *>(p));
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::LookupPlayer(dbref executor, const UTF8 *pName,
    bool bConnected, dbref *pResult)
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
    *pResult = lookup_player(executor, const_cast<UTF8 *>(pName), bConnected);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::FetchConnectionInfoFields(dbref player,
    long anFields[4])
{
    if (!Good_obj(player))
    {
        return MUX_E_INVALIDARG;
    }
    fetch_ConnectionInfoFields(player, anFields);
    return MUX_S_OK;
}

MUX_RESULT CObjectInfo::PutConnectionInfoFields(dbref player,
    long anFields[4], CLinearTimeAbsolute &ltaNow)
{
    if (!Good_obj(player))
    {
        return MUX_E_INVALIDARG;
    }
    put_ConnectionInfoFields(player, anFields, ltaNow);
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
    LBuf buf = LBuf_Src("GetFormatCmd");
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
    LBuf buf = LBuf_Src("EvalExpr");
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
    LBuf topic_buf = LBuf_Src("LookupTopic");
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
    virtual MUX_RESULT DbConvert(const UTF8 *infile, const UTF8 *outfile,
        const UTF8 *basename, bool bCheck, bool bLoad, bool bUnload,
        const UTF8 *comsys_file, const UTF8 *mail_file);
    virtual MUX_RESULT GetConfig(DRIVER_CONFIG *pConfig);
    virtual MUX_RESULT MarkConnected(dbref player);
    virtual MUX_RESULT DumpChildExited(int child_pid);
    virtual MUX_RESULT SetStartTime(const CLinearTimeAbsolute &time);
    virtual MUX_RESULT GetStartTime(CLinearTimeAbsolute *pTime);
    virtual MUX_RESULT SetRestartTime(const CLinearTimeAbsolute &time);
    virtual MUX_RESULT SetRestartCount(unsigned int count);
    virtual MUX_RESULT GetRestartCount(unsigned int *pCount);
    virtual MUX_RESULT SetCpuCountFrom(const CLinearTimeAbsolute &time);
    virtual MUX_RESULT SetRecordPlayers(int count);
    virtual MUX_RESULT GetDoingHdr(UTF8 *buf, size_t bufSize);
    virtual MUX_RESULT SetDoingHdr(const UTF8 *hdr, size_t len);
    virtual MUX_RESULT GetRecordPlayers(int *pCount);
    virtual MUX_RESULT GetBCanRestart(bool *pbCanRestart);
    virtual MUX_RESULT CancelTask(void (*fpTask)(void *, int),
        void *arg_voidptr, int arg_Integer);
    virtual MUX_RESULT DeferImmediateTask(int iPriority,
        void (*fpTask)(void *, int), void *arg_voidptr,
        int arg_Integer);
    virtual MUX_RESULT DeferTask(const CLinearTimeAbsolute &ltWhen,
        int iPriority, void (*fpTask)(void *, int),
        void *arg_voidptr, int arg_Integer);
    virtual MUX_RESULT PrepareForCommand(dbref player);
    virtual MUX_RESULT ProcessCommand(dbref executor, dbref caller,
        dbref enactor, int eval, bool bHasCmdArg, UTF8 *command,
        const UTF8 *cargs[], int ncargs, UTF8 **ppLogBuf);
    virtual MUX_RESULT FinishCommand(void);
    virtual MUX_RESULT HaltQueue(dbref executor, dbref target);
    virtual MUX_RESULT WaitQueue(dbref executor, dbref caller,
        dbref enactor, int eval, bool bTimed,
        const CLinearTimeAbsolute &ltaWhen, dbref sem, int attr,
        UTF8 *command, int ncargs, const UTF8 *cargs[],
        reg_ref *regs[], NamedRegsMap *named);
    virtual MUX_RESULT MoveObject(dbref thing, dbref dest);
    virtual MUX_RESULT WhereRoom(dbref what, dbref *pRoom);
    virtual MUX_RESULT TimeFormat1(int seconds, size_t maxWidth,
        const UTF8 **ppResult);
    virtual MUX_RESULT TimeFormat2(int seconds,
        const UTF8 **ppResult);
    virtual MUX_RESULT GetDbTop(int *pDbTop);
    virtual MUX_RESULT GetInfoTable(const UTF8 ***pppTable);
    virtual MUX_RESULT Report(void);
    virtual MUX_RESULT PresyncDatabaseSigsegv(void);
    virtual MUX_RESULT DoRestart(dbref executor, dbref caller,
        dbref enactor, int eval, int key);
    virtual MUX_RESULT CacheClose(void);

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

// alloc.cpp callback for @list buffers output.
//
static void engine_alloc_notify(dbref player, const UTF8 *text)
{
    notify(player, text);
}

MUX_RESULT CGameEngine::LoadGame(const UTF8 *configFile,
    const UTF8 *inputDb, bool bMinDB)
{
    UNUSED_PARAMETER(inputDb);

    MUX_RESULT mr;

    // Wire up alloc.cpp's output callback so @list buffers works.
    //
    g_alloc_notify_fn = engine_alloc_notify;

    // Initialize engine-owned pools.  These depend on engine types
    // (BOOLEXP, BQUE) that the caller (driver or script binary)
    // cannot see without including engine headers.
    //
    pool_init(POOL_BOOL, sizeof(BOOLEXP));
    pool_init(POOL_QENTRY, sizeof(BQUE));

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

    // Sync function aliases into JIT lookup table now that config
    // (including alias.conf) has been processed.
    engine_api_sync_aliases();

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
        // Create a storage interface for the comsys module to use.
        // This routes all SQLite access through the engine's connection.
        //
        mux_IComsysStorage *pComsysStorage = nullptr;
        mr = mux_CreateInstance(CID_ComsysStorage, nullptr, UseSameProcess,
                                IID_IComsysStorage,
                                reinterpret_cast<void **>(&pComsysStorage));
        if (MUX_SUCCEEDED(mr))
        {
            mr = mudstate.pIComsysControl->Initialize(pComsysStorage);
            pComsysStorage->Release();
            if (MUX_SUCCEEDED(mr))
            {
                STARTLOG(LOG_ALWAYS, "INI", "MOD");
                log_text(T("Comsys module initialized (via engine storage)."));
                ENDLOG;
            }
            else
            {
                STARTLOG(LOG_ALWAYS, "INI", "MOD");
                log_printf(T("Comsys module Initialize failed (mr=%d)."), mr);
                ENDLOG;
            }
        }
        else
        {
            STARTLOG(LOG_ALWAYS, "INI", "MOD");
            log_printf(T("Comsys storage interface creation failed (mr=%d)."), mr);
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
        // Create a storage interface for the mail module to use.
        //
        mux_IMailStorage *pMailStorage = nullptr;
        mr = mux_CreateInstance(CID_MailStorage, nullptr, UseSameProcess,
                                IID_IMailStorage,
                                reinterpret_cast<void **>(&pMailStorage));
        if (MUX_SUCCEEDED(mr))
        {
            mr = mudstate.pIMailControl->Initialize(pMailStorage,
                mudconf.mail_expiration, mudconf.mail_max_per_player);
            pMailStorage->Release();
            if (MUX_SUCCEEDED(mr))
            {
                STARTLOG(LOG_ALWAYS, "INI", "MOD");
                log_text(T("Mail module initialized (via engine storage)."));
                ENDLOG;
            }
            else
            {
                STARTLOG(LOG_ALWAYS, "INI", "MOD");
                log_printf(T("Mail module Initialize failed (mr=%d)."), mr);
                ENDLOG;
            }
        }
        else
        {
            STARTLOG(LOG_ALWAYS, "INI", "MOD");
            log_printf(T("Mail storage interface creation failed (mr=%d)."), mr);
            ENDLOG;
        }
    }

    // Try to discover the Lua scripting module.
    //
    mudstate.pILuaControl = nullptr;
    mr = mux_CreateInstance(CID_LuaMod, nullptr, UseSameProcess,
                            IID_ILuaControl,
                            reinterpret_cast<void **>(&mudstate.pILuaControl));
    if (MUX_SUCCEEDED(mr))
    {
        mudstate.pILuaControl->SetLimits(
            mudconf.lua_instruction_limit,
            mudconf.lua_memory_limit);

        STARTLOG(LOG_ALWAYS, "INI", "MOD");
        log_text(T("Lua scripting module discovered."));
        ENDLOG;
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

    // PlayerSession interface is now created by the driver after LoadGame.

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
                mux_fclose(f);
            }
        }
    }

    return MUX_S_OK;
}

MUX_RESULT CGameEngine::Startup(void)
{
    // Set up the COM bridge so engine-side code can reach the driver's
    // connection manager through free-function stubs.
    //
    conn_bridge_init();

    Guest.StartUp();

    // Do a consistency check and set up the freelist.
    //
    do_dbck(NOTHING, NOTHING, NOTHING, 0, 0);

    route_init();
    walk_init();

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

#if defined(TINYMUX_JIT)
extern void dbt_compile_cleanup(void);
#endif

MUX_RESULT CGameEngine::Shutdown(void)
{
#if defined(TINYMUX_JIT)
    dbt_compile_cleanup();
#endif
    walk_shutdown();
    route_shutdown();
    conn_bridge_final();
    local_shutdown();

    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->shutdown();
        p = p->pNext;
    }
    return MUX_S_OK;
}

static void safe_copy_str(UTF8 *dst, size_t dstSize, const UTF8 *src)
{
    if (nullptr != src)
    {
        mux_strncpy(dst, src, dstSize - 1);
    }
    else
    {
        dst[0] = '\0';
    }
}

MUX_RESULT CGameEngine::GetConfig(DRIVER_CONFIG *pConfig)
{
    if (nullptr == pConfig)
    {
        return MUX_E_INVALIDARG;
    }

    memset(pConfig, 0, sizeof(DRIVER_CONFIG));

    // Ports and networking.
    //
    pConfig->nPorts = static_cast<int>(mudconf.ports.size());
    if (pConfig->nPorts > DRIVER_CONFIG_MAX_PORTS)
    {
        pConfig->nPorts = DRIVER_CONFIG_MAX_PORTS;
    }
    for (int i = 0; i < pConfig->nPorts; i++)
    {
        pConfig->ports[i] = mudconf.ports[i];
    }

#if defined(UNIX_SSL) || defined(_WIN32)
    pConfig->nSslPorts = static_cast<int>(mudconf.sslPorts.size());
    if (pConfig->nSslPorts > DRIVER_CONFIG_MAX_PORTS)
    {
        pConfig->nSslPorts = DRIVER_CONFIG_MAX_PORTS;
    }
    for (int i = 0; i < pConfig->nSslPorts; i++)
    {
        pConfig->sslPorts[i] = mudconf.sslPorts[i];
    }
    mux_strncpy(pConfig->ssl_certificate_file, mudconf.ssl_certificate_file, sizeof(pConfig->ssl_certificate_file) - 1);
    mux_strncpy(pConfig->ssl_certificate_key, mudconf.ssl_certificate_key, sizeof(pConfig->ssl_certificate_key) - 1);
    mux_strncpy(pConfig->ssl_certificate_password, mudconf.ssl_certificate_password, sizeof(pConfig->ssl_certificate_password) - 1);
#else
    pConfig->nSslPorts = 0;
#endif

    safe_copy_str(pConfig->ip_address, sizeof(pConfig->ip_address), mudconf.ip_address);
    pConfig->use_hostname       = mudconf.use_hostname;
    pConfig->retry_limit        = mudconf.retry_limit;
    pConfig->idle_timeout       = mudconf.idle_timeout;
    pConfig->conn_timeout       = mudconf.conn_timeout;
    pConfig->cmd_quota_max      = mudconf.cmd_quota_max;
    pConfig->output_limit       = mudconf.output_limit;
    pConfig->default_charset    = mudconf.default_charset;
    pConfig->max_players        = mudconf.max_players;
    pConfig->control_flags      = mudconf.control_flags;

    // Guest configuration.
    //
    mux_strncpy(pConfig->guest_prefix, mudconf.guest_prefix, sizeof(pConfig->guest_prefix) - 1);
    pConfig->number_guests      = mudconf.number_guests;
    pConfig->guest_char         = mudconf.guest_char;

    // Game identity.
    //
    mux_strncpy(pConfig->mud_name, mudconf.mud_name, sizeof(pConfig->mud_name) - 1);

    // File paths (pointer fields copied into fixed buffers).
    //
    safe_copy_str(pConfig->pid_file, sizeof(pConfig->pid_file), mudconf.pid_file);
    safe_copy_str(pConfig->log_dir, sizeof(pConfig->log_dir), mudconf.log_dir);
    safe_copy_str(pConfig->config_file, sizeof(pConfig->config_file), mudconf.config_file);

    // Messages.
    //
    mux_strncpy(pConfig->crash_msg, mudconf.crash_msg, sizeof(pConfig->crash_msg) - 1);
    mux_strncpy(pConfig->downmotd_msg, mudconf.downmotd_msg, sizeof(pConfig->downmotd_msg) - 1);
    mux_strncpy(pConfig->fullmotd_msg, mudconf.fullmotd_msg, sizeof(pConfig->fullmotd_msg) - 1);
    mux_strncpy(pConfig->pueblo_msg, mudconf.pueblo_msg, sizeof(pConfig->pueblo_msg) - 1);

    // Timing (raw 100ns ticks).
    //
    pConfig->max_cmdsecs        = mudconf.max_cmdsecs.Return100ns();
    pConfig->rpt_cmdsecs        = mudconf.rpt_cmdsecs.Return100ns();
    pConfig->timeslice          = mudconf.timeslice.Return100ns();
    pConfig->start_time_utc     = mudstate.start_time.ReturnSeconds();

    // Behavior flags and limits.
    //
    pConfig->sig_action         = mudconf.sig_action;
    pConfig->fork_dump          = mudconf.fork_dump;
    pConfig->name_spaces        = mudconf.name_spaces;
    pConfig->idle_wiz_dark      = mudconf.idle_wiz_dark;
    pConfig->reset_players      = mudconf.reset_players;
    pConfig->site_chars         = mudconf.site_chars;
    pConfig->start_room         = mudconf.start_room;

    // SQL.
    //
    mux_strncpy(pConfig->sql_server, mudconf.sql_server, sizeof(pConfig->sql_server) - 1);
    mux_strncpy(pConfig->sql_user, mudconf.sql_user, sizeof(pConfig->sql_user) - 1);
    mux_strncpy(pConfig->sql_password, mudconf.sql_password, sizeof(pConfig->sql_password) - 1);
    mux_strncpy(pConfig->sql_database, mudconf.sql_database, sizeof(pConfig->sql_database) - 1);

    // Mail relay.
    //
    mux_strncpy(pConfig->mail_server, mudconf.mail_server, sizeof(pConfig->mail_server) - 1);
    mux_strncpy(pConfig->mail_sendaddr, mudconf.mail_sendaddr, sizeof(pConfig->mail_sendaddr) - 1);
    mux_strncpy(pConfig->mail_sendname, mudconf.mail_sendname, sizeof(pConfig->mail_sendname) - 1);
    mux_strncpy(pConfig->mail_ehlo, mudconf.mail_ehlo, sizeof(pConfig->mail_ehlo) - 1);

    return MUX_S_OK;
}

MUX_RESULT CGameEngine::MarkConnected(dbref player)
{
    if (  Good_obj(player)
       && isPlayer(player))
    {
        s_Connected(player);
        return MUX_S_OK;
    }
    return MUX_E_INVALIDARG;
}

MUX_RESULT CGameEngine::DumpChildExited(int child_pid)
{
#if defined(HAVE_WORKING_FORK)
    if (!mudstate.dumping)
    {
        return MUX_S_FALSE;
    }

    mudstate.dumped = child_pid;
    if (mudstate.dumper == mudstate.dumped)
    {
        // Normal completion — fork() returned before SIGCHLD.
        //
        mudstate.dumper = 0;
        mudstate.dumped = 0;
    }
    else
    {
        // SIGCHLD arrived before fork() returned the PID.
        // dumped is set; fork_and_dump will notice on return.
        //
    }
    mudstate.dumping = false;

    local_dump_complete_signal();
    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->dump_complete_signal();
        p = p->pNext;
    }
    return MUX_S_OK;
#else
    UNUSED_PARAMETER(child_pid);
    return MUX_S_FALSE;
#endif
}

MUX_RESULT CGameEngine::SetStartTime(const CLinearTimeAbsolute &time)
{
    mudstate.start_time = time;
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::GetStartTime(CLinearTimeAbsolute *pTime)
{
    if (nullptr == pTime)
    {
        return MUX_E_INVALIDARG;
    }
    *pTime = mudstate.start_time;
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::SetRestartTime(const CLinearTimeAbsolute &time)
{
    mudstate.restart_time = time;
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::SetRestartCount(unsigned int count)
{
    mudstate.restart_count = count;
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::GetRestartCount(unsigned int *pCount)
{
    if (nullptr == pCount)
    {
        return MUX_E_INVALIDARG;
    }
    *pCount = mudstate.restart_count;
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::SetCpuCountFrom(const CLinearTimeAbsolute &time)
{
    mudstate.cpu_count_from = time;
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::SetRecordPlayers(int count)
{
    mudstate.record_players = count;
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::GetDoingHdr(UTF8 *buf, size_t bufSize)
{
    if (nullptr == buf || 0 == bufSize)
    {
        return MUX_E_INVALIDARG;
    }
    mux_strncpy(buf, mudstate.doing_hdr, bufSize - 1);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::SetDoingHdr(const UTF8 *hdr, size_t len)
{
    if (nullptr == hdr)
    {
        return MUX_E_INVALIDARG;
    }
    size_t nCopy = len;
    if (nCopy >= sizeof(mudstate.doing_hdr))
    {
        nCopy = sizeof(mudstate.doing_hdr) - 1;
    }
    memcpy(mudstate.doing_hdr, hdr, nCopy);
    mudstate.doing_hdr[nCopy] = '\0';
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::GetRecordPlayers(int *pCount)
{
    if (nullptr == pCount)
    {
        return MUX_E_INVALIDARG;
    }
    *pCount = mudstate.record_players;
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::GetBCanRestart(bool *pbCanRestart)
{
    if (nullptr == pbCanRestart)
    {
        return MUX_E_INVALIDARG;
    }
    *pbCanRestart = mudstate.bCanRestart;
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::CancelTask(void (*fpTask)(void *, int),
    void *arg_voidptr, int arg_Integer)
{
    scheduler.CancelTask(fpTask, arg_voidptr, arg_Integer);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::DeferImmediateTask(int iPriority,
    void (*fpTask)(void *, int), void *arg_voidptr, int arg_Integer)
{
    scheduler.DeferImmediateTask(iPriority, fpTask, arg_voidptr, arg_Integer);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::DeferTask(const CLinearTimeAbsolute &ltWhen,
    int iPriority, void (*fpTask)(void *, int), void *arg_voidptr,
    int arg_Integer)
{
    scheduler.DeferTask(ltWhen, iPriority, fpTask, arg_voidptr, arg_Integer);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::PrepareForCommand(dbref player)
{
    mudstate.curr_executor = player;
    mudstate.curr_enactor = player;
    for (int i = 0; i < MAX_GLOBAL_REGS; i++)
    {
        if (mudstate.global_regs[i])
        {
            RegRelease(mudstate.global_regs[i]);
            mudstate.global_regs[i] = nullptr;
        }
    }
    NamedRegsClear(mudstate.named_regs);

#if defined(STUB_SLAVE)
    mudstate.iRow = RS_TOP;
    if (nullptr != mudstate.pResultsSet)
    {
        mudstate.pResultsSet->Release();
        mudstate.pResultsSet = nullptr;
    }
#endif // STUB_SLAVE

    return MUX_S_OK;
}

MUX_RESULT CGameEngine::ProcessCommand(dbref executor, dbref caller,
    dbref enactor, int eval, bool bHasCmdArg, UTF8 *command,
    const UTF8 *cargs[], int ncargs, UTF8 **ppLogBuf)
{
    if (nullptr == ppLogBuf)
    {
        return MUX_E_INVALIDARG;
    }
    *ppLogBuf = process_command(executor, caller, enactor, eval, bHasCmdArg,
        command, cargs, ncargs);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::FinishCommand(void)
{
    mudstate.curr_cmd = T("");
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::HaltQueue(dbref executor, dbref target)
{
    halt_que(executor, target);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::WaitQueue(dbref executor, dbref caller,
    dbref enactor, int eval, bool bTimed,
    const CLinearTimeAbsolute &ltaWhen, dbref sem, int attr,
    UTF8 *command, int ncargs, const UTF8 *cargs[],
    reg_ref *regs[], NamedRegsMap *named)
{
    wait_que(executor, caller, enactor, eval, bTimed, ltaWhen, sem, attr,
        command, ncargs, cargs, regs, named);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::MoveObject(dbref thing, dbref dest)
{
    move_object(thing, dest);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::WhereRoom(dbref what, dbref *pRoom)
{
    if (nullptr == pRoom)
    {
        return MUX_E_INVALIDARG;
    }
    *pRoom = where_room(what);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::TimeFormat1(int seconds, size_t maxWidth,
    const UTF8 **ppResult)
{
    if (nullptr == ppResult)
    {
        return MUX_E_INVALIDARG;
    }
    *ppResult = time_format_1(seconds, maxWidth);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::TimeFormat2(int seconds,
    const UTF8 **ppResult)
{
    if (nullptr == ppResult)
    {
        return MUX_E_INVALIDARG;
    }
    *ppResult = time_format_2(seconds);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::GetDbTop(int *pDbTop)
{
    if (nullptr == pDbTop)
    {
        return MUX_E_INVALIDARG;
    }
    *pDbTop = mudstate.db_top;
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::GetInfoTable(const UTF8 ***pppTable)
{
    if (nullptr == pppTable)
    {
        return MUX_E_INVALIDARG;
    }
    *pppTable = local_get_info_table();
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::Report(void)
{
    report();
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::PresyncDatabaseSigsegv(void)
{
    local_presync_database_sigsegv();

    // Notify all registered module sinks.
    //
    ServerEventsSinkNode *p = g_pServerEventsSinkListHead;
    while (nullptr != p)
    {
        p->pSink->presync_database_sigsegv();
        p = p->pNext;
    }
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::DoRestart(dbref executor, dbref caller,
    dbref enactor, int eval, int key)
{
    do_restart(executor, caller, enactor, eval, key);
    return MUX_S_OK;
}

MUX_RESULT CGameEngine::CacheClose(void)
{
    cache_close();
    return MUX_S_OK;
}

static void dbconvert_info(int fmt, int flags, int ver)
{
    const UTF8 *cp;

    if (fmt == F_MUX)
    {
        cp = T("MUX");
    }
    else
    {
        cp = T("*unknown*");
    }
    mux_fprintf(stderr, T("%s version %d:"), cp, ver);
    if (  ver < MIN_SUPPORTED_VERSION
       || MAX_SUPPORTED_VERSION < ver)
    {
        mux_fprintf(stderr, T(" Unsupported version"));
        exit(1);
    }
    else if (  (  (  1 == ver
                  || 2 == ver)
               && (flags & MANDFLAGS_V2) != MANDFLAGS_V2)
            || (  3 == ver
               && (flags & MANDFLAGS_V3) != MANDFLAGS_V3)
            || (  4 == ver
               && (flags & MANDFLAGS_V4) != MANDFLAGS_V4)
            || (  5 == ver
               && (flags & MANDFLAGS_V5) != MANDFLAGS_V5))
    {
        mux_fprintf(stderr, T(" Unsupported flags"));
        exit(1);
    }
    if (flags & V_DATABASE)
        mux_fprintf(stderr, T(" Database"));
    if (flags & V_ATRNAME)
        mux_fprintf(stderr, T(" AtrName"));
    if (flags & V_ATRKEY)
        mux_fprintf(stderr, T(" AtrKey"));
    if (flags & V_ATRMONEY)
        mux_fprintf(stderr, T(" AtrMoney"));
    mux_fprintf(stderr, T(ENDLINE));
}

MUX_RESULT CGameEngine::DbConvert(const UTF8 *infile, const UTF8 *outfile,
    const UTF8 *basename, bool bCheck, bool bLoad, bool bUnload,
    const UTF8 *comsys_file, const UTF8 *mail_file)
{
    int setflags, clrflags, ver;
    int db_ver, db_format, db_flags;

    SeedRandomNumberGenerator();

    pool_init(POOL_LBUF, LBUF_SIZE);
    pool_init(POOL_MBUF, MBUF_SIZE);
    pool_init(POOL_SBUF, SBUF_SIZE);
    pool_init(POOL_BOOL, sizeof(struct boolexp));

    pcache_init();
    cf_init();

    // Decide what conversions to do and how to format the output file.
    //
    setflags = clrflags = ver = 0;
    bool do_redirect = false;

    bool do_write = true;
    if (bCheck || bLoad)
    {
        do_write = false;
    }
    if (bLoad)
    {
        clrflags = 0xffffffff;
        setflags = OUTPUT_FLAGS;
        ver = OUTPUT_VERSION;
        do_redirect = true;
    }
    else if (bUnload)
    {
        clrflags = 0xffffffff;
        setflags = UNLOAD_FLAGS;
        ver = UNLOAD_VERSION;
    }

    // Open the database
    //
    init_attrtab();

    int cc = init_dbfile(basename);
    if (cc == HF_OPEN_STATUS_ERROR)
    {
        mux_fprintf(stderr, T("Can\xE2\x80\x99t open SQLite database.\n"));
        return MUX_E_FAIL;
    }
    else if (cc == HF_OPEN_STATUS_OLD)
    {
        if (setflags == OUTPUT_FLAGS)
        {
            mux_fprintf(stderr, T("Would overwrite existing SQLite database.\n"));
            CLOSE;
            return MUX_E_FAIL;
        }
    }
    else if (cc == HF_OPEN_STATUS_NEW)
    {
        if (setflags == UNLOAD_FLAGS)
        {
            mux_fprintf(stderr, T("SQLite database is empty.\n"));
            CLOSE;
            return MUX_E_FAIL;
        }
    }

    bool bLoadedFromSQLite = false;
    if (nullptr == infile && HF_OPEN_STATUS_OLD == cc)
    {
        int sqlite_load_rc = sqlite_load_game();
        if (sqlite_load_rc < 0)
        {
            mux_fprintf(stderr, T("Input: SQLite database load failed.\n"));
            return MUX_E_FAIL;
        }
        bLoadedFromSQLite = (sqlite_load_rc > 0);
    }

    if (bLoadedFromSQLite)
    {
        mux_fprintf(stderr, T("Input: SQLite database\n"));
        db_format = F_MUX;
        db_ver = OUTPUT_VERSION;
        db_flags = OUTPUT_FLAGS;
    }
    else
    {
        if (nullptr == infile)
        {
            mux_fprintf(stderr, T("No input flatfile provided and SQLite has no loadable game data.\n"));
            return MUX_E_FAIL;
        }
        FILE *fpIn;
        if (!mux_fopen(&fpIn, infile, T("rb")))
        {
            return MUX_E_FAIL;
        }

        setvbuf(fpIn, nullptr, _IOFBF, 16384);
        CSQLiteDB &sqldb = g_pSQLiteBackend->GetDB();
        if (!sqldb.Begin() || !sqldb.ClearAttributes())
        {
            sqldb.Rollback();
            mux_fprintf(stderr, T("SQLite attribute clear failed before flatfile import.\n"));
            mux_fclose(fpIn);
            return MUX_E_FAIL;
        }
        mudstate.bSQLiteLoading = true;
        if (db_read(fpIn, &db_format, &db_ver, &db_flags) < 0)
        {
            mudstate.bSQLiteLoading = false;
            sqldb.Rollback();
            mux_fclose(fpIn);
            return MUX_E_FAIL;
        }
        mudstate.bSQLiteLoading = false;
        if (!sqldb.Commit())
        {
            sqldb.Rollback();
            mux_fprintf(stderr, T("SQLite attribute import commit failed.\n"));
            mux_fclose(fpIn);
            return MUX_E_FAIL;
        }
        if (!sqlite_sync_runtime())
        {
            if (!clear_sqlite_after_sync_failure(sqldb))
            {
                mux_fprintf(stderr, T("SQLite cleanup failed after sync failure.\n"));
            }
            mux_fprintf(stderr, T("SQLite metadata sync failed.\n"));
            return MUX_E_FAIL;
        }
        mux_fprintf(stderr, T("Input: "));
        dbconvert_info(db_format, db_flags, db_ver);

        if (bCheck)
        {
            do_dbck(NOTHING, NOTHING, NOTHING, 0, DBCK_FULL);
        }
        mux_fclose(fpIn);
    }

    // Import comsys from flatfile into SQLite.
    //
    if (bLoad && comsys_file)
    {
        load_comsys(const_cast<UTF8 *>(comsys_file));
        if (!sqlite_sync_comsys())
        {
            mux_fprintf(stderr, T("Import comsys into SQLite failed.\n"));
            return MUX_E_FAIL;
        }
        mux_fprintf(stderr, T("Imported comsys into SQLite.\n"));
    }

    // Import mail from flatfile into SQLite.
    //
    if (bLoad && mail_file)
    {
        FILE *fpMail;
        if (mux_fopen(&fpMail, mail_file, T("rb")))
        {
            setvbuf(fpMail, nullptr, _IOFBF, 16384);
            load_mail(fpMail);
            mux_fclose(fpMail);
            if (!sqlite_sync_mail())
            {
                mux_fprintf(stderr, T("Import mail into SQLite failed.\n"));
                return MUX_E_FAIL;
            }
            mux_fprintf(stderr, T("Imported mail into SQLite.\n"));
        }
    }

    // Export comsys from SQLite to flatfile.
    //
    if (bUnload && comsys_file)
    {
        int sqlite_comsys_rc = sqlite_load_comsys();
        if (sqlite_comsys_rc > 0)
        {
            save_comsys(const_cast<UTF8 *>(comsys_file));
            mux_fprintf(stderr, T("Exported comsys from SQLite.\n"));
        }
        else if (sqlite_comsys_rc < 0)
        {
            mux_fprintf(stderr, T("Export comsys from SQLite failed.\n"));
            return MUX_E_FAIL;
        }
    }

    // Export mail from SQLite to flatfile.
    //
    if (bUnload && mail_file)
    {
        int sqlite_mail_rc = sqlite_load_mail();
        if (sqlite_mail_rc > 0)
        {
            FILE *fpMail;
            if (mux_fopen(&fpMail, mail_file, T("wb")))
            {
                dump_mail(fpMail);
                mux_fclose(fpMail);
                mux_fprintf(stderr, T("Exported mail from SQLite.\n"));
            }
        }
        else if (sqlite_mail_rc < 0)
        {
            mux_fprintf(stderr, T("Export mail from SQLite failed.\n"));
            return MUX_E_FAIL;
        }
    }

    if (do_write)
    {
        FILE *fpOut;
        if (!mux_fopen(&fpOut, outfile, T("wb")))
        {
            return MUX_E_FAIL;
        }

        db_flags = (db_flags & ~clrflags) | setflags;
        if (db_format != F_MUX)
        {
            db_ver = 3;
        }
        if (ver != 0)
        {
            db_ver = ver;
        }
        mux_fprintf(stderr, T("Output: "));
        dbconvert_info(F_MUX, db_flags, db_ver);
        setvbuf(fpOut, nullptr, _IOFBF, 16384);
        db_write(fpOut, F_MUX, db_ver | db_flags);
        mux_fclose(fpOut);
    }
    CLOSE;
#ifdef SELFCHECK
    db_free();
#endif
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
        const UTF8 *username, const UTF8 *ipaddr, int *pTimeout,
        int64_t *pConnlogId);
    virtual MUX_RESULT AnnounceDisconnect(dbref player, int numConnections,
        bool isSuspect, bool wasAutoDark, const UTF8 *reason,
        int64_t connlogId);
    virtual MUX_RESULT FcacheSend(DESC *d, int num);
    virtual MUX_RESULT FcacheRawSend(SOCKET fd, int num);
    virtual MUX_RESULT CreateGuest(DESC *d, const UTF8 **ppName);
    virtual MUX_RESULT CheckGuest(dbref player, bool *pResult);

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
    const UTF8 *username, const UTF8 *ipaddr, int *pTimeout,
    int64_t *pConnlogId)
{
    // Preload attributes for the player and nearby rooms.
    //
    cache_preload_nearby(player, mudconf.cache_preload_depth);

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

    // Log connection to connlog table.
    //
    if (nullptr != pConnlogId)
    {
        CLinearTimeAbsolute ltaUtc;
        ltaUtc.GetUTC();
        int64_t utcSeconds = ltaUtc.ReturnSeconds();
        *pConnlogId = g_pSQLiteBackend->GetDB().ConnlogInsert(
            player, utcSeconds, host, ipaddr);
    }
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
    const UTF8 *reason, int64_t connlogId)
{
    // Update connlog row with disconnect time and reason.
    //
    if (0 != connlogId)
    {
        CLinearTimeAbsolute ltaUtc;
        ltaUtc.GetUTC();
        int64_t utcSeconds = ltaUtc.ReturnSeconds();
        g_pSQLiteBackend->GetDB().ConnlogUpdate(connlogId, utcSeconds, reason);
    }

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

MUX_RESULT CPlayerSession::FcacheSend(DESC *d, int num)
{
    if (nullptr == d)
    {
        return MUX_E_INVALIDARG;
    }
    fcache_dump(d, num);
    return MUX_S_OK;
}

MUX_RESULT CPlayerSession::FcacheRawSend(SOCKET fd, int num)
{
    fcache_rawdump(fd, num);
    return MUX_S_OK;
}

MUX_RESULT CPlayerSession::CreateGuest(DESC *d, const UTF8 **ppName)
{
    if (nullptr == ppName)
    {
        return MUX_E_INVALIDARG;
    }
    *ppName = Guest.Create(d);
    if (nullptr == *ppName)
    {
        return MUX_E_FAIL;
    }
    return MUX_S_OK;
}

MUX_RESULT CPlayerSession::CheckGuest(dbref player, bool *pResult)
{
    if (nullptr == pResult)
    {
        return MUX_E_INVALIDARG;
    }
    *pResult = Guest.CheckGuest(player);
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
// CComsysStorage — engine-provided SQLite access for comsys_mod.so.
// ===========================================================================

class CComsysStorage : public mux_IComsysStorage
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT LoadAllChannels(PFN_CHANNEL_CB pfn, void *context);
    virtual MUX_RESULT LoadAllChannelUsers(PFN_CHANNEL_USER_CB pfn, void *context);
    virtual MUX_RESULT LoadAllPlayerChannels(PFN_PLAYER_CHANNEL_CB pfn, void *context);
    virtual MUX_RESULT SyncChannel(const UTF8 *name, const UTF8 *header,
        int type, int temp1, int temp2, int charge, int charge_who,
        int amount_col, int num_messages, int chan_obj);
    virtual MUX_RESULT SyncChannelUser(const UTF8 *channel_name, int who,
        bool is_on, bool comtitle_status, bool gag_join_leave,
        const UTF8 *title);
    virtual MUX_RESULT SyncPlayerChannel(int who, const UTF8 *alias,
        const UTF8 *channel_name);
    virtual MUX_RESULT DeleteChannel(const UTF8 *name);
    virtual MUX_RESULT DeleteChannelUser(const UTF8 *channel_name, int who);
    virtual MUX_RESULT DeletePlayerChannel(int who, const UTF8 *alias);
    virtual MUX_RESULT DeleteAllPlayerChannels(int who);
    virtual MUX_RESULT ClearComsysTables(void);

    CComsysStorage(void);
    virtual ~CComsysStorage();

private:
    uint32_t m_cRef;
};

CComsysStorage::CComsysStorage(void) : m_cRef(1) {}
CComsysStorage::~CComsysStorage() {}

MUX_RESULT CComsysStorage::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid || IID_IComsysStorage == iid)
    {
        *ppv = static_cast<mux_IComsysStorage *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    AddRef();
    return MUX_S_OK;
}

uint32_t CComsysStorage::AddRef(void)  { m_cRef++; return m_cRef; }
uint32_t CComsysStorage::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CComsysStorage::LoadAllChannels(PFN_CHANNEL_CB pfn, void *context)
{
    if (nullptr == pfn) return MUX_E_INVALIDARG;
    bool ok = g_pSQLiteBackend->GetDB().LoadAllChannels(
        [pfn, context](const UTF8 *name, const UTF8 *header,
            int type, int temp1, int temp2, int charge, int charge_who,
            int amount_col, int num_messages, int chan_obj)
        {
            pfn(context, name, header, type, temp1, temp2, charge,
                charge_who, amount_col, num_messages, chan_obj);
        });
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CComsysStorage::LoadAllChannelUsers(PFN_CHANNEL_USER_CB pfn, void *context)
{
    if (nullptr == pfn) return MUX_E_INVALIDARG;
    bool ok = g_pSQLiteBackend->GetDB().LoadAllChannelUsers(
        [pfn, context](const UTF8 *channel_name, int who,
            bool is_on, bool comtitle_status, bool gag_join_leave,
            const UTF8 *title)
        {
            pfn(context, channel_name, who, is_on, comtitle_status,
                gag_join_leave, title);
        });
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CComsysStorage::LoadAllPlayerChannels(PFN_PLAYER_CHANNEL_CB pfn, void *context)
{
    if (nullptr == pfn) return MUX_E_INVALIDARG;
    bool ok = g_pSQLiteBackend->GetDB().LoadAllPlayerChannels(
        [pfn, context](int who, const UTF8 *alias,
            const UTF8 *channel_name)
        {
            pfn(context, who, alias, channel_name);
        });
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CComsysStorage::SyncChannel(const UTF8 *name, const UTF8 *header,
    int type, int temp1, int temp2, int charge, int charge_who,
    int amount_col, int num_messages, int chan_obj)
{
    bool ok = g_pSQLiteBackend->GetDB().SyncChannel(name, header, type,
        temp1, temp2, charge, charge_who, amount_col, num_messages, chan_obj);
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CComsysStorage::SyncChannelUser(const UTF8 *channel_name, int who,
    bool is_on, bool comtitle_status, bool gag_join_leave, const UTF8 *title)
{
    bool ok = g_pSQLiteBackend->GetDB().SyncChannelUser(channel_name, who,
        is_on, comtitle_status, gag_join_leave, title);
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CComsysStorage::SyncPlayerChannel(int who, const UTF8 *alias,
    const UTF8 *channel_name)
{
    bool ok = g_pSQLiteBackend->GetDB().SyncPlayerChannel(who, alias,
        channel_name);
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CComsysStorage::DeleteChannel(const UTF8 *name)
{
    bool ok = g_pSQLiteBackend->GetDB().DeleteChannel(name);
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CComsysStorage::DeleteChannelUser(const UTF8 *channel_name, int who)
{
    bool ok = g_pSQLiteBackend->GetDB().DeleteChannelUser(channel_name, who);
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CComsysStorage::DeletePlayerChannel(int who, const UTF8 *alias)
{
    bool ok = g_pSQLiteBackend->GetDB().DeletePlayerChannel(who, alias);
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CComsysStorage::DeleteAllPlayerChannels(int who)
{
    bool ok = g_pSQLiteBackend->GetDB().DeleteAllPlayerChannels(who);
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CComsysStorage::ClearComsysTables(void)
{
    bool ok = g_pSQLiteBackend->GetDB().ClearComsysTables();
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

// CComsysStorageFactory
//
CComsysStorageFactory::CComsysStorageFactory(void) : m_cRef(1) {}
CComsysStorageFactory::~CComsysStorageFactory() {}

MUX_RESULT CComsysStorageFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid || mux_IID_IClassFactory == iid)
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

uint32_t CComsysStorageFactory::AddRef(void)  { m_cRef++; return m_cRef; }
uint32_t CComsysStorageFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef) { delete this; return 0; }
    return m_cRef;
}

MUX_RESULT CComsysStorageFactory::CreateInstance(mux_IUnknown *pUnknownOuter,
    MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);
    CComsysStorage *p = nullptr;
    try { p = new CComsysStorage; } catch (...) { ; }
    if (nullptr == p) return MUX_E_OUTOFMEMORY;
    MUX_RESULT mr = p->QueryInterface(iid, ppv);
    p->Release();
    return mr;
}

MUX_RESULT CComsysStorageFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// ===========================================================================
// CMailStorage — engine-provided SQLite access for mail_mod.so.
// ===========================================================================

class CMailStorage : public mux_IMailStorage
{
public:
    virtual MUX_RESULT QueryInterface(MUX_IID iid, void **ppv);
    virtual uint32_t   AddRef(void);
    virtual uint32_t   Release(void);

    virtual MUX_RESULT LoadAllMailHeaders(PFN_MAIL_HEADER_CB pfn, void *context);
    virtual MUX_RESULT LoadAllMailBodies(PFN_MAIL_BODY_CB pfn, void *context);
    virtual MUX_RESULT LoadAllMailAliases(PFN_MAIL_ALIAS_CB pfn, void *context);
    virtual MUX_RESULT GetMeta(const UTF8 *key, int *pValue);
    virtual MUX_RESULT InsertMailHeader(int to_player, int from_player,
        int body_number, const UTF8 *tolist, const UTF8 *time_str,
        const UTF8 *subject, int read_flags, int64_t *pRowid);
    virtual MUX_RESULT UpdateMailReadFlags(int64_t rowid, int read_flags);
    virtual MUX_RESULT DeleteMailHeader(int64_t rowid);
    virtual MUX_RESULT DeleteAllMailHeaders(int to_player);
    virtual MUX_RESULT SyncMailBody(int number, const UTF8 *message);
    virtual MUX_RESULT DeleteMailBody(int number);
    virtual MUX_RESULT SyncMailAlias(int owner, const UTF8 *name,
        const UTF8 *desc, int desc_width, const UTF8 *members);
    virtual MUX_RESULT ClearMailAliases(void);
    virtual MUX_RESULT ClearMailTables(void);

    CMailStorage(void);
    virtual ~CMailStorage();

private:
    uint32_t m_cRef;
};

CMailStorage::CMailStorage(void) : m_cRef(1) {}
CMailStorage::~CMailStorage() {}

MUX_RESULT CMailStorage::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid || IID_IMailStorage == iid)
    {
        *ppv = static_cast<mux_IMailStorage *>(this);
    }
    else
    {
        *ppv = nullptr;
        return MUX_E_NOINTERFACE;
    }
    AddRef();
    return MUX_S_OK;
}

uint32_t CMailStorage::AddRef(void)  { m_cRef++; return m_cRef; }
uint32_t CMailStorage::Release(void)
{
    m_cRef--;
    if (0 == m_cRef) { delete this; return 0; }
    return m_cRef;
}

MUX_RESULT CMailStorage::LoadAllMailHeaders(PFN_MAIL_HEADER_CB pfn, void *context)
{
    if (nullptr == pfn) return MUX_E_INVALIDARG;
    bool ok = g_pSQLiteBackend->GetDB().LoadAllMailHeaders(
        [pfn, context](int64_t rowid, int to_player, int from_player,
            int body_number, const UTF8 *tolist, const UTF8 *time_str,
            const UTF8 *subject, int read_flags)
        {
            pfn(context, rowid, to_player, from_player, body_number,
                tolist, time_str, subject, read_flags);
        });
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CMailStorage::LoadAllMailBodies(PFN_MAIL_BODY_CB pfn, void *context)
{
    if (nullptr == pfn) return MUX_E_INVALIDARG;
    bool ok = g_pSQLiteBackend->GetDB().LoadAllMailBodies(
        [pfn, context](int number, const UTF8 *message)
        {
            pfn(context, number, message);
        });
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CMailStorage::LoadAllMailAliases(PFN_MAIL_ALIAS_CB pfn, void *context)
{
    if (nullptr == pfn) return MUX_E_INVALIDARG;
    bool ok = g_pSQLiteBackend->GetDB().LoadAllMailAliases(
        [pfn, context](int owner, const UTF8 *name,
            const UTF8 *desc, int desc_width, const UTF8 *members)
        {
            pfn(context, owner, name, desc, desc_width, members);
        });
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CMailStorage::GetMeta(const UTF8 *key, int *pValue)
{
    if (nullptr == key || nullptr == pValue) return MUX_E_INVALIDARG;
    bool ok = g_pSQLiteBackend->GetDB().GetMeta(
        reinterpret_cast<const char *>(key), pValue);
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CMailStorage::InsertMailHeader(int to_player, int from_player,
    int body_number, const UTF8 *tolist, const UTF8 *time_str,
    const UTF8 *subject, int read_flags, int64_t *pRowid)
{
    if (nullptr == pRowid) return MUX_E_INVALIDARG;
    int64_t id = g_pSQLiteBackend->GetDB().InsertMailHeaderReturningId(
        to_player, from_player, body_number, tolist, time_str,
        subject, read_flags);
    if (id < 0) return MUX_E_FAIL;
    *pRowid = id;
    return MUX_S_OK;
}

MUX_RESULT CMailStorage::UpdateMailReadFlags(int64_t rowid, int read_flags)
{
    bool ok = g_pSQLiteBackend->GetDB().UpdateMailReadFlags(rowid, read_flags);
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CMailStorage::DeleteMailHeader(int64_t rowid)
{
    bool ok = g_pSQLiteBackend->GetDB().DeleteMailHeader(rowid);
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CMailStorage::DeleteAllMailHeaders(int to_player)
{
    bool ok = g_pSQLiteBackend->GetDB().DeleteAllMailHeaders(to_player);
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CMailStorage::SyncMailBody(int number, const UTF8 *message)
{
    bool ok = g_pSQLiteBackend->GetDB().SyncMailBody(number, message);
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CMailStorage::DeleteMailBody(int number)
{
    bool ok = g_pSQLiteBackend->GetDB().DeleteMailBody(number);
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CMailStorage::SyncMailAlias(int owner, const UTF8 *name,
    const UTF8 *desc, int desc_width, const UTF8 *members)
{
    bool ok = g_pSQLiteBackend->GetDB().SyncMailAlias(owner, name, desc,
        desc_width, members);
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CMailStorage::ClearMailAliases(void)
{
    bool ok = g_pSQLiteBackend->GetDB().ClearMailAliases();
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

MUX_RESULT CMailStorage::ClearMailTables(void)
{
    bool ok = g_pSQLiteBackend->GetDB().ClearMailTables();
    return ok ? MUX_S_OK : MUX_E_FAIL;
}

// CMailStorageFactory
//
CMailStorageFactory::CMailStorageFactory(void) : m_cRef(1) {}
CMailStorageFactory::~CMailStorageFactory() {}

MUX_RESULT CMailStorageFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid || mux_IID_IClassFactory == iid)
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

uint32_t CMailStorageFactory::AddRef(void)  { m_cRef++; return m_cRef; }
uint32_t CMailStorageFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef) { delete this; return 0; }
    return m_cRef;
}

MUX_RESULT CMailStorageFactory::CreateInstance(mux_IUnknown *pUnknownOuter,
    MUX_IID iid, void **ppv)
{
    UNUSED_PARAMETER(pUnknownOuter);
    CMailStorage *p = nullptr;
    try { p = new CMailStorage; } catch (...) { ; }
    if (nullptr == p) return MUX_E_OUTOFMEMORY;
    MUX_RESULT mr = p->QueryInterface(iid, ppv);
    p->Release();
    return mr;
}

MUX_RESULT CMailStorageFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

// ---------------------------------------------------------------------------
// CJITCompileFactory — Lua bytecode → native JIT compilation.
// ---------------------------------------------------------------------------

#if defined(TINYMUX_JIT)
CJITCompileFactory::CJITCompileFactory(void) : m_cRef(1) {}
CJITCompileFactory::~CJITCompileFactory() {}

MUX_RESULT CJITCompileFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IUnknown *>(static_cast<mux_IClassFactory *>(this));
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

uint32_t CJITCompileFactory::AddRef(void)  { m_cRef++; return m_cRef; }
uint32_t CJITCompileFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef) { delete this; return 0; }
    return m_cRef;
}

MUX_RESULT CJITCompileFactory::CreateInstance(mux_IUnknown *pUnknownOuter,
    MUX_IID iid, void **ppv)
{
    if (nullptr != pUnknownOuter) return MUX_E_NOAGGREGATION;
    return jit_compile_create_instance(iid, ppv);
}

MUX_RESULT CJITCompileFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}
#endif // TINYMUX_JIT

// ---------------------------------------------------------------------------
// CLuaModFactory — Lua 5.4 scripting (embedded in engine.so).
// ---------------------------------------------------------------------------

CLuaModFactory::CLuaModFactory(void) : m_cRef(1) {}
CLuaModFactory::~CLuaModFactory() {}

MUX_RESULT CLuaModFactory::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_IUnknown *>(static_cast<mux_IClassFactory *>(this));
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

uint32_t CLuaModFactory::AddRef(void)  { m_cRef++; return m_cRef; }
uint32_t CLuaModFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef) { delete this; return 0; }
    return m_cRef;
}

MUX_RESULT CLuaModFactory::CreateInstance(mux_IUnknown *pUnknownOuter,
    MUX_IID iid, void **ppv)
{
    if (nullptr != pUnknownOuter) return MUX_E_NOAGGREGATION;
    return lua_mod_create_instance(iid, ppv);
}

MUX_RESULT CLuaModFactory::LockServer(bool bLock)
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
    { CID_ComsysStorage      },
    { CID_MailStorage        },
#if defined(TINYMUX_JIT)
    { CID_JITCompile         },
#endif
    { CID_LuaMod             },
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
    MAKE_FACTORY(CComsysStorageFactory,      CID_ComsysStorage)
    MAKE_FACTORY(CMailStorageFactory,         CID_MailStorage)
#if defined(TINYMUX_JIT)
    MAKE_FACTORY(CJITCompileFactory,        CID_JITCompile)
#endif
    MAKE_FACTORY(CLuaModFactory,            CID_LuaMod)

    return mr;
}

static MUX_INTERFACE_INFO engine_interfaces[] =
{
    { IID_ILog, CID_LogPSFactory }
};
#define NUM_ENGINE_INTERFACES (sizeof(engine_interfaces)/sizeof(engine_interfaces[0]))

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register(void)
{
    MUX_RESULT mr = mux_RegisterClassObjects(NUM_ENGINE_CLASSES, engine_classes, nullptr);
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

