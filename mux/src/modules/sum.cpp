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
CSum::CSum(void) : m_cRef(1), m_pChannel(NULL)
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

typedef struct
{
    UINT32 iMethod;
} PKTMETHOD;

typedef struct
{
    UINT32 iMethod;
    int a;
    int b;
} PKTADDCALL;

typedef struct
{
    int sum;
} PKTADDRETURN;

MUX_RESULT CSum_Disconnect(CHANNEL_INFO *pci)
{
    ISum *pISum = static_cast<ISum *>(pci->pInterface);
    if (NULL == pISum)
    {
        return MUX_E_NOINTERFACE;
    }

    mux_IMarshal *pIMarshal = NULL;
    MUX_RESULT mr = pISum->QueryInterface(mux_IID_IMarshal, (void **)&pIMarshal);
    if (MUX_SUCCEEDED(mr))
    {
        mr = pIMarshal->DisconnectObject();
        pIMarshal->Release();
    }
    return mr;
}

MUX_RESULT CSum_Call(CHANNEL_INFO *pci, size_t nBuffer, void *pBuffer)
{
    ISum *pISum = static_cast<ISum *>(pci->pInterface);
    if (NULL == pISum)
    {
        return MUX_E_NOINTERFACE;
    }

    if (nBuffer < sizeof(PKTMETHOD))
    {
        return MUX_E_INVALIDARG;
    }
    PKTMETHOD *pMethod = static_cast<PKTMETHOD *>(pBuffer);

    // The IUnknown methods (0, 1, and 2) do not make it across, so we don't
    // attempt to handle them here.  Instead, when the reference count on
    // CSumProxy goes to zero, it drops the connection and destroys itself.
    // We see that as a call to CSum_Disconnect.
    //
    switch (pMethod->iMethod)
    {
    case 3:  // Add()
        {
            if (nBuffer != sizeof(PKTADDCALL))
            {
                return MUX_E_INVALIDARG;
            }

            PKTADDCALL *pCallFrame = static_cast<PKTADDCALL *>(pBuffer);
            int a = pCallFrame->a;
            int b = pCallFrame->b;
            int sum = 0;
            pISum->Add(a, b, &sum);
            PKTADDRETURN *pReturnFrame = static_cast<PKTADDRETURN *>(pBuffer);
            pReturnFrame->sum = sum;
            // TODO: The API doesn't support telling the caller the size of the return frame.
            return MUX_S_OK;
        }
        break;
    }
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CSum::MarshalInterface(size_t *pnBuffer, void **pBuffer, MUX_IID riid, marshal_context ctx)
{
    // Parameter validation and initialization.
    //
    MUX_RESULT mr = MUX_S_OK;
    if (NULL == pnBuffer)
    {
        mr = MUX_E_INVALIDARG;
    }
    *pnBuffer = 0;

    if (  NULL == pBuffer
       || NULL == *pBuffer)
    {
        mr = MUX_E_INVALIDARG;
    }
    *pBuffer = NULL;

    if (MUX_FAILED(mr))
    {
        return mr;
    }

    if (IID_ISum != riid)
    {
       return MUX_E_FAIL;
    }

    if (CrossProcess != ctx)
    {
        return MUX_E_NOTIMPLEMENTED;
    }

    // Construct a packet sufficient to allow the proxy to communicate with us.
    //
    m_pChannel = Pipe_AllocateChannel(CSum_Call, NULL, CSum_Disconnect);

    ISum *pISum = NULL;
    mr = QueryInterface(IID_ISum, (void **)&pISum);
    if (MUX_SUCCEEDED(mr))
    {
        m_pChannel->pInterface = pISum;
        m_mipkt.nChannel = m_pChannel->nChannel;
        *pBuffer = &m_mipkt;
        *pnBuffer = sizeof(m_mipkt);
    }
    return mr;
}

MUX_RESULT CSum::UnmarshalInterface(size_t nBuffer, void *pBuffer, MUX_IID riid, void **ppv)
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
    // Get our interface pointer from the channel.
    //
    ISum *pISum = static_cast<ISum *>(m_pChannel->pInterface);
    m_pChannel->pInterface = NULL;

    // Tear down our side of the communication.  Our callback functions will
    // no longer be called.
    //
    Pipe_FreeChannel(m_pChannel);
    m_pChannel = NULL;

    pISum->Release();
    return MUX_S_OK;
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
