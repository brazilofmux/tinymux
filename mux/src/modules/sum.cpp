/*! \file sum.cpp
 * \brief Sum Out-of-Proc Module
 *
 */

#include "../autoconf.h"
#include "../config.h"
#include "../libmux.h"
#include "../modules.h"
#include "sum.h"

static INT32 g_cComponents  = 0;
static INT32 g_cServerLocks = 0;

#define NUM_CLASSES 1
static MUX_CLASS_INFO sum_classes[NUM_CLASSES] =
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
    // Advertise our components.
    //
    MUX_RESULT mr = mux_RegisterClassObjects(NUM_CLASSES, sum_classes, NULL);
    return mr;
}

extern "C" MUX_RESULT DCL_EXPORT DCL_API mux_Unregister(void)
{
    return mux_RevokeClassObjects(NUM_CLASSES, sum_classes);
}

// Sum component which is not directly accessible.
//
CSum::CSum(void) : m_cRef(1)
{
    g_cComponents++;
}

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

MUX_RESULT CSum_Disconnect(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
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

MUX_RESULT CSum_Call(CHANNEL_INFO *pci, QUEUE_INFO *pqi)
{
    ISum *pISum = static_cast<ISum *>(pci->pInterface);
    if (NULL == pISum)
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
    // CSumProxy goes to zero, it drops the connection and destroys itself.
    // We see that as a call to CSum_Disconnect.
    //
    switch (iMethod)
    {
    case 3:  // Add()
        {
            struct FRAME
            {
                int a;
                int b;
            } CallFrame;

            nWanted = sizeof(CallFrame);
            if (  !Pipe_GetBytes(pqi, &nWanted, &CallFrame)
               || nWanted != sizeof(CallFrame))
            {
                return MUX_E_INVALIDARG;
            }

            struct RETURN
            {
                int  sum;
            } ReturnFrame = { 0 };

            pISum->Add(CallFrame.a, CallFrame.b, &ReturnFrame.sum);

            Pipe_EmptyQueue(pqi);
            Pipe_AppendBytes(pqi, sizeof(ReturnFrame), &ReturnFrame);
            return MUX_S_OK;
        }
        break;
    }
    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CSum::MarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void *pv, marshal_context ctx)
{
    // Parameter validation and initialization.
    //
    MUX_RESULT mr = MUX_S_OK;
    if (NULL == pqi)
    {
        mr = MUX_E_INVALIDARG;
    }
    else if (IID_ISum != riid)
    {
        mr = MUX_E_FAIL;
    }
    else if (CrossProcess != ctx)
    {
        mr = MUX_E_NOTIMPLEMENTED;
    }
    else
    {
        ISum *pISum = NULL;
        if (NULL == pv)
        {
            mr = QueryInterface(IID_ISum, (void **)&pISum);
        }
        else
        {
            mux_IUnknown *pIUnknown = static_cast<mux_IUnknown *>(pv);
            mr = pIUnknown->QueryInterface(IID_ISum, (void **)&pISum);
        }
        if (MUX_SUCCEEDED(mr))
        {
            // Construct a packet sufficient to allow the proxy to communicate with us.
            //
            CHANNEL_INFO *pChannel = Pipe_AllocateChannel(CSum_Call, NULL, CSum_Disconnect);
            if (NULL != pChannel)
            {
                pChannel->pInterface = pISum;
                Pipe_AppendBytes(pqi, sizeof(pChannel->nChannel), (UTF8*)(&pChannel->nChannel));
                mr =  MUX_S_OK;
            }
            else
            {
                pISum->Release();
                pISum = NULL;
                mr = MUX_E_OUTOFMEMORY;
            }
        }
    }
    return mr;
}

MUX_RESULT CSum::UnmarshalInterface(QUEUE_INFO *pqi, MUX_IID riid, void **ppv)
{
    UNUSED_PARAMETER(pqi);
    UNUSED_PARAMETER(riid);
    UNUSED_PARAMETER(ppv);

    return MUX_E_NOTIMPLEMENTED;
}

MUX_RESULT CSum::ReleaseMarshalData(QUEUE_INFO *pqi)
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
            CSum_Disconnect(pChannel, pqi);
        }
    }
    return MUX_S_OK;
}

MUX_RESULT CSum::DisconnectObject(void)
{
    // This is called when the hosting process is about to go down to give the
    // component a chance to notify its proxy that it is about to shut down.
    // This is only implemented on the server side -- not the proxy.
    //
    // TODO: There isn't a mechanism for sending such a notification, yet.
    //
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
