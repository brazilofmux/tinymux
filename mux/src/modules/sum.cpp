/*! \file sum.cpp
 * \brief Sum Out-of-Proc Module
 *
 * $Id$
 *
 */

#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"
#include "sum.h"

static INT32 g_cComponents  = 0;
static INT32 g_cServerLocks = 0;

static ISum *g_pISum = NULL;

#define NUM_CLASSES 1
static CLASS_INFO sum_classes[NUM_CLASSES] =
{
    { CID_Sum }
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

    if (CID_Sum == cid)
    {
        CSumFactory *pSumFactory = NULL;
        try
        {
            pSumFactory = new CSumFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pSumFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pSumFactory->QueryInterface(iid, ppv);
        pSumFactory->Release();
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Register(void)
{
    MUX_RESULT mr = MUX_E_UNEXPECTED;

    if (NULL == g_pISum)
    {
        // Advertise our components.
        //
        mr = mux_RegisterClassObjects(NUM_CLASSES, sum_classes, NULL);
        if (MUX_FAILED(mr))
        {
            return mr;
        }

        // Create an instance of our CSum component.
        //
        ISum *pISum = NULL;
        mr = mux_CreateInstance(CID_Sum, NULL, UseSameProcess, IID_ISum, (void **)&pISum);
        if (MUX_SUCCEEDED(mr))
        {
            g_pISum = pISum;
            pISum = NULL;
        }
        else
        {
            (void)mux_RevokeClassObjects(NUM_CLASSES, sum_classes);
            mr = MUX_E_OUTOFMEMORY;
        }
    }
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Unregister(void)
{
    // Destroy our CSum component.
    //
    if (NULL != g_pISum)
    {
        g_pISum->Release();
        g_pISum = NULL;
    }

    return mux_RevokeClassObjects(NUM_CLASSES, sum_classes);
}

// Sum component which is not directly accessible.
//
CSum::CSum(void) : m_cRef(1)
{
    g_cComponents++;
}

#define LOG_ALWAYS      0x80000000  /* Always log it */

MUX_RESULT CSum::FinalConstruct(void)
{
    MUX_RESULT mr = MUX_S_OK;
    return mr;
}

CSum::~CSum()
{
    g_cComponents--;
}

MUX_RESULT CSum::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<ISum *>(this);
    }
    else if (IID_ISum == iid)
    {
        *ppv = static_cast<ISum *>(this);
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

UINT32 CSum::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CSum::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CSum::GetUnmarshalClass(MUX_IID riid, marshal_context ctx, MUX_CID *pcid)
{
    UNUSED_PARAMETER(ctx);

    if (NULL == pcid)
    {
        return MUX_E_INVALIDARG;
    }
    else if (  IID_ISum == riid
            && CrossProcess == ctx)
    {
        // We only support cross-process at the moment.
        //
        *pcid = CID_SumProxy;
        return MUX_S_OK;
    }
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CSum::MarshalInterface(size_t *pnBuffer, char **pBuffer, MUX_IID riid, marshal_context ctx)
{
    // We must construct a packet sufficient to allow the proxy to communicate with us.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CSum::UnmarshalInterface(size_t nBuffer, char *pBuffer, MUX_IID riid, void **ppv)
{
    return MUX_E_UNEXPECTED;
}

MUX_RESULT CSum::ReleaseMarshalData(char *pBuffer)
{
    if (NULL != pBuffer)
    {
        delete pBuffer;
    }
    return MUX_S_OK;
}

MUX_RESULT CSum::DisconnectObject(void)
{
    // Tear down our side of the communication.
    //
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CSum::Add(int a, int b, int *pSum)
{
    if (NULL == pSum)
    {
        return MUX_E_INVALIDARG;
    }

    *pSum = a + b;
    return MUX_S_OK;
}

// Factory for Sum component which is not directly accessible.
//
CSumFactory::CSumFactory(void) : m_cRef(1)
{
}

CSumFactory::~CSumFactory()
{
}

MUX_RESULT CSumFactory::QueryInterface(MUX_IID iid, void **ppv)
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

UINT32 CSumFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CSumFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CSumFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (NULL != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CSum *pSum = NULL;
    try
    {
        pSum = new CSum;
    }
    catch (...)
    {
        ; // Nothing.
    }

    MUX_RESULT mr;
    if (NULL == pSum)
    {
        return MUX_E_OUTOFMEMORY;
    }
    else
    {
        mr = pSum->FinalConstruct();
        if (MUX_FAILED(mr))
        {
            pSum->Release();
            return mr;
        }
    }

    mr = pSum->QueryInterface(iid, ppv);
    pSum->Release();
    return mr;
}

MUX_RESULT CSumFactory::LockServer(bool bLock)
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
