/*! \file stubslave.cpp
 * \brief This slave hosts modules in a separate process.
 *
 * $Id$
 *
 */

#include "copyright.h"
#include "autoconf.h"
#include "config.h"

#if defined(HAVE_DLOPEN) || defined(WIN32)

#include "libmux.h"
#include "modules.h"

QUEUE_INFO    Queue_In;
QUEUE_INFO    Queue_Out;

#define NUM_CLASSES 1
static CLASS_INFO stubslave_classes[NUM_CLASSES] =
{
    { CID_StubSlave }
};

void Stub_PipePump(void)
{
    static UINT8 arg[QUEUE_BLOCK_SIZE];
    size_t nWanted = sizeof(arg);
    while (  Pipe_GetBytes(&Queue_Out, &nWanted, arg)
          && 0 < nWanted)
    {
        write(1, arg, nWanted);
        nWanted = sizeof(arg);
    }

    int len = read(0, arg, sizeof(arg));
    if (0 < len)
    {
        Pipe_AppendBytes(&Queue_In, len, arg);
    }
}

bool bStubSlaveShutdown = false;

void Stub_ShoveChars(void)
{
    QUEUE_INFO Queue_Frame;
    Pipe_InitializeQueueInfo(&Queue_Frame);

    while (!bStubSlaveShutdown)
    {
        Stub_PipePump();
        Pipe_DecodeFrames(CHANNEL_INVALID, &Queue_Frame);
    }
}

extern "C" MUX_RESULT DCL_API stubslave_GetClassObject(MUX_CID cid, MUX_IID iid, void **ppv)
{
    MUX_RESULT mr = MUX_E_CLASSNOTAVAILABLE;

    if (CID_StubSlave == cid)
    {
        CStubSlaveFactory *pStubSlaveFactory = NULL;
        try
        {
            pStubSlaveFactory = new CStubSlaveFactory;
        }
        catch (...)
        {
            ; // Nothing.
        }

        if (NULL == pStubSlaveFactory)
        {
            return MUX_E_OUTOFMEMORY;
        }

        mr = pStubSlaveFactory->QueryInterface(iid, ppv);
        pStubSlaveFactory->Release();
    }
    return mr;
}

int main(int argc, char *argv[])
{
    Pipe_InitializeQueueInfo(&Queue_In);
    Pipe_InitializeQueueInfo(&Queue_Out);

    MUX_RESULT mr = MUX_S_OK;
    mr = mux_InitModuleLibrary(IsSlaveProcess, Stub_PipePump, &Queue_In, &Queue_Out);
    if (MUX_SUCCEEDED(mr))
    {
        mr = mux_RegisterClassObjects(NUM_CLASSES, stubslave_classes, stubslave_GetClassObject);
        if (MUX_SUCCEEDED(mr))
        {
            mr = mux_AddModule(T("sum"), T("./bin/sum.so"));
            if (MUX_SUCCEEDED(mr))
            {
                Stub_ShoveChars();
            }
            mr = mux_RevokeClassObjects(NUM_CLASSES, stubslave_classes);
        }
        mr = mux_FinalizeModuleLibrary();
    }

    return MUX_RESULT_TO_EXIT_STATUS(mr);
}

// CStubSlave component which is not directly accessible.
//
CStubSlave::CStubSlave(void) : m_cRef(1)
{
}

CStubSlave::~CStubSlave()
{
}

MUX_RESULT CStubSlave::QueryInterface(MUX_IID iid, void **ppv)
{
    if (mux_IID_IUnknown == iid)
    {
        *ppv = static_cast<mux_ISlaveControl *>(this);
    }
    else if (IID_ISlaveControl == iid)
    {
        *ppv = static_cast<mux_ISlaveControl *>(this);
    }
    else
    {
        *ppv = NULL;
        return MUX_E_NOINTERFACE;
    }
    reinterpret_cast<mux_IUnknown *>(*ppv)->AddRef();
    return MUX_S_OK;
}

UINT32 CStubSlave::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CStubSlave::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CStubSlave::Foo(void)
{
}

// Factory for CStubSlave component which is not directly accessible.
//
CStubSlaveFactory::CStubSlaveFactory(void) : m_cRef(1)
{
}

CStubSlaveFactory::~CStubSlaveFactory()
{
}

MUX_RESULT CStubSlaveFactory::QueryInterface(MUX_IID iid, void **ppv)
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

UINT32 CStubSlaveFactory::AddRef(void)
{
    m_cRef++;
    return m_cRef;
}

UINT32 CStubSlaveFactory::Release(void)
{
    m_cRef--;
    if (0 == m_cRef)
    {
        delete this;
        return 0;
    }
    return m_cRef;
}

MUX_RESULT CStubSlaveFactory::CreateInstance(mux_IUnknown *pUnknownOuter, MUX_IID iid, void **ppv)
{
    // Disallow attempts to aggregate this component.
    //
    if (NULL != pUnknownOuter)
    {
        return MUX_E_NOAGGREGATION;
    }

    CStubSlave *pStubSlave = NULL;
    try
    {
        pStubSlave = new CStubSlave;
    }
    catch (...)
    {
        ; // Nothing.
    }

    if (NULL == pStubSlave)
    {
        return MUX_E_OUTOFMEMORY;
    }

    MUX_RESULT mr = pStubSlave->QueryInterface(iid, ppv);
    pStubSlave->Release();
    return mr;
}

MUX_RESULT CStubSlaveFactory::LockServer(bool bLock)
{
    UNUSED_PARAMETER(bLock);
    return MUX_S_OK;
}

#endif
