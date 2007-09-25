/*! \file sqlproxy.cpp
 * \brief SQLProxy Module
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

#define NUM_CLASSES 1
static CLASS_INFO sum_classes[NUM_CLASSES] =
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
MUX_RESULT CQueryControlProxy::GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid)
{
    // This should only be called on the component side.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryControlProxy::MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, marshal_context ctx)
{
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

MUX_RESULT CQueryControlProxy::Connect(UTF8 *pServer, UTF8 *pDatabase, UTF8 *pUser, UTF8 *pPassword)
{
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryControlProxy::Advise(mux_IQuerySink *pIQuerySink)
{
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CQueryControlProxy::Query(UINT32 iQueryHandle, UTF8 *pDatabaseName, UTF8 *pQuery)
{
    return MUX_E_NOTIMPLEMENTED;
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
